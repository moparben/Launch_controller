# History Rewrite Summary

This repository has undergone a FINAL history rewrite to remove large and archival content to reduce repository size and improve developer tooling performance. The rewrite was performed on the branch `esp32_p4_display_controller` (and certain branches/refs) and force-pushed to your remote.

What was removed:
- old_stuff/
- test_firmware/
- archive/
- display_controller_archived/
- grok fix/
- grok_fix/

Note: the rewrite operation only removed references to the listed directories from all rewritten commits. Some third-party components (e.g., `managed_components`) may still contain tests and demo assets which are intentionally left intact.

Repository backups:
- A final local backup mirror was created during the rewrite at: `C:\esp32_projects\display_controller_repo_backup_final`.

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

Final pass notes:
- This final pass removed additional example/demo assets and large binary fonts/animation/video assets from the `managed_components/lvgl__lvgl` tree and other folders specified in the final `git-filter-repo` pass.
- A final verification build (`idf.py build`) completed successfully in a fresh clone, showing that the cleaned repository still builds and outputs `mipi_dsi_panel.bin`.
- The repository pack size is reduced considerably and we maintain the backup mirror at `C:\esp32_projects\display_controller_repo_backup_final` if you require recovery of removed files.

Branches updated in the final pass:
- `esp32_p4_display_controller` -> `9537cdf` (cleaned)
- `main` -> `d91717b`
- `master` -> `d91717b`
- `backup-before-build-untrack` -> `1d89c9a`

If further removals are requested, please open an issue with a precise path list and a maintainer will evaluate the request.

