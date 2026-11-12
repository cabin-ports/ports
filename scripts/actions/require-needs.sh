#!/usr/bin/env bash
# Usage: require-needs.sh
#
# Fails unless every job in NEEDS (the `needs` context as JSON) succeeded,
# judged twice: by its `needs` result, and by the conclusion of the job of
# that name in this run attempt (through `gh api`).  A `needs` result has
# read `success` for a matrix whose leg was cancelled before any runner
# picked it up, while the run's job list said `cancelled`.
set -euo pipefail

# shortcut: one read of the job list, add a bounded wait if a finished
# job ever lists without its conclusion.
jobs="$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/attempts/$GITHUB_RUN_ATTEMPT/jobs?per_page=100")"

# One row per needed job: its `needs` result and the conclusion of every
# job of that name in the run.
rows="$(jq -c --argjson run "$jobs" '
  [to_entries[] | .key as $job |
    {job: $job, needs: .value.result,
      conclusions: [$run.jobs[] | select(.name == $job) | .conclusion]}]' <<<"$NEEDS")"
jq -c '.[]' <<<"$rows"

# `jq -e` exits 0 only when this is `true`, so no row, a row that is not
# all `success`, and a failed `jq` each fail the check.
if ! jq -e 'length > 0 and all(.[]; .needs == "success" and .conclusions == ["success"])' <<<"$rows" >/dev/null; then
  echo "::error::not every needed job succeeded"
  exit 1
fi
