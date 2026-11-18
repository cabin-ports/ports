#!/usr/bin/env bash
# Usage: tmp-openssl-refusals.sh <cabin>
#
# TEMPORARY, removed before merge.  Dry-runs the openssl port under
# conditions it must refuse and prints what each refusal says.  Every
# case runs; the script fails at the end if a dry-run passed.
set -euo pipefail

cabin=$1
port=ports/op/openssl/4.1.0
work=$RUNNER_TEMP/openssl-refusals
status=0

# refused <name> [env arguments]: the port's dry-run must fail.
refused() {
  local name=$1
  shift
  echo "::group::refused: $name"
  if env -u PORT_PUBLISH_CHANGED_SINCE "$@" cargo run --locked -- --dry-run --port openssl --cabin "$cabin"; then
    echo "::error::$name: the dry-run passed"
    status=1
  fi
  echo "::endgroup::"
}

# An architecture the runner is not, an archiver for it, and the
# image's OpenSSL headers.
case "$RUNNER_OS" in
Linux) other=aarch64-unknown-linux-gnu ar=ar suffix=a headers=/usr/include ;;
macOS) other=x86_64-apple-macos ar=ar suffix=a headers=/opt/homebrew/opt/openssl@3/include ;;
Windows) other=aarch64-pc-windows-msvc ar=llvm-ar suffix=lib headers='/c/Program Files/OpenSSL/include' ;;
esac

mkdir -p "$work/empty" "$work/wrong/include/openssl" "$work/wrong/lib"
cp "$headers/openssl/opensslv.h" "$work/wrong/include/openssl/"
printf 'int cabin_probe;\n' >"$work/x.c"
clang --target="$other" -c "$work/x.c" -o "$work/x.o"
"$ar" rcs "$work/wrong/lib/libcrypto.$suffix" "$work/x.o"
"$ar" rcs "$work/wrong/lib/libssl.$suffix" "$work/x.o"

refused "an SDK directory without OpenSSL" CABIN_SDK_OPENSSL_DIR="$work/empty"
refused "libraries of another architecture" CABIN_SDK_OPENSSL_DIR="$work/wrong"

if [[ $RUNNER_OS == Windows ]]; then
  refused "an MSVC build without CABIN_SDK_OPENSSL_DIR" -u CABIN_SDK_OPENSSL_DIR

  # Every copy on PATH, in order; then the image's own order, without
  # the step that puts OpenSSL's bin first.
  where.exe libcrypto-3-x64.dll
  refused "another program's DLL first on PATH" PATH="${PATH/\/c\/Program Files\/OpenSSL\/bin:/}"

  cp -R scripts/actions/tmp-openssl/vendored-on-windows "$port/tests/"
  refused "the feature vendored on Windows"
  rm -r "$port/tests/vendored-on-windows"
fi

exit "$status"
