Reorganization & Image Regeneration Guide

Quick summary
- This project uses a `BUILD_DISPLAY_VERSION` CMake option to choose which display MCU source to compile.
- To avoid accidentally including prior builds or old files, keep older versions in `old_stuff/display_mcu_versions/` and rely on explicit CMake `SRCS` entries.
- The `main/CMakeLists.txt` now prefers the `grok fix` version if present (`../grok fix 3_04_01/display_mcu_v3_04_01.c`) so you can test fixes locally without copying them into main.

How to avoid building previous versions
1. Use `BUILD_DISPLAY_VERSION` only to select explicit versions.
   - No globs or "wildcard" `*.c` in `idf_component_register(SRCS ...)`.
2. Keep old code under `old_stuff/display_mcu_versions/` and do not reference it in `SRCS`.
3. Use `git` to manage feature branches (e.g., `grok-fix-v3-04-01`) and push PRs when you're ready to adopt a change.
4. Use CMake checks (already present) and avoid duplicate `splash.c` or `display_mcu` files.

Regenerating `img_rocket_png.c` from a PNG
1. Save your PNG into `assets/` (e.g., `main/assets/rocket.png`).
2. Use the included tool to create a 360x360 LVGL C file:

    python -m pip install pillow
    python tools/convert_to_lvgl.py main/assets/rocket.png main/img_rocket_png.c 360 360

3. The script will resize the PNG to 360x360, encode as RGB565, and output `main/img_rocket_png.c` containing `const lv_img_dsc_t img_rocket_png`.
4. The CMake file already includes `img_rocket_png.c` in the main SRCS list.

Optional: Clean up duplicates
- If you find multiple `splash.c` or `display_mcu*.c`, remove or rename the older ones and rely on CMake to pick the new one.

Tips and caveats
- Large image arrays increase binary size; keep resolution and color depth appropriate to your app.
- The script performs alpha compositing over white if the PNG has alpha channel — change behavior if desired.
- Use smaller images for memory-constrained builds, or use LVGL image files loaded from filesystem instead of embedding binary C data.

IDF target & build hygiene
- Set the IDF target to `esp32p4` by default via `main/sdkconfig.defaults` (CONFIG_IDF_TARGET="esp32p4"). This helps CMake and `idf.py` avoid target guessing during the first configure.
- For per-session needs, run `pwsh ./scripts/set-idf-target.ps1 -Target esp32p4` to ensure your session's `IDF_TARGET` is explicitly set.
- To safely delete `build/` run `pwsh ./scripts/clean-build.ps1` and confirm; use `-Force` to skip confirmation.
