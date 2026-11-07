//! Which committed ports a dry-run preflights.
//!
//! The whole catalog is always loaded and validated
//! ([`crate::plan::load_conversions`]) and every port's consumer tests
//! are checked; a selection narrows only the expensive part - fetching,
//! materializing, staging, building, and running probes.
//!
//! `--changed-since <base>` narrows only a change made of nothing but
//! package names the base does not have, plus allowlisted
//! documentation.  Its premise is inductive: in CI the base is `main`,
//! or the merge-queue entry ahead, and a change reaches `main` only once
//! its own run - full, or selective under this same argument - passed,
//! the queue requiring every entry to pass, not just a group's head
//! (its `ALLGREEN` grouping).  A change of that shape cannot alter
//! how a base package builds or resolves: its files are unchanged, the
//! publisher and its inputs are unchanged, and no base package can
//! require a new name, because the base already validated that every
//! requirement it declares matches one of its own versions.  So only the
//! new packages need a preflight, against a registry holding every
//! version their requirements could resolve to.  Anything else - a new
//! version of an existing name included, since existing requirements may
//! match it - preflights every port.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{Context, Result, bail};

use crate::plan::PortConversion;

/// Repository-root files that reach no build and no published archive.
/// Anything else outside the ports directory is a shared input.  The
/// hold list only gates `--publish-new`, and every mode validates it.
pub(crate) const NOT_BUILD_INPUTS: &[&str] = &["README.md", "AGENTS.md", crate::batch::HOLDS_FILE];

/// Which ports a dry-run preflights.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Selection {
    /// Every committed port.
    All,
    /// What the change since this Git revision needs (module docs).
    /// The comparison runs against the working tree, uncommitted and
    /// untracked files included, so it sees exactly what the preflight
    /// reads.
    ChangedSince(String),
    /// Every committed version of these package names, plus their
    /// dependencies: a focused local check, never CI's policy.
    Ports(Vec<String>),
}

/// The publication-ordered conversions a dry-run preflights under
/// `selection`, after printing what it chose and why.
///
/// # Errors
/// Returns an error when Git cannot establish the change (an unknown
/// base, one that is not an ancestor of `HEAD`, or a directory outside
/// a repository), or when a selected name has no committed version.
pub fn select(
    conversions: Vec<PortConversion>,
    selection: &Selection,
    ports_dir: &Path,
) -> Result<Vec<PortConversion>> {
    let roots: BTreeSet<String> = match selection {
        Selection::All => return Ok(conversions),
        Selection::Ports(names) => {
            println!("selection: the requested ports and their dependencies (a local check)");
            names.iter().cloned().collect()
        }
        Selection::ChangedSince(base) => match changed_since(ports_dir, base)? {
            Change::NewPackages(names) if names.is_empty() => {
                println!(
                    "selection: no port or build input changed since {base}; nothing to preflight"
                );
                return Ok(Vec::new());
            }
            Change::NewPackages(names) => {
                println!(
                    "selection: only new packages changed since {base}: {}",
                    names.iter().cloned().collect::<Vec<_>>().join(", ")
                );
                names
            }
            Change::Other(reason) => {
                println!("selection: every port, since {reason}");
                return Ok(conversions);
            }
        },
    };
    closure(conversions, &roots)
}

/// What a change since some base consists of.
#[derive(Debug, PartialEq, Eq)]
enum Change {
    /// Only files under these new package names, plus documentation
    /// (none at all for a documentation-only change).
    NewPackages(BTreeSet<String>),
    /// Anything else, named by the first path that makes it so.
    Other(String),
}

/// Compare `base` with the working tree of the repository holding
/// `ports_dir`.
fn changed_since(ports_dir: &Path, base: &str) -> Result<Change> {
    let (top, prefix) = locate(ports_dir)?;
    let (top, prefix) = (top.as_path(), prefix.as_str());
    let base_commit = format!("{base}^{{commit}}");
    let base = git(
        top,
        &["rev-parse", "--verify", "--end-of-options", &base_commit],
    )
    .with_context(|| format!("resolving the comparison base `{base}`"))?;
    let base = base.trim();
    git(top, &["merge-base", "--is-ancestor", base, "HEAD"]).with_context(|| {
        format!("{base} is not an ancestor of HEAD; pass the commit the change starts from")
    })?;

    let base_files = git(
        top,
        &["ls-tree", "-r", "-z", "--name-only", base, "--", prefix],
    )?;
    let base_names: BTreeSet<&str> = base_files
        .split('\0')
        .filter_map(|path| path.strip_prefix(prefix)?.split('/').nth(1))
        .collect();
    let diff = git(
        top,
        &[
            "diff",
            "-z",
            "--name-status",
            "--no-renames",
            "--no-ext-diff",
            "--no-color",
            base,
        ],
    )?;
    let mut untracked = git(top, &["ls-files", "-z", "--others", "--exclude-standard"])?;
    // Discovery reads ignored files under the ports directory too.
    untracked += &git(top, &["ls-files", "-z", "--others", "--", prefix])?;
    let mut changes = Vec::new();
    let mut fields = diff.split('\0').filter(|field| !field.is_empty());
    while let Some(status) = fields.next() {
        let Some(path) = fields.next() else {
            bail!("git diff printed status `{status}` without a path");
        };
        changes.push((status, path));
    }
    changes.extend(
        untracked
            .split('\0')
            .filter(|path| !path.is_empty())
            .map(|path| ("A", path)),
    );
    Ok(classify(prefix, &base_names, &changes))
}

/// The root of the repository holding `ports_dir`, and the ports
/// directory's root-relative path with its trailing `/`.  Commands run
/// from the root print root-relative paths whatever `diff.relative`
/// says.
pub(crate) fn locate(ports_dir: &Path) -> Result<(PathBuf, String)> {
    let located = git(
        ports_dir,
        &["rev-parse", "--show-toplevel", "--show-prefix"],
    )?;
    let mut lines = located.lines();
    let (Some(top), Some(prefix)) = (lines.next(), lines.next()) else {
        bail!(
            "git rev-parse printed no repository root for {}",
            ports_dir.display()
        );
    };
    Ok((PathBuf::from(top), prefix.to_owned()))
}

/// Classify changed `(status, root-relative path)` pairs; `prefix` is
/// the ports directory's root-relative path with its trailing `/`, and
/// `base_names` the package names the base has (the directories one
/// level below its prefix directories).  One path outside the narrow
/// shape makes the whole change [`Change::Other`].
fn classify(prefix: &str, base_names: &BTreeSet<&str>, changes: &[(&str, &str)]) -> Change {
    let mut names = BTreeSet::new();
    for &(status, path) in changes {
        let Some(port_path) = path.strip_prefix(prefix) else {
            if NOT_BUILD_INPUTS.contains(&path) {
                continue;
            }
            return Change::Other(format!("{path} is a shared input"));
        };
        // `<prefix>/<name>/<version>/<file>`: anything shallower is not
        // part of a package the publisher loads.  The package is the
        // `<name>` directory, never its prefix directory, which other
        // names share; discovery has already refused a misplaced name.
        let mut parts = port_path.splitn(4, '/');
        let (Some(_), Some(name), Some(_), Some(_)) =
            (parts.next(), parts.next(), parts.next(), parts.next())
        else {
            return Change::Other(format!(
                "{path} is outside any {prefix}<prefix>/<name>/<version>/"
            ));
        };
        if base_names.contains(name) {
            return Change::Other(format!(
                "{path} changes `{name}`, which the base already has"
            ));
        }
        if status != "A" {
            return Change::Other(format!("{path} has status {status}, not an addition"));
        }
        names.insert(name.to_owned());
    }
    Change::NewPackages(names)
}

/// Every committed version of the `roots` names, plus every committed
/// version any selected package's declared requirements match,
/// transitively, in publication order.  Optional and dev requirements
/// are followed too: the probe resolves only default features, but
/// following every declared edge keeps each version the resolver could
/// pick under any feature set, at the cost of staging a few packages
/// nothing activates.
fn closure(
    conversions: Vec<PortConversion>,
    roots: &BTreeSet<String>,
) -> Result<Vec<PortConversion>> {
    let mut pending = Vec::new();
    for root in roots {
        let before = pending.len();
        pending.extend(
            conversions
                .iter()
                .enumerate()
                .filter(|(_, c)| c.scoped_name.base_name() == root)
                .map(|(index, _)| index),
        );
        if pending.len() == before {
            bail!("no committed port version is named `{root}`");
        }
    }
    let selected = closure_of(&conversions, pending);

    let total = conversions.len();
    let chosen: Vec<PortConversion> = conversions
        .into_iter()
        .zip(selected)
        .filter_map(|(conversion, selected)| selected.then_some(conversion))
        .collect();
    let (root_ports, dependencies): (Vec<&PortConversion>, Vec<&PortConversion>) = chosen
        .iter()
        .partition(|c| roots.contains(c.scoped_name.base_name()));
    let list = |ports: &[&PortConversion]| {
        if ports.is_empty() {
            return "none".to_owned();
        }
        ports
            .iter()
            .map(|c| format!("{} {}", c.scoped_name.as_str(), c.published_version))
            .collect::<Vec<_>>()
            .join(", ")
    };
    println!("selection: roots: {}", list(&root_ports));
    println!("selection: dependencies: {}", list(&dependencies));
    println!(
        "selection: preflighting {} of {total} packages",
        chosen.len()
    );
    Ok(chosen)
}

/// Which conversions `roots` reach, themselves included: every version
/// any reached package's declared requirements match, transitively (see
/// [`closure`] for why optional and dev requirements count).
pub(crate) fn closure_of(
    conversions: &[PortConversion],
    roots: impl IntoIterator<Item = usize>,
) -> Vec<bool> {
    let mut selected = vec![false; conversions.len()];
    let mut pending: Vec<usize> = roots.into_iter().collect();
    while let Some(index) = pending.pop() {
        if std::mem::replace(&mut selected[index], true) {
            continue;
        }
        pending.extend(requirements_of(conversions, index));
    }
    selected
}

/// The versions `conversions[index]`'s declared requirements match.
pub(crate) fn requirements_of(
    conversions: &[PortConversion],
    index: usize,
) -> impl Iterator<Item = usize> + '_ {
    conversions[index]
        .declared_requirements
        .iter()
        .flat_map(move |req| {
            conversions.iter().enumerate().filter(move |(_, c)| {
                c.scoped_name == req.scoped && req.req.matches(&c.published_version)
            })
        })
        .map(|(index, _)| index)
}

/// Run `git` in `dir` and return its stdout, failing on a non-zero
/// exit or a non-UTF-8 path (no port path can be one).
pub(crate) fn git(dir: &Path, args: &[&str]) -> Result<String> {
    let output = Command::new("git")
        .current_dir(dir)
        .args(args)
        .output()
        .with_context(|| format!("running git {}", args.join(" ")))?;
    if !output.status.success() {
        bail!(
            "git {} failed ({}): {}",
            args.join(" "),
            output.status,
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }
    String::from_utf8(output.stdout)
        .with_context(|| format!("git {} printed non-UTF-8", args.join(" ")))
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::PathBuf;

    use super::*;
    use crate::plan::tests::{package_manifest, write_package};

    fn new_packages(names: &[&str]) -> Change {
        Change::NewPackages(names.iter().map(|name| (*name).to_owned()).collect())
    }

    #[test]
    fn only_additions_under_new_package_names_narrow_a_change() {
        let base = BTreeSet::from(["zlib", "libpng"]);
        let fmt = ("A", "ports/fm/fmt/12.2.0/cabin.toml");
        for (changes, expected) in [
            (
                vec![fmt, ("A", "ports/fm/fmt/12.2.0/tests/main.cc")],
                new_packages(&["fmt"]),
            ),
            (
                vec![
                    fmt,
                    ("A", "ports/xx/xxhash/0.8.3/cabin.toml"),
                    ("M", "README.md"),
                ],
                new_packages(&["fmt", "xxhash"]),
            ),
            // A new name is new though the base has its prefix: the
            // package is the name directory, not the prefix.
            (
                vec![
                    ("A", "ports/zl/zlib-ng/2.2.4/cabin.toml"),
                    ("A", "ports/zl/zlib-ng/2.2.4/patches/0001-a.patch"),
                    ("A", "ports/li/libpng-apng/1.0.0/cabin.toml"),
                ],
                new_packages(&["libpng-apng", "zlib-ng"]),
            ),
            // A one-character name is its own prefix.
            (
                vec![("A", "ports/z/z/1.0.0/cabin.toml")],
                new_packages(&["z"]),
            ),
            (
                vec![("M", "README.md"), ("D", "AGENTS.md")],
                new_packages(&[]),
            ),
            // The hold list reaches no build; every mode validates it.
            (
                vec![fmt, ("M", "publish-holds.txt")],
                new_packages(&["fmt"]),
            ),
            (vec![], new_packages(&[])),
        ] {
            assert_eq!(classify("ports/", &base, &changes), expected, "{changes:?}");
        }
    }

    /// One path outside the narrow shape widens the whole change,
    /// whatever else it adds.
    #[test]
    fn any_other_path_widens_the_whole_change() {
        let base = BTreeSet::from(["zlib", "libpng"]);
        for other in [
            // A new version of an existing name can satisfy existing
            // requirements.
            ("A", "ports/zl/zlib/1.4.0/cabin.toml"),
            ("M", "ports/zl/zlib/1.3.1/cabin.toml"),
            ("A", "ports/zl/zlib/1.3.1/tests/main.cc"),
            ("A", "ports/zl/zlib/1.3.1/README.md"),
            ("D", "ports/li/libpng/1.6.50/cabin.toml"),
            ("T", "ports/zl/zlib/1.3.1/patches/0001-fix.patch"),
            // Not inside a version directory.
            ("A", "ports/fm/fmt/README.md"),
            ("A", "ports/fm/fmt"),
            ("A", "ports/fm/README.md"),
            ("A", "ports/fm"),
            // The flat layout's depth.
            ("A", "ports/fmt/12.2.0/cabin.toml"),
            // Not an addition, under a name the base lacks.
            ("M", "ports/fm/fmt/12.2.0/cabin.toml"),
            ("M", "src/plan.rs"),
            ("M", "Cargo.lock"),
            ("M", "rust-toolchain.toml"),
            ("M", ".github/workflows/ci.yml"),
            ("M", ".gitattributes"),
            ("A", "docs/README.md"),
        ] {
            let changes = [
                ("A", "ports/fm/fmt/12.2.0/cabin.toml"),
                ("M", "README.md"),
                other,
            ];
            let Change::Other(reason) = classify("ports/", &base, &changes) else {
                panic!("{other:?} must widen the change");
            };
            assert!(reason.contains(other.1), "{reason}");
        }
    }

    /// The selection is every version of the roots plus every version
    /// any reached requirement matches - optional, feature-activated,
    /// and dev edges included - deduplicated, in publication order.
    #[test]
    fn the_closure_keeps_every_candidate_version_and_nothing_unrelated() {
        let dir = assert_fs::TempDir::new().unwrap();
        let port = |name: &str, version: &str, extra: &str| {
            let manifest = package_manifest(&format!("cabin-ports/{name}"), version, extra);
            write_package(dir.path(), name, version, &manifest);
        };
        let dep = |name: &str, req: &str| format!("\"cabin-ports/{name}\" = \"{req}\"\n");
        port("zlib", "1.3.1", "");
        port("zlib", "1.3.2", "");
        port(
            "zlib",
            "2.0.0",
            &format!("[dependencies]\n{}\n", dep("xxhash", "^0.8")),
        );
        port("xxhash", "0.8.3", "");
        port("apng", "1.0.0", "");
        port("devdep", "1.0.0", "");
        port("unrelated", "1.0.0", "");
        port(
            "libpng",
            "1.6.50",
            "[features]\napng = [\"dep:cabin-ports/apng\"]\n\n[dependencies]\n\
             \"cabin-ports/zlib\" = \"^1.3\"\n\
             \"cabin-ports/apng\" = { version = \"1\", optional = true }\n\n",
        );
        // Requests libpng's non-default feature, which activates an
        // optional dependency the default publication edges never see.
        port(
            "newport",
            "1.0.0",
            &format!(
                "[dependencies]\n\"cabin-ports/libpng\" = {{ version = \"^1.6\", features = \
                 [\"apng\"] }}\n\n[dev-dependencies]\n{}\n",
                dep("devdep", "1")
            ),
        );
        port(
            "other",
            "1.0.0",
            &format!("[dependencies]\n{}\n", dep("zlib", ">=1.3")),
        );

        let select = |roots: &[&str]| -> Vec<String> {
            let conversions = crate::plan::load_conversions(dir.path()).unwrap();
            let roots = roots.iter().map(|root| (*root).to_owned()).collect();
            closure(conversions, &roots)
                .unwrap()
                .iter()
                .map(|c| format!("{} {}", c.scoped_name.base_name(), c.published_version))
                .collect()
        };
        assert_eq!(
            select(&["newport"]),
            [
                "apng 1.0.0",
                "devdep 1.0.0",
                "zlib 1.3.1",
                "zlib 1.3.2",
                "libpng 1.6.50",
                "newport 1.0.0"
            ]
        );
        // A shared dependency appears once; `>=1.3` reaches zlib 2.0.0
        // and, through it, xxhash.
        assert_eq!(
            select(&["newport", "other"]),
            [
                "apng 1.0.0",
                "devdep 1.0.0",
                "xxhash 0.8.3",
                "zlib 1.3.1",
                "zlib 1.3.2",
                "libpng 1.6.50",
                "other 1.0.0",
                "zlib 2.0.0",
                "newport 1.0.0"
            ]
        );

        // A root with no loaded version - a directory without a
        // manifest, or a typo - never shrinks to an empty selection.
        let conversions = crate::plan::load_conversions(dir.path()).unwrap();
        let err = closure(conversions, &BTreeSet::from(["nosuch".to_owned()])).unwrap_err();
        assert!(err.to_string().contains("`nosuch`"), "{err}");
    }

    /// Run `git` for a fixture, isolated from the developer's own Git
    /// configuration (signing, hooks, default branch).
    fn fixture_git(repo: &Path, args: &[&str]) -> String {
        let output = Command::new("git")
            .current_dir(repo)
            .env("GIT_CONFIG_GLOBAL", repo.with_file_name("gitconfig"))
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

    fn write(repo: &Path, path: &str, contents: &str) {
        let path = repo.join(path);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, contents).unwrap();
    }

    fn commit(repo: &Path, message: &str) -> String {
        fixture_git(repo, &["add", "-A"]);
        fixture_git(repo, &["commit", "-q", "-m", message]);
        fixture_git(repo, &["rev-parse", "HEAD"])
    }

    /// A repository whose base commit holds `zlib` and a README.
    fn base_repo(dir: &assert_fs::TempDir) -> (PathBuf, String) {
        let repo = dir.path().join("repo");
        fs::create_dir_all(&repo).unwrap();
        fixture_git(&repo, &["init", "-q"]);
        write(&repo, "ports/zl/zlib/1.3.1/cabin.toml", "zlib\n");
        write(&repo, "README.md", "ports\n");
        let base = commit(&repo, "base");
        (repo, base)
    }

    /// The delta spans every commit since the base, not just the last,
    /// plus uncommitted and untracked files.
    #[test]
    fn a_change_spans_every_commit_since_the_base_and_the_working_tree() {
        let dir = assert_fs::TempDir::new().unwrap();
        let (repo, base) = base_repo(&dir);
        let ports = repo.join("ports");
        write(&repo, "ports/fm/fmt/12.2.0/cabin.toml", "fmt\n");
        commit(&repo, "add fmt");
        write(&repo, "README.md", "ports, edited\n");
        commit(&repo, "edit the README");
        write(&repo, "ports/xx/xxhash/0.8.3/cabin.toml", "untracked\n");
        assert_eq!(
            changed_since(&ports, &base).unwrap(),
            new_packages(&["fmt", "xxhash"])
        );
        assert_eq!(
            changed_since(&ports, "HEAD~1").unwrap(),
            new_packages(&["xxhash"])
        );

        write(
            &repo,
            "ports/zl/zlib/1.3.1/cabin.toml",
            "zlib, uncommitted\n",
        );
        assert!(matches!(
            changed_since(&ports, &base).unwrap(),
            Change::Other(reason) if reason.contains("ports/zl/zlib/1.3.1/cabin.toml")
        ));
        write(&repo, "ports/zl/zlib/1.3.1/cabin.toml", "zlib\n");

        write(&repo, "notes.txt", "untracked\n");
        assert!(matches!(
            changed_since(&ports, &base).unwrap(),
            Change::Other(reason) if reason.contains("notes.txt")
        ));
        fs::remove_file(repo.join("notes.txt")).unwrap();
        // Ignored or not, an untracked file in an existing port widens it.
        write(&repo, ".git/info/exclude", "1.4.0/\n");
        write(&repo, "ports/zl/zlib/1.4.0/cabin.toml", "ignored\n");
        assert!(matches!(
            changed_since(&ports, &base).unwrap(),
            Change::Other(reason) if reason.contains("ports/zl/zlib/1.4.0/cabin.toml")
        ));

        // An earlier commit's change to an existing port widens a
        // change whose last commit only adds a port.
        let dir = assert_fs::TempDir::new().unwrap();
        let (repo, base) = base_repo(&dir);
        write(&repo, "ports/zl/zlib/1.3.1/cabin.toml", "zlib, edited\n");
        commit(&repo, "edit zlib");
        write(&repo, "ports/fm/fmt/12.2.0/cabin.toml", "fmt\n");
        commit(&repo, "add fmt");
        assert!(matches!(
            changed_since(&repo.join("ports"), &base).unwrap(),
            Change::Other(_)
        ));
    }

    /// The base's names are the directories below its prefix
    /// directories: a new name under a prefix the base has is new, a
    /// new version of a name the base has is not, and a move between
    /// two places (deleting one path, adding another) is no addition.
    #[test]
    fn a_new_name_under_an_existing_prefix_is_new() {
        let dir = assert_fs::TempDir::new().unwrap();
        let (repo, base) = base_repo(&dir);
        let ports = repo.join("ports");
        write(&repo, "ports/zl/zlib-ng/2.2.4/cabin.toml", "zlib-ng\n");
        commit(&repo, "add zlib-ng");
        assert_eq!(
            changed_since(&ports, &base).unwrap(),
            new_packages(&["zlib-ng"])
        );

        write(&repo, "ports/zl/zlib/1.4.0/cabin.toml", "zlib\n");
        assert!(matches!(
            changed_since(&ports, &base).unwrap(),
            Change::Other(reason) if reason.contains("ports/zl/zlib/1.4.0/cabin.toml")
        ));
        fs::remove_dir_all(repo.join("ports/zl/zlib/1.4.0")).unwrap();

        fixture_git(&repo, &["mv", "ports/zl/zlib-ng", "ports/zl/zlibng"]);
        commit(&repo, "rename zlib-ng");
        assert!(matches!(
            changed_since(&ports, "HEAD~1").unwrap(),
            Change::Other(reason) if reason.contains("ports/zl/zlib-ng/2.2.4/cabin.toml")
        ));
    }

    /// A pull request's test merge compared with its first parent sees
    /// only the pull request, though `main` moved meanwhile.
    #[test]
    fn a_test_merge_compared_with_its_first_parent_sees_only_the_pull_request() {
        let dir = assert_fs::TempDir::new().unwrap();
        let (repo, base) = base_repo(&dir);
        fixture_git(&repo, &["switch", "-q", "-c", "pr"]);
        write(&repo, "ports/fm/fmt/12.2.0/cabin.toml", "fmt\n");
        commit(&repo, "add fmt");
        fixture_git(&repo, &["switch", "-q", "main"]);
        write(&repo, "ports/zl/zlib/1.3.1/cabin.toml", "zlib, edited\n");
        commit(&repo, "edit zlib");
        fixture_git(&repo, &["merge", "-q", "--no-ff", "-m", "test merge", "pr"]);

        let ports = repo.join("ports");
        assert_eq!(
            changed_since(&ports, "HEAD^1").unwrap(),
            new_packages(&["fmt"])
        );
        assert!(matches!(
            changed_since(&ports, &base).unwrap(),
            Change::Other(_)
        ));
    }

    /// A base Git cannot verify fails the run rather than narrowing it.
    #[test]
    fn an_unverifiable_base_is_an_error() {
        let dir = assert_fs::TempDir::new().unwrap();
        let (repo, base) = base_repo(&dir);
        let ports = repo.join("ports");
        fixture_git(&repo, &["switch", "-q", "-c", "side"]);
        write(&repo, "README.md", "side\n");
        let side = commit(&repo, "side");
        fixture_git(&repo, &["switch", "-q", "main"]);
        write(&repo, "ports/fm/fmt/12.2.0/cabin.toml", "fmt\n");
        commit(&repo, "add fmt");

        let err = changed_since(&ports, &side).unwrap_err();
        assert!(
            format!("{err:#}").contains("not an ancestor of HEAD"),
            "{err:#}"
        );
        let missing = "0123456789abcdef0123456789abcdef01234567";
        assert!(changed_since(&ports, missing).is_err());

        // A shallow clone lacks the base's objects.
        let shallow = dir.path().join("shallow");
        fixture_git(
            dir.path(),
            &[
                "clone",
                "-q",
                "--no-local",
                "--depth",
                "1",
                "repo",
                "shallow",
            ],
        );
        let err = changed_since(&shallow.join("ports"), &base).unwrap_err();
        assert!(format!("{err:#}").contains(&base), "{err:#}");

        let outside = assert_fs::TempDir::new().unwrap();
        fs::create_dir(outside.path().join("ports")).unwrap();
        assert!(changed_since(&outside.path().join("ports"), "HEAD").is_err());
    }
}
