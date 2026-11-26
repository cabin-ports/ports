# Publishing

Only `main` publishes, through the **Publish** workflow (`.github/workflows/publish.yml`), and
only to `https://registry.cabinpkg.com`.  A run elsewhere, or on another ref, publishes nothing.
It has two upload modes:

- **`all`** (a manual dispatch; the default): every port, with changed bytes respun as packaging
  revisions.  This is the deliberate full publish and the only way to correct a published version.
- **`new`** (the schedule, or a manual dispatch): only the versions the registry has no upload
  receipt for, never a packaging revision ([Automatic publishing](#automatic-publishing)).

A third dispatch mode, **`plan`**, previews `new` without uploading anything.  Neither upload mode
runs unless the repository variable `PORTS_PUBLISH_ENABLED` is exactly `true`
([Enabling and disabling](#enabling-and-disabling)), and merging a pull request publishes nothing
until automatic publishing is switched on as well.

A published version is immutable, so a correction to a published port reaches the registry only
as a [packaging revision](https://cabinpkg.com/docs/foundation-ports/#packaging-revisions) of the
same upstream version, which only an `all` dispatch publishes.

## Recipe distribution

Foundation ports are distributed as recipes
([Licenses and notices](../CONTRIBUTING.md#licenses-and-notices)): the registry is to hold each
version's manifest and declared patches, and the user's Cabin rebuilds the package from the
pinned upstream archive.  The pinned Cabin cannot publish a recipe yet, and this workflow uploads
the staged sources, which the ports' license information no longer covers.  So
`PORTS_PUBLISH_ENABLED` stays unset until the maintainer authorizes publishing, after Cabin and
the registry support recipe-only publication.  Recipe publishing then starts from a new log, as for
a new registry ([Registry wipe](#registry-wipe)): a log that records the staged-source uploads
would keep `new` from uploading their recipes.

## Execution boundaries

- **Credential boundary.**  The `build` job installs the pinned `cabin` and builds `port-publish`
  without any credential.  Only the `prepare` job builds ports: it stages what the run would
  upload and builds the probe of each port it stages, and the programs of a port that has any,
  with no OIDC token and no permission to push.  A build is where a port's build script runs, so
  no other job builds.  `prepare` records the commit it staged, the run, and the checksums of each
  version's archive and metadata document.  Only the `publish` job may request the run's OIDC
  token for a publish.  It runs just the two prebuilt binaries and the workflow scripts under
  `.github/scripts/`, builds nothing, and runs no port code: it stages its own commit again and
  uploads a version only when that record is its own run's, names the same commit, and lists the
  version with exactly the archive and metadata it has just staged.  Otherwise it uploads nothing
  and fails.  Nothing `prepare` writes is trusted.  Its record can stop an upload and cannot
  change a byte of one.  `publish` restores no cache.  It extracts the record, and the upstream
  archives `prepare` downloaded, by their fixed names into directories of their own, and takes
  the two binaries by the id of the artifact `build` uploaded; `record` takes the receipts by the
  id of the artifact `publish` uploaded.  Another artifact can take a name, never an id.  The
  record and the archives keep their names because their ids would be `prepare`'s to give: a
  list of ids selects several artifacts, each extracted under its own name.  An archive is used
  only when it hashes to the SHA-256 its port pins and is downloaded otherwise, so an upstream
  host that fails after `prepare` cannot fail the upload.  Running the consumer tests is the
  pull-request and merge-queue dry-run's job: no job of this workflow runs one.  (The
  `exchange-check` probe below also requests the token, but runs only `curl`.)  The `lock` and
  `record` jobs alone may push, only to the receipts branch, and run only
  `.github/scripts/receipts.sh` (`git`, `gh`, and `jq`), checked out at the workflow's own commit.
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
  ([The lock](#the-lock)), and upload jobs of both modes are serialized and queued, never
  cancelled.  An `all` run whose commit is no longer `main`'s head when its upload starts skips
  the upload with a warning instead of publishing an intermediate state; dispatch again on the new
  head.  A commit landing during the upload's own preflight is not detected; the next dispatch's
  upload supersedes what that run published.

## Trusted publishing

Which repository, workflow, and ref may exchange a token is registry data registered by the
registry operator: `cabin-ports/ports`, `publish.yml`, `refs/heads/main`.  The binding matches no
event, so scheduled and dispatched runs share it.  Dispatching with `exchange-check` exchanges and
immediately revokes a token, proving that binding end to end while publishing nothing.

## Automatic publishing

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

### Eligibility

A port version in the catalog that passes validation is eligible for publication.  A version that
must not be distributed, for an unresolved licensing or other substantive problem, stays out of the
catalog or is removed from it, and the pull request that declines or removes it states why
([Published versions](../CONTRIBUTING.md#published-versions)).

A version is uploaded when the log has no `uploaded` line for it and every version its declared
requirements match is uploaded or uploads in the same run.  A
version with an `uploaded` line is never uploaded again, whatever its recipe now stages, so a
rejected upload is never revived and a correction ships only as an `all` dispatch.  Without
`--new-revision`, the registry refuses different bytes for a version it already has (`409`) and
answers identical bytes as a no-op, but once every revision of a version is rejected it revives
identical bytes and accepts different ones.  So a version whose upload may have landed without its
receipt is never resubmitted unattended ([Failures](#failures) and [The lock](#the-lock)).

### What it builds

Only the candidates and the versions their requirements match stage.  The `prepare` job builds
them without running a probe, and the `publish` job stages them again and builds nothing
([Execution boundaries](#execution-boundaries)); both run under the lock, so they read the same
log.  An uploaded requirement must stage exactly the
bytes of its last receipt; otherwise the candidates that need it wait for its packaging revision.
A run in which every version has an upload receipt or a `failed` line, or requires a version
with a `failed` line, stops after the `build` job, before taking the lock; a
`new` run whose candidates all wait on a requirement's packaging revision still takes the lock and
stages them to find that out.

### Failures

A failed upload fails the run, after the receipts of the uploads before it and a `failed` line for
the version it stopped at.  That upload may have landed with its response lost, so the version, and
what requires it, waits for a dispatch with `retry-failed`, which an operator starts only after
checking the registry's admin listings: a version with a rejected revision gets its `uploaded`
line instead ([Reconciling the log](#reconciling-the-log)), since resubmitting would revive it,
and a `409` means the registry has other bytes for it, which need a packaging revision.

A port that does not build fails the `prepare` job.  Nothing was uploaded, so the `publish` job is
skipped and the `record` job releases the lock; every later run fails the same way, uploading
nothing, until the port is fixed or removed from the catalog.

### The lock

Every run that may upload, in either mode, holds the log's lock: a `lock` file naming the run and
attempt, which the `lock` job commits on top of the branch tip it read, so of two runs racing for it
one push fails and that run uploads nothing.  A push refused while the tip did not move fails the
run, whatever started it.  A run that finds the lock held by a running run
uploads nothing: a scheduled one leaves it to a later run, a dispatch fails.  Dispatch again
rather than rerun a run whose `lock` job failed: the rerun reuses that run's tools, which are kept
one day, and once they are gone its `publish` job fails holding the lock.  The `publish` job
uploads only if the lock names its own attempt, so rerunning a run's jobs never uploads under an
earlier attempt's lock.  The `record` job appends the receipts of the lock's attempt only while the
run holds the lock, so an old run's jobs cannot replay them, and releases it only when that
attempt's `publish` job succeeded and left its receipts, or was skipped.  After a failure or a
cancellation an upload may have landed without its receipt, so the lock stays, and every later run
of either mode fails until an operator reconciles the log
([Reconciling the log](#reconciling-the-log)).

### Freshness

A scheduled run uploads its own commit, which must have passed the merge queue's `required` check.
Under the lock it re-reads the log and compares its commit with `main`: a candidate whose ports or
requirements main has since changed waits for a later run, and every candidate does when
main changed any other file outside `ports/` except the documentation files `README.md`,
`AGENTS.md`, `CONTRIBUTING.md`, `docs/maintaining.md`, and `docs/publishing.md`.  Unrelated port
merges never stop it.  This is a last check, not a transaction with later merges.

### Dependents and verification

The registry accepts a dependent before what it requires is verified, but a consumer resolves it
only once all of that is.  The plan lists the required versions not recorded as verified.
Verification is the cabinpkg/cabin **Registry Verify** workflow's, which runs every 5 minutes;
nothing here renders or waits for a verdict.

### Schedule

The schedule (`17,47 * * * *`) runs `new` only while the repository variables `PORTS_AUTO_PUBLISH`
and `PORTS_PUBLISH_ENABLED` are both exactly `true`.  `PORTS_AUTO_PUBLISH` unset, empty, or
`false` starts no job; any other combination, such as `TRUE` or an unset `PORTS_PUBLISH_ENABLED`,
starts only the `gate` job, which stops before anything is built.  A scheduled run that is late or
skipped loses nothing, because each run starts from the log, not from the previous run.

### Enabling and disabling

Every upload, dispatched or scheduled, requires `PORTS_PUBLISH_ENABLED` to be exactly `true`:
while it is not, an `all` or `new` dispatch fails before anything is built, and the `publish` job
checks it again right before uploading.  It stays unset until the maintainer authorizes publishing
([Recipe distribution](#recipe-distribution)).

- **Enable**, only then: initialize the log (below), review a `plan` dispatch, set
  `PORTS_PUBLISH_ENABLED` to `true`, and set `PORTS_AUTO_PUBLISH` to `true` for the schedule
  (repository variables; needs admin).  Once both are on, a merged port is ready to publish and
  publishes by default.
- **Disable**: set `PORTS_PUBLISH_ENABLED`, or `PORTS_AUTO_PUBLISH` for the schedule alone, to
  anything else or delete it.  A run whose `publish` job has not started then uploads nothing;
  without `PORTS_PUBLISH_ENABLED`, such a dispatch fails before uploading and keeps the lock
  ([Reconciling the log](#reconciling-the-log)).  A `publish` job reads the variables as they
  were when it started, so a started one finishes its batch unless the run is cancelled (which
  keeps the lock), and everything a stopped run uploaded stays in the registry.  Turning either
  off never retracts an accepted package.

### Initializing the log

Initialize the log while no run is publishing: create the `publish-receipts` branch holding
`receipts.jsonl`, seeded with an `uploaded` line for every revision the registry already holds,
from the operator's `GET /api/v1/admin/versions?status=` listings (`pending`, `verified`, and
`rejected`), so the first run neither resubmits nor revives one.  The log and the lock are trusted
at write-access level: protect the branch from deletion and force pushes, and restrict
who may push it.  `new` fails without the branch; `all` then records nothing, and an `all` run that
started before the branch existed uploads nothing once it does.

### Registry wipe

Stop both modes first, archive the log, and start a new one for the new registry; an old log would
keep an empty registry looking populated.  Nothing here wipes, yanks, or recreates the catalog by
itself.

### Reconciling the log

After a run that kept the lock (its `record` job says why): if only its push failed, rerun that job
alone.  Otherwise compare the run's `receipts-*` artifacts (kept 30 days) and its log with the admin
listings, append an `uploaded` line for each upload that landed without one, a rejected one
included, and delete `lock` in the same commit; then dispatch again.
Rerunning the run's other jobs uploads nothing, and a rerun `publish` job leaves the `record` job
nothing to record: it takes the receipts of the latest `publish` job only.  A `plan` dispatch, or
`port-publish --plan-new --index-url https://registry.cabinpkg.com --receipts receipts.jsonl --cabin <cabin>`
locally, shows what the next run would upload.
