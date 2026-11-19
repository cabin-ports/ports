#!/usr/bin/env bash
# Temporary CI probe (removed before merge): which OpenCL implementation can
# run a kernel on this runner.
set -uo pipefail
src="scripts/actions/tmp-opencl-probe.c"
out="$RUNNER_TEMP/opencl-probe"
case "$RUNNER_OS" in
Linux)
  sudo apt-get install --yes --no-install-recommends ocl-icd-opencl-dev clinfo
  ls -l /etc/OpenCL/vendors
  clinfo -l
  gcc -o "$out" "$src" -lOpenCL
  "$out"
  ;;
macOS)
  sw_vers
  sysctl -n machdep.cpu.brand_string
  cc -o "$out" "$src" -framework OpenCL
  "$out"
  ;;
Windows)
  ls -l /c/Windows/System32/OpenCL.dll
  reg query 'HKLM\SOFTWARE\Khronos\OpenCL\Vendors'
  reg query 'HKLM\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors'
  ;;
esac
echo "probe exit $?"
