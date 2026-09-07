param(
    [ValidateSet("patch", "minor", "major")]
    [string]$Part = "patch"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $repoRoot
try {
    $status = git status --porcelain
    if ($status) {
        throw "Release versioning requires a clean working tree."
    }

    $versionPath = Join-Path $repoRoot "VERSION"
    $parts = (Get-Content -Raw $versionPath).Trim().Split('.') | ForEach-Object { [int]$_ }
    if ($parts.Count -ne 3) {
        throw "VERSION must contain MAJOR.MINOR.PATCH"
    }

    switch ($Part) {
        "major" { $parts[0]++; $parts[1] = 0; $parts[2] = 0 }
        "minor" { $parts[1]++; $parts[2] = 0 }
        "patch" { $parts[2]++ }
    }

    $next = "$($parts[0]).$($parts[1]).$($parts[2])"
    Set-Content -Path $versionPath -Value $next -NoNewline -Encoding ascii
    Write-Host "Prepared VERSION $next. Review the diff, then commit and tag deliberately." -ForegroundColor Green
    git diff -- VERSION
}
finally {
    Pop-Location
}
