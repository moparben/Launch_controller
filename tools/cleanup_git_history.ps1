<#
  cleanup_git_history.ps1
  Safe git history clean-up helper for display_controller repo
  - Default mode is DRY-RUN and creates a mirror backup before altering history
  - Requires git-filter-repo (https://github.com/newren/git-filter-repo) OR BFG (https://rtyley.github.io/bfg-repo-cleaner/)
  - The script only runs the destructive rewrite when -Confirm:$true is specified
#>

param(
    [String[]]$PathsToRemove = @('old_stuff/', 'test_firmware/', 'archive/', 'display_controller_archived/', 'grok fix/', 'grok_fix/'),
    [string]$BackupDir = "${PWD}\repo_backup_before_rewrite",
    [string]$MirrorCloneDir = "${PWD}\repo_mirror_for_rewrite",
    [string]$Remote = 'origin',
    [switch]$Confirm,
    [switch]$Force
)

Write-Host "Preparing to clean repository history for paths: $($PathsToRemove -join ', ')"

function Test-CommandExists {
    param($cmd)
    $null -ne (Get-Command $cmd -ErrorAction SilentlyContinue)
}

if (-not(Test-CommandExists git)) {
    Write-Error "git not found in PATH. Aborting."
    exit 1
}

if (-not((Test-Path .git))) {
    Write-Error "This folder does not appear to be a Git repository. Run this from the repo root."
    exit 1
}

if (-not (Test-Path $BackupDir)) {
    Write-Host "Creating read-only backup mirror at: $BackupDir"
    git clone --mirror . "$BackupDir"
} else {
    Write-Host "Backup dir $BackupDir already exists; skipping clone."
}

if (-not(Test-Path $MirrorCloneDir)) {
    Write-Host "Creating a temporary mirror clone for rewriting: $MirrorCloneDir"
    git clone --mirror . "$MirrorCloneDir"
} else {
    Write-Host "Mirror clone $MirrorCloneDir already exists; remove it to start fresh."
}

Push-Location $MirrorCloneDir
try {
    if (Test-CommandExists git-filter-repo) {
        # Build --invert-paths arg list
        $pathsArgs = $PathsToRemove | ForEach-Object { "--path $_" } | Out-String
        Write-Host "Git filter-repo available; running filter-repo in the mirror clone (DRY-RUN unless -Confirm passed)."
        if ($Confirm) {
            git filter-repo --invert-paths $($PathsToRemove | ForEach-Object { "--path $_" })
            Write-Host "Filter applied. Please verify everything; to force-push run the push command below."
            if ($Force) {
                Write-Host "Pushing rewritten refs to remote '$Remote' (force push)."
                git push $Remote --all --force
                git push $Remote --tags --force
            } else {
                Write-Host "Force push skipped because -Force not used. Inspect the mirror clone and then run with -Force to push."
            }
        } else {
            Write-Host "DRY-RUN: Running git log queries to show impact"
            foreach ($p in $PathsToRemove) {
                Write-Host "Showing recent commits that touched $p"
                git log --stat --max-count=5 -- "${p}" | Out-Host
            }
            Write-Host "Dry-run complete. Re-run with -Confirm to mutate the history."
        }
    } elseif (Test-CommandExists bfg) {
        # Using BFG jar - easier for deleting files, requires Java
        Write-Host "BFG detected; running BFG with specified paths (requires Java)."
        if ($Confirm) {
            # BFG syntax expects a patterns file; we'll create a temporary patterns file
            $patternFile = [IO.Path]::GetTempFileName()
            $PathsToRemove | ForEach-Object { Add-Content -Path $patternFile -Value $_ }
            Write-Host "Running bfg --delete-files with patterns from $patternFile"
            bfg --delete-files $patternFile .
            Write-Host "BFG rewrite is complete. Run 'git reflog expire --expire=now --all && git gc --prune=now --aggressive' then push --force."
        } else {
            Write-Host "DRY-RUN: Showing commits that touched the selected paths"
            foreach ($p in $PathsToRemove) { git log --stat --max-count=5 -- "${p}" | Out-Host }
            Write-Host "Dry-run complete. Re-run with -Confirm to mutate the history."
        }
    } else {
        Write-Host "No supported history rewrite tool detected (git-filter-repo or bfg)."
        Write-Host "Install: https://github.com/newren/git-filter-repo or BFG Repo Cleaner."
    }
} finally {
    Pop-Location
}

Write-Host "Cleanup script finished. Inspect the mirror clone at $MirrorCloneDir; the original repository history is backed up at $BackupDir."
