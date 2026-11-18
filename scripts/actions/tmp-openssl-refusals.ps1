# Usage: tmp-openssl-refusals.ps1 <cabin>
#
# TEMPORARY, removed before merge.  The Windows cases of
# tmp-openssl-refusals.sh, which made the two directories, in the
# dry-run step's own shell.  Every case runs; the script fails at the
# end if a dry-run passed.
param([Parameter(Mandatory)][string]$Cabin)

$work = Join-Path $env:RUNNER_TEMP 'openssl-refusals'
$tests = 'ports\op\openssl\4.1.0\tests'
$sdk = $env:CABIN_SDK_OPENSSL_DIR
$path = $env:PATH
$failed = $false
Remove-Item Env:PORT_PUBLISH_CHANGED_SINCE -ErrorAction SilentlyContinue

# The port's dry-run must fail.
function Refused([string]$Name) {
    Write-Host "::group::refused: $Name"
    cargo run --locked -- --dry-run --port openssl --cabin $Cabin
    if ($LASTEXITCODE -eq 0) {
        Write-Host "::error::${Name}: the dry-run passed"
        $script:failed = $true
    }
    Write-Host '::endgroup::'
}

$env:CABIN_SDK_OPENSSL_DIR = Join-Path $work 'empty'
Refused 'an SDK directory without OpenSSL'
$env:CABIN_SDK_OPENSSL_DIR = Join-Path $work 'wrong'
Refused 'libraries of another architecture'
Remove-Item Env:CABIN_SDK_OPENSSL_DIR
Refused 'an MSVC build without CABIN_SDK_OPENSSL_DIR'
$env:CABIN_SDK_OPENSSL_DIR = $sdk

# Every copy on PATH, in order; then the image's own order, without the
# step that puts OpenSSL's bin first.
where.exe libcrypto-3-x64.dll
$first = 'C:\Program Files\OpenSSL\bin;'
$env:PATH = $path.Remove($path.IndexOf($first), $first.Length)
where.exe libcrypto-3-x64.dll
Refused "another program's DLL first on PATH"
$env:PATH = $path

Copy-Item -Recurse 'scripts\actions\tmp-openssl\vendored-on-windows' $tests
Refused 'the feature vendored on Windows'
Remove-Item -Recurse (Join-Path $tests 'vendored-on-windows')

if ($failed) { exit 1 }
exit 0
