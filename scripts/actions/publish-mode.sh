#!/usr/bin/env bash
# Usage: publish-mode.sh
#
# Appends the publish run's mode to $GITHUB_OUTPUT.  A scheduled run is
# `new` while ENABLED (the PORTS_AUTO_PUBLISH variable) is exactly
# `true`, and `off` otherwise: workflow expressions compare
# case-insensitively, so only this checks the exact bytes.  Any other
# event's mode is INPUT, which must be `all`, `new`, or `plan`.
set -euo pipefail

if [[ $GITHUB_EVENT_NAME == schedule ]]; then
  if [[ ${ENABLED:-} != true ]]; then
    echo "::notice::PORTS_AUTO_PUBLISH is not exactly 'true'; automatic publishing is off"
    echo "mode=off" >>"$GITHUB_OUTPUT"
    exit 0
  fi
  mode=new
else
  mode="${INPUT:-}"
fi
case "$mode" in
all | new | plan) echo "mode=$mode" >>"$GITHUB_OUTPUT" ;;
*)
  echo "::error::unknown mode '$mode'"
  exit 1
  ;;
esac
