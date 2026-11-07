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

The ports and the publisher were imported from `cabinpkg/cabin`'s `ports/` and
`crates/xtask-port-publish/` at commit `8bd98f51aa6619dfb3360a17d5f0c53f62e88035`, where their
earlier history lives.

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
CI reads the pin from `Cargo.lock` and fails unless every `cabinpkg-*` package is locked to one
commit that is on `cabinpkg/cabin`'s `main`.

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

Nothing else moves the pin: it never follows Cabin's `main`, and Cabin's own CI runs no
full-catalog dry-run, so the pull request's CI - the [checks](#checks) on Linux, macOS, and
Windows, with `cabin` installed from the new commit - is the merge gate that checks that commit
against every port.  Merge it once that CI and review pass; merging re-publishes nothing (see
[Publishing](#publishing)).

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
`cargo clippy --all-targets`, and `gh actions-lock --verify`; its dry-run is selective (below).

### Selective dry-runs

`--dry-run --changed-since <rev>` preflights only what the change since `<rev>` needs.  It compares
`<rev>`, which must be an ancestor of `HEAD`, with the working tree, uncommitted and untracked files
included, and ignored files under `ports/` too:

- A change that only adds files under `ports/<name>/<version>/` for names `<rev>` lacks, besides
  editing the root `README.md`, `AGENTS.md`, or `publish-holds.txt`, preflights every version of
  those names plus every committed version any of their requirements (optional and dev ones
  included) matches, transitively.  An added name with no `cabin.toml` under it is an error.
- A change that edits nothing but those three root files preflights nothing.
- Anything else preflights every port: a new version of an existing name (existing requirements
  may resolve to it), any other change under an existing name, and every other file.

Whatever it selects, the dry-run first loads and validates the whole catalog (manifests,
identities, requirement satisfiability, publication order, consumer-test file names) and prints its
selection and the reason before staging anything.  On a branch, compare with where it starts:

```console
$ cargo run --locked -- --dry-run --changed-since "$(git merge-base HEAD origin/main)" --cabin target/pinned-cabin/bin/cabin
```

`--port <name>` (repeatable) instead preflights the named ports and their dependencies without
Git - a focused local check, not a replacement for the selection CI makes.

CI sets `PORT_PUBLISH_CHANGED_SINCE` (the variable form of `--changed-since`) to the first parent
of a pull request's test merge once its second parent is verified as the pull request's head, or
to a merge group's base once `HEAD` is verified as the group's head; otherwise it warns and checks
every port.  The narrowing is inductive: the base is `main` or the merge-queue entry ahead, a change
reaches `main` only once its own run (full or selective) passed, and the merge queue requires every
entry to pass, not just a group's head (its `ALLGREEN` grouping strategy).  So a selective run
proves only its printed selection, and an existing port broken by newer runner images surfaces at
the next full run.  Like every dry-run, it probes default features only.

## Publishing

Only `main` publishes, through the **Publish** workflow (`.github/workflows/publish.yml`), and
only to `https://registry.cabinpkg.com`.  A run elsewhere, or on another ref, publishes nothing.
It has two upload modes:

- **`all`** (a manual dispatch; the default): every port, with changed bytes respun as packaging
  revisions.  This is the deliberate full publish and the only way to correct a published version.
- **`new`** (the schedule, or a manual dispatch): only the versions the registry has no upload
  receipt for, never a packaging revision ([Automatic publishing](#automatic-publishing)).

A third dispatch mode, **`plan`**, previews `new` without uploading anything.  Merging a pull
request publishes nothing until automatic publishing is switched on.

- **Credential boundary.**  The `build` job installs the pinned `cabin` and builds `port-publish`
  without any credential.  Only the `publish` job may request the run's OIDC token for a publish,
  and it runs just those two prebuilt binaries: it builds the probes of the ports it stages but
  never runs one, because running upstream code next to the token would hand that code the token.
  Running the consumer tests is the pull-request and merge-queue dry-run's job: no job of this
  workflow runs port code, the `plan` preview included.  (The `exchange-check` probe below also
  requests the token, but runs only `curl`.)  The `lock` and `record` jobs alone may push, only to
  the receipts branch, and run only `git`, `gh`, and `jq`.
- **Registry readiness.**  The `await-deploy` job runs Cabin's own `cargo workflow-await-deploy`
  guard from the pinned commit and waits until the registry has deployed a Cabin commit that
  contains it, so the client never sends metadata the running registry does not accept.
- **Uploads.**  Each mode preflights what it uploads, then uploads it in publication order, in
  batches of 50.  Each batch is one `cabin publish` invocation, which exchanges the run's OIDC
  token for its own short-lived token confined to the `cabin-ports` scope and revokes it on exit.
  `all` preflights every port and passes `--new-revision` on every upload, so a merged change to
  a port's bytes publishes as a new packaging revision, while re-publishing byte-identical bytes
  is a no-op: a rerun over an unchanged tree changes nothing.
- **Ordering.**  A run that may upload holds the receipts log's lock, when the log exists
  ([Automatic publishing](#automatic-publishing)), and upload jobs of both modes are serialized and
  queued, never cancelled.  An `all`
  run whose commit is no longer `main`'s head when its upload starts skips the upload with a
  warning instead of publishing an intermediate state; dispatch again on the new head.  A commit
  landing during the upload's own preflight is not detected; the next dispatch's upload supersedes
  what that run published.

Which repository, workflow, and ref may exchange a token is registry data registered by the
registry operator: `cabin-ports/ports`, `publish.yml`, `refs/heads/main`.  The binding matches no
event, so scheduled and dispatched runs share it.  Dispatching with `exchange-check` exchanges and
immediately revokes a token, proving that binding end to end while publishing nothing.

### Automatic publishing

`port-publish --publish-new` uploads the **first publication** of versions, and `--plan-new`
previews it.  The registry cannot tell this tool which versions it already has - its read routes
serve verified revisions only, and during the private alpha only to an admitted token - so the
answer is the registry's **receipts log**: `receipts.jsonl` on the `publish-receipts` branch, one
JSON object per line, for exactly one registry.  The `record` job appends the client's report of
every upload either mode makes (`"outcome": "uploaded"`, with its `checksum`, `revision`,
`no_op`, and `verification` at upload time, plus the run's `source` commit, pinned `cabin`
revision, and `run` URL), and a `"outcome": "failed"` line for the version an upload invocation
stopped at, whose upload may have landed.  Uploaded is not verified: the log never records
verdicts.

- **Eligibility.**  A version is uploaded when the log has no `uploaded` line for it, the
  reviewed hold list `publish-holds.txt` (`<name> <version> <reason>` per line, validated by
  every mode) does not name it, and every version its declared requirements match is uploaded or
  uploads in the same run.  A version with an `uploaded` line is never uploaded again, whatever
  its recipe now stages, so a rejected upload is never revived and a correction ships only as an
  `all` dispatch.  Without `--new-revision`, the registry refuses different bytes for a version
  it already has (`409`) and answers identical bytes as a no-op, but once every revision of a
  version is rejected it revives identical bytes and accepts different ones.  So a version whose
  upload may have landed without its receipt is never resubmitted unattended (the failures and
  the lock below).  Holds bind `new` only.
- **What it builds.**  Only the candidates and the versions their requirements match stage, and
  build without running a probe.  An uploaded requirement must stage exactly the bytes of its last
  receipt; otherwise the candidates that need it wait for its packaging revision.  A run in which
  every version has an upload receipt, a `failed` line, or a hold stops in the `gate` job, before
  any tool is built; one whose candidates all wait on a blocked requirement still builds the tools
  to find that out.
- **Failures.**  A failed upload fails the run, after the receipts of the uploads before it and a
  `failed` line for the version it stopped at.  That upload may have landed with its response
  lost, so the version, and what requires it, waits for a dispatch with `retry-failed`, which an
  operator starts only after checking the registry's admin listings: a version with a rejected
  revision gets a hold instead, since resubmitting would revive it, and a `409` means the
  registry has other bytes for it, which need a packaging revision.
- **Lock.**  Every run that may upload, in either mode, holds the log's lock: a `lock` file naming
  the run and attempt, which the `lock` job commits on top of the branch tip it read, so of two
  runs racing for it one push fails and that run uploads nothing.  A run that finds the lock held
  by a running run uploads nothing: a scheduled one leaves it to a later run, a dispatch fails.
  The `publish` job uploads only if the lock names its own attempt, so rerunning a run's jobs never
  uploads under an earlier attempt's lock.  The `record` job appends the receipts of the lock's
  attempt only while the run holds the lock, so an old run's jobs cannot replay them, and releases
  it only when that attempt's `publish` job succeeded and left its receipts, or was skipped.
  After a failure or a cancellation an upload may have landed without its receipt, so the lock
  stays, and every later run of either mode fails until an operator reconciles the log (below).
- **Freshness.**  A scheduled run uploads its own commit, which must have passed the merge
  queue's `required` check.  Under the lock it re-reads the log and compares its commit
  with `main`: a candidate whose ports, requirements, or hold main has since changed waits for a
  later run, and every candidate does when main changed anything outside `ports/`.  Unrelated
  merges never stop it.  This is a last check, not a transaction with later merges.
- **Dependents and verification.**  The registry accepts a dependent before what it requires is
  verified, but a consumer resolves it only once all of that is.  The plan lists the required
  versions not recorded as verified.  Verification is the cabinpkg/cabin **Registry Verify**
  workflow's, which runs every 5 minutes; nothing here renders or waits for a verdict.

The schedule (`17,47 * * * *`) runs `new` only while the repository variable `PORTS_AUTO_PUBLISH`
is exactly `true`.  Unset, empty, or `false` starts no job; another spelling such as `TRUE` starts
only the `gate` job, which stops before anything is built.  A scheduled run that is late or skipped
loses nothing, because each run starts from the log, not from the previous run.

- **Enable**, with the operator's approval: initialize the log (below), review a `plan` dispatch,
  then set `PORTS_AUTO_PUBLISH` to `true` (repository variables; needs admin).  Once it is on, a
  merged port is distribution-ready: settle its publication decision before it merges, or add its
  hold in the same pull request (a hold must name a committed version, and a later pull request
  can lose the race with a scheduled run).
- **Disable**: set it to anything else or delete it.  Queued runs then upload nothing; a running
  upload finishes its batch unless the run is cancelled (which keeps the lock), and everything a
  stopped run uploaded stays in the registry.  Turning it off never retracts an accepted package.
- **Initialize** the log while no run is publishing: create the `publish-receipts` branch holding
  `receipts.jsonl`, seeded with an `uploaded` line for every revision the registry already holds,
  from the operator's `GET /api/v1/admin/versions?status=` listings (`pending`, `verified`, and
  `rejected`), so the first run neither resubmits nor revives one.  The log, the lock, and the
  holds are trusted at write-access level: protect the branch from deletion and force pushes, and
  restrict who may push it.  `new` fails without the branch; `all` then records nothing, and an
  `all` run that started before the branch existed uploads nothing once it does.
- **Registry wipe**: stop both modes first, archive the log, and start a new one for the new
  registry; an old log would keep an empty registry looking populated.  Nothing here wipes, yanks,
  or recreates the catalog by itself.
- **Reconcile** after a run that kept the lock (its `record` job says why): if only its push
  failed, rerun that job alone.  Otherwise compare the run's `receipts-*` artifacts (kept 30 days)
  and its log with the admin listings, append an `uploaded` line for each upload that landed
  without one, add a hold for any version with a rejected revision, and delete `lock` in the same
  commit; then dispatch again.  Rerunning the run's other jobs uploads nothing.  A `plan`
  dispatch, or `port-publish --plan-new --index-url https://registry.cabinpkg.com --receipts
  receipts.jsonl --cabin <cabin>` locally, shows what the next run would upload.

## License

The publisher is licensed under [Apache-2.0](LICENSE).  Each port's upstream library keeps its own
license, declared in its manifest.
