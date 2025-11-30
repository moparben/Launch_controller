<#
    Helper script: Move grok fix folders into old_stuff and disable C sources
    - Use this when you want to archive experimental grok-fix work and ensure
      CMake won't accidentally pick it up.
    - It renames folders like "grok fix 3_04_01" to "old_stuff/grok_fix_3_04_01"
    - It appends ".disabled" to any *.c files found under the folder.
#>

param (
    [string]$ProjectRoot = (Get-Location)
)

Push-Location $ProjectRoot
try {
    $grokFolders = Get-ChildItem -Path . -Directory -Filter 'grok fix*' -ErrorAction SilentlyContinue
    if (-not $grokFolders) {
        Write-Host "No 'grok fix *' folders found. Nothing to do."
        return
    }
    foreach ($f in $grokFolders) {
        $dstName = "old_stuff/" + ($f.Name -replace ' ', '_')
        $dstPath = Join-Path -Path (Get-Location) -ChildPath $dstName
        if (-not (Test-Path $dstPath)) {
            New-Item -ItemType Directory -Path $dstPath -Force | Out-Null
        }
        Write-Host "Moving $($f.FullName) -> $dstPath"
        Get-ChildItem -Path $f.FullName -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($f.FullName.Length + 1)
            $dstFile = Join-Path -Path $dstPath -ChildPath $rel
            $dstDir = Split-Path $dstFile -Parent
            if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Path $dstDir -Force | Out-Null }
            Move-Item -Path $_.FullName -Destination $dstFile -Force
            if ($dstFile -like "*.c") {
                Rename-Item -Path $dstFile -NewName ($dstFile + ".disabled") -Force
            }
        }
        # Remove the original folder if empty
        Remove-Item -Path $f.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "Grok fix folders moved and C sources disabled; please commit the changes." -ForegroundColor Green
} finally {
    Pop-Location
}
