#!/usr/bin/env bash
# Usage: publish-mode.sh
#
# Appends the publish run's mode to $GITHUB_OUTPUT.  A scheduled run is
# `new` while ENABLED (the PORTS_AUTO_PUBLISH variable) is exactly
# `true`, and `off` otherwise: workflow expressions compare
# case-insensitively, so only this checks the exact bytes.  Any other
# event's mode is INPUT, which must be `all`, `new`, or `plan`.  `all`
# and `new` upload only while PUBLISH (the PORTS_PUBLISH_ENABLED
# variable) is exactly `true`: otherwise a scheduled run is `off`, and
# any other run fails.
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
all | new | plan) ;;
*)
  echo "::error::unknown mode '$mode'"
  exit 1
  ;;
esac
if [[ $mode != plan && ${PUBLISH:-} != true ]]; then
  if [[ $GITHUB_EVENT_NAME == schedule ]]; then
    echo "::notice::PORTS_PUBLISH_ENABLED is not exactly 'true'; publishing is off"
    echo "mode=off" >>"$GITHUB_OUTPUT"
    exit 0
  fi
  echo "::error::PORTS_PUBLISH_ENABLED is not exactly 'true'; uploading nothing (docs/publishing.md, \"Enabling and disabling\")"
  exit 1
fi
echo "mode=$mode" >>"$GITHUB_OUTPUT"
