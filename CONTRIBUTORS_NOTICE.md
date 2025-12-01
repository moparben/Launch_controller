# Important: Final repository history rewrite completed — action required

Dear contributors,

We completed a FINAL history rewrite across the repository with a focus on removing large build artifacts, archived demo/test files, and several large LVGL fonts/screenshots/other binary assets. The `esp32_p4_display_controller` branch has been rewritten and pushed with a cleaned commit history. Additionally, `main` and `master` were also updated during the final pass.

Summary of the final pass
- Rewritten `esp32_p4_display_controller` commit: `9537cdf` (message: docs: finalize HISTORY_REWRITE with final pass summary)
- `main` and `master` updated to: `d91717b`
- Mirror backup (pre-rewrite): `C:\esp32_projects\display_controller_repo_backup_final` (local mirror exists; keep it safe)

Why this matters
- History rewrites are destructive: they change commit SHAs across affected branches. This is done to reduce the repository size and remove large binary assets that do not belong in version history.
- Any local clones, branches, or open PRs created against the previous history must be re-synced by each contributor using the instructions below.

Recommended steps for contributors

Option 1: Fresh clone (Recommended)
```powershell
# Remove the old clone (backup local changes if needed) and re-clone
rd /s /q "C:\path\to\old\display_controller_clone"
git clone https://github.com/moparben/Launch_controller.git "C:\path\to\display_controller"
cd "C:\path\to\display_controller"
git checkout esp32_p4_display_controller
```

Option 2: Reset an existing copy (Conservative)
```powershell
cd "C:\path\to\display_controller"
git fetch origin --prune
git checkout esp32_p4_display_controller
git reset --hard origin/esp32_p4_display_controller
```

If you have un-pushed local changes (e.g., open PRs / local branches) — DO NOT reset hard without backup.
Create patches first:
```powershell
cd "C:\path\to\display_controller"
git format-patch origin/esp32_p4_display_controller..my-local-branch -o "C:\path\to\patches"
# Re-clone then re-apply patches with git am
```

If you need to rebase/or re-open a PR, export your patches or create a new branch and open a PR in the cleaned repository.

Other notes and contacts
- A mirror backup was created at: `C:\esp32_projects\display_controller_repo_backup_final` — this contains the original full history if you need to recover specific files.
- Branches of note updated on origin: `esp32_p4_display_controller`, `main`, `master`, `backup-before-build-untrack`.
- If you need help re-basing local changes, please open an issue, or reach out to the maintainers — they can help you re-create PRs or restore missing data from the mirror.

What we will do next
- Create a PR containing this notice to make sure it's prominent in the default branch and easy to find.
- Provide a short migration checklist for CI maintainers and recommended steps for local developers.

Thank you for your patience and cooperation.

-- The Rocket team
# Important: Repository history rewrite - collaborator action required

Dear contributors,

We performed two history rewrite operations on the `esp32_projects/display_controller` repo `esp32_p4_display_controller` branch to remove large build artifacts and archived demo/test files from the history, and force-pushed the cleaned history to the remote.

If you previously cloned the repository, please follow one of the following options to resynchronize safely:

Option 1: Fresh clone (recommended)

```powershell
# Delete local copy if not needed and re-clone
rd /s /q "C:\path\to\old\display_controller_clone"
git clone https://github.com/moparben/Launch_controller.git "C:\path\to\display_controller"
cd "C:\path\to\display_controller"
git checkout esp32_p4_display_controller
```

Option 2: Reset an existing copy (conservative)

```powershell
cd "C:\path\to\display_controller"
git fetch origin
git checkout esp32_p4_display_controller
git reset --hard origin/esp32_p4_display_controller
```

Notes:
- A history rewrite is destructive and changes commit SHAs. Any open branches, feature branches, or pending PRs that were based on the old history may need rebasing or re-checking.
- If you have branches with changes not yet pushed, please create a patch or copy of your work before resetting.

If you need help restoring local changes or rebasing your PRs, the maintainers or I can help — please ping me.
