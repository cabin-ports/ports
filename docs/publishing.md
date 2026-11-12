# Publishing

Only `main` publishes, through the **Publish** workflow (`.github/workflows/publish.yml`), and
only to `https://registry.cabinpkg.com`.  A run elsewhere, or on another ref, publishes nothing.
It has two upload modes:

- **`all`** (a manual dispatch; the default): every port, with changed bytes respun as packaging
  revisions.  This is the deliberate full publish and the only way to correct a published version.
- **`new`** (the schedule, or a manual dispatch): only the versions the registry has no upload
  receipt for, never a packaging revision ([Automatic publishing](#automatic-publishing)).

A third dispatch mode, **`plan`**, previews `new` without uploading anything.  Merging a pull
request publishes nothing until automatic publishing is switched on.

A published version is immutable, so a correction to a published port reaches the registry only
as a [packaging revision](https://cabinpkg.com/docs/foundation-ports/#packaging-revisions) of the
same upstream version, which only an `all` dispatch publishes.

## Execution boundaries

- **Credential boundary.**  The `build` job installs the pinned `cabin` and builds `port-publish`
  without any credential.  Only the `publish` job may request the run's OIDC token for a publish,
  and it runs just those two prebuilt binaries and the workflow scripts under `scripts/actions/`:
  it builds the probes of the ports it stages but never runs one, because running upstream code
  next to the token would hand that code the token.
  Running the consumer tests is the pull-request and merge-queue dry-run's job: no job of this
  workflow runs port code, the `plan` preview included.  (The `exchange-check` probe below also
  requests the token, but runs only `curl`.)  The `lock` and `record` jobs alone may push, only to
  the receipts branch, and run only `scripts/actions/receipts.sh` (`git`, `gh`, and `jq`), checked
  out at the workflow's own commit.
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

### Eligibility and holds

A version is uploaded when the log has no `uploaded` line for it, the reviewed hold list
`publish-holds.txt` (`<name> <version> <reason>` per line, validated by every mode) does not name
it, and every version its declared requirements match is uploaded or uploads in the same run.  A
version with an `uploaded` line is never uploaded again, whatever its recipe now stages, so a
rejected upload is never revived and a correction ships only as an `all` dispatch.  Without
`--new-revision`, the registry refuses different bytes for a version it already has (`409`) and
answers identical bytes as a no-op, but once every revision of a version is rejected it revives
identical bytes and accepts different ones.  So a version whose upload may have landed without its
receipt is never resubmitted unattended ([Failures](#failures) and [The lock](#the-lock)).  Holds
bind `new` only; a version that no mode may upload is removed from the catalog instead
([Held ports](held-ports.md)).

### What it builds

Only the candidates and the versions their requirements match stage, and build without running a
probe.  An uploaded requirement must stage exactly the bytes of its last receipt; otherwise the
candidates that need it wait for its packaging revision.  A run in which every version has an
upload receipt, a `failed` line, or a hold, or requires a version with a `failed` line or a hold,
stops after the `build` job, before taking the lock; a `new` run whose candidates all wait on a
requirement's packaging revision still takes the lock and stages them to find that out.

### Failures

A failed upload fails the run, after the receipts of the uploads before it and a `failed` line for
the version it stopped at.  That upload may have landed with its response lost, so the version, and
what requires it, waits for a dispatch with `retry-failed`, which an operator starts only after
checking the registry's admin listings: a version with a rejected revision gets a hold instead,
since resubmitting would revive it, and a `409` means the registry has other bytes for it, which
need a packaging revision.

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
Under the lock it re-reads the log and compares its commit with `main`: a candidate whose ports,
requirements, or hold main has since changed waits for a later run, and every candidate does when
main changed any other file outside `ports/` except the documentation files `README.md`,
`AGENTS.md`, `CONTRIBUTING.md`, `docs/maintaining.md`, and `docs/publishing.md`.  Unrelated port
merges never stop it.  This is a last check, not a transaction with later merges.

### Dependents and verification

The registry accepts a dependent before what it requires is verified, but a consumer resolves it
only once all of that is.  The plan lists the required versions not recorded as verified.
Verification is the cabinpkg/cabin **Registry Verify** workflow's, which runs every 5 minutes;
nothing here renders or waits for a verdict.

### Schedule

The schedule (`17,47 * * * *`) runs `new` only while the repository variable `PORTS_AUTO_PUBLISH`
is exactly `true`.  Unset, empty, or `false` starts no job; another spelling such as `TRUE` starts
only the `gate` job, which stops before anything is built.  A scheduled run that is late or skipped
loses nothing, because each run starts from the log, not from the previous run.

### Enabling and disabling

- **Enable**, with the operator's approval: initialize the log (below), review a `plan` dispatch,
  then set `PORTS_AUTO_PUBLISH` to `true` (repository variables; needs admin).  Once it is on, a
  merged port is distribution-ready and publishes by default.  A hold
  ([CONTRIBUTING.md](../CONTRIBUTING.md#pull-requests)) lands in the port's own pull request (a
  hold must name a committed version, and a later pull request can lose the race with a scheduled
  run).
- **Disable**: set it to anything else or delete it.  Queued runs then upload nothing; a running
  upload finishes its batch unless the run is cancelled (which keeps the lock), and everything a
  stopped run uploaded stays in the registry.  Turning it off never retracts an accepted package.

### Initializing the log

Initialize the log while no run is publishing: create the `publish-receipts` branch holding
`receipts.jsonl`, seeded with an `uploaded` line for every revision the registry already holds,
from the operator's `GET /api/v1/admin/versions?status=` listings (`pending`, `verified`, and
`rejected`), so the first run neither resubmits nor revives one.  The log, the lock, and the holds
are trusted at write-access level: protect the branch from deletion and force pushes, and restrict
who may push it.  `new` fails without the branch; `all` then records nothing, and an `all` run that
started before the branch existed uploads nothing once it does.

### Registry wipe

Stop both modes first, archive the log, and start a new one for the new registry; an old log would
keep an empty registry looking populated.  Nothing here wipes, yanks, or recreates the catalog by
itself.

### Reconciling the log

After a run that kept the lock (its `record` job says why): if only its push failed, rerun that job
alone.  Otherwise compare the run's `receipts-*` artifacts (kept 30 days) and its log with the admin
listings, append an `uploaded` line for each upload that landed without one, add a hold for any
version with a rejected revision, and delete `lock` in the same commit; then dispatch again.
Rerunning the run's other jobs uploads nothing.  A `plan` dispatch, or
`port-publish --plan-new --index-url https://registry.cabinpkg.com --receipts receipts.jsonl --cabin <cabin>`
locally, shows what the next run would upload.
