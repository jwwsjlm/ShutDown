param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x64', 'x86')]
    [string]$Architecture,
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'
$qtVersion = '5.15.2'
$sourceRoot = Join-Path $env:RUNNER_TEMP "qt-$qtVersion-src-$Architecture"
$archive = Join-Path $env:RUNNER_TEMP "qt-everywhere-src-$qtVersion.zip"
$sourceUrl = "https://download.qt.io/archive/qt/5.15/$qtVersion/single/qt-everywhere-src-$qtVersion.zip"

if (Test-Path (Join-Path $InstallRoot 'lib/cmake/Qt5/Qt5Config.cmake')) {
    Write-Host "Static Qt cache hit: $InstallRoot"
    exit 0
}

if (-not (Test-Path $archive)) {
    Write-Host "Downloading Qt source: $sourceUrl"
    Invoke-WebRequest -Uri $sourceUrl -OutFile $archive
}

if (-not (Test-Path $sourceRoot)) {
    $extractRoot = Join-Path $env:RUNNER_TEMP "qt-$qtVersion-extract-$Architecture"
    if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
    Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force
    $top = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
    if (-not $top) { throw 'Qt source archive did not contain a top-level directory' }
    Move-Item -LiteralPath $top.FullName -Destination $sourceRoot
}

New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null

$configure = Join-Path $sourceRoot 'configure.bat'
if (-not (Test-Path $configure)) { throw "Qt configure.bat not found: $configure" }

$vsArch = if ($Architecture -eq 'x64') { 'x64' } else { 'x86' }
Write-Host "Building static Qt $qtVersion for $Architecture ($vsArch)"
Push-Location $sourceRoot
try {
    & $configure `
        -prefix $InstallRoot `
        -release `
        -static `
        -static-runtime `
        -opensource `
        -confirm-license `
        -schannel `
        -nomake tests `
        -nomake examples `
        -submodules qtbase
    if ($LASTEXITCODE -ne 0) { throw "Qt configure failed with exit code $LASTEXITCODE" }

    nmake /J $env:NUMBER_OF_PROCESSORS
    if ($LASTEXITCODE -ne 0) { throw "Qt build failed with exit code $LASTEXITCODE" }

    nmake install
    if ($LASTEXITCODE -ne 0) { throw "Qt install failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

if (-not (Test-Path (Join-Path $InstallRoot 'lib/cmake/Qt5/Qt5Config.cmake'))) {
    throw "Static Qt installation is incomplete: $InstallRoot"
}

$licenseTarget = Join-Path $InstallRoot 'licenses/Qt'
New-Item -ItemType Directory -Path $licenseTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'LICENSES/*') -Destination $licenseTarget -Recurse -Force
