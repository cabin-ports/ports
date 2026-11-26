#!/usr/bin/env bash
# Usage: require-needs.sh
#
# Fails unless every job in NEEDS (the `needs` context as JSON) succeeded,
# judged twice: by its `needs` result, and by the conclusion of the job of
# that name in this run attempt (through `gh api`).  A matrix's `needs`
# result has not shown a leg cancelled before any runner picked it up,
# while the run's job list said `cancelled`.
set -euo pipefail

# Read once: a needed job listed without its conclusion fails the check.
# An attempt also lists the jobs a re-run of the failed ones kept.
jobs="$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/attempts/$GITHUB_RUN_ATTEMPT/jobs?per_page=100")"

# One row per needed job: its `needs` result and the conclusion of every
# job of that name in the run.
rows="$(jq -cn --argjson needs "$NEEDS" --argjson run "$jobs" '
  [$needs | objects | to_entries[] | .key as $job |
    {job: $job, needs: .value.result,
      conclusions: [$run.jobs[] | select(.name == $job) | .conclusion]}]')"
jq -cn --argjson rows "$rows" '$rows[]'

# `jq -e` exits 0 only when this is `true`, so a job list cut short by
# paging, no row, a row that is not all `success`, and a failed `jq` each
# fail the check.
if ! jq -en --argjson rows "$rows" --argjson run "$jobs" '
  ($run.jobs | length) == $run.total_count and ($rows | length) > 0 and
  all($rows[]; .needs == "success" and .conclusions == ["success"])' >/dev/null; then
  echo "::error::not every needed job succeeded"
  exit 1
fi
