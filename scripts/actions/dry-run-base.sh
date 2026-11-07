#!/usr/bin/env bash
# Usage: dry-run-base.sh
#
# Picks the dry-run's comparison base for the checked-out commit and
# appends `PORT_PUBLISH_CHANGED_SINCE=<base>` to $GITHUB_ENV: on
# pull_request, the test merge's first parent once its second parent is
# PR_HEAD; on merge_group, GROUP_BASE once HEAD is GROUP_HEAD.  Anything
# else, including a commit Git cannot resolve, writes nothing and warns,
# so the dry-run checks every port.
set -euo pipefail

base=
case "$GITHUB_EVENT_NAME" in
pull_request)
  if head="$(git rev-parse --verify --quiet 'HEAD^2')" && [[ $head == "${PR_HEAD:-}" ]]; then
    base="$(git rev-parse 'HEAD^1')"
  fi
  ;;
merge_group)
  if head="$(git rev-parse --verify --quiet HEAD)" && [[ $head == "${GROUP_HEAD:-}" ]]; then
    base="${GROUP_BASE:-}"
  fi
  ;;
esac

if [[ -n $base ]]; then
  echo "PORT_PUBLISH_CHANGED_SINCE=$base" >>"$GITHUB_ENV"
else
  echo "::warning::HEAD does not match the $GITHUB_EVENT_NAME event; the dry-run checks every port"
fi
