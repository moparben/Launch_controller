<#
 move_vscode_temp_to_d.ps1
 Purpose:  Move user TEMP/TMP for VS Code usage to D:\VSCodeTemp and optionally set for the user; will not modify system environment by default.
 Usage:
   .\move_vscode_temp_to_d.ps1 [-SetUserEnv] [-SetSystemEnv]
 Flags:
   -SetUserEnv : Set the current user's TEMP/TMP to D:\VSCodeTemp (setx limits apply)
   -SetSystemEnv : Attempt to set system environment variables (requires elevation)
#>

param(
    [switch]$SetUserEnv,
    [switch]$SetSystemEnv
)

$target = 'D:\VSCodeTemp'
if (-not(Test-Path $target)) {
    Write-Host "Creating $target"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
} else {
    Write-Host "$target already exists"
}

Write-Host "Setting current process TEMP/TMP to $target"
$env:TEMP = $target
$env:TMP = $target

if ($SetUserEnv) {
    Write-Host "Setting user-level environment variables (setx TEMP/TMP). This affects new processes and future VS Code launches."
    setx TEMP "$target"
    setx TMP "$target"
    Write-Host "User-level TEMP/TMP set. Restart VS Code to use the new values."
}

if ($SetSystemEnv) {
    Write-Host "Setting system-level environment variables (requires elevation). Attempting to set with setx /M."
    if (-not([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warning "You must run this PowerShell session as Administrator to set system-level environment variables. Skipping system env update."
    } else {
        setx /M TEMP "$target"
        setx /M TMP "$target"
        Write-Host "System-level TEMP/TMP set. Reboot may be required for some services to pick up values."
    }
}

Write-Host "Current process environment: TEMP=$env:TEMP TMP=$env:TMP"
