#!/usr/bin/env bash
# Usage: check-inline-runs.sh
#
# Fails when a `run:` step of .github/workflows/*.y*ml, with its `${{ }}`
# expressions and comment lines removed, holds a shell keyword (`if`,
# `case`, `for`, `while`, `until`, `select`, `do`, `function`, or
# PowerShell's `foreach`, `switch`, `try`), a function definition, `&&`,
# `||`, a pipeline, a heredoc or here-string, command or process
# substitution, `eval`, or `bash -c`, `sh -c`, or `python -c`, unless its
# first line is `# inline: <reason>`.  Tokens decide, never line counts.
# Quotes are not parsed, so a `|` inside a quoted argument counts too.
# Runs from the repository root and needs Go, like ci.yml's shfmt and
# actionlint.
set -euo pipefail

# The `$` names belong to the yq expression.
# shellcheck disable=SC2016
hits="$(go run github.com/mikefarah/yq/v4@v4.54.1 --no-doc '
  explode(.) | filename as $file | .jobs | to_entries | .[] | .key as $job |
  (.value.steps // [])[] | select(has("run")) | . as $step |
  select(.run | test("^# inline: +\\S") | not) |
  (.run | sub("(?s)\\$\\{\\{.*?\\}\\}"; "") | sub("(?m)^\\s*#.*$"; "")) as $sh |
  (["if", "case", "for", "while", "until", "select", "do", "function",
    "foreach", "switch", "try"] |
    map([., "(?m)(^|[;&|({!])\\s*" + . + "\\b"])) +
  [["() {", "\\(\\)\\s*[{(]"], ["&&", "&&"], ["||", "\\|\\|"],
    ["|", "(^|[^|])\\|([^|]|$)"], ["<<", "<<"], ["@\"", "(?m)(^|[^$])@[\"\\x27]$"],
    ["$(", "\\$\\(([^(]|$)"], ["`", "`"], ["<(", "[<>]\\("], ["eval", "\\beval\\b"],
    ["sh -c", "(^|[^.])\\b(ba)?sh([ \\t]+\\S+)*?[ \\t]+-[a-zA-Z]*c[a-zA-Z]*\\b"],
    ["python -c", "\\bpython[0-9.]*([ \\t]+-\\S+)*?[ \\t]+-[a-zA-Z]*c[a-zA-Z]*\\b"]] |
  map(select(.[1] as $re | $sh | test($re)) | "\"" + .[0] + "\"") | join(", ") |
  select(. != "") |
  $file + ":" + ($step.run | line | tostring) + " job " + $job + " step \"" +
    ($step.name // $step.id // "") + "\": inline control flow (" + . + ")"
' .github/workflows/*.y*ml)"

if [[ -n $hits ]]; then
  echo "$hits" >&2
  echo "Move policy into port-publish (src/) or glue into scripts/actions/, or justify the block with a first line '# inline: <reason>' (AGENTS.md \"GitHub Actions maintainability\")." >&2
  exit 1
fi
