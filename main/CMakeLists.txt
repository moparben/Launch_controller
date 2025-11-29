idf_component_register(SRCS "display_mcu_v3_04.c" "display_mcu_v3_02_clean.c" "touch_calibration.c" "ui_manager.c" "ui_styles.c"
                    INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_DISPLAY_V3_04 CAL_APP_SOFTWARE_SWAP_XY=0 CAL_DISPLAY_WIDTH=1280 CAL_DISPLAY_HEIGHT=800)

# Note: archived/legacy display MCU files are stored in old_stuff/display_mcu_versions
# to avoid accidental inclusion. If you need a prior version (e.g., v3_02), copy or
# reference it from there and update the SRCS list accordingly.