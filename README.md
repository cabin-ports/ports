# Cabin foundation ports

This repository holds Cabin's **curated foundation ports** and `port-publish`, the tool that
verifies them and publishes them as `cabin-ports/<name>` registry packages.  A foundation port
adapts an important existing C/C++ library - one that does not yet ship a native `cabin.toml` - to
[Cabin](https://github.com/cabinpkg/cabin)'s build model.  You can browse the published catalog at
https://cabinpkg.com, and [Foundation ports](https://cabinpkg.com/docs/foundation-ports/) documents
how they are consumed and the rules a port meets.

```
Cargo.toml, Cargo.lock      the port-publish package (src/, tests/)
ports/<name>/<version>/     one port: cabin.toml, optional patches/ and tests/
.github/workflows/          CI (ci.yml) and the manual publish workflow (publish.yml)
```

The ports and the publisher were imported from `cabinpkg/cabin` at commit `CABIN_REV_PLACEHOLDER`
(`ports/` and `crates/xtask-port-publish/`), where their earlier history lives.

## Ports

Every `ports/<name>/<version>/` directory holds a single `cabin.toml` carrying the canonical scoped
identity (`cabin-ports/<name>`) and a complete `[package.upstream]` block, plus its `patches/` when
it declares any and a `tests/` consumer test when it has one.  The manifest and patches publish
verbatim: editing a byte of them - a comment included - changes the published archive.  `tests/`
is never published.

Ports reach consumers as published `cabin-ports/*` registry packages; nothing here is consumed from
a user's manifest directly.  To stage one, the publisher downloads the archive, verifies its pinned
checksum, safely extracts it, applies the declared `[[package.upstream.copy]]` steps and then the
declared `[package.upstream].patches` as byte-exact unified diffs.

### Placing prebuilt files with `[[package.upstream.copy]]`

Some libraries ship a build-time file under a name the compiler does not expect - for example,
libpng ships its configuration as `scripts/pnglibconf.h.prebuilt`, which its build normally copies
to `pnglibconf.h`.  A port declares that placement declaratively:

```toml
[[package.upstream.copy]]
from = "scripts/pnglibconf.h.prebuilt"
to = "pnglibconf.h"
```

Each step copies one already-present file in the extracted source to a second in-tree location.
Both paths are validated as relative paths inside the source tree, the copy runs after extraction
and before the manifest is written (so the committed `cabin.toml` always wins), and the source file
is covered by the archive's pinned SHA-256.  This is a **static file copy**, not a build script: it
cannot run commands, generate content, or read anything outside the extracted archive.

### Correcting sources with `[package.upstream].patches`

Small corrections that no copy step can express - build-system fixes, portability tweaks - are
declared as unified-diff files under the port's `patches/` directory:

```toml
[package.upstream]
# ... url / checksum / format / strip-prefix ...
patches = ["patches/0001-fix-msvc-build.patch"]
```

Patches apply in declared order, after every copy step, and application is byte-exact: fixed `-p1`
strip, no fuzz, no offset search, no newline normalization, text diffs only.  Like the copy steps,
this is declarative transformation, never a script; a patch that does not apply fails the
materialization.  `.gitattributes` keeps Git from converting their line endings.  See
[`[package.upstream]`](https://cabinpkg.com/docs/manifest/#packageupstream) for the full rules.

Retiring a specific *version* of a foundation port removes `ports/<name>/<version>/`; retiring an
entire port removes the whole `ports/<name>/` directory.

### What ports are not

- They are **not Cabin's public registry**.
- They are **not a submission queue** for arbitrary C/C++ libraries; this repository is curated.
- They are **not** a mechanism for distributing pre-built binaries or compiled artifacts.
- They are **not** a workaround for missing build-script support - they only describe libraries
  whose source layout fits Cabin's existing target model (a list of sources plus include
  directories), optionally placing a prebuilt file with a static `[[package.upstream.copy]]` step
  or correcting one with a declared byte-exact patch.  Ports that need code generation or a
  configure run do not belong here until the build graph runs generated actions.

### Policy

- Sources must be pinned by URL and SHA-256.  Floating references (`latest`, branches, tag-only
  without integrity) are rejected.
- No upstream build-system invocation today.  Cabin does not run CMake, Autotools, Meson, Make, or
  upstream `configure` scripts.
- Patches under `patches/` (if any) should be limited to what is strictly required to make a port
  build through Cabin: build-system fixes and portability tweaks, never feature changes or
  divergence from upstream behavior.
- A `library` target that embodies a well-known native library claims its identity with
  [`links`](https://cabinpkg.com/docs/manifest/#links) (`zlib` claims `"z"`), judged against
  upstream conventions.  Header-only ports claim nothing, and an alternative implementation with
  its own symbol namespace claims its own identity (`miniz` claims `"miniz"`, not `"z"`).
- A foundation port should be **retired** once its upstream project ships and maintains a native
  `cabin.toml`.
- A published version is immutable, so a correction to a published port ships as a
  [packaging revision](https://cabinpkg.com/docs/foundation-ports/#packaging-revisions) of the
  same upstream version.

## Cabin revision

`port-publish` links Cabin's crates, and drives a `cabin` binary, from **one pinned commit** of
`cabinpkg/cabin`: every `cabinpkg-*` dependency in `Cargo.toml` names the same `rev`, and the
`cabin` binary must be installed from that commit.  Packaging, materialization, and the registry
client therefore come from a single Cabin revision, and the tool never looks for `cabin` on `PATH`.
CI reads the pin from `Cargo.lock` and fails unless exactly one Cabin commit is locked.

To move the pin, in one pull request:

1. Pick a commit on `cabinpkg/cabin`'s `main` (never an unmerged branch) and set it as the `rev` of
   every `cabinpkg-*` dependency in `Cargo.toml`.
2. Start `Cargo.lock` from Cabin's own lockfile at that commit, so the dependencies the tool shares
   with the `cabin` binary resolve to the versions the binary is built with, and let Cargo adjust
   it:

   ```console
   $ curl -fsSL "https://raw.githubusercontent.com/cabinpkg/cabin/$rev/Cargo.lock" -o Cargo.lock
   $ cargo check
   ```

3. Run the checks below with a `cabin` installed from the new commit.

Publishing waits for a registry deploy that contains the pinned commit (see
[Publishing](#publishing)), so a pin that a deployed registry does not yet serve cannot publish.

## Checks

Install the pinned `cabin`, then run the publisher's tests and the complete dry-run preflight, which
stages every port, builds it against the staged package, and runs its consumer test:

```console
$ rev="$(sed -n 's|^source = "git+https://github.com/cabinpkg/cabin?rev=\([0-9a-f]\{40\}\)#.*|\1|p' Cargo.lock | sort -u)"
$ cargo install --locked --debug --git https://github.com/cabinpkg/cabin --rev "$rev" cabinpkg --root target/pinned-cabin
$ PORT_PUBLISH_CABIN="$PWD/target/pinned-cabin/bin/cabin" cargo test --locked
$ cargo run --locked -- --dry-run --cabin target/pinned-cabin/bin/cabin
```

The tests that drive `cabin` read it from `PORT_PUBLISH_CABIN` and fail without it.  The dry-run
downloads each pinned upstream archive once into Cabin's cache (`<cache>/ports/archives/`; see
`--help`).  CI runs these on Linux, macOS, and Windows, plus `cargo fmt --check`,
`cargo clippy --all-targets`, and `gh actions-lock --verify`.

## Publishing

Publishing is manual: dispatching the **Publish** workflow (`.github/workflows/publish.yml`) on
`main` publishes every port to `https://registry.cabinpkg.com`, so a merged change reaches the
registry at the next dispatch.  A dispatch on any other ref publishes nothing.

- **Credential boundary.**  The `build` job installs the pinned `cabin` and builds `port-publish`
  without any credential.  Only the `publish` job may request the run's OIDC token, and it runs
  just those two prebuilt binaries: it builds every port's probe but never runs one, because
  running upstream code next to the token would hand that code the token.  Running the consumer
  tests is the pull-request dry-run's job.
- **Registry readiness.**  The `await-deploy` job runs Cabin's own `cargo workflow-await-deploy`
  guard from the pinned commit and waits until the registry has deployed a Cabin commit that
  contains it, so the client never sends metadata the running registry does not accept.
- **Uploads.**  `port-publish --publish` performs the same preflight and then uploads every
  package in publication order, in batches of 50.  Each batch is one `cabin publish` invocation,
  which exchanges the run's OIDC token for its own short-lived token confined to the `cabin-ports`
  scope and revokes it on exit.  Every upload passes `--new-revision`, so a merged change to a
  port's bytes publishes as a new packaging revision, while re-publishing byte-identical bytes is
  a no-op: a rerun over an unchanged tree changes nothing.
- **Ordering.**  Publish runs are serialized and never cancelled.  A run whose commit is no longer
  `main`'s head skips its upload instead of publishing an intermediate state.

Which repository, workflow, and ref may exchange a token is registry data registered by the
registry operator: `cabin-ports/ports`, `publish.yml`, `refs/heads/main`.  Dispatching with
`exchange-check` exchanges and immediately revokes a token, proving that binding end to end while
publishing nothing.

## License

The publisher is licensed under [Apache-2.0](LICENSE).  Each port's upstream library keeps its own
license, declared in its manifest.
