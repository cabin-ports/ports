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

## Cabin revision

- Every `cabinpkg-*` dependency names the same full commit of `cabinpkg/cabin`'s `main`, and the
  `cabin` binary used for checks and publishing is installed from that commit.  Never pin a
  branch, an unmerged commit, or a local path, and never fall back to a `cabin` on `PATH`.
- Port-agnostic logic (manifests, materialization, packaging, the registry client) belongs in
  Cabin and is consumed through the pin; this package keeps only port policy and publish
  orchestration.  Do not copy Cabin code here.

## Workflows

- `publish.yml` keeps credentials out of every job that compiles or runs code it did not
  prebuild: only the `publish` job requests `id-token: write`, and it never runs a port's code.
- `required` in `ci.yml` is the only required check; it must fail unless every job it gates
  succeeded.  Workflows run on `pull_request` and `merge_group`.
- `.github/workflows/actions.lock` is generated: after adding or editing a workflow or `uses:`
  line, run `gh actions-lock --no-narrow` with the extension version `ci.yml` pins and commit the
  result; never edit it by hand.  `uses:` lines name an exact release tag.

## Checks and pull requests

- Run the checks in README.md ("Checks") before opening or updating a pull request.
- Every change lands through a pull request and the merge queue as one squash commit; commit
  subjects follow Conventional Commits.
