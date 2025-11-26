/**
 * Simple Touch Calibration System for GT9xx (GT911/GT9271) + JD9365 Display
 * 
 * This is a clean, minimal implementation that:
 * 1. Shows 3 calibration targets (corners + center)
 * 2. Collects raw touch coordinates
 * 3. Computes a simple affine transform
 * 4. Saves/loads calibration to NVS
 * 
 * The key insight: Goodix GT9xx touch controllers may report native raw
 * coordinates in device-specific ranges (not necessarily equal to the
 * display pixel resolution). This calibration captures 3 points and
 * computes an affine transform between the raw touch coordinates and
 * display coordinates. If no calibration is present, the system will
 * estimate raw ranges from observed touches to provide a reasonable
 * default mapping.
 * but the display is rotated 90°, so we need to map touch to display coords.
 */

#ifndef TOUCH_CALIBRATION_H
#define TOUCH_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display dimensions (after rotation)
#define CAL_DISPLAY_WIDTH   800
#define CAL_DISPLAY_HEIGHT  1280

// These are defaults and used only when a calibrated transform isn't
// available. For GT9xx devices the native raw ranges may differ — the
// calibration implementation dynamically observes raw bounds and uses
// those to compute a default mapping when required.
#define CAL_TOUCH_WIDTH     1280
#define CAL_TOUCH_HEIGHT    800

// Calibration point structure
typedef struct {
    // Display coordinates (where target is drawn)
    int16_t disp_x;
    int16_t disp_y;
    // Raw touch coordinates (what GT911 reports)
    int16_t touch_x;
    int16_t touch_y;
    // Whether this point has been captured
    bool captured;
} cal_point_t;

// Affine transform coefficients
// display_x = ax * touch_x + bx * touch_y + cx
// display_y = ay * touch_x + by * touch_y + cy
typedef struct {
    float ax, bx, cx;  // X transform
    float ay, by, cy;  // Y transform
    bool valid;
} touch_transform_t;

// Calibration state machine
typedef enum {
    CAL_STATE_IDLE,           // Calibration not active
    CAL_STATE_POINT_1,        // Waiting for point 1 (top-left)
    CAL_STATE_POINT_2,        // Waiting for point 2 (top-right)
    CAL_STATE_POINT_3,        // Waiting for point 3 (center)
    CAL_STATE_COMPLETE,       // All points captured, transform computed
    CAL_STATE_TESTING         // Testing calibration accuracy
} cal_state_t;

// Colors (RGB565)
#define CAL_COLOR_BG        0x0000  // Black
#define CAL_COLOR_TARGET    0xFFFF  // White
#define CAL_COLOR_CAPTURED  0x07E0  // Green
#define CAL_COLOR_ERROR     0xF800  // Red
#define CAL_COLOR_TEXT      0xFFFF  // White
#define CAL_COLOR_CURSOR    0xF81F  // Magenta

/**
 * Initialize the calibration system
 * Loads existing calibration from NVS if available
 */
esp_err_t cal_init(void);

/**
 * Start a new calibration sequence
 * This clears any existing calibration and begins collecting points
 */
void cal_start(void);

/**
 * Cancel calibration and return to idle state
 */
void cal_cancel(void);

/**
 * Get current calibration state
 */
cal_state_t cal_get_state(void);

/**
 * Check if calibration is valid and can be used
 */
bool cal_is_valid(void);

/**
 * Process a raw touch event during calibration
 * @param raw_x Raw X coordinate from GT911
 * @param raw_y Raw Y coordinate from GT911
 * @return true if touch was processed (consumed), false otherwise
 */
bool cal_process_touch(uint16_t raw_x, uint16_t raw_y);

/**
 * Transform raw touch coordinates to display coordinates
 * @param raw_x Raw X from GT911
 * @param raw_y Raw Y from GT911
 * @param disp_x Output: transformed X for display
 * @param disp_y Output: transformed Y for display
 * @return true if transform was applied, false if using default mapping
 */
bool cal_transform(uint16_t raw_x, uint16_t raw_y, 
                   uint16_t *disp_x, uint16_t *disp_y);

/**
 * Update observed raw min/max values (used for default mapping when no
 * calibration saved). This will be called internally but may be exposed
 * for debug.
 */
void cal_update_bounds(uint16_t raw_x, uint16_t raw_y);

/**
 * Set raw min/max bounds explicitly. This seeds the automatic mapping that is
 * used while no explicit transform is stored. Callers can pass 0 for min to
 * indicate a zero-based range.
 */
void cal_set_bounds(uint16_t min_x, uint16_t max_x, uint16_t min_y, uint16_t max_y);

/**
 * Debug helper: draw a point overlay for raw/mapped coordinates.
 * If 'mapped' is false, draw as 'raw' (magenta), otherwise draw 'mapped' (green).
 */
void cal_debug_draw_point(uint16_t x, uint16_t y, bool mapped);

/**
 * Enable/disable the debug overlay for raw/mapped coordinates
 */
void cal_set_overlay(bool enable);
bool cal_get_overlay(void);

/**
 * Draw the current calibration screen
 * Should be called after cal_start() and after each cal_process_touch()
 */
void cal_draw_screen(void);

/**
 * Save current calibration to NVS
 */
esp_err_t cal_save(void);

/**
 * Load calibration from NVS
 */
esp_err_t cal_load(void);

/**
 * Clear saved calibration from NVS
 */
esp_err_t cal_clear(void);

/**
 * Get the current transform for debugging
 */
const touch_transform_t* cal_get_transform(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_CALIBRATION_H
