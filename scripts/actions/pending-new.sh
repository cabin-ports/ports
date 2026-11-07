#!/usr/bin/env bash
# Usage: pending-new.sh TOOLS LOG
#
# Prints the versions `port-publish --publish-new` would upload to
# REGISTRY before staging, given its receipts log LOG and the ports in
# ./ports, and appends `work=true` to $GITHUB_OUTPUT when there is one,
# `work=false` otherwise.  TOOLS holds the `build` job's `port-publish`
# and `cabin`; `--list-new` runs no `cabin`.  PORT_PUBLISH_RETRY_FAILED
# reaches `port-publish`.
set -euo pipefail

list="$(mktemp "$RUNNER_TEMP/pending.XXXXXX")"
"$1/port-publish" --list-new "$list" \
  --index-url "$REGISTRY" \
  --receipts "$2" \
  --ports-dir ports \
  --cabin "$1/cabin"
if [[ -s $list ]]; then
  echo "to upload, in order:"
  cat "$list"
  echo "work=true" >>"$GITHUB_OUTPUT"
else
  echo "nothing to upload; nothing to do"
  echo "work=false" >>"$GITHUB_OUTPUT"
fi
