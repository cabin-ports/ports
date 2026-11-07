#!/usr/bin/env bash
# Usage: receipts.sh probe | read FILE | acquire | held FILE | record DIR
#
# The receipts log's protocol (docs/publishing.md, "Automatic
# publishing"): `receipts.jsonl` and the `lock` file, `<run id> <run
# attempt>`, on the $RECEIPTS_BRANCH branch of
# $GITHUB_SERVER_URL/$GITHUB_REPOSITORY.  Each command works in a new
# repository under $RUNNER_TEMP.  `acquire` and `record` send GH_TOKEN
# with each fetch and push, and push only that branch, fast-forward.
#
#   probe       (gate) Appends `receipts=true` to $GITHUB_OUTPUT when the
#               branch exists.  Without it, an `all` run (MODE) notes
#               that its receipts go unrecorded; any other run fails.
#   read FILE   Writes the branch tip's log to FILE.  `read` and `held`
#               read Git's objects, never the checkout, so a symlink on
#               the branch cannot make them read a runner's file.
#   acquire     (lock) Commits this attempt's lock on the tip and appends
#               `locked=true` and `go=true`, or only `go=true` for a
#               `plan` run or a run without a log (RECEIPTS).  A running
#               run's lock, or a lost race, skips a scheduled run and
#               fails a dispatch; any other lock fails the run.
#   held FILE   (publish) Fails unless the tip's lock names this attempt,
#               then writes that tip's log, if any, to FILE.  Without the
#               branch, passes only a run that found no log (RECEIPTS).
#   record DIR  (record) Appends DIR/receipts-<the lock's attempt>.jsonl,
#               stamped with this run, to the log, releasing the lock in
#               the same commit when that attempt's publish job (PUBLISH,
#               PUBLISHED_IN) succeeded and its receipts downloaded
#               (DOWNLOAD), or that attempt skipped it.  Otherwise the
#               lock stays and the command fails.
set -euo pipefail

remote="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY"
runs="$remote/actions/runs"
header=

# Git in the state repository, sending the token once `authenticate`
# has run.
state_git() {
  if [[ -n $header ]]; then
    git -C "$state" -c "http.extraheader=$header" "$@"
  else
    git -C "$state" "$@"
  fi
}

authenticate() {
  header="AUTHORIZATION: basic $(printf 'x-access-token:%s' "$GH_TOKEN" | base64 | tr -d '\n')"
  echo "::add-mask::$header"
}

# Sets `found` as `git ls-remote --exit-code` exits: 2 when the branch
# does not exist, and anything else but 0 when Git cannot tell.
find_branch() {
  found=0
  git ls-remote --exit-code "$remote" "refs/heads/$RECEIPTS_BRANCH" >/dev/null || found=$?
}

# Checks the branch tip out into a new state repository.
fetch_tip() {
  state="$(mktemp -d "$RUNNER_TEMP/receipts.XXXXXX")"
  git -C "$state" init -q
  git -C "$state" remote add origin "$remote"
  state_git fetch -q --no-tags --depth=1 origin "refs/heads/$RECEIPTS_BRANCH"
  git -C "$state" checkout -q FETCH_HEAD
  tip="$(git -C "$state" rev-parse HEAD)"
}

commit() {
  git -C "$state" -c user.name='github-actions[bot]' \
    -c user.email='41898282+github-actions[bot]@users.noreply.github.com' \
    commit -q -m "$1"
}

push() {
  state_git push -q origin "HEAD:refs/heads/$RECEIPTS_BRANCH"
}

# A later scheduled run resumes a skipped one; a dispatch must say that
# it did nothing.
skip() {
  if [[ $GITHUB_EVENT_NAME == schedule ]]; then
    echo "::notice::$1; this run uploads nothing and a later one resumes"
    exit 0
  fi
  echo "::error::$1; this run uploads nothing, dispatch again later"
  exit 1
}

probe() {
  find_branch
  if [[ $found == 2 && $MODE == all ]]; then
    echo "::notice::no $RECEIPTS_BRANCH branch: this run's receipts are not recorded"
  elif [[ $found != 0 ]]; then
    echo "::error::cannot read the $RECEIPTS_BRANCH branch; is the receipts log initialized (docs/publishing.md, \"Automatic publishing\")?"
    exit 1
  else
    echo "receipts=true" >>"$GITHUB_OUTPUT"
  fi
}

read_log() {
  fetch_tip
  git -C "$state" show "$tip:receipts.jsonl" >"$1"
}

acquire() {
  if [[ $MODE == plan || $RECEIPTS != true ]]; then
    echo "go=true" >>"$GITHUB_OUTPUT"
    return
  fi
  authenticate
  fetch_tip
  if [[ -e $state/lock ]]; then
    read -r holder attempt <"$state/lock"
    # An earlier attempt of this run kept it: never inherited.
    if [[ $holder != "$GITHUB_RUN_ID" ]]; then
      status="$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$holder" --jq .status)"
      if [[ $status != completed ]]; then
        skip "$runs/$holder is publishing"
      fi
    fi
    echo "::error::$runs/$holder (attempt $attempt) ended without recording all its uploads; reconcile the receipts log, then delete its lock (docs/publishing.md, \"Automatic publishing\")"
    exit 1
  fi
  echo "$GITHUB_RUN_ID $GITHUB_RUN_ATTEMPT" >"$state/lock"
  git -C "$state" add lock
  commit "Lock for $runs/$GITHUB_RUN_ID"
  if ! push; then
    # Only a lost race is a skip: a push refused while the tip stayed
    # put would otherwise skip every scheduled run, uploading nothing.
    state_git fetch -q --no-tags --depth=1 origin "refs/heads/$RECEIPTS_BRANCH"
    now="$(git -C "$state" rev-parse FETCH_HEAD)"
    if [[ $now == "$tip" ]]; then
      echo "::error::the $RECEIPTS_BRANCH branch refused the lock but did not move; this run uploads nothing"
      exit 1
    fi
    skip "the $RECEIPTS_BRANCH branch moved"
  fi
  echo "locked=true" >>"$GITHUB_OUTPUT"
  echo "go=true" >>"$GITHUB_OUTPUT"
}

held() {
  find_branch
  if [[ $found == 2 && $RECEIPTS != true ]]; then
    return
  elif [[ $found != 0 ]]; then
    echo "::error::cannot read the $RECEIPTS_BRANCH branch; uploading nothing"
    exit 1
  fi
  fetch_tip
  lock=
  if git -C "$state" cat-file -e "$tip:lock" 2>/dev/null; then
    lock="$(git -C "$state" show "$tip:lock")"
  fi
  if [[ $lock != "$GITHUB_RUN_ID $GITHUB_RUN_ATTEMPT" ]]; then
    echo "::error::this attempt does not hold the $RECEIPTS_BRANCH lock; uploading nothing (docs/publishing.md, \"Automatic publishing\")"
    exit 1
  fi
  # Only --publish-new reads the log, and fails without one.
  if git -C "$state" cat-file -e "$tip:receipts.jsonl" 2>/dev/null; then
    git -C "$state" show "$tip:receipts.jsonl" >"$1"
  fi
}

record() {
  authenticate
  fetch_tip
  holder='' attempt=''
  if [[ -e $state/lock ]]; then
    read -r holder attempt <"$state/lock"
  fi
  if [[ $holder != "$GITHUB_RUN_ID" ]]; then
    echo "::error::this run does not hold the $RECEIPTS_BRANCH lock; recording nothing"
    exit 1
  fi
  artifact="$1/receipts-$attempt.jsonl"
  if [[ -f $artifact ]]; then
    jq -c --arg source "$GITHUB_SHA" --arg cabin "$CABIN_REV" --arg run "$runs/$GITHUB_RUN_ID" \
      '. + {source: $source, cabin: $cabin, run: $run}' "$artifact" >>"$state/receipts.jsonl"
  fi
  released=false
  if [[ ($PUBLISH == success && $PUBLISHED_IN == "$attempt" && $DOWNLOAD == success && -f $artifact) ||
    ($PUBLISH == skipped && $GITHUB_RUN_ATTEMPT == "$attempt") ]]; then
    git -C "$state" rm -q lock
    released=true
  fi
  git -C "$state" add receipts.jsonl
  if ! git -C "$state" diff --cached --quiet; then
    commit "Record the receipts of $runs/$GITHUB_RUN_ID (attempt $attempt)"
    push
  fi
  if [[ $released != true ]]; then
    echo "::error::the publish job under the lock of attempt $attempt ended '$PUBLISH', or its receipts are missing, so an upload may lack its receipt: the lock stays until the log is reconciled (docs/publishing.md, \"Automatic publishing\")"
    exit 1
  fi
}

case "${1:-}:$#" in
probe:1) probe ;;
read:2) read_log "$2" ;;
acquire:1) acquire ;;
held:2) held "$2" ;;
record:2) record "$2" ;;
*)
  echo "usage: $0 probe | read FILE | acquire | held FILE | record DIR" >&2
  exit 2
  ;;
esac
