#!/usr/bin/env bash
# Usage: require-ci.sh
#
# Fails unless GITHUB_SHA has a successful `required` check run (through
# `gh api`): an unattended upload needs the merge queue's evidence for
# exactly this commit, since main points at the merge group commit CI
# tested.
set -euo pipefail

passed="$(gh api "repos/$GITHUB_REPOSITORY/commits/$GITHUB_SHA/check-runs?check_name=required" \
  --jq '[.check_runs[] | select(.conclusion == "success")] | length')"
if [[ ! $passed =~ ^[1-9][0-9]*$ ]]; then
  echo "::error::$GITHUB_SHA has no successful 'required' check run; uploading nothing"
  exit 1
fi
