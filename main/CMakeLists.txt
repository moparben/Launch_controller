idf_component_register(SRCS "display_mcu_v3_03.c" "display_mcu_v3_02_clean.c" "touch_calibration.c" "ui_manager.c" "ui_styles.c"
                    INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_DISPLAY_V3_03)

# Note: archived/legacy display MCU files are stored in old_stuff/display_mcu_versions
# to avoid accidental inclusion. If you need a prior version (e.g., v3_02), copy or
# reference it from there and update the SRCS list accordingly.