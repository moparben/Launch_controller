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
