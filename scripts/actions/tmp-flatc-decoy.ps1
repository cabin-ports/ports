param([Parameter(Mandatory)][string]$Cabin)

$work = Join-Path $env:RUNNER_TEMP 'flatc-decoy'
$decoy = Join-Path $work 'decoy'
$schema = 'ports/fl/flatc/25.12.19/tests/generate/schema/monster.fbs'
New-Item -ItemType Directory -Force $decoy | Out-Null
Remove-Item Env:PORT_PUBLISH_CHANGED_SINCE -ErrorAction SilentlyContinue

function Stop-Probe([string]$why) {
    Write-Host "::error::$why"
    exit 1
}

# A `flatc` that only fails, first on PATH.
$exe = $IsWindows ? '.exe' : ''
Set-Content (Join-Path $work 'decoy.rs') 'fn main() { std::process::exit(42) }'
rustc -o (Join-Path $decoy "flatc$exe") (Join-Path $work 'decoy.rs')
if ($LASTEXITCODE -ne 0) { Stop-Probe 'the decoy did not compile' }
$env:PATH = $decoy + [IO.Path]::PathSeparator + $env:PATH
$found = (Get-Command flatc).Source
Write-Host "flatc on PATH: $found"
flatc --version
Write-Host "it exits $LASTEXITCODE"
if ($LASTEXITCODE -ne 42 -or (Split-Path $found) -ne $decoy) { Stop-Probe 'the decoy is not the flatc on PATH' }

# The port's build-dependency probe generates and runs beside it.
cargo run --locked -- --dry-run --port flatc --cabin $Cabin --work-dir (Join-Path $work 'work')
if ($LASTEXITCODE -ne 0) { Stop-Probe 'the dry-run failed beside a decoy flatc' }

# What `cabin install` put in its root writes the header the build
# dependency wrote.
$installed = Join-Path $work "work/run/probes/flatc/25.12.19/root/bin/flatc$exe"
& $installed --version
if ($LASTEXITCODE -ne 0) { Stop-Probe 'the installed flatc does not run' }
& $installed --cpp -o (Join-Path $work 'installed') $schema
if ($LASTEXITCODE -ne 0) { Stop-Probe 'the installed flatc does not generate' }
# -Force: Cabin keeps a build script's output under a dot directory.
$built = @(Get-ChildItem -Recurse -Force -Filter monster_generated.h (Join-Path $work 'work/run/probes'))
if ($built.Count -eq 0) { Stop-Probe 'the dry-run left no generated header' }
$want = (Get-FileHash (Join-Path $work 'installed/monster_generated.h')).Hash
foreach ($header in $built) {
    $same = (Get-FileHash $header.FullName).Hash -eq $want
    Write-Host "the build dependency wrote $($header.FullName); the installed flatc writes the same: $same"
    if (-not $same) { Stop-Probe 'the installed flatc and the build dependency disagree' }
}
Write-Host 'PROBE OK'
