# Maintaining

This guide covers the repository's development infrastructure: the pinned Cabin revision, CI, and
the merge queue.  The checks to run before a pull request are in
[CONTRIBUTING.md](../CONTRIBUTING.md#checks), and publishing is in [publishing.md](publishing.md).

The ports and the publisher were imported from `cabinpkg/cabin`'s `ports/` and
`crates/xtask-port-publish/` at commit `8bd98f51aa6619dfb3360a17d5f0c53f62e88035`, where their
earlier history lives.

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

3. Run the [checks](../CONTRIBUTING.md#checks) with a `cabin` installed from the new commit.

Nothing else moves the pin: it never follows Cabin's `main`, and Cabin's own CI runs no
full-catalog dry-run, so the pull request's CI - the checks on Linux, macOS, and Windows, with
`cabin` installed from the new commit - is the merge gate that checks that commit against every
port.  Merge it once that CI and review pass; merging re-publishes nothing (see
[Publishing](publishing.md)).

Publishing waits for a registry deploy that contains the pinned commit
([Execution boundaries](publishing.md#execution-boundaries)), so a pin that a deployed registry
does not yet serve cannot publish.

## CI

`ci.yml` runs on pull requests and in the merge queue.  Its `pin` job checks the Cabin pin; its
`check` job installs the pinned `cabin` and runs the publisher's tests and the dry-run (the
[checks](../CONTRIBUTING.md#checks)) on Linux, macOS, and Windows; and its `lint` job runs
`cargo fmt --check`, `cargo clippy --locked --all-targets -- -D warnings`, and
`gh actions-lock --verify`.  Its `required` job is the only required check and fails unless every
job it gates succeeded.  CI's dry-run is selective (below).

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
  may resolve to it), any other change under an existing name, and every other file.  That
  includes the other documentation, `CONTRIBUTING.md` and `docs/`: editing it preflights every
  port.

Whatever it selects, the dry-run first loads and validates the whole catalog (manifests,
identities, requirement satisfiability, publication order, consumer-test file names) and prints its
selection and the reason before staging anything.  [CONTRIBUTING.md](../CONTRIBUTING.md#checks)
shows the branch command and the focused `--port` check.

### Merge queue

CI sets `PORT_PUBLISH_CHANGED_SINCE` (the variable form of `--changed-since`) to the first parent
of a pull request's test merge once its second parent is verified as the pull request's head, or
to a merge group's base once `HEAD` is verified as the group's head; otherwise it warns and checks
every port.  The narrowing is inductive: the base is `main` or the merge-queue entry ahead, a change
reaches `main` only once its own run (full or selective) passed, and the merge queue requires every
entry to pass, not just a group's head (its `ALLGREEN` grouping strategy).  So a selective run
proves only its printed selection, and an existing port broken by newer runner images surfaces at
the next full run.  Like every dry-run, it probes default features only.
