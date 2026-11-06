# AGENTS.md

This repository holds Cabin's curated foundation ports (`ports/`) and `port-publish`, the single
Rust package that verifies and publishes them (`src/`, `tests/`).  README.md documents the layout,
the pinned Cabin revision, the checks, and publishing; Cabin's
[Foundation ports](https://cabinpkg.com/docs/foundation-ports/) documents the port contract.

## Ports

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
  produced: it compiles each port's probe but never runs a port's code.
- `ci.yml` runs on `pull_request` and `merge_group`.  Its `required` job is the only required
  check and must fail unless every job it gates succeeded; its `pin` job keeps the Cabin pin a
  single commit on `cabinpkg/cabin`'s `main`.  Its dry-run narrows only through `--changed-since`
  from a verified event base; anything unverified checks every port, never none.
- `.github/workflows/actions.lock` is generated: after adding or editing a workflow or `uses:`
  line, run `gh actions-lock --no-narrow` with the extension version `ci.yml` pins and commit the
  result; never edit it by hand.  `uses:` lines name an exact release tag.

## Checks and pull requests

- Run the checks in README.md ("Checks") before opening or updating a pull request.  A selective
  dry-run proves only the selection it prints, and `--port` is a focused local check: before a pull
  request, run `--changed-since` from the branch's merge base, or the full dry-run.
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
