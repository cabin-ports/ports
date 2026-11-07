//! Tests for the workflow glue in `scripts/actions/`.  Each script runs
//! under `bash`, as a workflow step runs it, in a temporary directory,
//! with its inputs set explicitly and a fake `gh` first on `PATH`.

use std::fmt::Write as _;
use std::path::Path;
use std::process::{Command, Output};

use assert_fs::TempDir;
use assert_fs::fixture::ChildPath;
use assert_fs::prelude::*;

const REV: &str = "0123456789abcdef0123456789abcdef01234567";
const OTHER_REV: &str = "89abcdef0123456789abcdef0123456789abcdef";
const LOCKED_TO_REV: &str = "git+https://github.com/cabinpkg/cabin?rev=0123456789abcdef0123456789abcdef01234567#0123456789abcdef0123456789abcdef01234567";
const PIN_ERROR: &str =
    "::error::Cargo.lock must lock every cabinpkg-* package to one full cabinpkg/cabin commit";

/// Runs `scripts/actions/<name>` with `args` in `cwd`, with `dir/bin`
/// first on `PATH`, the runner variables the scripts read cleared (the
/// runner running these tests sets some), and `env` set.  Setting
/// `PATH` also makes Windows look `bash` up through it, as a runner's
/// `shell: bash` does, instead of in System32 first.
fn run(name: &str, args: &[&str], cwd: &Path, dir: &TempDir, env: &[(&str, &str)]) -> Output {
    let script = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("scripts")
        .join("actions")
        .join(name);
    let path = std::env::var_os("PATH").unwrap_or_default();
    let path = std::env::join_paths(
        std::iter::once(dir.child("bin").to_path_buf()).chain(std::env::split_paths(&path)),
    )
    .unwrap();
    let mut command = Command::new("bash");
    command
        .arg(script)
        .args(args)
        .current_dir(cwd)
        .env("PATH", path);
    for var in [
        "GITHUB_ENV",
        "GITHUB_EVENT_NAME",
        "GITHUB_OUTPUT",
        "GROUP_BASE",
        "GROUP_HEAD",
        "PR_HEAD",
    ] {
        command.env_remove(var);
    }
    command.envs(env.iter().copied()).output().unwrap()
}

fn stdout(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).into_owned()
}

fn stderr(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}

/// An empty file, as the runner creates `$GITHUB_OUTPUT` and `$GITHUB_ENV`.
fn command_file(dir: &TempDir, name: &str) -> ChildPath {
    let file = dir.child(name);
    file.write_str("").unwrap();
    file
}

fn read(file: &ChildPath) -> String {
    std::fs::read_to_string(file).unwrap()
}

/// Writes `dir/Cargo.lock`: the root package, which depends on every
/// cabinpkg-* package, a crates.io package, and `packages` as
/// `(name, source)`, without a `source` line for `None`.
fn lockfile(dir: &TempDir, packages: &[(&str, Option<&str>)]) {
    let mut lock = String::from(
        "version = 4\n\n[[package]]\nname = \"cabin-ports\"\nversion = \"0.0.0\"\n\
         dependencies = [\n \"cabinpkg-core\",\n \"cabinpkg-env\",\n \"serde\",\n]\n\n\
         [[package]]\nname = \"serde\"\nversion = \"1.0.0\"\n\
         source = \"registry+https://github.com/rust-lang/crates.io-index\"\nchecksum = \"00\"\n",
    );
    for (name, source) in packages {
        write!(
            lock,
            "\n[[package]]\nname = \"{name}\"\nversion = \"0.1.0\"\n"
        )
        .unwrap();
        if let Some(source) = source {
            writeln!(lock, "source = \"{source}\"").unwrap();
        }
    }
    dir.child("Cargo.lock").write_str(&lock).unwrap();
}

/// A fake `gh` that prints `reply` and exits with `status` when asked
/// to compare `REV` with Cabin's main, and fails on any other call.
fn fake_gh(dir: &TempDir, reply: &str, status: u8) {
    let gh = dir.child("bin/gh");
    gh.write_str(&format!(
        "#!/bin/sh\n\
         [ \"$*\" = 'api repos/cabinpkg/cabin/compare/{REV}...main --jq .status' ] || \
         {{ echo \"unexpected: gh $*\" >&2; exit 64; }}\n\
         echo '{reply}'\nexit {status}\n"
    ))
    .unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt as _;
        std::fs::set_permissions(gh.path(), std::fs::Permissions::from_mode(0o755)).unwrap();
    }
}

/// Runs `cabin-rev.sh args` against a lockfile with `packages` and a
/// `gh` replying `reply` with `status`, returning the run and what it
/// wrote to `$GITHUB_OUTPUT`.
fn cabin_rev(
    args: &[&str],
    packages: &[(&str, Option<&str>)],
    reply: &str,
    status: u8,
) -> (Output, String) {
    let dir = TempDir::new().unwrap();
    lockfile(&dir, packages);
    fake_gh(&dir, reply, status);
    let output_file = command_file(&dir, "output");
    let output = run(
        "cabin-rev.sh",
        args,
        dir.path(),
        &dir,
        &[("GITHUB_OUTPUT", output_file.to_str().unwrap())],
    );
    (output, read(&output_file))
}

const PINNED: &[(&str, Option<&str>)] = &[
    ("cabinpkg-core", Some(LOCKED_TO_REV)),
    ("cabinpkg-env", Some(LOCKED_TO_REV)),
];

/// The `pin` job's check: a commit `rev...main` reports as `identical`
/// or `ahead` is an ancestor of Cabin's main, so it is printed and
/// becomes the job's `cabin-rev` output.
#[test]
fn cabin_rev_outputs_a_pin_on_cabins_main() {
    for reply in ["identical", "ahead"] {
        let (output, written) = cabin_rev(&["--on-main"], PINNED, reply, 0);
        assert!(output.status.success(), "{reply}: {}", stderr(&output));
        assert_eq!(stdout(&output), format!("{REV}\n"), "{reply}");
        assert_eq!(written, format!("cabin-rev={REV}\n"), "{reply}");
    }
}

/// CONTRIBUTING.md's local use: without `--on-main` the script never
/// calls `gh` (here it would fail), and prints the commit without a
/// `$GITHUB_OUTPUT`, also from a Windows checkout's CRLF lockfile.
#[test]
fn cabin_rev_without_on_main_reads_only_the_lockfile() {
    let dir = TempDir::new().unwrap();
    lockfile(&dir, PINNED);
    fake_gh(&dir, "identical", 1);
    let output = run("cabin-rev.sh", &[], dir.path(), &dir, &[]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(stdout(&output), format!("{REV}\n"));

    let lock = dir.child("Cargo.lock");
    let crlf = read(&lock).replace('\n', "\r\n");
    lock.write_str(&crlf).unwrap();
    let output = run("cabin-rev.sh", &[], dir.path(), &dir, &[]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(stdout(&output), format!("{REV}\n"));
}

/// Every cabinpkg-* package and every git source must be locked to the
/// same full cabinpkg/cabin commit; anything else fails before `gh` is
/// asked (it would answer `identical`) and outputs nothing.
#[test]
fn cabin_rev_rejects_anything_but_one_full_cabin_commit() {
    let short = "git+https://github.com/cabinpkg/cabin?rev=0123456#0123456";
    let other = format!("git+https://github.com/cabinpkg/cabin?rev={OTHER_REV}#{OTHER_REV}");
    let fragment = format!("git+https://github.com/cabinpkg/cabin?rev={REV}#{OTHER_REV}");
    let fork = format!("git+https://github.com/someone/cabin?rev={REV}#{REV}");
    let branch = format!("git+https://github.com/cabinpkg/cabin?branch=main#{REV}");
    let registry = "registry+https://github.com/rust-lang/crates.io-index";
    let foreign = format!("git+https://github.com/someone/other?rev={REV}#{REV}");
    let reject = |case: &str, packages: &[(&str, Option<&str>)]| {
        let (output, written) = cabin_rev(&["--on-main"], packages, "identical", 0);
        assert_eq!(output.status.code(), Some(1), "{case}");
        assert!(
            stderr(&output).contains(PIN_ERROR),
            "{case}: {}",
            stderr(&output)
        );
        assert_eq!(stdout(&output), "", "{case}");
        assert_eq!(written, "", "{case}");
    };
    reject("no cabinpkg package", &[]);
    reject("a package without a source", &[("cabinpkg-core", None)]);
    reject(
        "another package without a source",
        &[PINNED[0], ("cabinpkg-env", None)],
    );
    reject("two commits", &[PINNED[0], ("cabinpkg-env", Some(&other))]);
    reject(
        "a rev that is not the locked commit",
        &[("cabinpkg-core", Some(&fragment))],
    );
    reject("an abbreviated commit", &[("cabinpkg-core", Some(short))]);
    reject("a fork", &[("cabinpkg-core", Some(&fork))]);
    reject("a branch", &[("cabinpkg-core", Some(&branch))]);
    reject(
        "a registry package",
        &[PINNED[0], ("cabinpkg-env", Some(registry))],
    );
    reject(
        "another git source",
        &[PINNED[0], PINNED[1], ("other", Some(&foreign))],
    );
}

/// A commit that is not on Cabin's main fails, and so does a failed
/// `gh` call, whatever it printed.
#[test]
fn cabin_rev_rejects_a_commit_off_cabins_main() {
    for (reply, status) in [("behind", 0), ("diverged", 0), ("identical", 1)] {
        let (output, written) = cabin_rev(&["--on-main"], PINNED, reply, status);
        assert!(!output.status.success(), "{reply} {status}");
        if status == 0 {
            let error =
                format!("::error::{REV} is not on cabinpkg/cabin's main (compare: {reply})");
            assert!(
                stderr(&output).contains(&error),
                "{reply}: {}",
                stderr(&output)
            );
        }
        assert_eq!(stdout(&output), "", "{reply} {status}");
        assert_eq!(written, "", "{reply} {status}");
    }
}

/// A misspelled `--on-main` must not skip the check it asks for.
#[test]
fn cabin_rev_rejects_unknown_arguments() {
    for args in [&["--on-mian"][..], &["--on-main", "--on-main"]] {
        let (output, written) = cabin_rev(args, PINNED, "identical", 0);
        assert_eq!(output.status.code(), Some(2), "{args:?}");
        assert_eq!(stdout(&output), "", "{args:?}");
        assert_eq!(written, "", "{args:?}");
    }
}

/// Run `git` in a fixture repository, isolated from the developer's own
/// Git configuration, returning its trimmed output.
fn git(repo: &Path, args: &[&str]) -> String {
    let output = Command::new("git")
        .current_dir(repo)
        .env("GIT_CONFIG_GLOBAL", repo.with_file_name("gitconfig"))
        .env("GIT_CONFIG_NOSYSTEM", "1")
        .args(["-c", "user.name=t", "-c", "user.email=t@ports.invalid"])
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

/// A repository at `dir/repo` whose `HEAD` is a pull request's test
/// merge: `(repo, HEAD, first parent, second parent)`.
fn test_merge(dir: &TempDir) -> (ChildPath, String, String, String) {
    let repo = dir.child("repo");
    repo.create_dir_all().unwrap();
    git(repo.path(), &["init", "-q"]);
    git(
        repo.path(),
        &["commit", "-q", "--allow-empty", "-m", "base"],
    );
    git(repo.path(), &["checkout", "-q", "-b", "pr"]);
    git(repo.path(), &["commit", "-q", "--allow-empty", "-m", "pr"]);
    git(repo.path(), &["checkout", "-q", "-"]);
    git(
        repo.path(),
        &["commit", "-q", "--allow-empty", "-m", "main"],
    );
    git(repo.path(), &["merge", "-q", "--no-ff", "--no-edit", "pr"]);
    let head = git(repo.path(), &["rev-parse", "HEAD"]);
    let first = git(repo.path(), &["rev-parse", "HEAD^1"]);
    let second = git(repo.path(), &["rev-parse", "HEAD^2"]);
    (repo, head, first, second)
}

/// Runs `dry-run-base.sh` in `repo` for `event` with `env`, returning
/// the run and what it wrote to `$GITHUB_ENV`.
fn dry_run_base(dir: &TempDir, repo: &Path, event: &str, env: &[(&str, &str)]) -> (Output, String) {
    let env_file = command_file(dir, "env");
    let mut vars = vec![
        ("GITHUB_EVENT_NAME", event),
        ("GITHUB_ENV", env_file.to_str().unwrap()),
    ];
    vars.extend_from_slice(env);
    let output = run("dry-run-base.sh", &[], repo, dir, &vars);
    (output, read(&env_file))
}

/// A pull request's test merge whose second parent is the pull
/// request's head narrows the dry-run to the change since the first
/// parent.
#[test]
fn dry_run_base_is_a_verified_test_merges_first_parent() {
    let dir = TempDir::new().unwrap();
    let (repo, _, first, second) = test_merge(&dir);
    let (output, env) = dry_run_base(&dir, &repo, "pull_request", &[("PR_HEAD", &second)]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(env, format!("PORT_PUBLISH_CHANGED_SINCE={first}\n"));
    assert!(
        !stdout(&output).contains("::warning::"),
        "{}",
        stdout(&output)
    );
}

/// A merge group whose head is checked out narrows the dry-run to the
/// change since the group's base, which the event names (not a parent).
#[test]
fn dry_run_base_is_a_verified_merge_groups_base() {
    let dir = TempDir::new().unwrap();
    let (repo, head, _, _) = test_merge(&dir);
    let base = "b".repeat(40);
    let (output, env) = dry_run_base(
        &dir,
        &repo,
        "merge_group",
        &[("GROUP_HEAD", &head), ("GROUP_BASE", &base)],
    );
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(env, format!("PORT_PUBLISH_CHANGED_SINCE={base}\n"));
    assert!(
        !stdout(&output).contains("::warning::"),
        "{}",
        stdout(&output)
    );
}

/// Event metadata that does not match the checked-out commit, or is
/// missing, sets no base: the dry-run then checks every port, never
/// none.
#[test]
fn dry_run_base_without_verified_metadata_checks_every_port() {
    let dir = TempDir::new().unwrap();
    let (repo, head, first, second) = test_merge(&dir);
    let linear = dir.child("linear");
    linear.create_dir_all().unwrap();
    git(linear.path(), &["init", "-q"]);
    git(
        linear.path(),
        &["commit", "-q", "--allow-empty", "-m", "only"],
    );
    let only = git(linear.path(), &["rev-parse", "HEAD"]);
    let base = "b".repeat(40);
    let full = |case: &str, cwd: &Path, event: &str, env: &[(&str, &str)]| {
        let (output, written) = dry_run_base(&dir, cwd, event, env);
        assert!(output.status.success(), "{case}: {}", stderr(&output));
        assert_eq!(written, "", "{case}");
        let warning = format!(
            "::warning::HEAD does not match the {event} event; the dry-run checks every port"
        );
        assert!(
            stdout(&output).contains(&warning),
            "{case}: {}",
            stdout(&output)
        );
    };
    let pr = "pull_request";
    full("no pull request head", &repo, pr, &[("PR_HEAD", "")]);
    full("the first parent", &repo, pr, &[("PR_HEAD", &first)]);
    full("the merge itself", &repo, pr, &[("PR_HEAD", &head)]);
    full("no second parent", &linear, pr, &[("PR_HEAD", &only)]);
    let group = "merge_group";
    full(
        "no group head",
        &repo,
        group,
        &[("GROUP_HEAD", ""), ("GROUP_BASE", &base)],
    );
    full(
        "another group head",
        &repo,
        group,
        &[("GROUP_HEAD", &first), ("GROUP_BASE", &base)],
    );
    full(
        "no group base",
        &repo,
        group,
        &[("GROUP_HEAD", &head), ("GROUP_BASE", "")],
    );
    let all = [
        ("PR_HEAD", second.as_str()),
        ("GROUP_HEAD", &head),
        ("GROUP_BASE", &base),
    ];
    full("another event", &repo, "push", &all);
    full("no event", &repo, "", &all);
}
