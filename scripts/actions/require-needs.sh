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

# One line per needed job: its name, its `needs` result, then the
# conclusion of each job of that name (`none` while it has none).
report="$(jq -r --argjson run "$jobs" '
  to_entries[] | .key as $job |
  [$job, .value.result] + [$run.jobs[] | select(.name == $job) | .conclusion // "none"] |
  join(" ")' <<<"$NEEDS")"
echo "$report"

# An empty report is one empty line here, which fails too.
if grep -qvxE '[^ ]+ success success' <<<"$report"; then
  echo "::error::not every needed job succeeded"
  exit 1
fi
