if(NOT DEFINED BUILD_DISPLAY_VERSION)
    # Default to the v3_04 baseline - this is the current preferred and
    # validated version for production builds.  If you need to test a
    # grok-fix variant, enable the USE_GROK_FIX option explicitly (see
    # below) instead of leaving transient files lying around.
    set(BUILD_DISPLAY_VERSION "v3_04" CACHE STRING "Display MCU version to build (v3_02|v3_03|v3_04)")
endif()

message(STATUS "Selected display version: ${BUILD_DISPLAY_VERSION}")

option(USE_GROK_FIX "Use a local grok fix copy (project root: grok fix 3_04_01)" OFF)

if(BUILD_DISPLAY_VERSION STREQUAL "v3_02")
    # v3_02 is archived; find it in old_stuff if needed
    set(display_srcs "display_mcu_v3_02_clean.c")
    set(display_def BUILD_DISPLAY_V3_02)
elseif(BUILD_DISPLAY_VERSION STREQUAL "v3_03")
    # v3_03 is archived; find it in old_stuff if needed
    set(display_srcs "display_mcu_v3_03.c")
    set(display_def BUILD_DISPLAY_V3_03)
elseif(BUILD_DISPLAY_VERSION STREQUAL "v3_04")
    # Default v3_04 selection.
    # display_srcs and display_def defaults for v3_04
    set(display_srcs "display_mcu_v3_04.c")
    set(display_def BUILD_DISPLAY_V3_04)
    # If a developer explicitly requests to use the grok fix copy, allow
    # enabling it with a cmake flag: -DUSE_GROK_FIX=ON.  Otherwise do not
    # pick up ad-hoc grok folders accidentally.
    if(USE_GROK_FIX)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../grok fix 3_04_01/display_mcu_v3_04_01.c")
            message(STATUS "Using grok-fix display_mcu_v3_04_01.c from grok fix 3_04_01 (USE_GROK_FIX=ON)")
            set(display_srcs "../grok fix 3_04_01/display_mcu_v3_04_01.c")
            set(display_def BUILD_DISPLAY_V3_04_01)
        else()
            message(WARNING "USE_GROK_FIX=ON but grok fix file not found: ${CMAKE_CURRENT_LIST_DIR}/../grok fix 3_04_01/display_mcu_v3_04_01.c")
        endif()
    endif()
endif()

    # If an IDF-specific helper exists in main, include it in SRCS but avoid
    # the 'display_mcu*' pattern to prevent duplicate detection.
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/display_helpers_idf.c")
        list(APPEND display_srcs "display_helpers_idf.c")
    endif()
if(NOT DEFINED display_srcs)
    set(display_srcs "display_mcu_v3_04.c")
endif()
if(NOT DEFINED display_def)
    set(display_def BUILD_DISPLAY_V3_04)
endif()
message(STATUS "Active display sources: ${display_srcs}")
idf_component_register(SRCS ${display_srcs} "touch_calibration.c" "ui_manager.c" "ui_styles.c" "img_rocket_png.c" "splash.c" "cmds_splash.c" "cmds_cal.c" "cmds_panel.c"
                    INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${display_def} CAL_APP_SOFTWARE_SWAP_XY=0 CAL_DISPLAY_WIDTH=1280 CAL_DISPLAY_HEIGHT=800 DISPLAY_TAG="DISPLAY_v3.04" ENABLE_JD9365_ROTATION_TEST=0)

# If the configuration-based Kconfig for calibration overlay was set, map it
# into the traditional compile-time macro that the calibration module expects.
if(CONFIG_CAL_FORCE_DISABLE_OVERLAY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CAL_FORCE_DISABLE_OVERLAY=1)
endif()
if(CONFIG_CAL_DISABLE_CALIBRATION)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CAL_DISABLE_CALIBRATION=1)
endif()

# Safety check: ensure there is no duplicate splash implementation across the project.
get_filename_component(_dir_main "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
file(GLOB_RECURSE _splash_files "${CMAKE_SOURCE_DIR}/*/splash.c")
# Filter out archived/test directories that should not be part of a normal build
set(_splash_files_filter "")
foreach(_f ${_splash_files})
    # Ignore files in old_stuff, test_firmware, archive, grok fix, or managed components
    if(NOT _f MATCHES "(/old_stuff/|/test_firmware/|/archive/|grok[ _]fix|/managed_components/|/tools/old_stuff/)")
        list(APPEND _splash_files_filter ${_f})
    endif()
endforeach()
list(LENGTH _splash_files_filter _splash_count)
if(_splash_count GREATER 1)
    message(FATAL_ERROR "Multiple splash.c files detected (${_splash_count}). Ensure only the desired splash.c is included to avoid duplicate symbol definition. Files:\n${_splash_files_filter}")
endif()

# Safety check: ensure that only a single display_mcu source exists across the workspace
file(GLOB_RECURSE _display_candidates "${CMAKE_SOURCE_DIR}/*/display_mcu*.c")
# Filter out archived/test directories that should not be part of a normal build
set(_display_candidates_filter "")
foreach(_f ${_display_candidates})
    if(NOT _f MATCHES "(/old_stuff/|/test_firmware/|/archive/|grok[ _]fix|/managed_components/|/tools/old_stuff/)")
        list(APPEND _display_candidates_filter ${_f})
    endif()
endforeach()
list(LENGTH _display_candidates_filter _display_count)
if(_display_count GREATER 1)
    message(FATAL_ERROR "Multiple display_mcu*.c files detected (${_display_count}). Ensure only the desired display_mcu file is included in the build. Files:\n${_display_candidates_filter}")
endif()

# Note: archived/legacy display MCU files are stored in old_stuff/display_mcu_versions
# to avoid accidental inclusion. If you need a prior version (e.g., v3_02), copy or
# reference it from there and update the SRCS list accordingly.