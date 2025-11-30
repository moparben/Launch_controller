<#
    clean-build.ps1 - safe helper to delete the build/ directory.
    - Detects if the folder looks like a CMake build tree (presence of CMakeCache.txt).
    - If not a standard build tree, it warns and requires explicit -Force to proceed.
    - Confirm prompt before deleting to prevent accidental removal.

    Usage:
        pwsh -NoProfile -ExecutionPolicy Bypass ./scripts/clean-build.ps1
        pwsh -NoProfile -ExecutionPolicy Bypass ./scripts/clean-build.ps1 -Force
#>

param (
    [switch]$Force = $false
)

Set-Location -LiteralPath $PSScriptRoot\..

$buildPath = Join-Path -Path (Get-Location) -ChildPath "build"
if (-not (Test-Path $buildPath)) {
    Write-Host "No build directory found at: $buildPath" -ForegroundColor Yellow
    exit 0
}

$looksLikeCmakeBuild = Test-Path (Join-Path $buildPath "CMakeCache.txt")

Write-Host "Build directory found at: $buildPath"
if ($looksLikeCmakeBuild) {
    Write-Host "Looks like a CMake build directory (CMakeCache.txt found)." -ForegroundColor Green
} else {
    Write-Host "This build directory does NOT contain CMakeCache.txt. It may not be a standard CMake build directory." -ForegroundColor Yellow
}

if (-not $Force) {
    $msg = "Do you want to delete the build folder and all its contents? (y/N)"
    $confirmation = Read-Host $msg
    if ($confirmation -ne 'y' -and $confirmation -ne 'Y') {
        Write-Host "Aborting delete. Use -Force to bypass confirmation." -ForegroundColor Yellow
        exit 0
    }
}

Write-Host "Deleting build directory..." -ForegroundColor Cyan
try {
    Remove-Item -LiteralPath $buildPath -Recurse -Force -ErrorAction Stop
    Write-Host "Build directory deleted." -ForegroundColor Green
} catch {
    Write-Error "Failed to delete build directory: $_"
    exit 1
}
