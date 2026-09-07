param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [switch]$Run
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

$CMakeArgs = @("-S", $RepoRoot, "-B", $BuildPath)
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

cmake --build $BuildPath --parallel --config $Config --target tvorin
Assert-LastCommandSucceeded "CMake build"

$Exe = Join-Path $BuildPath "$Config\tvorin.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $BuildPath "tvorin.exe"
}
if (-not (Test-Path $Exe)) {
    throw "Build finished, but executable was not found under: $BuildPath"
}

Write-Host "Built: $Exe" -ForegroundColor Green
if ($Run) {
    Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe)
}
