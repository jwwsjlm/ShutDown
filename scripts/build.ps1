param(
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$build = Join-Path $project 'build'

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
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
$mingw = Get-ChildItem (Join-Path $qtRoot 'Tools') -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object {
        $gcc = Join-Path $_.FullName 'bin\gcc.exe'
        $gxx = Join-Path $_.FullName 'bin\g++.exe'
        if ((Test-Path $gcc) -and (Test-Path $gxx)) { [pscustomobject]@{ Gcc = $gcc; Gxx = $gxx } }
    } |
    Select-Object -First 1
if (-not $mingw) {
    throw "未找到与 Qt 配套的 MinGW 编译器：$(Join-Path $qtRoot 'Tools')"
}
Write-Host "使用 Qt: $qtPrefix"
Write-Host "使用 CMake: $($cmake.Source)"
Write-Host "使用 MinGW: $($mingw.Gxx)"

& $cmake.Source -S $project -B $build -G 'MinGW Makefiles' `
    -DCMAKE_PREFIX_PATH=$qtPrefix `
    -DCMAKE_C_COMPILER=$mingw.Gcc `
    -DCMAKE_CXX_COMPILER=$mingw.Gxx
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake.Source --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & $cmake.Source --build $build --target test
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
