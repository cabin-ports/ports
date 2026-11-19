//! First publication of new package versions: `--list-new` lists,
//! `--plan-new` previews, and `--publish-new` uploads, never as a
//! packaging revision (docs/publishing.md, "Automatic publishing").
//!
//! The receipts log, not the registry, says what is uploaded: the
//! registry's read routes hide pending and rejected uploads.  An
//! `uploaded` line is final, so a rejected upload is never revived by
//! resubmitting it.  A `failed` line's upload may have landed, so that
//! version waits for `--retry-failed`.  A version without a line is a
//! candidate unless something it requires is blocked.  No probe runs:
//! `--plan-new` builds them and records what it built, and
//! `--publish-new` builds nothing and uploads only what that record
//! lists ([`crate::prepared`]).  An uploaded requirement
//! must stage the bytes of its last receipt, or its dependents wait for
//! its packaging revision.

use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, bail};
use cabin_core::Checksum;
use serde::{Deserialize, Serialize};

use crate::Options;
use crate::plan::PortConversion;
use crate::{preflight, prepared, remote, select};

/// `--list-new`, `--plan-new`, and `--publish-new`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct New {
    /// The registry, by the index URL its receipts log records.
    /// `--list-new` and `--plan-new` contact nothing.
    pub index_url: String,
    /// That registry's receipts log.
    pub receipts: PathBuf,
    /// Retry versions whose upload failed.
    pub retry_failed: bool,
    /// `--publish-new`: `main`'s current commit, against which the
    /// checked-out snapshot is checked before anything uploads.
    /// `None` previews or lists.
    pub main: Option<String>,
    /// `--list-new`: write the candidates here, one label per line in
    /// upload order, and stop before staging.
    pub list: Option<PathBuf>,
}

/// One line of the receipts log.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Receipt {
    /// The index URL the upload targeted.
    pub registry: String,
    /// Scoped package name.
    pub name: String,
    pub version: String,
    pub outcome: Outcome,
    /// The uploaded archive's checksum (for `failed`, the staged one).
    pub checksum: Checksum,
    /// Everything else recorded with it: the registry's `revision`,
    /// `no_op`, and `verification`, and the workflow's provenance.
    #[serde(flatten)]
    pub details: serde_json::Map<String, serde_json::Value>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Outcome {
    Uploaded,
    Failed,
}

/// `(scoped name, version)`.
type Key = (String, String);

fn key(conversion: &PortConversion) -> Key {
    (
        conversion.scoped_name.as_str().to_owned(),
        conversion.published_version.to_string(),
    )
}

fn label(conversion: &PortConversion) -> String {
    format!(
        "{} {}",
        conversion.scoped_name.as_str(),
        conversion.published_version
    )
}

/// What a receipts log records about each version.
#[derive(Debug, Default)]
pub struct Receipts {
    /// The last `uploaded` line per version.
    uploaded: BTreeMap<Key, Receipt>,
    failed: BTreeSet<Key>,
}

impl Receipts {
    /// Read `path`, the receipts log of the registry at `registry`.
    ///
    /// # Errors
    /// A missing or unreadable file, a line that is not a receipt, or
    /// a receipt for another registry: state this tool cannot trust is
    /// never read as "nothing uploaded yet".
    pub fn read(path: &Path, registry: &str) -> Result<Self> {
        let text = fs::read_to_string(path)
            .with_context(|| format!("reading the receipts log {}", path.display()))?;
        Self::parse(&text, registry)
            .with_context(|| format!("reading the receipts log {}", path.display()))
    }

    fn parse(text: &str, registry: &str) -> Result<Self> {
        let mut receipts = Self::default();
        for (number, line) in text.lines().enumerate() {
            if line.trim().is_empty() {
                continue;
            }
            let receipt: Receipt = serde_json::from_str(line)
                .with_context(|| format!("line {} is not a receipt", number + 1))?;
            if receipt.registry != registry {
                bail!(
                    "line {} records an upload to {}, not {registry}; a receipts log belongs \
                     to exactly one registry",
                    number + 1,
                    receipt.registry
                );
            }
            let key = (receipt.name.clone(), receipt.version.clone());
            match receipt.outcome {
                Outcome::Uploaded => {
                    receipts.uploaded.insert(key, receipt);
                }
                Outcome::Failed => {
                    receipts.failed.insert(key);
                }
            }
        }
        Ok(receipts)
    }
}

/// Where a version stands for this run.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Status {
    /// The receipts log records its upload; never uploaded here again.
    Uploaded,
    /// Uploaded, but its recipe now stages other bytes: what requires
    /// it waits for a manual packaging revision of it.
    Diverged(String),
    /// To upload.
    Candidate,
    /// Not uploaded this run, for the reason given.
    Blocked(String),
}

/// Each version's status from the receipts alone.
pub fn classify(
    conversions: &[PortConversion],
    receipts: &Receipts,
    retry_failed: bool,
) -> Vec<Status> {
    conversions
        .iter()
        .map(|conversion| {
            let key = key(conversion);
            if receipts.uploaded.contains_key(&key) {
                return Status::Uploaded;
            }
            if receipts.failed.contains(&key) && !retry_failed {
                return Status::Blocked(
                    "its upload failed and may have landed: check the registry, then retry with \
                     --retry-failed (docs/publishing.md)"
                        .to_owned(),
                );
            }
            Status::Candidate
        })
        .collect()
}

/// A candidate whose staging closure reaches a blocked or diverged
/// version, through uploaded versions too, is blocked: it would be
/// validated against bytes the registry does not serve.
pub fn propagate(conversions: &[PortConversion], status: &mut [Status]) {
    let before = status.to_vec();
    for (index, status) in status.iter_mut().enumerate() {
        if *status != Status::Candidate {
            continue;
        }
        let reach = select::closure_of(conversions, [index]);
        let cause = (0..before.len()).find_map(|dep| {
            let why = match &before[dep] {
                _ if dep == index || !reach[dep] => return None,
                Status::Blocked(_) => "is blocked",
                Status::Diverged(_) => "awaits a manual packaging revision",
                _ => return None,
            };
            Some(format!(
                "requires {}, which {why}",
                label(&conversions[dep])
            ))
        });
        if let Some(cause) = cause {
            *status = Status::Blocked(cause);
        }
    }
}

/// The candidates, in publication order.
pub fn upload_order(status: &[Status]) -> Vec<usize> {
    (0..status.len())
        .filter(|&index| status[index] == Status::Candidate)
        .collect()
}

/// Mark each staged, uploaded version whose staged checksum is not its
/// last receipt's: the registry serves other bytes than were validated.
fn check_staged_bytes(
    conversions: &[PortConversion],
    status: &mut [Status],
    receipts: &Receipts,
    staged: impl IntoIterator<Item = (usize, String)>,
) {
    for (index, checksum) in staged {
        if status[index] != Status::Uploaded {
            continue;
        }
        let last = &receipts.uploaded[&key(&conversions[index])].checksum;
        if last.as_str() != checksum {
            status[index] =
                Status::Diverged(format!("stages {checksum}, but its last upload is {last}"));
        }
    }
}

/// Defer every candidate that `main` no longer agrees with: when main
/// changed a path outside the ports directory (the publisher, the Cabin
/// pin, the workflow) other than the files `select::NOT_BUILD_INPUTS`
/// lists, all of them; otherwise each one whose staging closure touches
/// a name main changed.  A later run uploads them from main.  This is a
/// last check before uploading, not a transaction with later merges.
///
/// # Errors
/// When Git cannot compare the checked-out commit with `main`.
pub fn defer_stale(
    conversions: &[PortConversion],
    status: &mut [Status],
    ports_dir: &Path,
    main: &str,
) -> Result<()> {
    let (top, prefix) = select::locate(ports_dir)?;
    let top = top.as_path();
    let (head, main) = head_and_main(top, main)?;
    if head == main {
        println!("new: main is still {main}, this snapshot");
        return Ok(());
    }
    println!("new: main moved to {main}; checking what it changed since {head}");
    let diff = select::git(
        top,
        &["diff", "-z", "--name-only", "--no-renames", &head, &main],
    )?;
    let defer = |status: &mut [Status], index: usize, why: &str| {
        status[index] = Status::Blocked(format!("deferred: {why}; a later run uploads from main"));
    };
    let mut changed_names = BTreeSet::new();
    for path in diff.split('\0').filter(|path| !path.is_empty()) {
        if let Some(port_path) = path.strip_prefix(&prefix) {
            // `<prefix>/<name>/...`: the name, not the prefix it shares.
            changed_names.insert(port_path.split('/').nth(1).unwrap_or_default());
        } else if !select::NOT_BUILD_INPUTS.contains(&path) {
            for index in upload_order(status) {
                defer(status, index, &format!("main changed {path}"));
            }
            return Ok(());
        }
    }
    for index in upload_order(status) {
        let reach = select::closure_of(conversions, [index]);
        let why = conversions
            .iter()
            .zip(reach)
            .filter(|(_, reached)| *reached)
            .find_map(|(conversion, _)| {
                let name = conversion.scoped_name.base_name();
                changed_names.contains(name).then(|| {
                    format!(
                        "main changed {prefix}{}/{name}/",
                        crate::plan::port_prefix(name)
                    )
                })
            });
        if let Some(why) = why {
            defer(status, index, &why);
        }
    }
    Ok(())
}

/// The checked-out commit and `main`'s, resolved in the repository
/// holding `dir`.
///
/// # Errors
/// When Git cannot resolve either: an unresolvable main never reads as
/// unchanged.
pub fn head_and_main(dir: &Path, main: &str) -> Result<(String, String)> {
    Ok((head(dir)?, commit(dir, main).context("resolving main")?))
}

/// The commit checked out in the repository holding `dir`.
///
/// # Errors
/// When Git cannot resolve it.
pub fn head(dir: &Path) -> Result<String> {
    commit(dir, "HEAD")
}

fn commit(dir: &Path, rev: &str) -> Result<String> {
    select::git(
        dir,
        &[
            "rev-parse",
            "--verify",
            "--end-of-options",
            &format!("{rev}^{{commit}}"),
        ],
    )
    .map(|out| out.trim().to_owned())
}

/// Run `--list-new`, `--plan-new`, or `--publish-new` over the loaded
/// catalog.
///
/// # Errors
/// Unreadable state, an unwritable list, a failed preflight, or a
/// failed upload.
pub fn run(new: &New, conversions: &[PortConversion], options: &Options) -> Result<()> {
    let receipts = Receipts::read(&new.receipts, &new.index_url)?;
    let mut status = classify(conversions, &receipts, new.retry_failed);
    println!(
        "new: {} of {} versions have an upload receipt for {}",
        status.iter().filter(|s| **s == Status::Uploaded).count(),
        conversions.len(),
        new.index_url
    );
    if let Some(main) = &new.main {
        defer_stale(conversions, &mut status, &options.ports_dir, main)?;
    }
    propagate(conversions, &mut status);
    if let Some(list) = &new.list {
        let labels: String = upload_order(&status)
            .into_iter()
            .map(|index| label(&conversions[index]) + "\n")
            .collect();
        return fs::write(list, labels).with_context(|| format!("writing {}", list.display()));
    }
    print_plan(conversions, &status, &receipts);

    let staged: Vec<usize> = select::closure_of(conversions, upload_order(&status))
        .into_iter()
        .enumerate()
        .filter_map(|(index, staged)| staged.then_some(index))
        .collect();
    let mut package_dirs = BTreeMap::new();
    if !staged.is_empty() {
        println!(
            "new: staging {} candidates and the {} versions they require",
            upload_order(&status).len(),
            staged.len() - upload_order(&status).len()
        );
        let subset: Vec<PortConversion> = staged.iter().map(|&i| conversions[i].clone()).collect();
        let report = preflight::preflight(&preflight::PreflightRequest {
            conversions: &subset,
            cache_dir: &options.cache_dir,
            work_dir: &options.work_dir,
            cabin: &options.cabin,
            // The merge queue already ran every consumer test, and an
            // upload builds nothing (lib.rs).
            probes: if new.main.is_none() {
                preflight::Probes::Build
            } else {
                preflight::Probes::Skip
            },
        })?;
        let before = status.clone();
        check_staged_bytes(
            conversions,
            &mut status,
            &receipts,
            staged.iter().copied().zip(report.checksums.iter().cloned()),
        );
        propagate(conversions, &mut status);
        if status != before {
            println!("new: staging changed the plan");
            print_plan(conversions, &status, &receipts);
        }
        package_dirs = staged
            .iter()
            .copied()
            .zip(report.package_dirs.into_iter().zip(report.checksums))
            .collect();
    }

    let order = upload_order(&status);
    if new.main.is_some() && order.is_empty() {
        println!("new: nothing to upload");
        return Ok(());
    }
    let uploads: Vec<remote::Upload<'_>> = order
        .iter()
        .map(|&index| {
            let (dir, checksum) = &package_dirs[&index];
            remote::Upload {
                package_dir: dir,
                name: conversions[index].scoped_name.as_str(),
                version: conversions[index].published_version.to_string(),
                checksum,
            }
        })
        .collect();
    if new.main.is_none() {
        prepared::write(options, &uploads)?;
        println!("plan complete: {} versions to upload", order.len());
        return Ok(());
    }
    prepared::require(options, &uploads)?;
    remote::publish_all(
        &uploads,
        &new.index_url,
        &options.cabin,
        false,
        options.receipts_out.as_deref(),
    )?;
    println!("new: uploaded {} versions", uploads.len());
    Ok(())
}

fn print_plan(conversions: &[PortConversion], status: &[Status], receipts: &Receipts) {
    let order = upload_order(status);
    let list = |indices: &[usize]| {
        if indices.is_empty() {
            return "none".to_owned();
        }
        indices
            .iter()
            .map(|&i| label(&conversions[i]))
            .collect::<Vec<_>>()
            .join(", ")
    };
    println!("new: upload, in order: {}", list(&order));
    for (conversion, status) in conversions.iter().zip(status) {
        match status {
            Status::Blocked(why) => println!("new: not uploading {}: {why}", label(conversion)),
            Status::Diverged(why) => println!(
                "new: {} awaits a manual packaging revision: {why}",
                label(conversion)
            ),
            Status::Uploaded | Status::Candidate => {}
        }
    }
    // Uploaded is not verified: a dependent resolves only once what it
    // requires is verified, which this tool cannot observe.
    let unverified: Vec<usize> = select::closure_of(conversions, order)
        .into_iter()
        .enumerate()
        .filter(|&(index, reached)| {
            reached
                && status[index] == Status::Uploaded
                && receipts.uploaded[&key(&conversions[index])]
                    .details
                    .get("verification")
                    .and_then(serde_json::Value::as_str)
                    != Some("verified")
        })
        .map(|(index, _)| index)
        .collect();
    if !unverified.is_empty() {
        println!(
            "new: required, uploaded, and not recorded as verified (dependents resolve only \
             once they are): {}",
            list(&unverified)
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plan::tests::{package_manifest, write_package};

    const REGISTRY: &str = "https://registry.example";

    fn receipt(name: &str, version: &str, outcome: &str, checksum: char) -> String {
        format!(
            "{{\"registry\":\"{REGISTRY}\",\"name\":\"cabin-ports/{name}\",\"version\":\
             \"{version}\",\"outcome\":\"{outcome}\",\"checksum\":\"sha256:{}\",\
             \"verification\":\"pending\"}}\n",
            checksum.to_string().repeat(64)
        )
    }

    /// zlib 1.3.1; libpng requires zlib; app requires libpng; tool
    /// stands alone.
    fn catalog() -> (assert_fs::TempDir, Vec<PortConversion>) {
        let dir = assert_fs::TempDir::new().unwrap();
        let ports = dir.path().join("ports");
        let port = |name: &str, extra: &str| {
            let manifest = package_manifest(&format!("cabin-ports/{name}"), "1.0.0", extra);
            write_package(&ports, name, "1.0.0", &manifest);
        };
        port("zlib", "");
        port("libpng", "[dependencies]\n\"cabin-ports/zlib\" = \"1\"\n\n");
        port("app", "[dependencies]\n\"cabin-ports/libpng\" = \"1\"\n\n");
        port("tool", "");
        let conversions = crate::plan::load_conversions(&ports).unwrap();
        (dir, conversions)
    }

    fn statuses(conversions: &[PortConversion], status: &[Status]) -> Vec<String> {
        conversions
            .iter()
            .zip(status)
            .map(|(c, s)| {
                let s = match s {
                    Status::Uploaded => "uploaded".to_owned(),
                    Status::Diverged(_) => "diverged".to_owned(),
                    Status::Candidate => "candidate".to_owned(),
                    Status::Blocked(why) => format!("blocked: {why}"),
                };
                format!("{} {s}", c.scoped_name.base_name())
            })
            .collect()
    }

    fn plan(conversions: &[PortConversion], log: &str, retry: bool) -> Vec<String> {
        let receipts = Receipts::parse(log, REGISTRY).unwrap();
        let mut status = classify(conversions, &receipts, retry);
        propagate(conversions, &mut status);
        statuses(conversions, &status)
    }

    #[test]
    fn only_versions_without_an_upload_receipt_are_candidates() {
        let (_dir, conversions) = catalog();
        // Publication order: tool and zlib, then libpng, then app.
        assert_eq!(
            plan(&conversions, "", false),
            [
                "tool candidate",
                "zlib candidate",
                "libpng candidate",
                "app candidate"
            ]
        );
        // An upload receipt is final whatever the registry's public
        // index shows: pending versions are invisible there.
        let log =
            receipt("zlib", "1.0.0", "uploaded", 'a') + &receipt("tool", "1.0.0", "uploaded", 'b');
        assert_eq!(
            plan(&conversions, &log, false),
            [
                "tool uploaded",
                "zlib uploaded",
                "libpng candidate",
                "app candidate"
            ]
        );
        // Another registry's log, or a corrupt one, is never read as
        // empty.
        let other = log.replace(REGISTRY, "https://other.example");
        assert!(Receipts::parse(&other, REGISTRY).is_err());
        assert!(Receipts::parse("{\"registry\":", REGISTRY).is_err());
        assert!(Receipts::parse(&log.replace("sha256:", "md5:"), REGISTRY).is_err());
        assert!(Receipts::parse(&log.replace("uploaded", "verified"), REGISTRY).is_err());
    }

    /// A failed upload may have landed, so it and what requires it wait
    /// for `--retry-failed`; a later upload receipt settles it.
    #[test]
    fn a_failed_upload_waits_for_retry_failed() {
        let (_dir, conversions) = catalog();
        let failed = receipt("libpng", "1.0.0", "failed", 'c');
        let plan_failed = plan(&conversions, &failed, false);
        assert_eq!(plan_failed[..2], ["tool candidate", "zlib candidate"]);
        assert!(
            plan_failed[2].starts_with("libpng blocked: its upload failed"),
            "{plan_failed:?}"
        );
        assert_eq!(
            plan_failed[3],
            "app blocked: requires cabin-ports/libpng 1.0.0, which is blocked"
        );
        assert_eq!(
            plan(&conversions, &failed, true),
            [
                "tool candidate",
                "zlib candidate",
                "libpng candidate",
                "app candidate"
            ]
        );
        let settled = failed + &receipt("libpng", "1.0.0", "uploaded", 'c');
        assert_eq!(plan(&conversions, &settled, false)[2], "libpng uploaded");
    }

    #[test]
    fn a_requirement_staging_other_bytes_than_uploaded_blocks_its_dependents() {
        let (_dir, conversions) = catalog();
        let log = receipt("zlib", "1.0.0", "uploaded", 'a');
        let receipts = Receipts::parse(&log, REGISTRY).unwrap();
        let mut status = classify(&conversions, &receipts, false);
        propagate(&conversions, &mut status);
        let zlib = 1;
        let same = format!("sha256:{}", "a".repeat(64));
        check_staged_bytes(&conversions, &mut status, &receipts, [(zlib, same)]);
        assert_eq!(status[zlib], Status::Uploaded);

        let other = format!("sha256:{}", "f".repeat(64));
        check_staged_bytes(&conversions, &mut status, &receipts, [(zlib, other)]);
        propagate(&conversions, &mut status);
        assert_eq!(
            statuses(&conversions, &status),
            [
                "tool candidate",
                "zlib diverged",
                "libpng blocked: requires cabin-ports/zlib 1.0.0, which awaits a manual \
                 packaging revision",
                "app blocked: requires cabin-ports/zlib 1.0.0, which awaits a manual \
                 packaging revision",
            ]
        );

        // Through an uploaded requirement too: app reaches zlib's bytes
        // through libpng's.
        let log = receipt("zlib", "1.0.0", "uploaded", 'a')
            + &receipt("libpng", "1.0.0", "uploaded", 'b');
        let receipts = Receipts::parse(&log, REGISTRY).unwrap();
        let mut status = classify(&conversions, &receipts, false);
        let other = format!("sha256:{}", "f".repeat(64));
        check_staged_bytes(&conversions, &mut status, &receipts, [(zlib, other)]);
        propagate(&conversions, &mut status);
        assert_eq!(
            statuses(&conversions, &status)[2..],
            [
                "libpng uploaded",
                "app blocked: requires cabin-ports/zlib 1.0.0, which awaits a manual packaging \
                 revision",
            ]
        );
    }

    fn fixture_git(repo: &Path, args: &[&str]) -> String {
        let output = std::process::Command::new("git")
            .current_dir(repo)
            .env("GIT_CONFIG_GLOBAL", repo.join("no-global-config"))
            .env("GIT_CONFIG_NOSYSTEM", "1")
            .args(["-c", "user.name=t", "-c", "user.email=t@ports.invalid"])
            .args(["-c", "init.defaultBranch=main"])
            .args(args)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "git {args:?}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        String::from_utf8(output.stdout).unwrap().trim().to_owned()
    }

    /// A snapshot finishes when main only added unrelated work; a
    /// candidate whose closure main changed, or every candidate after a
    /// shared input changed, waits for a later run.
    #[test]
    fn main_defers_only_the_candidates_it_changed() {
        let (dir, conversions) = catalog();
        let repo = dir.path();
        let ports = repo.join("ports");
        fixture_git(repo, &["init", "-q"]);
        fixture_git(repo, &["add", "-A"]);
        fixture_git(repo, &["commit", "-q", "-m", "snapshot"]);
        let snapshot = fixture_git(repo, &["rev-parse", "HEAD"]);
        let commit = |path: &str, contents: &str| {
            let path = repo.join(path);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, contents).unwrap();
            fixture_git(repo, &["add", "-A"]);
            fixture_git(repo, &["commit", "-q", "-m", "main moves"]);
            let main = fixture_git(repo, &["rev-parse", "HEAD"]);
            fixture_git(repo, &["checkout", "-q", &snapshot]);
            main
        };
        let defer = |main: &str| {
            let mut status = classify(&conversions, &Receipts::default(), false);
            defer_stale(&conversions, &mut status, &ports, main).unwrap();
            propagate(&conversions, &mut status);
            fixture_git(repo, &["checkout", "-q", "main"]);
            fixture_git(repo, &["reset", "-q", "--hard", &snapshot]);
            statuses(&conversions, &status)
        };
        let all_candidates = [
            "tool candidate",
            "zlib candidate",
            "libpng candidate",
            "app candidate",
        ];

        assert_eq!(defer(&snapshot), all_candidates);
        let main = commit("ports/ne/newport/1.0.0/cabin.toml", "unrelated\n");
        assert_eq!(defer(&main), all_candidates);
        // A name sharing a candidate's prefix is another package.
        let main = commit("ports/zl/zlib-ng/1.0.0/cabin.toml", "unrelated\n");
        assert_eq!(defer(&main), all_candidates);
        let main = commit("README.md", "docs\n");
        assert_eq!(defer(&main), all_candidates);

        // A new version of a name a candidate requires defers that
        // candidate and what requires it.
        let main = commit("ports/zl/zlib/1.1.0/cabin.toml", "zlib\n");
        let plan = defer(&main);
        assert_eq!(plan[0], "tool candidate");
        for line in &plan[1..] {
            assert!(
                line.contains("deferred: main changed ports/zl/zlib/"),
                "{plan:?}"
            );
        }

        let main = commit("Cargo.lock", "pin moved\n");
        for line in defer(&main) {
            assert!(line.contains("deferred: main changed Cargo.lock"), "{line}");
        }

        assert!(
            defer_stale(&conversions, &mut [], &ports, "no-such-rev").is_err(),
            "an unresolvable main must fail, not read as unchanged"
        );
    }
}
