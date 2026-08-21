param(
    [switch]$Clean,
    [switch]$Test,
    [ValidateSet('x64','x86')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project ("build-" + $Architecture)
$generator = 'Visual Studio 17 2022'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { throw '未找到 cmake.exe。请安装 CMake 并重新打开 PowerShell。' }
$cmakePath = if ($cmake.Source) { $cmake.Source } else { $cmake.Path }
$cmakeArch = if ($Architecture -eq 'x86') { 'Win32' } else { 'x64' }

if ($Clean -and (Test-Path $build)) { Remove-Item -LiteralPath $build -Recurse -Force }

Write-Host "使用 CMake: $cmakePath"
Write-Host "目标架构: $Architecture ($cmakeArch)"
& $cmakePath -S $project -B $build -G $generator -A $cmakeArch -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmakePath --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) {
    & $cmakePath --build $build --config Release --target RUN_TESTS
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-Host "构建完成: $build\\Release\\ShutDown.exe"
