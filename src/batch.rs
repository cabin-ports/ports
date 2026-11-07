//! First publication of new package versions: `--list-new` lists,
//! `--plan-new` previews, and `--publish-new` uploads, never as a
//! packaging revision (docs/publishing.md, "Automatic publishing").
//!
//! Which versions the registry already has is the receipts log's
//! answer, not the registry's: the read routes serve verified
//! revisions only, and during the private alpha only to an admitted
//! token, so pending and rejected uploads are invisible to this tool.
//! The log holds one JSON object per line for one registry, appended
//! for every upload the publish workflow makes:
//!
//! - an `uploaded` line is the registry's receipt (a created revision,
//!   or the byte-identical no-op).  That version is never uploaded
//!   here again, whatever its recipe now stages: a correction ships
//!   only as a manual packaging revision, and a rejected upload is
//!   never revived by resubmitting it.
//! - a `failed` line names the version an upload invocation stopped
//!   at.  Its upload may have landed with the response lost, and a
//!   resubmission would revive it if verification rejected it since,
//!   so it waits for a run with `--retry-failed`, which an operator
//!   starts after checking the registry (docs/publishing.md).
//!
//! A version without a line is a candidate unless the reviewed hold
//! list ([`HOLDS_FILE`]) names it or a version it requires is blocked.
//! Only the candidates and what their requirements match stage, and
//! no probe runs: an uploaded requirement must stage the bytes of its
//! last receipt, or the candidate was validated against bytes the
//! registry cannot serve, and it waits for that requirement's manual
//! revision.

use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, bail};
use cabin_core::Checksum;
use serde::{Deserialize, Serialize};

use crate::Options;
use crate::plan::PortConversion;
use crate::{preflight, remote, select};

/// The reviewed hold list, beside the ports directory: one
/// `<name> <version> <reason>` line per version `--publish-new` must
/// not upload, `#` starting a comment line.
pub const HOLDS_FILE: &str = "publish-holds.txt";

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

/// The hold list beside `ports_dir`, every entry naming a loaded
/// version; a missing file holds nothing.
///
/// # Errors
/// An unreadable or malformed file, or an entry naming no committed
/// version (a typo must not leave the intended version unheld).
pub fn load_holds(
    ports_dir: &Path,
    conversions: &[PortConversion],
) -> Result<BTreeMap<Key, String>> {
    let ports_dir = fs::canonicalize(ports_dir)
        .with_context(|| format!("resolving {}", ports_dir.display()))?;
    let path = ports_dir.parent().unwrap_or(&ports_dir).join(HOLDS_FILE);
    let text = match fs::read_to_string(&path) {
        Ok(text) => text,
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => return Ok(BTreeMap::new()),
        Err(err) => return Err(err).with_context(|| format!("reading {}", path.display())),
    };
    let holds = parse_holds(&text).with_context(|| format!("reading {}", path.display()))?;
    let loaded: BTreeSet<Key> = conversions.iter().map(key).collect();
    if let Some((name, version)) = holds.keys().find(|held| !loaded.contains(*held)) {
        bail!(
            "{} holds {name} {version}, which is not a committed port version",
            path.display()
        );
    }
    Ok(holds)
}

fn parse_holds(text: &str) -> Result<BTreeMap<Key, String>> {
    let mut holds = BTreeMap::new();
    for (number, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let (Some(name), Some(version)) = (fields.next(), fields.next()) else {
            bail!("line {}: expected `<name> <version> <reason>`", number + 1);
        };
        let reason = fields.collect::<Vec<_>>().join(" ");
        if reason.is_empty() {
            bail!("line {}: a hold needs a reason", number + 1);
        }
        let held = (
            crate::plan::scoped_package_name(name)
                .with_context(|| format!("line {}", number + 1))?
                .as_str()
                .to_owned(),
            semver::Version::parse(version)
                .with_context(|| format!("line {}: version `{version}`", number + 1))?
                .to_string(),
        );
        if holds.insert(held, reason).is_some() {
            bail!("line {}: {name} {version} is held twice", number + 1);
        }
    }
    Ok(holds)
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

/// Each version's status from the receipts and holds alone.
pub fn classify(
    conversions: &[PortConversion],
    receipts: &Receipts,
    holds: &BTreeMap<Key, String>,
    retry_failed: bool,
) -> Vec<Status> {
    conversions
        .iter()
        .map(|conversion| {
            let key = key(conversion);
            if receipts.uploaded.contains_key(&key) {
                return Status::Uploaded;
            }
            if let Some(reason) = holds.get(&key) {
                return Status::Blocked(format!("held: {reason}"));
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
/// changed a path outside the ports directory (the publisher, the
/// Cabin pin, the workflow), all of them; otherwise each one whose
/// staging closure touches a name main changed or a version main holds.
/// A later run uploads them from main.  This is a last check before
/// uploading, not a transaction with later merges.
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
    let holds_path = format!(
        "{}{HOLDS_FILE}",
        prefix
            .trim_end_matches('/')
            .rsplit_once('/')
            .map_or(String::new(), |(dir, _)| format!("{dir}/"))
    );
    let mut changed_names = BTreeSet::new();
    for path in diff.split('\0').filter(|path| !path.is_empty()) {
        if let Some(port_path) = path.strip_prefix(&prefix) {
            // `<prefix>/<name>/...`: the name, not the prefix it shares.
            changed_names.insert(port_path.split('/').nth(1).unwrap_or_default());
        } else if path != holds_path && !select::NOT_BUILD_INPUTS.contains(&path) {
            for index in upload_order(status) {
                defer(status, index, &format!("main changed {path}"));
            }
            return Ok(());
        }
    }
    let held_at_main = if select::git(top, &["ls-tree", "--name-only", &main, "--", &holds_path])?
        .trim()
        .is_empty()
    {
        BTreeMap::new()
    } else {
        parse_holds(&select::git(
            top,
            &["show", &format!("{main}:{holds_path}")],
        )?)
        .with_context(|| format!("reading {holds_path} at main"))?
    };
    for index in upload_order(status) {
        let reach = select::closure_of(conversions, [index]);
        let why = conversions
            .iter()
            .zip(reach)
            .filter(|(_, reached)| *reached)
            .find_map(|(conversion, _)| {
                let name = conversion.scoped_name.base_name();
                if changed_names.contains(name) {
                    Some(format!(
                        "main changed {prefix}{}/{name}/",
                        crate::plan::port_prefix(name)
                    ))
                } else if held_at_main.contains_key(&key(conversion)) {
                    Some(format!("main holds {}", label(conversion)))
                } else {
                    None
                }
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
    let rev = |rev: &str| {
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
    };
    Ok((rev("HEAD")?, rev(main).context("resolving main")?))
}

/// Run `--list-new`, `--plan-new`, or `--publish-new` over the loaded
/// catalog.
///
/// # Errors
/// Unreadable state, an unwritable list, a failed preflight, or a
/// failed upload.
pub fn run(
    new: &New,
    conversions: &[PortConversion],
    holds: &BTreeMap<Key, String>,
    options: &Options,
) -> Result<()> {
    let receipts = Receipts::read(&new.receipts, &new.index_url)?;
    let mut status = classify(conversions, &receipts, holds, new.retry_failed);
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
            // Never on main, where the upload runs (lib.rs): the merge
            // queue already ran every consumer test.
            run_probes: false,
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
    if new.main.is_none() {
        println!("plan complete: {} versions to upload", order.len());
        return Ok(());
    }
    if order.is_empty() {
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

    fn plan(conversions: &[PortConversion], log: &str, holds: &str, retry: bool) -> Vec<String> {
        let receipts = Receipts::parse(log, REGISTRY).unwrap();
        let holds = parse_holds(holds).unwrap();
        let mut status = classify(conversions, &receipts, &holds, retry);
        propagate(conversions, &mut status);
        statuses(conversions, &status)
    }

    #[test]
    fn only_versions_without_an_upload_receipt_are_candidates() {
        let (_dir, conversions) = catalog();
        // Publication order: tool and zlib, then libpng, then app.
        assert_eq!(
            plan(&conversions, "", "", false),
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
            plan(&conversions, &log, "", false),
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

    #[test]
    fn a_held_or_blocked_version_blocks_what_requires_it() {
        let (_dir, conversions) = catalog();
        let plan = plan(
            &conversions,
            "",
            "# comment\nzlib 1.0.0 awaiting a decision\n",
            false,
        );
        assert_eq!(plan[0], "tool candidate");
        assert_eq!(plan[1], "zlib blocked: held: awaiting a decision");
        assert_eq!(
            plan[2],
            "libpng blocked: requires cabin-ports/zlib 1.0.0, which is blocked"
        );
        assert_eq!(
            plan[3],
            "app blocked: requires cabin-ports/zlib 1.0.0, which is blocked"
        );

        for (holds, error) in [
            ("zlib 1.0.0\n", "needs a reason"),
            ("zlib\n", "expected"),
            ("zlib 1.0 why\n", "version"),
            ("zlib 1.0.0 a\nzlib 1.0.0 b\n", "held twice"),
            ("Zlib 1.0.0 why\n", "line 1"),
        ] {
            let err = format!("{:#}", parse_holds(holds).unwrap_err());
            assert!(err.contains(error), "{holds:?}: {err}");
        }
    }

    #[test]
    fn a_hold_must_name_a_committed_version() {
        let (dir, conversions) = catalog();
        let ports = dir.path().join("ports");
        assert!(load_holds(&ports, &conversions).unwrap().is_empty());
        fs::write(dir.path().join(HOLDS_FILE), "zlib 1.0.0 why\n").unwrap();
        assert_eq!(load_holds(&ports, &conversions).unwrap().len(), 1);
        fs::write(dir.path().join(HOLDS_FILE), "zlib 9.9.9 why\n").unwrap();
        let err = load_holds(&ports, &conversions).unwrap_err().to_string();
        assert!(err.contains("not a committed port version"), "{err}");
    }

    /// A failed upload may have landed, so it and what requires it wait
    /// for `--retry-failed`; a later upload receipt settles it.
    #[test]
    fn a_failed_upload_waits_for_retry_failed() {
        let (_dir, conversions) = catalog();
        let failed = receipt("libpng", "1.0.0", "failed", 'c');
        let plan_failed = plan(&conversions, &failed, "", false);
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
            plan(&conversions, &failed, "", true),
            [
                "tool candidate",
                "zlib candidate",
                "libpng candidate",
                "app candidate"
            ]
        );
        let settled = failed + &receipt("libpng", "1.0.0", "uploaded", 'c');
        assert_eq!(
            plan(&conversions, &settled, "", false)[2],
            "libpng uploaded"
        );
    }

    /// A candidate validated against an uploaded requirement's local
    /// bytes uploads only when the registry has those same bytes.
    #[test]
    fn a_requirement_staging_other_bytes_than_uploaded_blocks_its_dependents() {
        let (_dir, conversions) = catalog();
        let log = receipt("zlib", "1.0.0", "uploaded", 'a');
        let receipts = Receipts::parse(&log, REGISTRY).unwrap();
        let mut status = classify(&conversions, &receipts, &BTreeMap::new(), false);
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
        let mut status = classify(&conversions, &receipts, &BTreeMap::new(), false);
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
    /// candidate whose closure main changed or now holds, or every
    /// candidate after a shared input changed, waits for a later run.
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
            let mut status = classify(&conversions, &Receipts::default(), &BTreeMap::new(), false);
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

        let main = commit(HOLDS_FILE, "libpng 1.0.0 held after the snapshot\n");
        let plan = defer(&main);
        assert_eq!(plan[..2], ["tool candidate", "zlib candidate"]);
        assert!(
            plan[2].contains("main holds cabin-ports/libpng 1.0.0"),
            "{plan:?}"
        );
        assert!(
            plan[3].contains("main holds cabin-ports/libpng 1.0.0"),
            "{plan:?}"
        );

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
