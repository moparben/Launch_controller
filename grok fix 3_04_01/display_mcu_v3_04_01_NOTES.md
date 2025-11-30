# Display MCU v3.04.01 - Recovery Notes

Path: `c:/esp32_projects/display_controller/grok fix 3_04_01/display_mcu_v3_04_01.c`

## What I found
- The file `display_mcu_v3_04_01.c` exists and appears to be a "grok fix" incremental update of `display_mcu_v3_04.c`.
- It is committed in git; commit: `32457b3` includes this file and changes. See `git log` for details.
- This version contains fixes and cleanup from v3.04 (commit notes indicate backlight enable/disable moved). 

## Key changes & highlights in v3.04.01
- Fixed double `lv_display_flush_ready()` bug by moving to a `flush_wait_cb` which waits on `draw_finish_sem`.
- Removed `touch_task()`: LVGL now owns touch polling (no double-reads or conflicting tasks).
- Hardware 90° rotation attempt and software fallback when swap fails: `g_hw_rotation_applied` tracks this.
- LVGL API calls are protected by `_lock_t lvgl_api_lock` to avoid concurrent access.
- ISR-safe semaphore handling: the wait & notify patterns are managed properly.
- Cleaner buffer allocation using `heap_caps_malloc`, with SPIRAM fallback and internal fallback.
- Calibration overlay is guarded by macros to avoid enabling in production inadvertently.
- Test pattern draw + backlight enable is delayed until UI setup has completed.

## Files of interest
- `main/display_mcu_v3_04.c` — the v3.04 baseline.
- `grok fix 3_04_01/display_mcu_v3_04_01.c` — the new production-ready version with improvements.
- `old_stuff/display_mcu_versions/` — historic/archived versions (references exist).
- Build/config/CMake variables include `CAL_APP_SOFTWARE_SWAP_XY` and similar build-time flags for rotation and calibration overlays.

## How to test (quick steps)
1. Enter the IDF build environment:

```powershell
cd c:\esp32_projects\display_controller
. c:\esp32_projects\activate_idf.ps1
idf.py build
```
2. Flash to the device (set the correct serial port):

```powershell
idf.py -p COM6 flash monitor
```
3. Verify the following behavior in `idf.py monitor` logs:
   - `Starting Display MCU v3.04.01 - Production Ready`
   - `Hardware 90° rotation` success or fallback message
   - `Display MCU v3.04.01 initialized successfully`
   - No double `lv_display_flush_ready` warnings; `flush_wait_cb: timeout` may appear if DMA fails.
   - Touch reporting via LVGL (no conflicting `touch_task` activity)
4. Validate LVGL cursor & UI, backlight enable only after UI shown and `ui_manager_show_home()`.

## Recommended follow-ups
- If you want to merge this change into `main`, we can:
  1. Run a full build/test and make a PR that replaces `main/display_mcu_v3_04.c` or merges the needed fixes.
  2. Ensure compile-time flags (CAL_* macros) are documented and controlled in `CMakeLists.txt`.
- Consider enabling `esp_lvgl_port` path and testing both paths (port vs manual).
- Add a unit test or integration test for the `flush`/`wait` behavior and touch handling.

## What I can do next
- Create a PR that moves `grok fix 3_04_01/display_mcu_v3_04_01.c` into `main` after tests.
- Run a test build and basic validation (if you give me permission to flash or specify a serial port).
- Summarize the previous chat content into this notes file (done now) or into a more detailed `CHAT_RECOVERY.md` on your request.

If you want, I can commit these notes and/or open a PR to merge the changes into `main` so everything is tracked in Git.
