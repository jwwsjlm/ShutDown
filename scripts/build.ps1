param(
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project 'build'

$cmakePath = $null
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
    if (-not $cmakePath) { $cmakePath = $cmakeCommand.Path }
}
if (-not $cmakePath) {
    # Qt Maintenance Tool commonly installs CMake next to the Qt toolchains,
    # but does not add it to PATH.  Use that copy before asking the user to
    # install CMake separately.
    $cmakeCandidates = @(
        'H:\Dev\Qt\Tools\CMake_64\bin\cmake.exe',
        'C:\Qt\Tools\CMake_64\bin\cmake.exe'
    ) | Where-Object { Test-Path $_ }
    if ($cmakeCandidates) {
        $cmakePath = $cmakeCandidates | Select-Object -First 1
    }
}
if (-not $cmakePath) {
    throw '未找到 cmake.exe。请先安装 CMake，并重新打开 PowerShell。'
}

if ($Clean -and (Test-Path $build)) {
    Remove-Item -LiteralPath $build -Recurse -Force
}

$qtRoots = @('H:\Dev\Qt', 'C:\Qt') | Where-Object { Test-Path $_ }
$qmake = $null
foreach ($root in $qtRoots) {
    $qmake = Get-ChildItem -Path $root -Recurse -Filter qmake.exe -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\(mingw_64|mingw\d+_64)\\bin\\qmake\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($qmake) { break }
}
if (-not $qmake) {
    throw '未找到 Qt qmake.exe。请确认 Qt 已安装桌面 MinGW/MSVC 组件。'
}

$qtPrefix = Split-Path (Split-Path $qmake.FullName -Parent) -Parent
$qtRoot = Split-Path (Split-Path $qtPrefix -Parent) -Parent
$toolCandidates = @('mingw1120_64', 'mingw1310_64', 'mingw810_64')
$mingw = $null
foreach ($toolName in $toolCandidates) {
    $toolDir = Join-Path (Join-Path $qtRoot 'Tools') $toolName
    $gcc = Join-Path $toolDir 'bin\gcc.exe'
    $gxx = Join-Path $toolDir 'bin\g++.exe'
    if ((Test-Path $gcc) -and (Test-Path $gxx)) {
        $mingw = [pscustomobject]@{ Gcc = $gcc; Gxx = $gxx }
        break
    }
}
if (-not $mingw) {
    throw "未找到与 Qt 配套的 MinGW 编译器：$(Join-Path $qtRoot 'Tools')"
}
Write-Host "使用 Qt: $qtPrefix"
Write-Host "使用 CMake: $cmakePath"
Write-Host "使用 MinGW: $($mingw.Gxx)"
$env:PATH = "$(Split-Path $mingw.Gxx -Parent);$(Join-Path $qtPrefix 'bin');$env:PATH"

& $cmakePath -S $project -B $build -G 'MinGW Makefiles' `
    "-DCMAKE_PREFIX_PATH=$qtPrefix" `
    "-DCMAKE_C_COMPILER=$($mingw.Gcc)" `
    "-DCMAKE_CXX_COMPILER=$($mingw.Gxx)"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmakePath --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & $cmakePath --build $build --target test
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
