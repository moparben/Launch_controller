<#
    set-idf-target.ps1
    PowerShell helper to ensure IDF_TARGET is set to esp32p4 for this project
    Usage: .\set-idf-target.ps1 [-Target esp32p4]
    This script activates the workspace IDF env and runs idf.py set-target to
    explicitly set the target so CMake does not have to guess from sdkconfig.
#>

param (
    [string]$Target = 'esp32p4'
)

Write-Host "Setting IDF target to: $Target"

    $activatePath = Join-Path -Path $PSScriptRoot -ChildPath "..\activate_idf.ps1"
    if (-not (Test-Path $activatePath)) {
        $activatePath = "D:\\esp32_projects\\activate_idf.ps1"
    }
if (-not (Test-Path $activatePath)) {
    Write-Error "Could not find activate_idf.ps1; please set your IDF environment first."
    exit 1
}

& $activatePath
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to activate IDF environment."; exit 1 }

# Ensure idf.py set-target runs but do not fail the script if the command returns
# non-zero due to build dir cleanup issues; we still want to set the IDF_TARGET
# environment variable to eliminate the cmake 'guessed target' message.
try {
    idf.py set-target $Target -y
} catch {
    Write-Warning "Running 'idf.py set-target' failed or returned non-zero; continuing anyway (the sdkconfig may already contain the target)"
}

Write-Host "Setting IDF_TARGET in-process to: $Target"
$env:IDF_TARGET = $Target
[System.Environment]::SetEnvironmentVariable('IDF_TARGET', $Target, 'Process')
Write-Host "IDF target for this session set to $Target" -ForegroundColor Green
