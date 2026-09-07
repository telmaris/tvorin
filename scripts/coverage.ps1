param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build-tests-coverage",
    [string]$OpenCppCoveragePath = "",
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT }
              elseif (Test-Path (Join-Path $RepoRoot "deps\raylib\lib\raylib.lib")) { Join-Path $RepoRoot "deps\raylib" }
              else { "" }
$BuildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $RepoRoot $BuildDir
}

function Assert-LastCommandSucceeded([string]$StepName) {
    if ($LASTEXITCODE -ne 0) {
        throw "$StepName failed with exit code $LASTEXITCODE"
    }
}

if (-not $OpenCppCoveragePath) {
    $command = Get-Command OpenCppCoverage -ErrorAction SilentlyContinue
    if ($command) {
        $OpenCppCoveragePath = $command.Source
    }
}
if (-not $OpenCppCoveragePath) {
    $OpenCppCoveragePath = "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe"
}
if (-not (Test-Path $OpenCppCoveragePath)) {
    throw "OpenCppCoverage.exe not found. Install it or pass -OpenCppCoveragePath."
}

Write-Host "Configuring coverage build..." -ForegroundColor Cyan
$CMakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildPath,
    "-DBUILD_TESTING=ON",
    "-DENABLE_COVERAGE=ON",
    "-DOPENCPPCOVERAGE_EXECUTABLE=$OpenCppCoveragePath"
)
if ($RaylibRoot) {
    $RaylibInclude = Join-Path $RaylibRoot "include"
    $RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
    if (-not (Test-Path $RaylibInclude) -or -not (Test-Path $RaylibLibrary)) {
        throw "RAYLIB_ROOT does not contain include/ and lib/raylib.lib: $RaylibRoot"
    }
    $CMakeArgs += "-Draylib_INCLUDE_DIR=$RaylibInclude", "-Draylib_LIBRARY=$RaylibLibrary"
}
cmake @CMakeArgs
Assert-LastCommandSucceeded "CMake configure"

Write-Host "Building and running instrumented tests ($Config)..." -ForegroundColor Cyan
cmake --build $BuildPath --parallel --config $Config --target coverage
Assert-LastCommandSucceeded "Coverage run"

$HtmlReport = Join-Path $BuildPath "coverage\index.html"
$XmlReport = Join-Path $BuildPath "coverage.xml"
if (-not (Test-Path $HtmlReport) -or -not (Test-Path $XmlReport)) {
    throw "Coverage completed without both HTML and Cobertura reports."
}

[xml]$CoverageXml = Get-Content $XmlReport
$Root = $CoverageXml.coverage
$Percent = [math]::Round(([double]$Root.'line-rate') * 100.0, 2)
Write-Host "Coverage: $Percent% ($($Root.'lines-covered')/$($Root.'lines-valid') lines)" `
    -ForegroundColor Cyan

$Classes = @($CoverageXml.SelectNodes("//class"))
if ($Classes.Count -gt 0) {
    Write-Host "Lowest covered files:" -ForegroundColor Cyan
    $Classes |
        Where-Object { $_.filename -and $_.lines.line.Count -gt 0 } |
        Sort-Object { [double]$_.'line-rate' } |
        Select-Object -First 10 |
        ForEach-Object {
            $filePercent = [math]::Round(([double]$_.'line-rate') * 100.0, 2)
            Write-Host ("  {0,6}%  {1}" -f $filePercent, $_.filename)
        }
}

Write-Host "HTML: $HtmlReport" -ForegroundColor Green
Write-Host "XML:  $XmlReport" -ForegroundColor Green
if ($OpenReport) {
    Start-Process $HtmlReport
}
