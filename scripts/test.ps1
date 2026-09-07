param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "build-tests",
    [switch]$List
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

function Stop-RunningTestExecutable([string]$ExecutablePath) {
    $fullExecutablePath = [System.IO.Path]::GetFullPath($ExecutablePath)
    $executableName = [System.IO.Path]::GetFileName($fullExecutablePath)
    $runningTests = Get-CimInstance Win32_Process -Filter "Name = '$executableName'" |
        Where-Object {
            $_.ExecutablePath -and
            [System.IO.Path]::GetFullPath($_.ExecutablePath) -ieq $fullExecutablePath
        }

    foreach ($runningTest in $runningTests) {
        Write-Host "Stopping stale $executableName (PID $($runningTest.ProcessId)) before linking..." `
            -ForegroundColor DarkYellow
        Stop-Process -Id $runningTest.ProcessId -Force
        Wait-Process -Id $runningTest.ProcessId -ErrorAction SilentlyContinue
    }
}

$CMakeArgs = @("-S", $RepoRoot, "-B", $BuildPath, "-DBUILD_TESTING=ON")
if ($RaylibRoot) {
    $RaylibInclude = Join-Path $RaylibRoot "include"
    $RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
    if (-not (Test-Path $RaylibInclude) -or -not (Test-Path $RaylibLibrary)) {
        throw "RAYLIB_ROOT does not contain include/ and lib/raylib.lib: $RaylibRoot"
    }
    $CMakeArgs += "-Draylib_INCLUDE_DIR=$RaylibInclude", "-Draylib_LIBRARY=$RaylibLibrary"
}

Write-Host "Configuring tests..." -ForegroundColor Cyan
cmake @CMakeArgs
Assert-LastCommandSucceeded "CMake configure"

$TestExe = Join-Path $BuildPath "tests\$Config\tvorin_tests.exe"
Stop-RunningTestExecutable $TestExe

Write-Host "Building test targets ($Config)..." -ForegroundColor Cyan
cmake --build $BuildPath --parallel --config $Config --target tvorin_tests rts_data_validator
Assert-LastCommandSucceeded "CMake build"

if ($List) {
    ctest --test-dir $BuildPath -C $Config --show-only
    Assert-LastCommandSucceeded "Test listing"
    return
}

Write-Host "Running tests..." -ForegroundColor Cyan
ctest --test-dir $BuildPath -C $Config --output-on-failure
Assert-LastCommandSucceeded "Test run"
