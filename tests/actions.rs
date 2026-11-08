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

/// A fake `gh` that prints `reply` and exits with `status` when called
/// with exactly the arguments `call`, and fails on any other call.
fn fake_gh(dir: &TempDir, call: &str, reply: &str, status: u8) {
    let gh = dir.child("bin/gh");
    gh.write_str(&format!(
        "#!/bin/sh\n\
         [ \"$*\" = '{call}' ] || {{ echo \"unexpected: gh $*\" >&2; exit 64; }}\n\
         echo '{reply}'\nexit {status}\n"
    ))
    .unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt as _;
        std::fs::set_permissions(gh.path(), std::fs::Permissions::from_mode(0o755)).unwrap();
    }
}

/// The `gh` call that compares `REV` with Cabin's main.
fn compare_call() -> String {
    format!("api repos/cabinpkg/cabin/compare/{REV}...main --jq .status")
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
    fake_gh(&dir, &compare_call(), reply, status);
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
    fake_gh(&dir, &compare_call(), "identical", 1);
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

/// Runs `publish-mode.sh` for `event` with `env`, returning the run and
/// what it wrote to `$GITHUB_OUTPUT`.
fn publish_mode(event: &str, env: &[(&str, &str)]) -> (Output, String) {
    let dir = TempDir::new().unwrap();
    let output_file = command_file(&dir, "output");
    let mut vars = vec![
        ("GITHUB_EVENT_NAME", event),
        ("GITHUB_OUTPUT", output_file.to_str().unwrap()),
    ];
    vars.extend_from_slice(env);
    let output = run("publish-mode.sh", &[], dir.path(), &dir, &vars);
    (output, read(&output_file))
}

/// A scheduled run is `new` only while the variable is exactly `true`
/// (expressions compare case-insensitively); any other value turns it
/// off with a notice, which is no failure.
#[test]
fn publish_mode_schedules_new_only_while_exactly_enabled() {
    let (output, written) = publish_mode("schedule", &[("ENABLED", "true"), ("INPUT", "")]);
    assert!(output.status.success(), "{}", stderr(&output));
    assert_eq!(written, "mode=new\n");
    for enabled in ["TRUE", " true", "", "false"] {
        let (output, written) = publish_mode("schedule", &[("ENABLED", enabled), ("INPUT", "all")]);
        assert!(output.status.success(), "{enabled:?}: {}", stderr(&output));
        assert_eq!(written, "mode=off\n", "{enabled:?}");
        assert!(
            stdout(&output).contains("::notice::PORTS_AUTO_PUBLISH is not exactly 'true'"),
            "{enabled:?}: {}",
            stdout(&output)
        );
    }
}

/// A dispatch runs the mode it names, whatever the variable says, and
/// fails on anything else.
#[test]
fn publish_mode_dispatches_the_named_mode() {
    for mode in ["all", "new", "plan"] {
        let (output, written) =
            publish_mode("workflow_dispatch", &[("ENABLED", ""), ("INPUT", mode)]);
        assert!(output.status.success(), "{mode}: {}", stderr(&output));
        assert_eq!(written, format!("mode={mode}\n"));
    }
    for mode in ["", "New", "off"] {
        let (output, written) =
            publish_mode("workflow_dispatch", &[("ENABLED", "true"), ("INPUT", mode)]);
        assert_eq!(output.status.code(), Some(1), "{mode:?}");
        assert_eq!(written, "", "{mode:?}");
        assert!(
            stdout(&output).contains(&format!("::error::unknown mode '{mode}'")),
            "{mode:?}: {}",
            stdout(&output)
        );
    }
}

/// `require-ci.sh` with a `gh` that counts `reply` successful
/// `required` check runs on `REV` and exits with `status`.
fn require_ci(reply: &str, status: u8) -> Output {
    let dir = TempDir::new().unwrap();
    fake_gh(
        &dir,
        &format!(
            "api repos/o/r/commits/{REV}/check-runs?check_name=required --jq \
             [.check_runs[] | select(.conclusion == \"success\")] | length"
        ),
        reply,
        status,
    );
    run(
        "require-ci.sh",
        &[],
        dir.path(),
        &dir,
        &[("GITHUB_REPOSITORY", "o/r"), ("GITHUB_SHA", REV)],
    )
}

/// An unattended upload needs at least one successful `required` run on
/// its commit; none, or a failed `gh` call, fails it.
#[test]
fn require_ci_needs_a_successful_required_run() {
    for reply in ["1", "2"] {
        let output = require_ci(reply, 0);
        assert!(output.status.success(), "{reply}: {}", stderr(&output));
    }
    let output = require_ci("0", 0);
    assert_eq!(output.status.code(), Some(1));
    let error = format!("::error::{REV} has no successful 'required' check run");
    assert!(stdout(&output).contains(&error), "{}", stdout(&output));
    for (reply, status) in [("1", 1), ("", 0)] {
        let output = require_ci(reply, status);
        assert!(!output.status.success(), "{reply:?} {status}");
    }
}

/// `receipts.sh` against a bare `origin.git` reached through a `file://`
/// URL.  The publish workflow runs it on Linux only.
#[cfg(unix)]
mod receipts {
    use std::os::unix::fs::PermissionsExt as _;

    use super::*;

    const BRANCH: &str = "publish-receipts";
    const LOG: &str =
        "{\"name\":\"cabin-ports/zlib\",\"version\":\"1.3.1\",\"outcome\":\"uploaded\"}\n";

    fn executable(file: &ChildPath, text: &str) {
        file.write_str(text).unwrap();
        std::fs::set_permissions(file.path(), std::fs::Permissions::from_mode(0o755)).unwrap();
    }

    /// The runner's view of a repository whose receipts branch, unless
    /// absent, starts as one commit of `files`.  Its `gh` fails on any
    /// call until a test fakes one.
    struct Repo {
        dir: TempDir,
    }

    impl Repo {
        fn new(files: &[(&str, &str)]) -> Self {
            let dir = TempDir::new().unwrap();
            git(dir.path(), &["init", "-q", "--bare", "origin.git"]);
            if !files.is_empty() {
                let seed = dir.child("seed");
                seed.create_dir_all().unwrap();
                git(seed.path(), &["init", "-q"]);
                for (name, text) in files {
                    seed.child(name).write_str(text).unwrap();
                }
                git(seed.path(), &["add", "."]);
                git(seed.path(), &["commit", "-q", "-m", "seed"]);
                let push = format!("HEAD:refs/heads/{BRANCH}");
                git(seed.path(), &["push", "-q", "../origin.git", &push]);
            }
            dir.child("runner").create_dir_all().unwrap();
            fake_gh(&dir, "nothing", "", 64);
            Self { dir }
        }

        /// Runs `receipts.sh args` as attempt 1 of the dispatched run 111,
        /// with `env` on top, returning the run and its `$GITHUB_OUTPUT`.
        fn run(&self, args: &[&str], env: &[(&str, &str)]) -> (Output, String) {
            let output_file = command_file(&self.dir, "output");
            let server = format!("file://{}", self.dir.path().display());
            let runner = self.dir.child("runner");
            let gitconfig = self.dir.child("gitconfig");
            let mut vars = vec![
                ("GITHUB_SERVER_URL", server.as_str()),
                ("GITHUB_REPOSITORY", "origin.git"),
                ("RECEIPTS_BRANCH", BRANCH),
                ("RUNNER_TEMP", runner.to_str().unwrap()),
                ("GITHUB_OUTPUT", output_file.to_str().unwrap()),
                ("GITHUB_EVENT_NAME", "workflow_dispatch"),
                ("GITHUB_RUN_ID", "111"),
                ("GITHUB_RUN_ATTEMPT", "1"),
                ("GITHUB_SHA", REV),
                ("GH_TOKEN", "token"),
                ("RECEIPTS", "true"),
                ("GIT_CONFIG_GLOBAL", gitconfig.to_str().unwrap()),
                ("GIT_CONFIG_NOSYSTEM", "1"),
            ];
            vars.extend_from_slice(env);
            let output = super::run("receipts.sh", args, self.dir.path(), &self.dir, &vars);
            (output, read(&output_file))
        }

        /// `git args` in `origin.git`.
        fn origin(&self, args: &[&str]) -> String {
            git(self.dir.child("origin.git").path(), args)
        }

        fn tip(&self) -> String {
            self.origin(&["rev-parse", &format!("refs/heads/{BRANCH}")])
        }

        /// The tip's `file`, if it has one.
        fn show(&self, file: &str) -> Option<String> {
            let output = Command::new("git")
                .current_dir(self.dir.child("origin.git").path())
                .args(["show", &format!("{BRANCH}:{file}")])
                .output()
                .unwrap();
            output
                .status
                .success()
                .then(|| String::from_utf8(output.stdout).unwrap())
        }

        fn runs(&self) -> String {
            format!(
                "file://{}/origin.git/actions/runs",
                self.dir.path().display()
            )
        }

        /// Makes `origin.git` refuse every push.
        fn refuse_pushes(&self) {
            executable(
                &self.dir.child("origin.git/hooks/pre-receive"),
                "#!/bin/sh\necho refused >&2\nexit 1\n",
            );
        }
    }

    fn lock(run: &str) -> String {
        format!("{run}\n")
    }

    #[test]
    fn probe_and_read_find_the_log() {
        let repo = Repo::new(&[("receipts.jsonl", LOG)]);
        for mode in ["all", "new", "plan"] {
            let (output, written) = repo.run(&["probe"], &[("MODE", mode)]);
            assert!(output.status.success(), "{mode}: {}", stderr(&output));
            assert_eq!(written, "receipts=true\n", "{mode}");
        }
        let file = repo.dir.child("receipts.jsonl");
        let (output, _) = repo.run(&["read", file.to_str().unwrap()], &[]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(read(&file), LOG);
    }

    /// Only an `all` run may go on without the receipts branch, and only
    /// when Git can tell that the branch does not exist.
    #[test]
    fn probe_without_the_log_passes_only_all() {
        let repo = Repo::new(&[]);
        let (output, written) = repo.run(&["probe"], &[("MODE", "all")]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "");
        let notice = "::notice::no publish-receipts branch: this run's receipts are not recorded";
        assert!(stdout(&output).contains(notice), "{}", stdout(&output));
        let error = "::error::cannot read the publish-receipts branch";
        for mode in ["new", "plan"] {
            let (output, written) = repo.run(&["probe"], &[("MODE", mode)]);
            assert_eq!(output.status.code(), Some(1), "{mode}");
            assert_eq!(written, "", "{mode}");
            assert!(
                stdout(&output).contains(error),
                "{mode}: {}",
                stdout(&output)
            );
        }
        let (output, written) = repo.run(
            &["probe"],
            &[("MODE", "all"), ("GITHUB_REPOSITORY", "missing.git")],
        );
        assert_eq!(output.status.code(), Some(1));
        assert_eq!(written, "");
        assert!(stdout(&output).contains(error), "{}", stdout(&output));
    }

    /// The lock commit goes on top of the tip it read: a fast-forward
    /// that names this run and attempt and keeps the log.
    #[test]
    fn acquire_takes_a_free_lock() {
        let repo = Repo::new(&[("receipts.jsonl", LOG)]);
        let before = repo.tip();
        let (output, written) = repo.run(&["acquire"], &[("GITHUB_RUN_ATTEMPT", "2")]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "locked=true\ngo=true\n");
        assert_eq!(repo.show("lock"), Some(lock("111 2")));
        assert_eq!(repo.show("receipts.jsonl").as_deref(), Some(LOG));
        assert_eq!(repo.origin(&["rev-parse", &format!("{BRANCH}^")]), before);
        assert_eq!(
            repo.origin(&["log", "-1", "--format=%s", BRANCH]),
            format!("Lock for {}/111", repo.runs())
        );
    }

    #[test]
    fn acquire_without_a_log_takes_no_lock() {
        let repo = Repo::new(&[("receipts.jsonl", LOG)]);
        let before = repo.tip();
        let (output, written) = repo.run(&["acquire"], &[("RECEIPTS", "")]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "go=true\n");
        assert_eq!(repo.tip(), before);
    }

    /// A running run's lock skips a scheduled run and fails a dispatch;
    /// a finished run's lock, or an earlier attempt's of this run (never
    /// inherited, so `gh` is not asked), fails both until reconciled.
    #[test]
    fn acquire_leaves_another_lock_in_place() {
        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("222 1"))]);
        let before = repo.tip();
        let call = "api repos/origin.git/actions/runs/222 --jq .status";
        fake_gh(&repo.dir, call, "in_progress", 0);
        let running = format!(
            "{}/222 is publishing; this run uploads nothing",
            repo.runs()
        );
        let (output, written) = repo.run(&["acquire"], &[("GITHUB_EVENT_NAME", "schedule")]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "");
        let notice = format!("::notice::{running} and a later one resumes");
        assert!(stdout(&output).contains(&notice), "{}", stdout(&output));
        let (output, written) = repo.run(&["acquire"], &[]);
        assert_eq!(output.status.code(), Some(1));
        assert_eq!(written, "");
        let error = format!("::error::{running}, dispatch again later");
        assert!(stdout(&output).contains(&error), "{}", stdout(&output));

        fake_gh(&repo.dir, call, "completed", 0);
        let stale = format!(
            "::error::{}/222 (attempt 1) ended without recording all its uploads",
            repo.runs()
        );
        for event in ["schedule", "workflow_dispatch"] {
            let (output, written) = repo.run(&["acquire"], &[("GITHUB_EVENT_NAME", event)]);
            assert_eq!(output.status.code(), Some(1), "{event}");
            assert_eq!(written, "", "{event}");
            assert!(
                stdout(&output).contains(&stale),
                "{event}: {}",
                stdout(&output)
            );
        }
        assert_eq!(repo.tip(), before);

        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        let (output, written) = repo.run(&["acquire"], &[("GITHUB_RUN_ATTEMPT", "2")]);
        assert_eq!(output.status.code(), Some(1));
        assert_eq!(written, "");
        let earlier = format!("::error::{}/111 (attempt 1) ended without", repo.runs());
        assert!(stdout(&output).contains(&earlier), "{}", stdout(&output));
    }

    /// Run 222 takes the lock between this run's fetch and push (a `git`
    /// shim runs its whole `acquire` first), so this run's push is a real
    /// non-fast-forward: a scheduled run skips, a dispatch fails, and the
    /// branch holds only run 222's lock.
    #[test]
    fn acquire_loses_a_race_without_taking_the_lock() {
        let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("scripts/actions/receipts.sh");
        let path = std::env::var_os("PATH").unwrap();
        let real_git = std::env::split_paths(&path)
            .map(|dir| dir.join("git"))
            .find(|git| git.is_file())
            .unwrap();
        for (event, status) in [("schedule", 0), ("workflow_dispatch", 1)] {
            let repo = Repo::new(&[("receipts.jsonl", LOG)]);
            let before = repo.tip();
            let marker = repo.dir.child("marker");
            marker.write_str("").unwrap();
            let rival = command_file(&repo.dir, "rival");
            executable(
                &repo.dir.child("bin/git"),
                "#!/bin/sh\n\
                 if [ -z \"$RIVAL\" ] && [ -e \"$MARKER\" ]; then\n\
                 case \" $* \" in *\" push \"*)\n\
                 rm \"$MARKER\"\n\
                 RIVAL=1 GITHUB_RUN_ID=222 GITHUB_OUTPUT=\"$RIVAL_OUTPUT\" \
                 bash \"$SCRIPT\" acquire >&2 || exit 99 ;;\n\
                 esac\n\
                 fi\n\
                 exec \"$REAL_GIT\" \"$@\"\n",
            );
            let (output, written) = repo.run(
                &["acquire"],
                &[
                    ("GITHUB_EVENT_NAME", event),
                    ("MARKER", marker.to_str().unwrap()),
                    ("RIVAL_OUTPUT", rival.to_str().unwrap()),
                    ("SCRIPT", script.to_str().unwrap()),
                    ("REAL_GIT", real_git.to_str().unwrap()),
                ],
            );
            assert_eq!(
                output.status.code(),
                Some(status),
                "{event}: {}",
                stderr(&output)
            );
            assert_eq!(written, "", "{event}");
            assert!(
                stdout(&output)
                    .contains("the publish-receipts branch moved; this run uploads nothing"),
                "{event}: {}",
                stdout(&output)
            );
            assert!(!marker.exists(), "{event}");
            assert_eq!(read(&rival), "locked=true\ngo=true\n", "{event}");
            assert_eq!(repo.show("lock"), Some(lock("222 1")), "{event}");
            assert_eq!(
                repo.origin(&["rev-parse", &format!("{BRANCH}^")]),
                before,
                "{event}"
            );
        }
    }

    /// A push refused while the tip stayed put is no lost race: a
    /// scheduled run that skipped it would skip every later run too.
    #[test]
    fn acquire_fails_a_refused_push_that_moved_nothing() {
        let repo = Repo::new(&[("receipts.jsonl", LOG)]);
        repo.refuse_pushes();
        let before = repo.tip();
        for event in ["schedule", "workflow_dispatch"] {
            let (output, written) = repo.run(&["acquire"], &[("GITHUB_EVENT_NAME", event)]);
            assert_eq!(output.status.code(), Some(1), "{event}");
            assert_eq!(written, "", "{event}");
            let error = "::error::the publish-receipts branch refused the lock but did not move";
            assert!(
                stdout(&output).contains(error),
                "{event}: {}",
                stdout(&output)
            );
        }
        assert_eq!(repo.tip(), before);
    }

    /// The publish job uploads only under its own attempt's lock, reading
    /// the log of the tip that holds it; only a run that found no log may
    /// go on while there still is none.
    #[test]
    fn held_requires_this_attempts_lock() {
        let file = |repo: &Repo| repo.dir.child("log");
        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        let (output, _) = repo.run(&["held", file(&repo).to_str().unwrap()], &[]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(read(&file(&repo)), LOG);

        let not_held = "::error::this attempt does not hold the publish-receipts lock";
        let refuse = |repo: &Repo, env: &[(&str, &str)], error: &str| {
            let (output, _) = repo.run(&["held", file(repo).to_str().unwrap()], env);
            assert_eq!(output.status.code(), Some(1), "{env:?}");
            assert!(
                stdout(&output).contains(error),
                "{env:?}: {}",
                stdout(&output)
            );
            assert!(!file(repo).exists(), "{env:?}");
        };
        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        refuse(&repo, &[("GITHUB_RUN_ATTEMPT", "2")], not_held);
        refuse(&repo, &[("GITHUB_RUN_ID", "222")], not_held);
        let repo = Repo::new(&[("receipts.jsonl", LOG)]);
        refuse(&repo, &[], not_held);
        refuse(&repo, &[("RECEIPTS", "")], not_held);

        let repo = Repo::new(&[]);
        let (output, _) = repo.run(
            &["held", file(&repo).to_str().unwrap()],
            &[("RECEIPTS", "")],
        );
        assert!(output.status.success(), "{}", stderr(&output));
        assert!(!file(&repo).exists());
        let unreadable = "::error::cannot read the publish-receipts branch; uploading nothing";
        refuse(&repo, &[], unreadable);
        let missing = [("RECEIPTS", ""), ("GITHUB_REPOSITORY", "missing.git")];
        refuse(&repo, &missing, unreadable);

        // An `all` run reads no log, so the lock alone suffices.
        let repo = Repo::new(&[("lock", &lock("111 1"))]);
        let (output, _) = repo.run(&["held", file(&repo).to_str().unwrap()], &[]);
        assert!(output.status.success(), "{}", stderr(&output));
        assert!(!file(&repo).exists());
    }

    /// `read` and `held` take the log from Git's objects: a symlink on
    /// the branch yields its target's name, never a runner file.
    #[test]
    fn read_and_held_never_follow_a_symlink() {
        let repo = Repo::new(&[("lock", &lock("111 1"))]);
        let secret = repo.dir.child("secret");
        secret.write_str("secret\n").unwrap();
        let seed = repo.dir.child("seed");
        std::os::unix::fs::symlink(secret.path(), seed.child("receipts.jsonl").path()).unwrap();
        git(seed.path(), &["add", "receipts.jsonl"]);
        git(seed.path(), &["commit", "-q", "-m", "link"]);
        let push = format!("HEAD:refs/heads/{BRANCH}");
        git(seed.path(), &["push", "-q", "../origin.git", &push]);
        for command in ["read", "held"] {
            let file = repo.dir.child(command);
            let (output, _) = repo.run(&[command, file.to_str().unwrap()], &[]);
            assert!(output.status.success(), "{command}: {}", stderr(&output));
            assert_eq!(
                read(&file),
                secret.path().display().to_string(),
                "{command}"
            );
        }
    }

    /// A receipts artifact of attempt 1 with two uploads.
    fn artifact(repo: &Repo) -> ChildPath {
        let dir = repo.dir.child("artifacts");
        dir.child("receipts-1.jsonl")
            .write_str("{\"name\":\"a\",\"outcome\":\"uploaded\"}\n{\"name\":\"b\",\"outcome\":\"failed\"}\n")
            .unwrap();
        dir
    }

    /// The log after appending `artifact`'s lines, stamped as run 111's.
    fn stamped(repo: &Repo) -> String {
        let stamp = format!(
            "\"source\":\"{REV}\",\"cabin\":\"{OTHER_REV}\",\"run\":\"{}/111\"",
            repo.runs()
        );
        format!(
            "{LOG}{{\"name\":\"a\",\"outcome\":\"uploaded\",{stamp}}}\n\
             {{\"name\":\"b\",\"outcome\":\"failed\",{stamp}}}\n"
        )
    }

    /// `record`'s inputs: the publish job's result and attempt, the
    /// artifact download's outcome, then this attempt.
    fn outcome<'a>(
        publish: &'a str,
        published_in: &'a str,
        download: &'a str,
        attempt: &'a str,
    ) -> [(&'a str, &'a str); 5] {
        [
            ("PUBLISH", publish),
            ("PUBLISHED_IN", published_in),
            ("DOWNLOAD", download),
            ("GITHUB_RUN_ATTEMPT", attempt),
            ("CABIN_REV", OTHER_REV),
        ]
    }

    /// One commit appends the lock's attempt's receipts and releases the
    /// lock when that attempt's publish job succeeded and its receipts
    /// downloaded, or when that attempt skipped the job.
    #[test]
    fn record_appends_and_releases() {
        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        let before = repo.tip();
        let dir = artifact(&repo);
        let (output, _) = repo.run(
            &["record", dir.to_str().unwrap()],
            &outcome("success", "1", "success", "2"),
        );
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(repo.show("lock"), None);
        assert_eq!(repo.show("receipts.jsonl"), Some(stamped(&repo)));
        assert_eq!(repo.origin(&["rev-parse", &format!("{BRANCH}^")]), before);
        assert_eq!(
            repo.origin(&["log", "-1", "--format=%s", BRANCH]),
            format!("Record the receipts of {}/111 (attempt 1)", repo.runs())
        );

        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 2"))]);
        let empty = repo.dir.child("empty");
        let (output, _) = repo.run(
            &["record", empty.to_str().unwrap()],
            &outcome("skipped", "", "failure", "2"),
        );
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(repo.show("lock"), None);
        assert_eq!(repo.show("receipts.jsonl").as_deref(), Some(LOG));
    }

    /// Unless the lock's attempt ended cleanly, the receipts it left are
    /// recorded but the lock stays and `record` fails, as it does with
    /// nothing pushed when this run does not hold the lock.
    #[test]
    fn record_keeps_the_lock_unless_the_upload_is_accounted_for() {
        for (case, env) in [
            ("failed", outcome("failure", "1", "success", "1")),
            ("cancelled", outcome("cancelled", "1", "success", "1")),
            ("download failed", outcome("success", "1", "failure", "1")),
            (
                "published in a later attempt",
                outcome("success", "2", "success", "2"),
            ),
            (
                "skipped in a later attempt",
                outcome("skipped", "", "success", "2"),
            ),
        ] {
            let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
            let dir = artifact(&repo);
            let (output, _) = repo.run(&["record", dir.to_str().unwrap()], &env);
            assert_eq!(output.status.code(), Some(1), "{case}");
            let error = "::error::the publish job under the lock of attempt 1 ended";
            assert!(
                stdout(&output).contains(error),
                "{case}: {}",
                stdout(&output)
            );
            assert_eq!(repo.show("lock"), Some(lock("111 1")), "{case}");
            assert_eq!(repo.show("receipts.jsonl"), Some(stamped(&repo)), "{case}");
        }

        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        let empty = repo.dir.child("empty");
        let (output, _) = repo.run(
            &["record", empty.to_str().unwrap()],
            &outcome("success", "1", "success", "1"),
        );
        assert_eq!(output.status.code(), Some(1), "no receipts file");
        assert_eq!(repo.show("lock"), Some(lock("111 1")));
        assert_eq!(repo.show("receipts.jsonl").as_deref(), Some(LOG));

        let not_holder = "::error::this run does not hold the publish-receipts lock";
        for files in [
            &[("receipts.jsonl", LOG), ("lock", &lock("222 1"))][..],
            &[("receipts.jsonl", LOG)],
        ] {
            let repo = Repo::new(files);
            let before = repo.tip();
            let dir = artifact(&repo);
            let (output, _) = repo.run(
                &["record", dir.to_str().unwrap()],
                &outcome("success", "1", "success", "1"),
            );
            assert_eq!(output.status.code(), Some(1), "{files:?}");
            assert!(stdout(&output).contains(not_holder), "{}", stdout(&output));
            assert_eq!(repo.tip(), before, "{files:?}");
        }
    }

    /// A refused push fails `record` and leaves the lock: rerunning it
    /// alone then records.
    #[test]
    fn record_fails_a_refused_push() {
        let repo = Repo::new(&[("receipts.jsonl", LOG), ("lock", &lock("111 1"))]);
        let before = repo.tip();
        let dir = artifact(&repo);
        repo.refuse_pushes();
        let env = outcome("success", "1", "success", "1");
        let (output, _) = repo.run(&["record", dir.to_str().unwrap()], &env);
        assert!(!output.status.success());
        assert_eq!(repo.tip(), before);
        std::fs::remove_file(repo.dir.child("origin.git/hooks/pre-receive")).unwrap();
        let (output, _) = repo.run(&["record", dir.to_str().unwrap()], &env);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(repo.show("lock"), None);
    }
}

/// `pending-new.sh` with a fake `port-publish`.  The publish workflow
/// runs it on Linux only.
#[cfg(unix)]
mod pending_new {
    use std::os::unix::fs::PermissionsExt as _;

    use super::*;

    const REGISTRY: &str = "https://registry.invalid";

    /// Runs `pending-new.sh` with a `port-publish` that, called with
    /// exactly the build job's arguments, writes `list` as the versions
    /// to upload and exits with `status`, returning the run and its
    /// `$GITHUB_OUTPUT`.
    fn pending_new(list: &str, status: u8) -> (Output, String) {
        let dir = TempDir::new().unwrap();
        let (tools, log, listed) = (
            dir.child("tools"),
            dir.child("receipts.jsonl"),
            dir.child("listed"),
        );
        listed.write_str(list).unwrap();
        let call = format!(
            "--index-url {REGISTRY} --receipts {} --ports-dir ports --cabin {}/cabin",
            log.display(),
            tools.display()
        );
        let tool = tools.child("port-publish");
        tool.write_str(&format!(
            "#!/bin/sh\n\
             [ \"$1\" = --list-new ] || exit 64\n\
             out=\"$2\"\n\
             shift 2\n\
             [ \"$*\" = '{call}' ] || {{ echo \"unexpected: port-publish $*\" >&2; exit 64; }}\n\
             cat '{}' > \"$out\"\n\
             exit {status}\n",
            listed.display()
        ))
        .unwrap();
        std::fs::set_permissions(tool.path(), std::fs::Permissions::from_mode(0o755)).unwrap();
        let output_file = command_file(&dir, "output");
        let output = run(
            "pending-new.sh",
            &[tools.to_str().unwrap(), log.to_str().unwrap()],
            dir.path(),
            &dir,
            &[
                ("GITHUB_OUTPUT", output_file.to_str().unwrap()),
                ("REGISTRY", REGISTRY),
                ("RUNNER_TEMP", dir.to_str().unwrap()),
            ],
        );
        (output, read(&output_file))
    }

    #[test]
    fn pending_new_lists_the_versions_to_upload() {
        let list = "cabin-ports/zlib 1.3.1\ncabin-ports/libpng 1.6.50\n";
        let (output, written) = pending_new(list, 0);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "work=true\n");
        let printed = format!("to upload, in order:\n{list}");
        assert!(stdout(&output).contains(&printed), "{}", stdout(&output));
    }

    /// An empty list is no work, and no failure.
    #[test]
    fn pending_new_without_a_version_to_upload_is_no_work() {
        let (output, written) = pending_new("", 0);
        assert!(output.status.success(), "{}", stderr(&output));
        assert_eq!(written, "work=false\n");
    }

    /// A failed `port-publish` fails the step, whatever list it left,
    /// and outputs nothing.
    #[test]
    fn pending_new_fails_with_port_publish() {
        let (output, written) = pending_new("cabin-ports/zlib 1.3.1\n", 1);
        assert_eq!(output.status.code(), Some(1), "{}", stderr(&output));
        assert_eq!(written, "");
    }
}
