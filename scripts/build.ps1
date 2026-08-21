param(
    [switch]$Clean,
    [switch]$Test,
    [ValidateSet('x64','x86')]
    [string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project 'build-local'
$cmakePath = 'H:\Dev\CMake\bin\cmake.exe'
if (-not (Test-Path $cmakePath)) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $cmakePath = $cmd.Source }
}
if (-not (Test-Path $cmakePath)) { throw 'cmake.exe not found' }
if ($Clean -and (Test-Path $build)) { Remove-Item -LiteralPath $build -Recurse -Force }
if ($Architecture -eq 'x86') { throw 'Local x86 toolchain is not installed; use x64 locally or CI for x86.' }
$env:PATH = 'C:\TDM-GCC-64\bin;H:\Dev\CMake\bin;' + $env:PATH
& $cmakePath -S $project -B $build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmakePath --build $build --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) { & $cmakePath --build $build --target test; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
Write-Host ('Build complete: ' + (Join-Path $build 'ShutDown.exe'))
