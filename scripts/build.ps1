param(
    [switch]$Clean,
    [switch]$Test,
    [ValidateSet('x64','x86')]
    [string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { throw 'cmake.exe not found' }
$ctest = Join-Path (Split-Path -Parent $cmake.Source) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw 'ctest.exe not found' }

$configurePreset = "windows-$Architecture"
$buildPreset = "$configurePreset-release"
$build = Join-Path $project "build/windows-$Architecture"

if ($Clean -and (Test-Path -LiteralPath $build)) {
    Remove-Item -LiteralPath $build -Recurse -Force
}

Push-Location $project
try {
    & $cmake.Source --preset $configurePreset '-DSHUTDOWN_VERSION_OVERRIDE='
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake.Source --build --preset $buildPreset --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Test) {
        & $ctest --preset $buildPreset
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally {
    Pop-Location
}

Write-Host ('Build complete: ' + (Join-Path $build 'Release/ShutDown.exe'))
