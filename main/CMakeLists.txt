idf_component_register(SRCS "display_mcu_v3_03.c" "display_mcu_v3_02_clean.c" "touch_calibration.c" "ui_manager.c" "ui_styles.c"
                    INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_DISPLAY_V3_03)