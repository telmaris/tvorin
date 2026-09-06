param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    # Match GitHub Actions for the normal fast feedback loop. Use
    # -Config Debug explicitly when stepping through a failing test.
    [string]$Config = "Release",

    [string]$BuildDir = "build-tests",

    [switch]$List,

    # AI behavior harnesses are long-running integration scenarios. Keep the
    # default aligned with GitHub Actions; opt in when investigating AI.
    [switch]$IncludeAIBehaviorHarness
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT }
              elseif (Test-Path (Join-Path $RepoRoot "deps\raylib\lib\raylib.lib")) { Join-Path $RepoRoot "deps\raylib" }
              else { Join-Path (Split-Path -Parent $RepoRoot) "work\local\raylib" }

$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
$RayguiInclude = Join-Path $RepoRoot "deps\raygui"
$BuildPath = Join-Path $RepoRoot $BuildDir

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

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

Write-Host "Configuring tests..." -ForegroundColor Cyan
cmake -S $RepoRoot -B $BuildPath `
    -DBUILD_TESTING=ON `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary" `
    "-Draygui_INCLUDE_DIR=$RayguiInclude"
Assert-LastCommandSucceeded "CMake configure"

$TestExe = Join-Path $BuildPath "tests\$Config\tvorin_tests.exe"
Stop-RunningTestExecutable $TestExe

Write-Host "Building tvorin_tests ($Config)..." -ForegroundColor Cyan
cmake --build $BuildPath --parallel --config $Config --target tvorin_tests
Assert-LastCommandSucceeded "CMake build"

if (-not (Test-Path $TestExe)) {
    throw "Test executable was not found: $TestExe"
}

if ($List) {
    Write-Host "Available tests:" -ForegroundColor Cyan
    & $TestExe --gtest_list_tests
    Assert-LastCommandSucceeded "Test listing"
    return
}

Write-Host "Running tests..." -ForegroundColor Cyan
$TestArguments = @("--gtest_color=yes", "--gtest_brief=0")
if (-not $IncludeAIBehaviorHarness) {
    $TestArguments += "--gtest_filter=-AIBehaviorHarnessTests.*"
    Write-Host "Skipping AI behavior harnesses (use -IncludeAIBehaviorHarness to run them)." -ForegroundColor DarkYellow
}
& $TestExe @TestArguments
Assert-LastCommandSucceeded "Test run"
