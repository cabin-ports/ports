# Contributing

This guide covers adding a foundation port, correcting one, and the checks a pull request must
pass.  Cabin's documentation is the specification this guide builds on:
[Foundation ports](https://cabinpkg.com/docs/foundation-ports/) defines the port contract and
[`[package.upstream]`](https://cabinpkg.com/docs/manifest/#packageupstream) the provenance table
every port declares.

## What belongs here

A foundation port adapts an important existing C/C++ library - one that does not yet ship a
native `cabin.toml` - to Cabin's build model.  The catalog is curated, and foundation ports are:

- **not** Cabin's public registry;
- **not** a submission queue for arbitrary C/C++ libraries;
- **not** a mechanism for distributing pre-built binaries or compiled artifacts;
- **not** a workaround for missing build-script support - they only describe libraries whose
  source layout fits Cabin's existing target model (a list of sources plus include directories),
  optionally placing a prebuilt file with a static `[[package.upstream.copy]]` step or correcting
  one with a declared byte-exact patch.  Ports that need code generation or a configure run do
  not belong here until the build graph runs generated actions.

A foundation port should be **retired** once its upstream project ships and maintains a native
`cabin.toml`.

## Port layout

Every `ports/<prefix>/<name>/<version>/` directory holds a single `cabin.toml` carrying the
canonical scoped identity (`cabin-ports/<name>`) and a complete `[package.upstream]` block, plus its
`patches/` when it declares any and its `tests/` consumer test (required for new ports):

```
ports/<prefix>/<name>/<version>/
  cabin.toml   the published manifest
  patches/     unified diffs declared by [package.upstream].patches, if any
  tests/       the consumer test (main.cc, main.c); never published
```

`<prefix>` is the first two characters of the name, or the whole name when it has only one: zlib
1.3.1 lives in `ports/zl/zlib/1.3.1/`, and a port named `z` in `ports/z/z/`.  The prefix only
groups directories; it is not part of the package identity.  The publisher refuses any other
directory or file above a version directory, so CI fails a misplaced port.

The manifest and patches publish verbatim: editing a byte of them - a comment included - changes
the published archive.  `tests/` is never published.  Cabin's
[anatomy of a foundation port](https://cabinpkg.com/docs/foundation-ports/#anatomy-of-a-foundation-port)
lists what the manifest may declare, such as only unconditional registry dependencies, and the
publisher refuses a manifest that breaks those rules.

A `library` target that embodies a well-known native library claims its identity with
[`links`](https://cabinpkg.com/docs/manifest/#links) (`zlib` claims `"z"`), judged against
upstream conventions.  Header-only ports claim nothing, and an alternative implementation with its
own symbol namespace claims its own identity (`miniz` claims `"miniz"`, not `"z"`).

## Upstream sources

Ports reach consumers as published `cabin-ports/*` registry packages; nothing here is consumed from
a user's manifest directly.  To stage one, the publisher downloads the archive, verifies its pinned
checksum, safely extracts it, applies the declared `[[package.upstream.copy]]` steps and then the
declared `[package.upstream].patches` as byte-exact unified diffs.

- Sources must be pinned by URL and SHA-256.  Floating references (`latest`, branches, tag-only
  without integrity) are rejected.
- No upstream build-system invocation today.  Cabin does not run CMake, Autotools, Meson, Make, or
  upstream `configure` scripts.

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
materialization.  `.gitattributes` keeps Git from converting their line endings, so never
normalize, re-wrap, or regenerate a committed patch.  See
[`[package.upstream]`](https://cabinpkg.com/docs/manifest/#packageupstream) for the full rules.

## Upstream fidelity

- Preserve upstream functionality and intended build semantics.  Never omit required sources,
  disable features, introduce stubs, or weaken tests merely to make a port pass CI.  Document
  limitations explicitly, for example in a `cabin.toml` comment; a library whose required build
  model Cabin cannot represent is not ported.
- Patches under `patches/` (if any) should be limited to what is strictly required to make a port
  build through Cabin - build-system fixes and portability tweaks - or to add the one notices
  file a package may need ([Licenses and notices](#licenses-and-notices)), never feature changes
  or divergence from upstream behavior.
  Upstream bugs and quality concerns are not a reason to patch; document a limitation instead
  when it matters to consumers.
- Verify dependencies, public include paths, link requirements, platform assumptions, upstream
  checksums, and license obligations ([Licenses and notices](#licenses-and-notices)).

## Licenses and notices

Preserve the license information that redistribution requires, and add nothing beyond it.
Simplifying a port never means dropping a notice that a license requires.

- **Keep what upstream ships.**  The published package keeps upstream's license and notice
  files; when `[package] include` narrows the published files, keep those files among them.
  Never edit an upstream file to move or tidy its license information, and never remove a
  copyright line.  One edit stays because licenses require it: a patch that changes a file whose
  license makes changed files say so (Apache-2.0, the GPL family) adds that statement, with the
  date of the change, to the file as one comment line.
- **Check before adding.**  For each third-party component whose code is in the published
  package, establish what its license requires of a redistribution:
  - the complete license text (Apache-2.0, together with the component's `NOTICE` file if it has
    one; BSL-1.0; the GPL family);
  - the copyright notice with the license's own text, including its conditions and disclaimer
    (MIT, BSD, ISC, Zlib);
  - the attribution its version asks for and the license's URI (Creative Commons BY and BY-SA);
  - nothing (public domain, CC0).

  Then check whether the package already carries it: in the source file's header, in an upstream
  license or notice file, or, for a text that names no holder (Apache-2.0, BSL-1.0, the GPL
  family), as the copy that ships for another component.  When it does, add nothing; most ports
  need no license patch.  A URL alone does not satisfy a license that requires its text or
  notice, such as MIT, BSD, Apache-2.0, or BSL-1.0.
- **One notices file, one patch.**  When a required notice or text is missing, add a single
  patch that only creates `THIRD_PARTY_NOTICES` at the archive root, and keep that file among the
  published files.  A port has at most one such patch, never one license file per component.  The
  file holds only what is missing:
  - for each component, its name, origin, the files that contain it, its copyright lines, and its
    SPDX identifier;
  - each distinct license text once, verbatim from the component's origin (from the license's
    canonical source only for a text that names no holder); components under the same text share
    one copy, each with its own copyright lines;
  - for Creative Commons material, the author, the work's title and URL, the license's name,
    version, and URI, and whether the code was changed, in place of the legal code.

  Where a component offers a choice of licenses, take an alternative that is compatible with the
  rest of the package, preferring one whose text the package already ships.  When the archive
  lacks upstream's own license text, the same patch adds it as a file under the name upstream's
  notices refer to.  The notices file has no creation date and no tool output; copyright years
  stay as written.  `cabin.toml` points to the file in one comment line and does not repeat the
  provenance.
- **`license` is metadata.**  The `[package] license` SPDX expression describes what the
  published package ships: `AND` between components under different terms, `OR` where upstream
  offers a choice, `WITH` for an exception.  It has no term for a file the package does not ship;
  where tests, docs, or tools carry other terms, prefer narrowing `[package] include` to
  declaring them.  Generated build-system files that ship beside the sources but that the port
  does not compile (`Makefile.in`, `configure`) keep their notices and add no term.  Do not
  repeat a term, and drop an `OR` group when one of its alternatives is already required:
  `MIT AND (BSL-1.0 OR MIT)` is `MIT`.  The expression replaces no notice and resolves no
  incompatibility.
- **Be proportional about copied code.**  Follow up what upstream itself identifies: its license
  and notice files, file headers, credit comments, and vendored directories.  A vendored
  component, or a substantial, identifiable copy, gets the treatment above once a look at the
  original confirms the copy and its license.  Do not research the provenance of short
  functions, standard algorithms, or common idioms that upstream does not identify as copied.
  Where upstream credits a short snippet whose copyright significance is uncertain, keep the
  credit, state the doubt in one `cabin.toml` comment line, and add no license file for it.
  Size alone never shows that a copy is permitted, and no attribution is dropped and no code
  relabeled to avoid a hold.
- **Creative Commons code.**  For code copied from Stack Overflow, cppreference, or a similar
  site, confirm the post, the license version that applies to it, and whether the code is a
  genuine, substantial copy.  If it is, attribute it in `THIRD_PARTY_NOTICES` as above and add
  its term to `license`: the code stays under that license.  A notice does not settle whether
  ShareAlike reaches the code around it; raise a substantial BY-SA block with the maintainers in
  the pull request.
- **Hold only for a real problem.**  Never claim a permission that no source grants.
  Substantial code whose source grants no permission to redistribute it, or a verified
  incompatibility between the licenses of code that must ship together, is a legal restriction
  on redistribution ([Pull requests](#pull-requests)).  Adding a license text resolves neither.
- **Leave published versions alone.**  Do not revise a published port only to rearrange its
  license files; a new version follows these rules.

## Consumer tests

Each port is checked by building a generated probe package against the staged port and running
it.  The port's consumer test replaces the probe's empty `main`, so a new port must include a
meaningful one that exercises a real public API and checks its behavior.  An empty `main` or a
compile-only smoke test is not sufficient evidence of correctness.

`tests/main.cc` is the C++ consumer.  `tests/main.c` is the C consumer, which a port has when a
library target that its default features enable declares, itself or through the package, an
`interface-c-standard` other than `"none"` or `interface-cxx-standard = "none"`.  Keep C
first-class alongside C++: a port with a C interface tests it from C too.  Any other file under
`tests/` is an error; dotfiles are ignored.

## Checks

Install the pinned `cabin`, then run the publisher's tests and the complete dry-run preflight, which
stages every port, builds it against the staged package, and runs its consumer test:

```console
$ rev="$(scripts/actions/cabin-rev.sh)"
$ cargo install --locked --debug --git https://github.com/cabinpkg/cabin --rev "$rev" cabinpkg --root target/pinned-cabin
$ PORT_PUBLISH_CABIN="$PWD/target/pinned-cabin/bin/cabin" cargo test --locked
$ cargo run --locked -- --dry-run --cabin target/pinned-cabin/bin/cabin
```

The tests that drive `cabin` read it from `PORT_PUBLISH_CABIN` and fail without it.  The dry-run
downloads each pinned upstream archive once into Cabin's cache (`<cache>/ports/archives/`; see
`--help`).

A change to `scripts/actions/` or `.github/workflows/` must also pass the `lint` job's
`shellcheck`, `shfmt`, `actionlint`, and `check-inline-runs.sh` steps;
[CI](docs/maintaining.md#ci) shows how to run them locally.

On a branch, a selective dry-run preflights only what the branch changes; compare with where it
starts:

```console
$ cargo run --locked -- --dry-run --changed-since "$(git merge-base HEAD origin/main)" --cabin target/pinned-cabin/bin/cabin
```

`--port <name>` (repeatable) instead preflights the named ports and their dependencies without
Git - a focused local check, not a replacement for the selection CI makes.  Before opening a pull
request, run the `--changed-since` dry-run from the branch's merge base, or the full dry-run.  A
selective dry-run proves only the selection it prints, and every dry-run probes default features
only, so it does not validate non-default features.
[Selective dry-runs](docs/maintaining.md#selective-dry-runs) describes what each change selects,
and [CI](docs/maintaining.md#ci) what pull-request CI runs.

## Pull requests

- Prefer one port per pull request.  Merge prerequisite ports separately before their dependents,
  and keep unrelated ports, shared tooling changes, and Cabin revision updates out of a port's
  pull request.
- Every change lands through a pull request and the merge queue as one squash commit; commit
  subjects follow [Conventional Commits](https://www.conventionalcommits.org/) (for example,
  `feat(ports): add zlib 1.3.1`).
- A pull request merges only once CI passes on Linux, macOS, and Windows and review approves it.
  Never weaken a check to make a port mergeable.
- While automatic publishing is on, a merged port is distribution-ready and publishes by default
  ([Enabling and disabling](docs/publishing.md#enabling-and-disabling)).  Upstream bugs and
  vulnerabilities short of a concrete critical security risk, quality concerns, and open
  questions are not holds: investigate, decide, and fix packaging gaps as above.  Add a
  `publish-holds.txt` line, in the same pull request, only for a blocker packaging cannot
  resolve: a legal restriction on redistribution, a concrete critical security risk, or a failure
  to produce a valid package.  The line states the evidence, the remediation attempted, and the
  exact blocking condition.  The operator's hold for a version with a rejected revision
  ([Failures](docs/publishing.md#failures)) is separate.

## Published versions

A published version is immutable, so a correction to a published port ships as a
[packaging revision](https://cabinpkg.com/docs/foundation-ports/#packaging-revisions) of the same
upstream version.  Keep such a correction in its own pull request, not folded into unrelated work;
merging it publishes nothing by itself, since only a full `all` publish uploads packaging revisions
([Publishing](docs/publishing.md)).

Retiring a specific *version* of a foundation port removes `ports/<prefix>/<name>/<version>/`;
retiring an entire port removes the whole `ports/<prefix>/<name>/` directory.
