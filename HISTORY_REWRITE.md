# History Rewrite Summary

This repository has undergone a history rewrite to remove large and archival content to reduce repository size and improve developer tooling performance.  The rewrite was performed on the branch `esp32_p4_display_controller` and force-pushed to your remote.

What was removed:
- old_stuff/
- test_firmware/
- archive/
- display_controller_archived/
- grok fix/
- grok_fix/

Note: the rewrite operation only removed references to the listed directories from all rewritten commits. Some third-party components (e.g., `managed_components`) may still contain tests and demo assets which are intentionally left intact.

Repository backups:
- A local backup mirror was created during the rewrite: `repo_backup_before_rewrite`.
- The mirror clone was created at `repo_mirror_for_rewrite` (deleted locally as part of cleanup).

Collaborators:
If you have a local clone of this repository, please run the following to reset cleanly to the rewritten history:

```powershell
git fetch origin
git checkout esp32_p4_display_controller
git reset --hard origin/esp32_p4_display_controller
```

If you prefer not to force reset, re-clone the repo again.

Further steps:
- If you want to permanently remove additional archive files from the repository, please open an issue or request a follow-up with an explicit path list.
- Keep in mind a history rewrite changes commit SHAs and requires developer coordination.

If you need assistance with the next steps (re-clones, PR rebases, or further history cleanups), please let me know the paths to target next.
