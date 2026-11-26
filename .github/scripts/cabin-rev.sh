#!/usr/bin/env bash
# Usage: cabin-rev.sh [--on-main]
#
# Prints the cabinpkg/cabin commit that ./Cargo.lock pins, and appends
# `cabin-rev=<commit>` to $GITHUB_OUTPUT when that is set.  Fails unless
# every cabinpkg-* package and every git source is locked to one full
# commit of cabinpkg/cabin.  With --on-main, also fails unless that
# commit is on cabinpkg/cabin's main (through `gh api`).
set -euo pipefail

on_main=false
if [[ $# -eq 1 && $1 == --on-main ]]; then
  on_main=true
elif [[ $# -ne 0 ]]; then
  echo "usage: $0 [--on-main]" >&2
  exit 2
fi

# A Windows checkout may have converted Cargo.lock to CRLF.
sources="$(awk '
  { sub(/\r$/, "") }
  /^\[\[package\]\]/ { if (keep) print src; keep = 0; src = "none" }
  /^name = "cabinpkg-/ { keep = 1 }
  /^source = / { src = $3; if (src ~ /^"git\+/) keep = 1 }
  END { if (keep) print src }
' Cargo.lock | sort -u)"
re='^"git\+https://github\.com/cabinpkg/cabin\?rev=([0-9a-f]{40})#([0-9a-f]{40})"$'
if [[ ! $sources =~ $re || ${BASH_REMATCH[1]} != "${BASH_REMATCH[2]}" ]]; then
  echo "Cabin sources: ${sources:-none}" >&2
  echo "::error::Cargo.lock must lock every cabinpkg-* package to one full cabinpkg/cabin commit" >&2
  exit 1
fi
rev="${BASH_REMATCH[1]}"

if [[ $on_main == true ]]; then
  # `rev...main` is `ahead` or `identical` exactly when rev is an
  # ancestor of main.
  status="$(gh api "repos/cabinpkg/cabin/compare/$rev...main" --jq .status)"
  if [[ $status != identical && $status != ahead ]]; then
    echo "::error::$rev is not on cabinpkg/cabin's main (compare: $status)" >&2
    exit 1
  fi
fi

echo "$rev"
if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  echo "cabin-rev=$rev" >>"$GITHUB_OUTPUT"
fi
