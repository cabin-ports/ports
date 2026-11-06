//! End-to-end tests for the `port-publish` binary.
//!
//! Offline by default: every port pins an `https://` URL (the
//! provenance rules require it) whose archive is pre-seeded into the
//! port cache, so the cache-first fetch never touches the network.
//! The `--publish` tests serve a fake registry on a loopback
//! `tiny_http` server.

use std::fs;
use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, Mutex};

use assert_fs::TempDir;
use assert_fs::prelude::*;
use sha2::{Digest, Sha256};

const SCOPE_ZLIB_INDEX: &str = "packages/cabin-ports/zlib.json";

/// The tool's own binary (built by cargo for this crate's tests).
fn tool() -> Command {
    Command::new(env!("CARGO_BIN_EXE_port-publish"))
}

/// The `cabin` binary the tests drive: `PORT_PUBLISH_CABIN`, built
/// from the Cabin revision this package pins (README.md).  Required,
/// never looked up on `PATH`: a different Cabin would stage different
/// packages than the pinned crates do.
fn cabin_binary() -> PathBuf {
    let cabin = std::env::var_os("PORT_PUBLISH_CABIN")
        .map(PathBuf::from)
        .expect("set PORT_PUBLISH_CABIN to the pinned cabin binary (README.md)");
    assert!(
        cabin.is_file(),
        "PORT_PUBLISH_CABIN={} is not a file",
        cabin.display()
    );
    // Some tests run the tool from another directory.
    std::path::absolute(cabin).expect("PORT_PUBLISH_CABIN resolves to an absolute path")
}

/// Preflight builds compile real C/C++ sources through `cabin`,
/// which needs Ninja plus C and C++ toolchains.  Fail (not skip)
/// when the host lacks them, mirroring Cabin's `crates/cabin` require
/// helpers.  On Windows, MSVC is discovered by cabin's own
/// toolchain logic (`cl` is normally not on PATH), so only Ninja is
/// checked there.
fn require_build_tools() {
    assert!(which("ninja"), "preflight tests need `ninja` on PATH");
    if cfg!(windows) {
        return;
    }
    let cc = ["cc", "clang", "gcc"].iter().any(|tool| which(tool));
    assert!(
        cc,
        "preflight tests need a C compiler (cc/clang/gcc) on PATH"
    );
    let cxx = ["c++", "clang++", "g++"].iter().any(|tool| which(tool));
    assert!(
        cxx,
        "preflight tests need a C++ compiler (c++/clang++/g++) on PATH"
    );
}

fn which(tool: &str) -> bool {
    let path = std::env::var_os("PATH").unwrap_or_default();
    std::env::split_paths(&path).any(|dir| {
        let candidate = dir.join(format!("{tool}{}", std::env::consts::EXE_SUFFIX));
        candidate.is_file()
    })
}

/// Build a gzipped tarball from `(path, contents)` entries and
/// return `(bytes, sha256_hex)`.
fn make_tar_gz(entries: &[(&str, &str)]) -> (Vec<u8>, String) {
    let encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    let mut builder = tar::Builder::new(encoder);
    for (rel, body) in entries {
        let bytes = body.as_bytes();
        let mut header = tar::Header::new_gnu();
        header.set_size(bytes.len() as u64);
        header.set_mode(0o644);
        header.set_entry_type(tar::EntryType::Regular);
        header.set_cksum();
        builder
            .append_data(&mut header, rel, std::io::Cursor::new(bytes))
            .unwrap();
    }
    let bytes = builder.into_inner().unwrap().finish().unwrap();
    let hex = cabin_core::hash::hex_digest(&Sha256::digest(&bytes));
    (bytes, hex)
}

/// Seed an archive into the port cache's content-addressed slot so
/// the tool's cache-first fetch finds it without network access.
fn seed_port_cache(cache_dir: &Path, bytes: &[u8], hex: &str) {
    let slot = cache_dir
        .join("ports")
        .join("archives")
        .join("sha256")
        .join(format!("{hex}.tar.gz"));
    fs::create_dir_all(slot.parent().unwrap()).unwrap();
    let mut file = fs::File::create(&slot).unwrap();
    file.write_all(bytes).unwrap();
}

/// Three fake ports: `zlib` (a sole library target keyed `z`),
/// `libpng` depending on it through an ordinary scoped registry
/// dependency, and `fastlz`, whose upstream only compiles once its
/// declared patch has run.  Returns the ports dir and the seeded
/// cache dir.
fn fake_ports(dir: &TempDir) -> (PathBuf, PathBuf) {
    let ports = dir.child("ports");
    let cache = dir.child("cache");

    seed_zlib(&ports, &cache);
    let (libpng_bytes, libpng_hex) = make_tar_gz(&[
        (
            "libpng-1.6.50/png.h",
            "#ifndef PNG_H\n#define PNG_H\nint png_answer(void);\n#endif\n",
        ),
        (
            "libpng-1.6.50/png.c",
            "#include \"png.h\"\n#include \"zlib.h\"\nint png_answer(void) { return \
             zlib_answer(); }\n",
        ),
    ]);
    seed_port_cache(cache.path(), &libpng_bytes, &libpng_hex);
    write_port_package(
        &ports,
        "libpng",
        "1.6.50",
        &libpng_hex,
        &[],
        "[dependencies]\n\"cabin-ports/zlib\" = \"^1.3\"\n\n[target.png]\ntype = \
         \"library\"\nsources = [\"png.c\"]\ninclude-dirs = [\".\"]\nc-standard = \
         \"c11\"\ndeps = [\"cabin-ports/zlib\"]\n",
    );

    let (fastlz_bytes, fastlz_hex) = make_tar_gz(&[
        (
            "fastlz-0.5.0/fastlz.h",
            "#ifndef FASTLZ_H\n#define FASTLZ_H\nint fastlz_answer(void);\n#endif\n",
        ),
        // Does not compile as shipped: FASTLZ_ANSWER is undefined
        // until the committed patch supplies it, so the probe build
        // can only succeed if the declared patch ran through the
        // shared materializer.
        (
            "fastlz-0.5.0/fastlz.c",
            "#include \"fastlz.h\"\nint fastlz_answer(void) { return FASTLZ_ANSWER; }\n",
        ),
    ]);
    seed_port_cache(cache.path(), &fastlz_bytes, &fastlz_hex);
    write_port_package(
        &ports,
        "fastlz",
        "0.5.0",
        &fastlz_hex,
        &["patches/0001-define-answer.patch"],
        "[target.fastlz]\ntype = \"library\"\nsources = \
         [\"fastlz.c\"]\ninclude-dirs = [\".\"]\nc-standard = \"c11\"\n",
    );
    ports
        .child("fastlz/0.5.0/patches/0001-define-answer.patch")
        .write_str(
            "--- a/fastlz.c\n\
             +++ b/fastlz.c\n\
             @@ -1,2 +1,3 @@\n \
             #include \"fastlz.h\"\n\
             +#define FASTLZ_ANSWER 7\n \
             int fastlz_answer(void) { return FASTLZ_ANSWER; }\n",
        )
        .unwrap();

    (ports.to_path_buf(), cache.to_path_buf())
}

/// The `zlib` port alone (a sole library target keyed `z`), for the
/// tests whose contract does not need the full three-port graph: a
/// single port pays one preflight build instead of three.
fn zlib_only(dir: &TempDir) -> (PathBuf, PathBuf) {
    let ports = dir.child("ports");
    let cache = dir.child("cache");
    seed_zlib(&ports, &cache);
    (ports.to_path_buf(), cache.to_path_buf())
}

fn seed_zlib(ports: &assert_fs::fixture::ChildPath, cache: &assert_fs::fixture::ChildPath) {
    let (zlib_bytes, zlib_hex) = make_tar_gz(&[
        (
            "zlib-1.3.1/zlib.h",
            "#ifndef ZLIB_H\n#define ZLIB_H\nint zlib_answer(void);\n#endif\n",
        ),
        (
            "zlib-1.3.1/zlib.c",
            "#include \"zlib.h\"\nint zlib_answer(void) { return 42; }\n",
        ),
    ]);
    seed_port_cache(cache.path(), &zlib_bytes, &zlib_hex);
    write_port_package(
        ports,
        "zlib",
        "1.3.1",
        &zlib_hex,
        &[],
        "[target.z]\ntype = \"library\"\nsources = [\"zlib.c\"]\ninclude-dirs = \
         [\".\"]\nc-standard = \"c11\"\n",
    );
}

/// Publication order: the dependency before the dependent (`fastlz`
/// has no dependencies, so it drains in the first rank by name).
fn assert_publication_order(stdout: &str) {
    let Some(order) = stdout
        .lines()
        .find(|line| line.starts_with("loaded "))
        .and_then(|line| line.split_once("publication order: "))
        .map(|(_, order)| order.to_owned())
    else {
        panic!("no publication order line: {stdout}")
    };
    let position = |needle: &str| {
        order
            .find(needle)
            .unwrap_or_else(|| panic!("{needle} missing from `{order}`"))
    };
    assert!(
        position("cabin-ports/zlib 1.3.1") < position("cabin-ports/libpng 1.6.50"),
        "zlib must publish before libpng: {order}"
    );
    let _ = position("cabin-ports/fastlz 0.5.0");
}

/// `fastlz` was staged from its committed manifest, published
/// verbatim (identity, provenance, and target key all straight from
/// the file), and probe-built from the registry.  Its upstream source
/// does not compile unpatched, so the probe artifact existing is proof
/// the declared patch ran through the shared materializer.
fn assert_patched_package_round_trip(registry: &std::path::Path, work: &std::path::Path) {
    let fastlz_index: serde_json::Value = serde_json::from_str(
        &fs::read_to_string(registry.join("packages/cabin-ports/fastlz.json")).unwrap(),
    )
    .unwrap();
    assert_eq!(fastlz_index["name"], "cabin-ports/fastlz");
    let fastlz_version = &fastlz_index["versions"]["0.5.0"];
    assert_eq!(
        fastlz_version["upstream"]["url"],
        "https://ports.invalid/fastlz-0.5.0.tar.gz"
    );
    assert_eq!(fastlz_version["upstream"]["strip-prefix"], "fastlz-0.5.0");
    let fastlz_probe = work.join("run").join("probes").join("fastlz");
    assert!(
        tree_contains(&fastlz_probe, "libfastlz.a") || tree_contains(&fastlz_probe, "fastlz.lib"),
        "no fastlz artifact under {}",
        fastlz_probe.display()
    );
}

/// Write a committed port: one `cabin.toml`
/// carrying the canonical scoped identity and complete
/// `[package.upstream]` provenance, published verbatim.
fn write_port_package(
    ports: &assert_fs::fixture::ChildPath,
    name: &str,
    version: &str,
    sha256: &str,
    patches: &[&str],
    body: &str,
) {
    let patches = if patches.is_empty() {
        String::new()
    } else {
        let quoted: Vec<String> = patches.iter().map(|p| format!("\"{p}\"")).collect();
        format!("patches = [{}]\n", quoted.join(", "))
    };
    let dir = ports.child(format!("{name}/{version}"));
    dir.child("cabin.toml")
        .write_str(&format!(
            "[package]\nname = \"cabin-ports/{name}\"\nversion = \"{version}\"\n\n\
             [package.upstream]\nurl = \
             \"https://ports.invalid/{name}-{version}.tar.gz\"\nchecksum = \
             \"sha256:{sha256}\"\nformat = \"tar.gz\"\nstrip-prefix = \
             \"{name}-{version}\"\n{patches}\n{body}"
        ))
        .unwrap();
}

/// Scrub a subset of the variables Cabin's `crates/cabin/tests/common/mod.rs`
/// `cabin()` helper scrubs (that helper is a private test module of
/// the `cabinpkg` crate, so it cannot be imported here), so a
/// developer's config, toolchain, or wrapper overrides cannot leak
/// into the spawned `cabin` builds.
fn scrubbed(mut command: Command) -> Command {
    command.env("CABIN_NO_CONFIG", "1");
    command.env("CABIN_TERM_COLOR", "never");
    for key in [
        "CABIN_CONFIG",
        "CABIN_CONFIG_HOME",
        "CC",
        "CXX",
        "AR",
        "NINJA",
        "CFLAGS",
        "CXXFLAGS",
        "CPPFLAGS",
        "LDFLAGS",
        "CABIN_NET_OFFLINE",
        "CABIN_RESOLVER_INCOMPATIBLE_STANDARDS",
        "CABIN_REGISTRY_TOKEN",
        // Ambient in the repository's own CI: without the strip, a
        // spawned `cabin publish` would take the trusted-publishing
        // detection path there and behave differently than locally.
        "GITHUB_ACTIONS",
        "ACTIONS_ID_TOKEN_REQUEST_URL",
        "ACTIONS_ID_TOKEN_REQUEST_TOKEN",
        "CABIN_COMPILER_WRAPPER",
        "CABIN_CACHE_DIR",
        "CABIN_CACHE_HOME",
        "CABIN_FMT",
        "CABIN_TIDY",
        "CABIN_PKG_CONFIG",
        "PKG_CONFIG_PATH",
        "PKG_CONFIG_LIBDIR",
        "PKG_CONFIG_SYSROOT_DIR",
        "NO_COLOR",
        "CLICOLOR",
        "CLICOLOR_FORCE",
        "PORT_PUBLISH_CHANGED_SINCE",
    ] {
        command.env_remove(key);
    }
    command
}

/// `true` when `name` exists anywhere under `dir`.
fn tree_contains(dir: &Path, name: &str) -> bool {
    let Ok(entries) = fs::read_dir(dir) else {
        return false;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            if tree_contains(&path, name) {
                return true;
            }
        } else if entry.file_name().to_string_lossy() == name {
            return true;
        }
    }
    false
}

/// The dry run's own contract: it completes without remote mutation
/// and a rerun over the same work dir succeeds (the scratch registry
/// is rebuilt, not appended to).  The registry and probe shapes the
/// preflight produces are asserted in
/// `publish_uploads_every_package_in_dependency_order`, which pays the
/// three-port build cost once for both concerns.
#[test]
fn dry_run_preflights_against_a_temporary_file_registry() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    let work = dir.child("work").to_path_buf();

    let run = |label: &str| {
        let output = scrubbed(tool())
            .arg("--dry-run")
            .arg("--ports-dir")
            .arg(&ports)
            .arg("--cache-dir")
            .arg(&cache)
            .arg("--work-dir")
            .arg(&work)
            .arg("--cabin")
            .arg(cabin_binary())
            .output()
            .unwrap();
        let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
        assert!(
            output.status.success(),
            "{label} stdout:\n{stdout}\nstderr:\n{}",
            String::from_utf8_lossy(&output.stderr)
        );
        stdout
    };
    let stdout = run("first run");
    assert!(stdout.contains("dry run complete"), "{stdout}");
    run("rerun over the same work dir");
}

/// A committed consumer test runs in the dry run, where its failure
/// fails the preflight, and is only built in the publish run, where
/// executing it beside the trusted-publishing token would hand that
/// token to upstream code.
#[test]
fn consumer_tests_run_in_the_dry_run_and_never_in_the_publish_run() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    // Relative to the probe's inherited working directory, so no host
    // path has to survive a C string literal and the ANSI code page.
    let marker = dir.child("ran");
    dir.child("ports/zlib/1.3.1/tests/main.cc")
        .write_str(
            "#include <cstdio>\nint main() {\n    std::FILE* f = std::fopen(\"ran\", \"w\");\n    \
             if (f) std::fclose(f);\n    return 3;\n}\n",
        )
        .unwrap();
    let run = |mode: &str, work: &str, index_url: Option<&str>| {
        let mut command = scrubbed(tool());
        command
            .current_dir(dir.path())
            .arg(mode)
            .arg("--ports-dir")
            .arg(&ports)
            .arg("--cache-dir")
            .arg(&cache)
            .arg("--work-dir")
            .arg(dir.child(work).path())
            .arg("--cabin")
            .arg(cabin_binary());
        if let Some(url) = index_url {
            command
                .arg("--index-url")
                .arg(url)
                .env("CABIN_REGISTRY_TOKEN", "cabin_testtoken1234");
        }
        command.output().unwrap()
    };

    let registry = FakeRegistry::start();
    let published = run("--publish", "publish-work", Some(&registry.url));
    assert!(
        published.status.success(),
        "stderr:\n{}",
        String::from_utf8_lossy(&published.stderr)
    );
    assert!(!marker.path().exists(), "the publish run executed a probe");
    let stdout = String::from_utf8_lossy(&published.stdout);
    assert!(!stdout.contains(", run "), "{stdout}");

    let dry = run("--dry-run", "dry-work", None);
    let stderr = String::from_utf8_lossy(&dry.stderr);
    assert!(!dry.status.success(), "a failing consumer test must fail");
    assert!(stderr.contains("preflight probe `port-probe`"), "{stderr}");
    assert!(
        marker.path().exists(),
        "the dry run never executed the probe"
    );
}

/// Run `git` in a fixture repository, isolated from the developer's own
/// Git configuration (signing, hooks, default branch).
fn git(repo: &Path, args: &[&str]) {
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
}

/// A selected dry-run fetches, stages, and probes only its selection:
/// `unfetched`'s archive is never seeded, so staging it would try - and
/// fail - to download it, as the full dry-run shows.  A documentation
/// edit alone preflights nothing, and a selected probe's failure fails
/// the run.
#[test]
fn a_selected_dry_run_preflights_only_the_selection() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let repo = dir.child("repo");
    let ports = repo.child("ports");
    let cache = dir.child("cache");
    seed_zlib(&ports, &cache);
    write_port_package(
        &ports,
        "unfetched",
        "1.0.0",
        &"0".repeat(64),
        &[],
        "[target.u]\ntype = \"library\"\nsources = [\"u.c\"]\nc-standard = \"c11\"\n",
    );
    repo.child("README.md").write_str("ports\n").unwrap();
    git(repo.path(), &["init", "-q"]);
    git(repo.path(), &["add", "-A"]);
    git(repo.path(), &["commit", "-q", "-m", "base"]);

    let dry_run = |selection: &[&str], changed_since: Option<&str>| {
        let mut command = scrubbed(tool());
        command
            .arg("--dry-run")
            .args(selection)
            .arg("--ports-dir")
            .arg(ports.path())
            .arg("--cache-dir")
            .arg(cache.path())
            .arg("--work-dir")
            .arg(dir.child("work").path())
            .arg("--cabin")
            .arg(cabin_binary());
        // CI hands the base over through the environment.
        if let Some(base) = changed_since {
            command.env("PORT_PUBLISH_CHANGED_SINCE", base);
        }
        let output = command.output().unwrap();
        let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
        let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
        (output.status.success(), stdout, stderr)
    };

    repo.child("README.md")
        .write_str("ports, edited\n")
        .unwrap();
    let (ok, stdout, stderr) = dry_run(&[], Some("HEAD"));
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(stdout.contains("nothing to preflight"), "{stdout}");
    assert!(
        stdout.contains("dry run complete: 0 of 2 packages"),
        "{stdout}"
    );

    // The change under test: a new, untracked port depending on zlib.
    let (bytes, hex) = make_tar_gz(&[
        ("libpng-1.6.50/png.h", "int png_answer(void);\n"),
        (
            "libpng-1.6.50/png.c",
            "#include \"png.h\"\n#include \"zlib.h\"\nint png_answer(void) { return \
             zlib_answer(); }\n",
        ),
    ]);
    seed_port_cache(cache.path(), &bytes, &hex);
    write_port_package(
        &ports,
        "libpng",
        "1.6.50",
        &hex,
        &[],
        "[dependencies]\n\"cabin-ports/zlib\" = \"^1.3\"\n\n[target.png]\ntype = \
         \"library\"\nsources = [\"png.c\"]\ninclude-dirs = [\".\"]\nc-standard = \
         \"c11\"\ndeps = [\"cabin-ports/zlib\"]\n",
    );
    let (ok, stdout, stderr) = dry_run(&[], Some("HEAD"));
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(stdout.contains("preflighting 2 of 3 packages"), "{stdout}");
    assert!(stdout.contains("staged cabin-ports/zlib 1.3.1"), "{stdout}");
    assert!(
        stdout.contains("built cabin-ports/libpng 1.6.50"),
        "{stdout}"
    );
    assert!(!stdout.contains("staged cabin-ports/unfetched"), "{stdout}");

    let (ok, _, stderr) = dry_run(&[], None);
    assert!(!ok, "the full dry-run must try to fetch `unfetched`");
    assert!(stderr.contains("unfetched"), "{stderr}");

    ports
        .child("libpng/1.6.50/tests/main.cc")
        .write_str("int main() { return 3; }\n")
        .unwrap();
    let (ok, _, stderr) = dry_run(&["--port", "libpng"], None);
    assert!(!ok, "a failing selected probe must fail the run");
    assert!(
        stderr.contains("preflight probe `port-probe` of cabin-ports/libpng 1.6.50"),
        "{stderr}"
    );
}

/// The cheap catalog-wide checks run whatever a dry-run selects: a
/// misnamed consumer test in an unselected port fails the run before
/// anything is staged.
#[test]
fn a_selected_dry_run_still_checks_every_consumer_test_name() {
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    let ports_child = dir.child("ports");
    write_port_package(
        &ports_child,
        "other",
        "1.0.0",
        &"0".repeat(64),
        &[],
        "[target.o]\ntype = \"library\"\nsources = [\"o.c\"]\nc-standard = \"c11\"\n",
    );
    ports_child
        .child("other/1.0.0/tests/main.cpp")
        .write_str("int main() { return 0; }\n")
        .unwrap();
    let output = scrubbed(tool())
        .args(["--dry-run", "--port", "zlib", "--ports-dir"])
        .arg(&ports)
        .arg("--cache-dir")
        .arg(&cache)
        .arg("--work-dir")
        .arg(dir.child("work").path())
        // Never reached: the check fails before any build.
        .arg("--cabin")
        .arg(env!("CARGO_BIN_EXE_port-publish"))
        .output()
        .unwrap();
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success(), "{stderr}");
    assert!(stderr.contains("is not a consumer test"), "{stderr}");
    assert!(stderr.contains("main.cpp"), "{stderr}");
}

/// Selection is dry-run only: `--publish` refuses it while parsing its
/// arguments, before any preflight or upload.
#[test]
fn publish_refuses_a_selection() {
    let new = ["--receipts", "r", "--receipts-out", "r", "--main", "HEAD"];
    for (mode, args, changed_since) in [
        ("--publish", &["--port", "zlib"][..], None),
        ("--publish", &[][..], Some("HEAD")),
        ("--publish-new", &["--port", "zlib"][..], None),
        ("--publish-new", &[][..], Some("HEAD")),
        ("--plan-new", &["--port", "zlib"][..], None),
    ] {
        let mut command = scrubbed(tool());
        command
            .args([
                mode,
                "--index-url",
                "http://127.0.0.1:9/",
                "--cabin",
                "cabin",
            ])
            .args(args);
        match mode {
            "--publish-new" => command.args(new),
            "--plan-new" => command.args(&new[..2]),
            _ => &mut command,
        };
        if let Some(base) = changed_since {
            command.env("PORT_PUBLISH_CHANGED_SINCE", base);
        }
        let output = command.output().unwrap();
        let stderr = String::from_utf8_lossy(&output.stderr);
        assert_eq!(output.status.code(), Some(2), "{mode} {args:?}: {stderr}");
        assert!(stderr.contains("cannot be used with"), "{stderr}");
    }
}

/// Each new mode demands its whole contract: a missing receipts log,
/// output log, or `main` is a usage error, and only `--publish-new`
/// takes `--main` (the preview never uploads).
#[test]
fn the_new_modes_require_their_state() {
    let base = ["--index-url", "http://127.0.0.1:9/", "--cabin", "cabin"];
    for args in [
        &["--publish-new", "--receipts-out", "o", "--main", "HEAD"][..],
        &["--publish-new", "--receipts", "r", "--main", "HEAD"],
        &["--publish-new", "--receipts", "r", "--receipts-out", "o"],
        &["--plan-new"],
        &["--plan-new", "--receipts", "r", "--main", "HEAD"],
        &["--plan-new", "--receipts", "r", "--receipts-out", "o"],
        &["--publish", "--receipts", "r"],
        &["--dry-run", "--retry-failed"],
    ] {
        let output = scrubbed(tool()).args(base).args(args).output().unwrap();
        assert_eq!(
            output.status.code(),
            Some(2),
            "{args:?}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

/// Probes run in parallel, yet the reported failure is the earliest
/// port's in publication order, as in a sequential run: `zlib`'s probe
/// fails a second after the later `zmini` probe has (or after 30 s),
/// so with two or more probe workers an implementation reporting the
/// first failure *seen* names the wrong port.
#[test]
fn the_earliest_failing_port_is_reported_when_a_later_one_fails_first() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    let ports_child = dir.child("ports");
    let (bytes, hex) = make_tar_gz(&[("zmini-1.0.0/zmini.c", "int zmini(void) { return 0; }\n")]);
    seed_port_cache(&cache, &bytes, &hex);
    write_port_package(
        &ports_child,
        "zmini",
        "1.0.0",
        &hex,
        &[],
        "[target.zmini]\ntype = \"library\"\nsources = [\"zmini.c\"]\nc-standard = \"c11\"\n",
    );
    let sentinel = dir.child("zmini-failed");
    let sentinel = sentinel.path().display();
    dir.child("ports/zlib/1.3.1/tests/main.cc")
        .write_str(&format!(
            "#include <chrono>\n#include <filesystem>\n#include <thread>\nint main() {{\n    \
             for (int i = 0; i < 300 && !std::filesystem::exists(R\"({sentinel})\"); ++i) {{\n        \
             std::this_thread::sleep_for(std::chrono::milliseconds(100));\n    }}\n    \
             std::this_thread::sleep_for(std::chrono::seconds(1));\n    return 3;\n}}\n"
        ))
        .unwrap();
    dir.child("ports/zmini/1.0.0/tests/main.cc")
        .write_str(&format!(
            "#include <fstream>\nint main() {{\n    std::ofstream(R\"({sentinel})\");\n    return 4;\n}}\n"
        ))
        .unwrap();

    let output = scrubbed(tool())
        .arg("--dry-run")
        .arg("--ports-dir")
        .arg(&ports)
        .arg("--cache-dir")
        .arg(&cache)
        .arg("--work-dir")
        .arg(dir.child("work").path())
        .arg("--cabin")
        .arg(cabin_binary())
        .output()
        .unwrap();
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success(), "{stderr}");
    let error = stderr
        .lines()
        .find(|line| line.starts_with("error: "))
        .unwrap_or_else(|| panic!("no error line: {stderr}"));
    assert!(
        error.contains("preflight probe `port-probe` of cabin-ports/zlib 1.3.1"),
        "{stderr}"
    );
}

/// How the fake registry's publish API answers.
#[derive(Default)]
struct Behavior {
    /// The first this many uploads answer `429` with a one-second
    /// `Retry-After` - the shape of a drained publish token bucket.
    limited_puts: u32,
    /// The registry's idempotency contract instead of accepting every
    /// upload: identical bytes for a stored version are a `200` no-op,
    /// different ones a `409` unless `?new-revision=true`.
    stateful: bool,
    /// Uploads already stored, by path without the query.
    stored: Vec<(String, Vec<u8>)>,
    /// The upload with this 1-based number answers `500`.
    fail_put: Option<usize>,
}

/// Fake remote registry: the sparse index serves `config.json` and a
/// `cabin-ports/zlib` entry (404 otherwise), the publish API accepts
/// authorized PUTs, and every request is recorded.
struct FakeRegistry {
    url: String,
    requests: Arc<Mutex<Vec<(String, String, usize)>>>,
    shutdown: Arc<std::sync::atomic::AtomicBool>,
    handle: Option<std::thread::JoinHandle<()>>,
}

impl FakeRegistry {
    fn start() -> Self {
        Self::start_with(Behavior::default())
    }

    #[allow(clippy::too_many_lines)] // one request loop answering every route
    fn start_with(behavior: Behavior) -> Self {
        let server = tiny_http::Server::http("127.0.0.1:0").unwrap();
        let url = format!("http://{}", server.server_addr());
        let requests: Arc<Mutex<Vec<(String, String, usize)>>> = Arc::default();
        let shutdown = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let remaining_429s = Arc::new(std::sync::atomic::AtomicU32::new(behavior.limited_puts));
        let mut stored: std::collections::HashMap<String, Vec<u8>> =
            behavior.stored.into_iter().collect();
        let mut puts = 0;
        let handle = {
            let base = url.clone();
            let requests = Arc::clone(&requests);
            let shutdown = Arc::clone(&shutdown);
            let remaining_429s = Arc::clone(&remaining_429s);
            std::thread::spawn(move || {
                while !shutdown.load(std::sync::atomic::Ordering::SeqCst) {
                    let Ok(Some(mut request)) =
                        server.recv_timeout(std::time::Duration::from_millis(50))
                    else {
                        continue;
                    };
                    let method = request.method().to_string();
                    let path = request.url().to_owned();
                    let mut body = Vec::new();
                    request.as_reader().read_to_end(&mut body).unwrap();
                    let authorized = request
                        .headers()
                        .iter()
                        .any(|h| h.field.equiv("Authorization"));
                    requests
                        .lock()
                        .unwrap()
                        .push((method.clone(), path.clone(), body.len()));
                    let respond = |request: tiny_http::Request, status: u16, body: &str| {
                        let response = tiny_http::Response::from_string(body)
                            .with_status_code(status)
                            .with_header(
                                tiny_http::Header::from_bytes(
                                    &b"Content-Type"[..],
                                    &b"application/json"[..],
                                )
                                .unwrap(),
                            );
                        let _ = request.respond(response);
                    };
                    if method == "GET" && path == "/config.json" {
                        respond(
                            request,
                            200,
                            &format!(
                                "{{\"schema\":1,\"kind\":\"file-registry\",\"packages\":\
                                 \"packages\",\"artifacts\":\"artifacts\",\"auth-required\":\
                                 true,\"api\":\"{base}\"}}"
                            ),
                        );
                    } else if method == "GET" && path == "/packages/cabin-ports/zlib.json" {
                        // The version the tool is about to publish is
                        // already visible in the public index.  The
                        // upload must still happen: pending versions
                        // are invisible here, so the index can never
                        // justify skipping a PUT.
                        respond(
                            request,
                            200,
                            "{\"schema\":1,\"name\":\"cabin-ports/zlib\",\"versions\":\
                             {\"1.3.1\":{\"yanked\":false}}}",
                        );
                    } else if method == "PUT" && path.starts_with("/api/v1/packages/") {
                        if !authorized {
                            respond(request, 401, "{\"error\":\"authentication required\"}");
                            continue;
                        }
                        let limited = remaining_429s
                            .try_update(
                                std::sync::atomic::Ordering::SeqCst,
                                std::sync::atomic::Ordering::SeqCst,
                                |n| n.checked_sub(1),
                            )
                            .is_ok();
                        if limited {
                            respond_rate_limited(request);
                            continue;
                        }
                        puts += 1;
                        if behavior.fail_put == Some(puts) {
                            respond(request, 500, "{\"errors\":[{\"detail\":\"injected\"}]}");
                            continue;
                        }
                        let (route, query) = path.split_once('?').unwrap_or((&path, ""));
                        let revision = &cabin_core::hash::hex_digest(&Sha256::digest(&body))[..16];
                        if behavior.stateful {
                            if stored.get(route) == Some(&body) {
                                respond(
                                    request,
                                    200,
                                    &format!(
                                        "{{\"ok\":true,\"no_op\":true,\"revision\":\
                                         \"{revision}\",\"verification\":\"verified\"}}"
                                    ),
                                );
                                continue;
                            }
                            if stored.contains_key(route) && query != "new-revision=true" {
                                respond(
                                    request,
                                    409,
                                    "{\"errors\":[{\"detail\":\"the version is already \
                                     published with different bytes\"}]}",
                                );
                                continue;
                            }
                            stored.insert(route.to_owned(), body);
                        }
                        let version = route.rsplit('/').next().unwrap_or_default().to_owned();
                        respond(
                            request,
                            201,
                            &format!(
                                "{{\"ok\":true,\"version\":\"{version}\",\"revision\":\
                                 \"{revision}\",\"verification\":\"pending\"}}"
                            ),
                        );
                    } else {
                        respond(request, 404, "{\"error\":\"not found\"}");
                    }
                }
            })
        };
        Self {
            url,
            requests,
            shutdown,
            handle: Some(handle),
        }
    }

    fn requests(&self) -> Vec<(String, String, usize)> {
        self.requests.lock().unwrap().clone()
    }
}

/// The drained-bucket refusal: `429` with a one-second `Retry-After`,
/// keeping the test fast.
fn respond_rate_limited(request: tiny_http::Request) {
    let response = tiny_http::Response::from_string(
        "{\"error\":{\"code\":\"rate_limited\",\"detail\":\"publish rate limit exceeded\"}}",
    )
    .with_status_code(429)
    .with_header(tiny_http::Header::from_bytes(&b"Retry-After"[..], &b"1"[..]).unwrap());
    let _ = request.respond(response);
}

impl Drop for FakeRegistry {
    fn drop(&mut self) {
        self.shutdown
            .store(true, std::sync::atomic::Ordering::SeqCst);
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
    }
}

#[test]
fn publish_uploads_every_package_in_dependency_order() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = fake_ports(&dir);
    let work = dir.child("work").to_path_buf();
    let registry = FakeRegistry::start();
    let receipts = dir.child("receipts.jsonl");

    let output = scrubbed(tool())
        .arg("--publish")
        .arg("--index-url")
        .arg(&registry.url)
        .arg("--receipts-out")
        .arg(receipts.path())
        .arg("--ports-dir")
        .arg(&ports)
        .arg("--cache-dir")
        .arg(&cache)
        .arg("--work-dir")
        .arg(&work)
        .arg("--cabin")
        .arg(cabin_binary())
        .env("CABIN_REGISTRY_TOKEN", "cabin_testtoken1234")
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "stdout:\n{stdout}\nstderr:\n{stderr}"
    );

    let puts: Vec<(String, usize)> = registry
        .requests()
        .into_iter()
        .filter(|(method, _, _)| method == "PUT")
        .map(|(_, path, len)| (path, len))
        .collect();
    // Dependency order: fastlz (no deps, first by name) and zlib
    // upload before libpng, all framed bodies non-trivial (metadata
    // JSON + zip archive).
    assert_eq!(
        puts.iter()
            .map(|(path, _)| path.as_str())
            .collect::<Vec<_>>(),
        [
            "/api/v1/packages/cabin-ports/fastlz/0.5.0?new-revision=true",
            "/api/v1/packages/cabin-ports/zlib/1.3.1?new-revision=true",
            "/api/v1/packages/cabin-ports/libpng/1.6.50?new-revision=true",
        ]
    );
    assert!(puts.iter().all(|(_, len)| *len > 500), "{puts:?}");
    // The upload runs through `cabin publish` itself, whose report of
    // each upload becomes its receipt.
    assert!(
        stdout.contains("cabin-ports/zlib 1.3.1 published (revision "),
        "{stdout}"
    );
    assert!(stdout.contains("verification: pending"), "{stdout}");
    assert_eq!(
        receipt_lines(receipts.path()),
        [
            "cabin-ports/fastlz 0.5.0 uploaded",
            "cabin-ports/zlib 1.3.1 uploaded",
            "cabin-ports/libpng 1.6.50 uploaded",
        ]
    );

    // The preflight's own products, asserted here where the three-port
    // build cost is already paid.
    assert_publication_order(&stdout);
    assert_preflight_products(&work);
}

/// The registry and probe trees the preflight leaves under
/// `work/run/`, shared by both modes and asserted once.
fn assert_preflight_products(work: &Path) {
    // The temporary file registry holds every package, scoped.
    let registry_dir = work.join("run").join("registry");
    assert!(registry_dir.join("config.json").is_file());
    let zlib_index: serde_json::Value =
        serde_json::from_str(&fs::read_to_string(registry_dir.join(SCOPE_ZLIB_INDEX)).unwrap())
            .unwrap();
    assert_eq!(zlib_index["name"], "cabin-ports/zlib");
    let version = &zlib_index["versions"]["1.3.1"];
    assert_eq!(
        version["upstream"]["url"],
        "https://ports.invalid/zlib-1.3.1.tar.gz"
    );
    assert_eq!(version["upstream"]["format"], "tar.gz");
    assert_eq!(version["upstream"]["strip-prefix"], "zlib-1.3.1");
    // The provenance checksum survives publish in its
    // algorithm-prefixed wire form.
    let upstream_checksum = version["upstream"]["checksum"].as_str().unwrap();
    assert!(
        upstream_checksum
            .strip_prefix("sha256:")
            .is_some_and(|digest| digest.len() == 64),
        "unexpected upstream checksum {upstream_checksum:?}"
    );
    let libpng_index: serde_json::Value = serde_json::from_str(
        &fs::read_to_string(registry_dir.join("packages/cabin-ports/libpng.json")).unwrap(),
    )
    .unwrap();
    assert_eq!(
        libpng_index["versions"]["1.6.50"]["dependencies"]["cabin-ports/zlib"],
        "^1.3"
    );
    // The artifact filename embeds the packaging revision the index
    // points at (the archive checksum's leading hex prefix).
    let revision = version["revision"].as_str().expect("revision recorded");
    assert_eq!(
        version["revisions"][revision]["checksum"]
            .as_str()
            .unwrap()
            .strip_prefix("sha256:")
            .unwrap()
            .get(..16),
        Some(revision)
    );
    assert!(
        registry_dir
            .join(format!(
                "artifacts/cabin-ports/zlib/cabin-ports-zlib-1.3.1-{revision}.zip"
            ))
            .is_file()
    );

    // Each port was built *from the registry* through its probe; the
    // target key `z` decides the artifact stem (libz.a / z.lib).
    let zlib_probe = work.join("run").join("probes").join("zlib");
    assert!(
        tree_contains(&zlib_probe, "libz.a") || tree_contains(&zlib_probe, "z.lib"),
        "no z artifact under {}",
        zlib_probe.display()
    );
    // libpng's probe resolved cabin-ports/libpng (and transitively
    // cabin-ports/zlib) from the generated registry.
    let libpng_probe = work.join("run").join("probes").join("libpng");
    assert!(
        tree_contains(&libpng_probe, "libpng.a") || tree_contains(&libpng_probe, "png.lib"),
        "no png artifact under {}",
        libpng_probe.display()
    );
    assert!(
        tree_contains(&libpng_probe, "libz.a") || tree_contains(&libpng_probe, "z.lib"),
        "no transitive z artifact under {}",
        libpng_probe.display()
    );

    assert_patched_package_round_trip(&registry_dir, work);
}

/// A drained publish token bucket answers `429` with `Retry-After`;
/// the tool must wait it out and retry the same package instead of
/// failing the run (every attempt charges the bucket, so a rerun
/// could otherwise never make progress past the burst).
#[test]
fn publish_retries_a_rate_limited_package() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    let work = dir.child("work").to_path_buf();
    let registry = FakeRegistry::start_with(Behavior {
        limited_puts: 1,
        ..Behavior::default()
    });

    let output = scrubbed(tool())
        .arg("--publish")
        .arg("--index-url")
        .arg(&registry.url)
        .arg("--ports-dir")
        .arg(&ports)
        .arg("--cache-dir")
        .arg(&cache)
        .arg("--work-dir")
        .arg(&work)
        .arg("--cabin")
        .arg(cabin_binary())
        .env("CABIN_REGISTRY_TOKEN", "cabin_testtoken1234")
        .output()
        .unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "stdout:\n{stdout}\nstderr:\n{stderr}"
    );
    // The pacing lives in the spawned `cabin publish` batch, on the
    // typed 429 error; its warning flows through the inherited stderr.
    assert!(
        stderr.contains("the registry rate limited cabin-ports/zlib"),
        "{stderr}"
    );

    // The limited PUT is retried: the one package uploads twice (429
    // then 201).
    let puts: Vec<String> = registry
        .requests()
        .into_iter()
        .filter(|(method, _, _)| method == "PUT")
        .map(|(_, path, _)| path)
        .collect();
    assert_eq!(
        puts,
        [
            "/api/v1/packages/cabin-ports/zlib/1.3.1?new-revision=true",
            "/api/v1/packages/cabin-ports/zlib/1.3.1?new-revision=true",
        ]
    );
}

/// A failing preflight must leave the remote registry untouched:
/// zlib preflights clean, libpng's sources do not compile, so the
/// run aborts before any upload - including zlib's.
#[test]
fn a_failed_preflight_makes_no_remote_mutation() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let ports = dir.child("ports");
    let cache = dir.child("cache");

    let (zlib_bytes, zlib_hex) = make_tar_gz(&[
        ("zlib-1.3.1/zlib.h", "int zlib_answer(void);\n"),
        (
            "zlib-1.3.1/zlib.c",
            "#include \"zlib.h\"\nint zlib_answer(void) { return 42; }\n",
        ),
    ]);
    seed_port_cache(cache.path(), &zlib_bytes, &zlib_hex);
    write_port_package(
        &ports,
        "zlib",
        "1.3.1",
        &zlib_hex,
        &[],
        "[target.z]\ntype = \"library\"\nsources = [\"zlib.c\"]\ninclude-dirs = \
         [\".\"]\nc-standard = \"c11\"\n",
    );
    let (libpng_bytes, libpng_hex) = make_tar_gz(&[(
        "libpng-1.6.50/png.c",
        "#error this port must fail its preflight build\n",
    )]);
    seed_port_cache(cache.path(), &libpng_bytes, &libpng_hex);
    write_port_package(
        &ports,
        "libpng",
        "1.6.50",
        &libpng_hex,
        &[],
        "[dependencies]\n\"cabin-ports/zlib\" = \"^1.3\"\n\n[target.png]\ntype = \
         \"library\"\nsources = [\"png.c\"]\ninclude-dirs = [\".\"]\nc-standard = \
         \"c11\"\ndeps = [\"cabin-ports/zlib\"]\n",
    );

    let registry = FakeRegistry::start();
    let output = scrubbed(tool())
        .arg("--publish")
        .arg("--index-url")
        .arg(&registry.url)
        .arg("--ports-dir")
        .arg(ports.path())
        .arg("--cache-dir")
        .arg(cache.path())
        .arg("--work-dir")
        .arg(dir.child("work").path())
        .arg("--cabin")
        .arg(cabin_binary())
        .env("CABIN_REGISTRY_TOKEN", "cabin_testtoken1234")
        .output()
        .unwrap();
    assert!(!output.status.success(), "the preflight failure must abort");
    assert!(
        registry.requests().is_empty(),
        "no request may reach the registry after a failed preflight: {:?}",
        registry.requests()
    );
}

/// `<name> <version> <outcome>` per receipts-log line, after checking
/// every line is a complete receipt for one registry.
fn receipt_lines(log: &Path) -> Vec<String> {
    let text = fs::read_to_string(log).unwrap_or_default();
    let mut registries = std::collections::BTreeSet::new();
    let lines = text
        .lines()
        .map(|line| {
            let receipt: serde_json::Value = serde_json::from_str(line).unwrap();
            registries.insert(receipt["registry"].as_str().unwrap().to_owned());
            assert!(
                receipt["checksum"]
                    .as_str()
                    .is_some_and(|c| c.starts_with("sha256:") && c.len() == 71),
                "{line}"
            );
            format!(
                "{} {} {}",
                receipt["name"].as_str().unwrap(),
                receipt["version"].as_str().unwrap(),
                receipt["outcome"].as_str().unwrap()
            )
        })
        .collect();
    assert!(registries.len() <= 1, "{registries:?}");
    lines
}

/// The `fake_ports` catalog committed in a Git repository (the
/// `--publish-new` freshness check compares commits), returning the
/// ports dir, the seeded cache, and the commit.
fn committed_fake_ports(dir: &TempDir) -> (PathBuf, PathBuf, String) {
    let (ports, cache) = fake_ports(dir);
    git(dir.path(), &["init", "-q"]);
    git(dir.path(), &["add", "ports"]);
    git(dir.path(), &["commit", "-q", "-m", "ports"]);
    let head = Command::new("git")
        .current_dir(dir.path())
        .args(["rev-parse", "HEAD"])
        .output()
        .unwrap();
    let head = String::from_utf8(head.stdout).unwrap().trim().to_owned();
    (ports, cache, head)
}

/// One `--plan-new` or `--publish-new` run over `ports` against
/// `registry`, reading the receipts log `log` and, for an upload,
/// appending to it: `(succeeded, stdout, stderr)`.
fn new_run(
    mode: &str,
    dir: &TempDir,
    (ports, cache, head): (&Path, &Path, &str),
    registry: &FakeRegistry,
    log: &Path,
    extra: &[&str],
) -> (bool, String, String) {
    if !log.exists() {
        fs::write(log, "").unwrap();
    }
    let mut command = scrubbed(tool());
    command
        .arg(mode)
        .arg("--index-url")
        .arg(&registry.url)
        .arg("--receipts")
        .arg(log)
        .arg("--ports-dir")
        .arg(ports)
        .arg("--cache-dir")
        .arg(cache)
        .arg("--work-dir")
        .arg(dir.child("work").path())
        .arg("--cabin")
        .arg(cabin_binary())
        .args(extra)
        .env("CABIN_REGISTRY_TOKEN", "cabin_testtoken1234");
    if mode == "--publish-new" {
        command
            .arg("--receipts-out")
            .arg(log)
            .arg("--main")
            .arg(head);
    }
    let output = command.output().unwrap();
    (
        output.status.success(),
        String::from_utf8_lossy(&output.stdout).into_owned(),
        String::from_utf8_lossy(&output.stderr).into_owned(),
    )
}

fn puts(registry: &FakeRegistry) -> Vec<String> {
    registry
        .requests()
        .into_iter()
        .filter(|(method, _, _)| method == "PUT")
        .map(|(_, path, _)| path)
        .collect()
}

/// `--publish-new` uploads only what has no receipt, skips a held
/// version, never asks for a packaging revision, and resumes a partial
/// failure from its receipts, resubmitting the failed upload only on
/// --retry-failed.  The preview makes no request at all.
#[test]
#[allow(clippy::too_many_lines)] // one resumable scenario, run by run
fn publish_new_uploads_each_version_once_and_resumes_from_its_receipts() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache, head) = committed_fake_ports(&dir);
    dir.child("publish-holds.txt")
        .write_str("# held for the test\nfastlz 0.5.0 awaiting a decision\n")
        .unwrap();
    let snapshot = (ports.as_path(), cache.as_path(), head.as_str());
    let log = dir.child("receipts.jsonl");
    // The second upload, libpng's, answers 500.
    let registry = FakeRegistry::start_with(Behavior {
        stateful: true,
        fail_put: Some(2),
        ..Behavior::default()
    });

    let (ok, stdout, stderr) = new_run("--plan-new", &dir, snapshot, &registry, log.path(), &[]);
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(
        stdout.contains("upload, in order: cabin-ports/zlib 1.3.1, cabin-ports/libpng 1.6.50"),
        "{stdout}"
    );
    assert!(
        stdout.contains("not uploading cabin-ports/fastlz 0.5.0: held: awaiting a decision"),
        "{stdout}"
    );
    assert!(
        stdout.contains("plan complete: 2 versions to upload"),
        "{stdout}"
    );
    assert!(!stdout.contains("staged cabin-ports/fastlz"), "{stdout}");
    assert!(registry.requests().is_empty(), "{:?}", registry.requests());
    assert_eq!(receipt_lines(log.path()), Vec::<String>::new());

    let (ok, stdout, stderr) = new_run("--publish-new", &dir, snapshot, &registry, log.path(), &[]);
    assert!(!ok, "a failed upload fails the run: {stdout}");
    assert!(
        // `exit status: 1` on Unix, `exit code: 1` on Windows.
        stderr.contains(": 1) at cabin-ports/libpng 1.6.50"),
        "{stderr}"
    );
    assert_eq!(
        receipt_lines(log.path()),
        [
            "cabin-ports/zlib 1.3.1 uploaded",
            "cabin-ports/libpng 1.6.50 failed"
        ]
    );

    // A requirement whose last receipt names other bytes than it stages
    // blocks what was validated against it.
    let receipts = fs::read_to_string(log.path()).unwrap();
    let first = receipts.lines().next().unwrap();
    let staged: serde_json::Value = serde_json::from_str(first).unwrap();
    let staged = staged["checksum"].as_str().unwrap();
    let diverged = dir.child("diverged.jsonl");
    diverged
        .write_str(&receipts.replace(staged, &format!("sha256:{}", "0".repeat(64))))
        .unwrap();
    let (ok, stdout, stderr) = new_run(
        "--publish-new",
        &dir,
        snapshot,
        &registry,
        diverged.path(),
        &["--retry-failed"],
    );
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(
        stdout.contains("cabin-ports/zlib 1.3.1 awaits a manual packaging revision"),
        "{stdout}"
    );
    assert!(stdout.contains("nothing to upload"), "{stdout}");
    assert_eq!(puts(&registry).len(), 2);

    // The failed upload may have landed: only an operator's
    // --retry-failed resubmits it.
    let (ok, stdout, stderr) = new_run("--publish-new", &dir, snapshot, &registry, log.path(), &[]);
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(
        stdout.contains("not uploading cabin-ports/libpng 1.6.50: its upload failed"),
        "{stdout}"
    );
    assert!(stdout.contains("nothing to upload"), "{stdout}");
    assert_eq!(puts(&registry).len(), 2);
    let (ok, stdout, stderr) = new_run(
        "--publish-new",
        &dir,
        snapshot,
        &registry,
        log.path(),
        &["--retry-failed"],
    );
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert_eq!(
        puts(&registry),
        [
            "/api/v1/packages/cabin-ports/zlib/1.3.1",
            "/api/v1/packages/cabin-ports/libpng/1.6.50",
            "/api/v1/packages/cabin-ports/libpng/1.6.50",
        ]
    );
    assert_eq!(
        receipt_lines(log.path()),
        [
            "cabin-ports/zlib 1.3.1 uploaded",
            "cabin-ports/libpng 1.6.50 failed",
            "cabin-ports/libpng 1.6.50 uploaded",
        ]
    );

    // Nothing left: no staging, no build, no request.
    let (ok, stdout, stderr) = new_run("--publish-new", &dir, snapshot, &registry, log.path(), &[]);
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(stdout.contains("nothing to upload"), "{stdout}");
    assert!(!stdout.contains("preflight:"), "{stdout}");
    assert_eq!(puts(&registry).len(), 3);
}

/// A lost response or a lost receipts log resubmits identical bytes,
/// which the registry answers as a no-op; different bytes for a
/// version the registry has are refused, never respun.
#[test]
fn publish_new_resubmits_identical_bytes_and_never_respins() {
    require_build_tools();
    let dir = TempDir::new().unwrap();
    let (ports, cache) = zlib_only(&dir);
    git(dir.path(), &["init", "-q"]);
    git(dir.path(), &["add", "ports"]);
    git(dir.path(), &["commit", "-q", "-m", "ports"]);
    let snapshot = (ports.as_path(), cache.as_path(), "HEAD");
    let registry = FakeRegistry::start_with(Behavior {
        stateful: true,
        ..Behavior::default()
    });

    let first = dir.child("first.jsonl");
    let (ok, stdout, stderr) = new_run(
        "--publish-new",
        &dir,
        snapshot,
        &registry,
        first.path(),
        &[],
    );
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    let lost = dir.child("lost.jsonl");
    let (ok, stdout, stderr) =
        new_run("--publish-new", &dir, snapshot, &registry, lost.path(), &[]);
    assert!(ok, "stdout:\n{stdout}\nstderr:\n{stderr}");
    assert!(
        stdout.contains("cabin-ports/zlib 1.3.1 was already published with identical bytes"),
        "{stdout}"
    );
    let receipt: serde_json::Value =
        serde_json::from_str(fs::read_to_string(lost.path()).unwrap().trim()).unwrap();
    assert_eq!(receipt["outcome"], "uploaded");
    assert_eq!(receipt["no_op"], true);

    let other = FakeRegistry::start_with(Behavior {
        stateful: true,
        stored: vec![(
            "/api/v1/packages/cabin-ports/zlib/1.3.1".to_owned(),
            b"other bytes".to_vec(),
        )],
        ..Behavior::default()
    });
    let conflict = dir.child("conflict.jsonl");
    let (ok, stdout, _) = new_run(
        "--publish-new",
        &dir,
        snapshot,
        &other,
        conflict.path(),
        &[],
    );
    assert!(!ok, "a 409 must fail the run: {stdout}");
    assert_eq!(
        receipt_lines(conflict.path()),
        ["cabin-ports/zlib 1.3.1 failed"]
    );
    assert_eq!(puts(&other), ["/api/v1/packages/cabin-ports/zlib/1.3.1"]);
}

/// The clap wiring is the mode contract, so its two fail-opens are
/// pinned: a bare invocation must not run the preflight, and an index
/// URL must not turn an explicit --dry-run into a publish (a `SetTrue`
/// flag satisfies `requires` by its defaulted value).
#[test]
fn requires_a_mode() {
    let bare = tool().args(["--cabin", "cabin"]).output().unwrap();
    assert!(!bare.status.success());

    let contradictory = tool()
        .args([
            "--cabin",
            "cabin",
            "--dry-run",
            "--index-url",
            "https://registry.invalid",
        ])
        .output()
        .unwrap();
    assert!(!contradictory.status.success());
    let stderr = String::from_utf8_lossy(&contradictory.stderr);
    assert!(stderr.contains("--dry-run"), "{stderr}");
}
