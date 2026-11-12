# AGENTS.md

This repository holds Cabin's curated foundation ports (`ports/`) and `port-publish`, the single
Rust package that verifies and publishes them (`src/`, `tests/`).  README.md introduces the
repository, CONTRIBUTING.md documents port authoring and the checks, docs/maintaining.md the pinned
Cabin revision and CI, and docs/publishing.md publishing; Cabin's
[Foundation ports](https://cabinpkg.com/docs/foundation-ports/) documents the port contract.

## Ports

- A port lives at `ports/<prefix>/<name>/<version>/`.  `<prefix>` is the first two characters of
  the unscoped package name, or the whole name when it has one character (`ports/zl/zlib/`,
  `ports/z/z/`).  Only `<name>` and `<version>` form the identity.  Discovery refuses anything
  else under `ports/` above a version directory, so CI fails a misplaced port.
- A port directory, except its unpublished `tests/`, is published verbatim, so every edit,
  including a comment, changes archive bytes.  Do not fold a port correction into unrelated work;
  a correction to a published version ships as a packaging revision of the same upstream version.
- Patches apply byte-exactly.  `.gitattributes` disables line-ending conversion for them; never
  normalize, re-wrap, or re-generate a committed patch.
- Keep C first-class alongside C++: probes and consumer tests cover both languages.
- New ports must include a meaningful consumer test (`tests/main.c`
  or `tests/main.cc`) that exercises a real public API and checks its
  behavior. An empty `main` or a compile-only smoke test is not
  sufficient evidence of correctness.
- Preserve upstream functionality and intended build semantics. Never
  omit required sources, disable features, introduce stubs, or weaken
  tests merely to make a port pass CI. Explicitly document limitations;
  block a port when Cabin cannot represent its required build model.
- Verify dependencies, public include paths, link requirements,
  platform assumptions, upstream checksums, and license obligations.
  Do not assume that a successful default-feature probe validates
  non-default features or unsupported configurations.
- Publish by default.  Resolve license, notice, and attribution gaps
  through packaging, and never hold a version for an ordinary upstream
  bug or an open question; CONTRIBUTING.md ("Upstream fidelity",
  "Licenses and notices", and "Pull requests") gives the rules and the
  grounds for a `publish-holds.txt` line.
- Preserve the license information redistribution requires, by the
  simplest means that complies: no license patch when the published
  package already carries it, otherwise one patch that adds
  `THIRD_PARTY_NOTICES`.  Never add one license file per component,
  Creative Commons legal code where the license's URI suffices, or
  `license` terms for code the package does not contain.  Follow up the
  third-party code upstream itself identifies; do not research the
  provenance of short functions and common idioms that upstream does
  not identify as copied, and never treat a copy as permitted because
  it is small.

## Comments

- Prefer self-explanatory code and concise comments. Explain non-obvious
  intent, constraints, invariants, and trade-offs; do not restate what
  the code, configuration, or tests already make clear.
- In `cabin.toml`, document non-obvious packaging decisions, patches,
  compatibility limitations, platform workarounds, and the reason for a
  non-obvious `license` term. Third-party notices belong in the
  package's notice files, not in comments. Avoid upstream API
  documentation, exhaustive bug or feature inventories, and lengthy
  source citations.
- Apply the same principles to Rust, tests, scripts, and workflows.
  Preserve public API contracts and reasoning essential to correctness,
  security, or publication safety. Avoid duplicating existing documentation.
- Do not impose comment-count limits or remove useful context merely
  to reduce line counts. Preserve legally required notices.
- A comment-only change to a published port changes its package bytes.
  Treat such cleanup as a packaging change requiring appropriate review
  and revision handling.

## Cabin revision

- Every `cabinpkg-*` dependency names the same full commit of `cabinpkg/cabin`'s `main`, and the
  `cabin` binary used for checks and publishing is installed from that commit.  Never pin a
  branch, an unmerged commit, or a local path, and never fall back to a `cabin` on `PATH`.
- Port-agnostic logic (manifests, materialization, packaging, the registry client) belongs in
  Cabin and is consumed through the pin; this package keeps only port policy and publish
  orchestration.  Do not copy Cabin code here.

## Workflows

- In `publish.yml`, only the `publish` job and the curl-only `exchange-check` probe request
  `id-token: write`.  The `publish` job runs only the binaries the credential-free `build` job
  produced and `scripts/actions/` from its own checkout.  It neither builds nor runs anything of a
  port: it uploads only what the `prepare` job built in the same run from the same commit, which
  `--prepared` checks against what it stages.  Only `prepare` builds, with no OIDC token and no
  permission to push, because a build is where a port's build script runs; no job of the workflow
  runs a consumer test.  Treat everything `prepare` could have written as untrusted in the jobs
  after it: no cache, the artifacts of `build` and `publish` only by the id each job output, and
  nothing of it executed.  Only the `lock` and `record` jobs request `contents: write`; they run
  only `scripts/actions/receipts.sh` (`git`, `gh`, and `jq`), checked out sparsely at the
  workflow's own commit, and push only the receipts branch.
- The schedule uploads only while the `PORTS_AUTO_PUBLISH` variable is exactly `true`.  Setting
  it, initializing or editing the receipts log, and dispatching an upload are operator decisions,
  never part of routine work, and so are deleting the receipts log's lock and retrying a failed
  upload.  `--publish-new` never passes `--new-revision`: a correction to an uploaded version
  ships only through an `all` dispatch.  While the variable is `true`, a merged port publishes
  on the next run, so a hold lands in its pull request.
- `ci.yml` runs on `pull_request` and `merge_group`.  Its `required` job is the only required
  check and must fail unless every job it gates succeeded; its `pin` job keeps the Cabin pin a
  single commit on `cabinpkg/cabin`'s `main`.  Its dry-run narrows only through `--changed-since`
  from a verified event base; anything unverified checks every port, never none.
- `.github/workflows/actions.lock` is generated: after adding or editing a workflow or `uses:`
  line, run `gh actions-lock --no-narrow` with the extension version `ci.yml` pins and commit the
  result; never edit it by hand.  `uses:` lines name an exact release tag.

## GitHub Actions maintainability

- Workflows are declarative orchestration: triggers, `needs`, permissions, concurrency, runners,
  `uses:` steps, artifacts, environment wiring, and `if:` expressions.  Prefer a native Actions
  feature or an established action to a script.  Keep publication policy, receipts and lock
  state, and substantial data processing out of YAML: selection, eligibility, holds, and receipts
  parsing belong in `port-publish` (`src/`, extending `batch.rs`, `select.rs`, or `plan.rs`), and
  Git and GitHub glue in small single-purpose scripts under `scripts/actions/`.  Before adding
  workflow logic, extend the CLI or script that already owns the responsibility; never implement
  one rule in both Rust and shell.
- Keep each `run:` step to one or a few plain commands; do not add or grow inline control flow,
  `&&`/`||`, pipelines, command substitution, or heredocs.  An inline script is allowed only where
  extracting it would weaken a credential boundary, obscure what a privileged job executes, or
  add disproportionate machinery, and it starts with `# inline: <reason>`.  There is no line-count
  limit; do not compress logic to pass a check.  The `lint` job's
  `scripts/actions/check-inline-runs.sh` flags these constructs; it is a tripwire for review, and
  this rule, not the check, is the policy.  Its validation is documented, not tested: a change to
  it shows a deliberately flagged step failing the `lint` job in its pull request, since a test
  would make the suite fetch `yq` through Go and the network.
- Scripts use `set -euo pipefail`, take explicit arguments or named variables, quote expansions,
  pass `shellcheck` and `shfmt -i 2`, and never turn a failure into success (no `|| true`, no
  `$(...)` whose exit status is lost).  Each nontrivial one has tests in this crate's test suite
  (temporary repositories, a local `file://` remote, a fake `gh`), or documented validation where
  a test is impractical.
- A privileged job is one with `id-token: write` or `contents: write`.  Grant neither to another
  job, and never widen what a privileged job executes.  A repository script it runs comes only
  from `scripts/actions/` at `github.workflow_sha`, checked out with `persist-credentials: false`,
  and in `lock` and `record` only from a sparse checkout of that directory.  Privileged jobs
  install no toolchain or dependency and never run port code or build scripts; of them, only
  `publish` runs the `build` job's binaries.  `exchange-check` stays curl-only with no checkout.
- Preserve the receipts protocol and publication semantics: only fast-forward pushes to the
  receipts branch, the `lock` file naming run and attempt, `record`'s release rules, the modes,
  holds, `retry-failed`, and packaging revisions; change them only as a reviewed policy change.

## Checks and pull requests

- Run the checks in CONTRIBUTING.md ("Checks") before opening or updating a pull request.  A
  selective dry-run proves only the selection it prints, and `--port` is a focused local check:
  before a pull request, run `--changed-since` from the branch's merge base, or the full dry-run.
- Every change lands through a pull request and the merge queue as one squash commit; commit
  subjects follow Conventional Commits.
- Prefer one port per pull request. Merge prerequisite ports separately
  before their dependents. Keep unrelated ports, shared tooling changes,
  and Cabin revision updates out of individual port PRs.
- Do not bypass failing checks or weaken CI to make a port mergeable.
  Merge only after the required checks and reviews pass.
- When a candidate cannot be completed safely, record the blocker,
  evidence, attempted fixes, and conditions for resuming it. Continue
  with independent candidates rather than waiting for user input.
- Keep campaign inventories, progress logs, review artifacts, and
  temporary worktrees outside this repository. Do not commit
  campaign-specific state.
- Routine port contributions must not publish to the production
  registry, yank packages, wipe data, or change deployment credentials.
  Production operations require separate explicit authorization.
