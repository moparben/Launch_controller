/* removed early forward declaration */
/**
 * Simple Touch Calibration System - Implementation
 * 
 * Clean 3-point calibration with affine transform
 */

#include "touch_calibration.h"
#if HAVE_TOUCH_CAL_LVGL
#include "lvgl.h"
#endif
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CAL";

// NVS namespace and keys
#define CAL_NVS_NAMESPACE   "touch_cal"
#define CAL_NVS_KEY_VALID   "valid"
#define CAL_NVS_KEY_AX      "ax"
#define CAL_NVS_KEY_BX      "bx"
#define CAL_NVS_KEY_CX      "cx"
#define CAL_NVS_KEY_AY      "ay"
#define CAL_NVS_KEY_BY      "by"
#define CAL_NVS_KEY_CY      "cy"

// External references from main display code
extern esp_lcd_panel_handle_t panel_handle;
extern SemaphoreHandle_t draw_finish_sem;

// Calibration target positions (3-point calibration)
// Using corners that are easily reachable and provide good coverage
#define CAL_MARGIN      100

static cal_point_t cal_points[3] = {
    // Point 1: Top-left area
    { .disp_x = CAL_MARGIN, .disp_y = CAL_MARGIN, 
      .touch_x = 0, .touch_y = 0, .captured = false },
    // Point 2: Top-right area  
    { .disp_x = CAL_DISPLAY_WIDTH - CAL_MARGIN, .disp_y = CAL_MARGIN,
      .touch_x = 0, .touch_y = 0, .captured = false },
    // Point 3: Center-bottom
    { .disp_x = CAL_DISPLAY_WIDTH / 2, .disp_y = CAL_DISPLAY_HEIGHT - CAL_MARGIN,
      .touch_x = 0, .touch_y = 0, .captured = false }
};

/* LVGL overlay forward declarations were moved to the implementation block
 * further below to avoid duplicate/forward declarations and reduce
 * preprocessor complexity. */

static touch_transform_t transform = {
    .ax = 0.0f, .bx = 1.0f, .cx = 0.0f,
    .ay = 1.0f, .by = 0.0f, .cy = 0.0f,
    .valid = false
};

static cal_state_t current_state = CAL_STATE_IDLE;

// Touch debounce
static uint32_t last_touch_time = 0;
#define TOUCH_DEBOUNCE_MS   500

// ============================================================================
// Drawing helpers
// ============================================================================

static uint16_t *cal_line_buf = NULL; static size_t cal_line_buf_cap = 0;
static uint16_t *cal_hw_line_buf = NULL; static size_t cal_hw_line_buf_cap = 0;

// Mutex to protect overlay and LVGL object state across RTOS tasks
static SemaphoreHandle_t cal_mutex = NULL;
// Mutex for drawing buffer usage (must be declared before functions that use it)
static SemaphoreHandle_t cal_draw_mutex = NULL;

static void draw_filled_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (cal_draw_mutex) xSemaphoreTake(cal_draw_mutex, pdMS_TO_TICKS(100));
    if (!panel_handle) return;
    
    // Clamp coordinates
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CAL_DISPLAY_WIDTH) x1 = CAL_DISPLAY_WIDTH;
    if (y1 > CAL_DISPLAY_HEIGHT) y1 = CAL_DISPLAY_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;
    
    int width = x1 - x0;
    // int height = y1 - y0; // Removed unused variable
    
    // Allocate/reserve static line buffer to minimize repeated allocations
    if ((size_t)width * sizeof(uint16_t) > cal_line_buf_cap) {
        if (cal_line_buf) heap_caps_free(cal_line_buf);
        cal_line_buf = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!cal_line_buf) return;
        cal_line_buf_cap = width * sizeof(uint16_t);
    }
    uint16_t *line = cal_line_buf;
    
    // Fill line with color
    for (int i = 0; i < width; i++) {
        line[i] = color;
    }
    
    // Draw line by line
    #if CAL_APP_SOFTWARE_SWAP_XY
    // App coordinate space is swapped; map app rect to hardware coordinates
    // Hardware coords: hw_x = y, hw_y = x
    int hw_x0 = y0;
    int hw_x1 = y1;
    int hw_y0 = x0;
    int hw_y1 = x1;
    if (hw_x1 <= hw_x0 || hw_y1 <= hw_y0) {
        return;
    }
    int hw_width = hw_x1 - hw_x0;
    // Allocate/reserve static hw buffer
    if ((size_t)hw_width * sizeof(uint16_t) > cal_hw_line_buf_cap) {
        if (cal_hw_line_buf) heap_caps_free(cal_hw_line_buf);
        cal_hw_line_buf = heap_caps_malloc(hw_width * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!cal_hw_line_buf) {
            return;
        }
        cal_hw_line_buf_cap = hw_width * sizeof(uint16_t);
    }
    uint16_t *hw_line = cal_hw_line_buf;
    for (int i = 0; i < hw_width; i++) hw_line[i] = color;
    for (int y = hw_y0; y < hw_y1; y++) {
        esp_err_t r = esp_lcd_panel_draw_bitmap(panel_handle, hw_x0, y, hw_x1, y + 1, hw_line);
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "draw_filled_rect: esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(r));
            break;
        }
        if (draw_finish_sem) {
            if (xSemaphoreTake(draw_finish_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW(TAG, "draw_filled_rect: wait for draw finish timed out");
            }
        }
    }
    // hw_line kept allocated for reuse
    #else
    for (int y = y0; y < y1; y++) {
        esp_err_t r = esp_lcd_panel_draw_bitmap(panel_handle, x0, y, x1, y + 1, line);
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "draw_filled_rect: esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(r));
            break;
        }
        if (draw_finish_sem) {
            if (xSemaphoreTake(draw_finish_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW(TAG, "draw_filled_rect: wait for draw finish timed out");
            }
        }
    }
    // line kept allocated for reuse
    #endif
    if (cal_draw_mutex) xSemaphoreGive(cal_draw_mutex);
}

static void draw_crosshair(int cx, int cy, int size, uint16_t color)
{
    int half = size / 2;
    int thick = 4;
    
    // Horizontal line
    draw_filled_rect(cx - half, cy - thick/2, cx + half, cy + thick/2, color);
    // Vertical line
    draw_filled_rect(cx - thick/2, cy - half, cx + thick/2, cy + half, color);
    // Center dot (different color for visibility)
    draw_filled_rect(cx - 3, cy - 3, cx + 3, cy + 3, 0xFFE0); // Yellow center
}

/* cal_debug_draw_point is now defined further below after the LVGL overlay implementation
 * so it can reference the static LVGL overlay variables and functions. */

static void draw_circle_outline(int cx, int cy, int radius, uint16_t color)
{
    // Simple circle using midpoint algorithm - draw as thick lines
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int dist_sq = x*x + y*y;
            int r_inner = (radius - 2) * (radius - 2);
            int r_outer = (radius + 2) * (radius + 2);
            if (dist_sq >= r_inner && dist_sq <= r_outer) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && px < CAL_DISPLAY_WIDTH && 
                    py >= 0 && py < CAL_DISPLAY_HEIGHT) {
                    draw_filled_rect(px, py, px + 1, py + 1, color);
                }
            }
        }
    }
}

static void draw_number(int x, int y, int num, uint16_t color)
{
    // Simple 5x7 pixel numbers
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
        {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}  // 9
    };
    
    if (num < 0 || num > 9) return;
    
    int scale = 3;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = digits[num][row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                draw_filled_rect(x + col * scale, y + row * scale,
                               x + col * scale + scale, y + row * scale + scale,
                               color);
            }
        }
    }
}

// ============================================================================
// Raw bounds tracking (for devices like GT9271 with unknown raw ranges)
// ============================================================================

// Forward declaration for function used above
static bool has_observed_bounds(void);

static uint16_t observed_min_x = 0xFFFF;
static uint16_t observed_max_x = 0x0000;
static uint16_t observed_min_y = 0xFFFF;
static uint16_t observed_max_y = 0x0000;
static bool observed_bounds_reported = false;
static volatile bool overlay_enabled = false; // default: don't show overlay; enable only during calibration
// (cal_mutex and cal_draw_mutex declared earlier)

#define CAL_MUTEX_TAKE(timeoutMs) do { if (cal_mutex) xSemaphoreTake(cal_mutex, pdMS_TO_TICKS(timeoutMs)); } while(0)
#define CAL_MUTEX_GIVE() do { if (cal_mutex) xSemaphoreGive(cal_mutex); } while(0)

#if HAVE_TOUCH_CAL_LVGL
// When LVGL display is registered, calibration overlay will be created on
// the LVGL screen layer via lv_async_call to avoid calling LVGL APIs from
// non-LVGL tasks.
static lv_display_t *cal_lv_display = NULL;
// Forward declarations for LVGL callback functions (used before definition)
static void cal_lvgl_update_overlay(void *arg);
static void cal_lvgl_invalidate_sys_layer_cb(void *arg);
static lv_obj_t *cal_overlay = NULL;
static lv_obj_t *cal_point_objs[3] = { NULL, NULL, NULL };
static lv_obj_t *cal_number_labels[3] = { NULL, NULL, NULL };
static lv_obj_t *cal_target_obj = NULL;

static void cal_lvgl_destroy_overlay(void *arg)
{
    LV_UNUSED(arg);
    CAL_MUTEX_TAKE(100);
    if (!cal_overlay) {
        CAL_MUTEX_GIVE();
        return;
    }
    lv_obj_del_async(cal_overlay);
    cal_overlay = NULL;
    for (int i = 0; i < 3; i++) {
        cal_point_objs[i] = NULL;
        cal_number_labels[i] = NULL;
    }
    cal_target_obj = NULL;
    // Schedule an LVGL async call to refresh/clear the underlying layer
    if (cal_lv_display) {
        // Avoid holding the cal_mutex during the async call
        CAL_MUTEX_GIVE();
        lv_async_call(cal_lvgl_update_overlay, NULL);
        lv_async_call(cal_lvgl_invalidate_sys_layer_cb, NULL);
    } else {
        CAL_MUTEX_GIVE();
    }
}

// LVGL async callback to invalidate the system layer and force redraw
static void cal_lvgl_invalidate_sys_layer_cb(void *arg)
{
    LV_UNUSED(arg);
    if (!cal_lv_display) return;
    lv_obj_invalidate(lv_display_get_layer_sys(cal_lv_display));
}

static void cal_lvgl_create_overlay(void *arg)
{
    LV_UNUSED(arg);
    /* Prevent overlay creation if force-disabled at compile time */
#if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
    ESP_LOGW(TAG, "cal_lvgl_create_overlay: overlay creation requested but CAL_FORCE_DISABLE_OVERLAY=1; skipping.");
    return;
#endif
    CAL_MUTEX_TAKE(100);
    if (!cal_lv_display) {
        CAL_MUTEX_GIVE();
        return;
    }
    if (cal_overlay) {
        CAL_MUTEX_GIVE();
        return; // already created
    }

    lv_obj_t *sys_layer = lv_display_get_layer_sys(cal_lv_display);
    cal_overlay = lv_obj_create(sys_layer);
    lv_obj_set_size(cal_overlay, CAL_DISPLAY_WIDTH, CAL_DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(cal_overlay, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(cal_overlay, LV_OPA_20, 0);
    lv_obj_add_flag(cal_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Create point objects and labels
    for (int i = 0; i < 3; i++) {
        cal_point_objs[i] = lv_obj_create(cal_overlay);
        lv_obj_set_size(cal_point_objs[i], 8, 8);
        lv_obj_set_style_radius(cal_point_objs[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cal_point_objs[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(cal_point_objs[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(cal_point_objs[i], LV_OBJ_FLAG_CLICKABLE);

        cal_number_labels[i] = lv_label_create(cal_overlay);
        lv_label_set_text_fmt(cal_number_labels[i], "%d", i + 1);
        lv_obj_set_style_text_color(cal_number_labels[i], lv_color_white(), 0);
    }

    // Create a target marker for the current point
    cal_target_obj = lv_obj_create(cal_overlay);
    lv_obj_set_size(cal_target_obj, 50, 50);
    lv_obj_set_style_border_color(cal_target_obj, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(cal_target_obj, 2, 0);
    lv_obj_set_style_radius(cal_target_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(cal_target_obj, LV_OBJ_FLAG_CLICKABLE);
    ESP_LOGI(TAG, "cal_lvgl_create_overlay: created overlay on display=%p", (void*)cal_lv_display);
    CAL_MUTEX_GIVE();
}

static void cal_lvgl_update_overlay(void *arg)
{
    LV_UNUSED(arg);
    /* Don't update overlay if force-disabled */
#if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
    return;
#endif
    CAL_MUTEX_TAKE(100);
    if (!cal_overlay || !cal_lv_display) {
        CAL_MUTEX_GIVE();
        return;
    }

    // Update placements and colors for points
    for (int i = 0; i < 3; i++) {
        cal_point_t *pt = &cal_points[i];
        if (!cal_point_objs[i]) continue;
        lv_obj_set_pos(cal_point_objs[i], pt->disp_x - 4, pt->disp_y - 4);
        lv_obj_set_pos(cal_number_labels[i], pt->disp_x - 8, pt->disp_y + 12);
        if (pt->captured) {
            lv_obj_set_style_bg_color(cal_point_objs[i], lv_palette_main(LV_PALETTE_GREEN), 0);
        } else {
            lv_obj_set_style_bg_color(cal_point_objs[i], lv_palette_darken(LV_PALETTE_GREY, 2), 0);
        }
    }

    // Highlight current target
    int current_idx = current_state - CAL_STATE_POINT_1;
    if (current_idx >= 0 && current_idx < 3 && cal_target_obj) {
        cal_point_t *target = &cal_points[current_idx];
        lv_obj_set_pos(cal_target_obj, target->disp_x - 25, target->disp_y - 25);
        lv_obj_set_style_border_color(cal_target_obj, lv_palette_main(LV_PALETTE_ORANGE), 0);
    ESP_LOGI(TAG, "cal_lvgl_update_overlay: updated overlay state (current=%d)", current_idx);
    CAL_MUTEX_GIVE();
    }
}

// Timer-based delete callback to remove temporary LVGL debug objects
static void cal_lvgl_delete_obj_cb(lv_timer_t *t)
{
    lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
    if (obj) lv_obj_del(obj);
    lv_timer_del(t);
}

void cal_register_lvgl_display(lv_display_t *display)
{
#if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
    ESP_LOGW(TAG, "cal_register_lvgl_display: registration requested but CAL_FORCE_DISABLE_OVERLAY=1; skipping registration");
    return;
#endif
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    ESP_LOGW(TAG, "cal_register_lvgl_display: registration requested but CAL_DISABLE_CALIBRATION=1; skipping registration");
    return;
#endif
    CAL_MUTEX_TAKE(100);
    cal_lv_display = display;
    CAL_MUTEX_GIVE();
}
#if HAVE_TOUCH_CAL_LVGL
void cal_debug_draw_point(uint16_t x, uint16_t y, bool mapped)
{
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    (void)x; (void)y; (void)mapped;
    return;
#endif
    uint16_t color = mapped ? CAL_COLOR_CAPTURED : CAL_COLOR_CURSOR;
    CAL_MUTEX_TAKE(100);
    if (cal_lv_display) {
        // Use LVGL overlay to draw ephemeral debug point
        struct lv_debug_point_args {
            uint16_t x, y;
            lv_color_t color;
        };
        struct lv_debug_point_args *args = heap_caps_malloc(sizeof(*args), MALLOC_CAP_SPIRAM);
        if (!args) return;
        args->x = x;
        args->y = y;
        args->color = mapped ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED);

        void lv_debug_point_draw(void *ctx) {
#if CAL_FORCE_DISABLE_OVERLAY
            (void)x; (void)y; (void)mapped; return;
#endif
            struct lv_debug_point_args *a = (struct lv_debug_point_args*)ctx;
            if (!cal_overlay) {
                // create overlay then re-schedule update
                cal_lvgl_create_overlay(NULL);
            }
            if (!cal_overlay) return;
            lv_obj_t *p = lv_obj_create(cal_overlay);
            lv_obj_set_size(p, 12, 12);
            lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(p, a->color, 0);
            lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
            lv_obj_set_pos(p, a->x - 6, a->y - 6);
            // Delete after 400ms using a timer
            lv_timer_t *del_t = lv_timer_create(cal_lvgl_delete_obj_cb, 400, p);
            (void)del_t;
            // Free arguments used for this async call
            heap_caps_free(a);
        }

        lv_async_call(lv_debug_point_draw, args);
        CAL_MUTEX_GIVE();
        return;
    }
    CAL_MUTEX_GIVE();
    if (!panel_handle) return;
    draw_crosshair(x, y, 12, color);
}
#else
/* Non-LVGL case: simple draw via panel framebuffer */
void cal_debug_draw_point(uint16_t x, uint16_t y, bool mapped)
{
    uint16_t color = mapped ? CAL_COLOR_CAPTURED : CAL_COLOR_CURSOR;
    if (!panel_handle) return;
    draw_crosshair(x, y, 12, color);
}
#endif
#endif // HAVE_TOUCH_CAL_LVGL

void cal_update_bounds(uint16_t raw_x, uint16_t raw_y)
{
    if (raw_x < observed_min_x) observed_min_x = raw_x;
    if (raw_x > observed_max_x) observed_max_x = raw_x;
    if (raw_y < observed_min_y) observed_min_y = raw_y;
    if (raw_y > observed_max_y) observed_max_y = raw_y;
    // If we have reasonable bounds and not yet reported, log for debug
    if (has_observed_bounds() && !observed_bounds_reported) {
        ESP_LOGI(TAG, "Observed raw range X: %d..%d, Y: %d..%d", observed_min_x, observed_max_x, observed_min_y, observed_max_y);
        observed_bounds_reported = true;
    }
}

void cal_set_bounds(uint16_t min_x, uint16_t max_x, uint16_t min_y, uint16_t max_y)
{
    if (min_x < max_x && min_y < max_y) {
        observed_min_x = min_x;
        observed_max_x = max_x;
        observed_min_y = min_y;
        observed_max_y = max_y;
        observed_bounds_reported = false; // permit logging once
        ESP_LOGI(TAG, "Calibration bounds seeded: X=%d..%d Y=%d..%d", observed_min_x, observed_max_x, observed_min_y, observed_max_y);
    }
}

void cal_set_overlay(bool enable)
{
    /*
     * If overlays have been force-disabled at compile time, reject any
     * runtime request that attempts to enable them. This ensures that
     * a runtime call (cal_start() or otherwise) cannot accidentally
     * cause an overlay to be drawn on production/embedded builds.
     */
#if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
    if (enable) {
        ESP_LOGW(TAG, "cal_set_overlay(true) requested but CAL_FORCE_DISABLE_OVERLAY=1; ignoring.");
        overlay_enabled = false;
        return;
    }
#endif
    // If the calibration subsystem is disabled entirely, ignore attempts
    // to enable overlay and log the request for diagnostics.
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    if (enable) {
        ESP_LOGW(TAG, "cal_set_overlay(true) requested but CAL_DISABLE_CALIBRATION=1; ignoring.");
        overlay_enabled = false;
        return;
    }
#endif

    CAL_MUTEX_TAKE(100);
    bool prev = overlay_enabled;
    overlay_enabled = enable;
    CAL_MUTEX_GIVE();

    // If we've just disabled the overlay, erase any overlay pixels from the
    // panel. We intentionally avoid drawing the calibration UI here to prevent
    // the calibration blue background appearing (which was the root cause of
    // the persistent 'blue overlay' artifact after reboots/crashes). Instead
    // we erase by forcing an overlay destroy (LVGL) or a neutral panel clear
    // (non-LVGL). Apps that require a more specific 'redraw' should provide
    // their own hook to repaint the normal UI after this call.
    if (!enable && prev) {
        if (!cal_lv_display) {
            // Non-LVGL drawing path - clear with a neutral color (black)
            if (panel_handle) {
                draw_filled_rect(0, 0, CAL_DISPLAY_WIDTH, CAL_DISPLAY_HEIGHT, 0x0000);
            }
        } else {
            // LVGL path - schedule overlay destruction and an LVGL redraw
            lv_async_call(cal_lvgl_destroy_overlay, NULL);
            lv_async_call(cal_lvgl_update_overlay, NULL);
        }
    }
}

bool cal_get_overlay(void)
{
    bool val = false;
    CAL_MUTEX_TAKE(100);
    val = overlay_enabled;
    CAL_MUTEX_GIVE();
    return val;
}

// Return true if we have reasonable observed bounds (avoid defaults)
static bool has_observed_bounds(void)
{
    return (observed_max_x > observed_min_x + 32) && (observed_max_y > observed_min_y + 32);
}

// ============================================================================
// Transform computation
// ============================================================================

static bool compute_transform(void)
{
    // 3-point affine transform:
    // We have 3 points with known display (Dx, Dy) and touch (Tx, Ty) coordinates
    // We want to find: Dx = ax*Tx + bx*Ty + cx
    //                  Dy = ay*Tx + by*Ty + cy
    
    // This is a system of 3 equations for each axis:
    // | Tx0 Ty0 1 | | ax |   | Dx0 |
    // | Tx1 Ty1 1 | | bx | = | Dx1 |
    // | Tx2 Ty2 1 | | cx |   | Dx2 |
    
    float T[3][3] = {
        { (float)cal_points[0].touch_x, (float)cal_points[0].touch_y, 1.0f },
        { (float)cal_points[1].touch_x, (float)cal_points[1].touch_y, 1.0f },
        { (float)cal_points[2].touch_x, (float)cal_points[2].touch_y, 1.0f }
    };
    
    float Dx[3] = { (float)cal_points[0].disp_x, 
                    (float)cal_points[1].disp_x, 
                    (float)cal_points[2].disp_x };
    float Dy[3] = { (float)cal_points[0].disp_y, 
                    (float)cal_points[1].disp_y, 
                    (float)cal_points[2].disp_y };
    
    // Compute determinant
    float det = T[0][0] * (T[1][1]*T[2][2] - T[1][2]*T[2][1])
              - T[0][1] * (T[1][0]*T[2][2] - T[1][2]*T[2][0])
              + T[0][2] * (T[1][0]*T[2][1] - T[1][1]*T[2][0]);
    
    if (fabsf(det) < 0.001f) {
        ESP_LOGE(TAG, "Singular matrix - calibration points are collinear");
        return false;
    }
    
    // Compute inverse using cofactor method
    float inv[3][3];
    inv[0][0] = (T[1][1]*T[2][2] - T[1][2]*T[2][1]) / det;
    inv[0][1] = (T[0][2]*T[2][1] - T[0][1]*T[2][2]) / det;
    inv[0][2] = (T[0][1]*T[1][2] - T[0][2]*T[1][1]) / det;
    inv[1][0] = (T[1][2]*T[2][0] - T[1][0]*T[2][2]) / det;
    inv[1][1] = (T[0][0]*T[2][2] - T[0][2]*T[2][0]) / det;
    inv[1][2] = (T[0][2]*T[1][0] - T[0][0]*T[1][2]) / det;
    inv[2][0] = (T[1][0]*T[2][1] - T[1][1]*T[2][0]) / det;
    inv[2][1] = (T[0][1]*T[2][0] - T[0][0]*T[2][1]) / det;
    inv[2][2] = (T[0][0]*T[1][1] - T[0][1]*T[1][0]) / det;
    
    // Compute coefficients for X transform
    transform.ax = inv[0][0]*Dx[0] + inv[0][1]*Dx[1] + inv[0][2]*Dx[2];
    transform.bx = inv[1][0]*Dx[0] + inv[1][1]*Dx[1] + inv[1][2]*Dx[2];
    transform.cx = inv[2][0]*Dx[0] + inv[2][1]*Dx[1] + inv[2][2]*Dx[2];
    
    // Compute coefficients for Y transform
    transform.ay = inv[0][0]*Dy[0] + inv[0][1]*Dy[1] + inv[0][2]*Dy[2];
    transform.by = inv[1][0]*Dy[0] + inv[1][1]*Dy[1] + inv[1][2]*Dy[2];
    transform.cy = inv[2][0]*Dy[0] + inv[2][1]*Dy[1] + inv[2][2]*Dy[2];
    
    transform.valid = true;
    
    ESP_LOGI(TAG, "Transform computed:");
    ESP_LOGI(TAG, "  X = %.4f*tx + %.4f*ty + %.2f", transform.ax, transform.bx, transform.cx);
    ESP_LOGI(TAG, "  Y = %.4f*tx + %.4f*ty + %.2f", transform.ay, transform.by, transform.cy);
    
    // Verify by transforming the calibration points back
    for (int i = 0; i < 3; i++) {
        uint16_t test_x, test_y;
        cal_transform(cal_points[i].touch_x, cal_points[i].touch_y, &test_x, &test_y);
        int err_x = abs((int)test_x - cal_points[i].disp_x);
        int err_y = abs((int)test_y - cal_points[i].disp_y);
        ESP_LOGI(TAG, "  Point %d: expected (%d,%d), got (%d,%d), error (%d,%d)",
                 i+1, cal_points[i].disp_x, cal_points[i].disp_y, test_x, test_y, err_x, err_y);
    }
    
    return true;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t cal_init(void)
{
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    ESP_LOGW(TAG, "cal_init: Calibration subsystem disabled via CAL_DISABLE_CALIBRATION=1");
    current_state = CAL_STATE_IDLE;
    transform.valid = true; // explain: keep transform valid to avoid auto start
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Initializing calibration system");
    current_state = CAL_STATE_IDLE;

    // Create mutex to protect calibration state
    if (!cal_mutex) {
        cal_mutex = xSemaphoreCreateMutex();
        if (!cal_mutex) {
            ESP_LOGW(TAG, "cal_init: Failed to create cal_mutex (continuing without it)");
        }
    }
    if (!cal_draw_mutex) {
        cal_draw_mutex = xSemaphoreCreateMutex();
        if (!cal_draw_mutex) {
            ESP_LOGW(TAG, "cal_init: Failed to create cal_draw_mutex (continuing without it)");
        }
    }
    
    // Try to load existing calibration
    if (cal_load() == ESP_OK && transform.valid) {
        ESP_LOGI(TAG, "Loaded existing calibration from NVS");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "No valid calibration found - will need calibration");
    // Reset observed bounds
    observed_min_x = 0xFFFF;
    observed_min_y = 0xFFFF;
    observed_max_x = 0x0000;
    observed_max_y = 0x0000;
    return ESP_OK;
#endif
}

void cal_start(void)
{
    ESP_LOGI(TAG, "Starting calibration sequence");
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    ESP_LOGW(TAG, "cal_start: Calibration subsystem disabled via CAL_DISABLE_CALIBRATION=1; ignoring start request");
    return;
#endif
    
    // Reset all points
    for (int i = 0; i < 3; i++) {
        cal_points[i].touch_x = 0;
        cal_points[i].touch_y = 0;
        cal_points[i].captured = false;
    }
    
    transform.valid = false;
    current_state = CAL_STATE_POINT_1;
    last_touch_time = 0;
    
    // Enable overlay for calibration UI and draw initial calibration screen
    // If overlays are force-disabled, log and skip overlay changes so
    // we still run calibration without a visual overlay
#if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
    ESP_LOGW(TAG, "cal_start() called but overlays are force-disabled (CAL_FORCE_DISABLE_OVERLAY=1); running calibration without UI overlay");
    cal_set_overlay(false);
#else
    cal_set_overlay(true);
#endif
    // If LVGL is registered, schedule overlay creation; else draw sync
#if HAVE_TOUCH_CAL_LVGL
    if (cal_lv_display) {
        lv_async_call(cal_lvgl_create_overlay, NULL);
        lv_async_call(cal_lvgl_update_overlay, NULL);
    } else {
        cal_draw_screen();
    }
#else
    cal_draw_screen();
#endif
}

void cal_cancel(void)
{
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    ESP_LOGW(TAG, "cal_cancel: Calibration subsystem disabled via CAL_DISABLE_CALIBRATION=1; ignoring cancel request");
    return;
#endif
    ESP_LOGI(TAG, "Calibration cancelled");
    current_state = CAL_STATE_IDLE;
    // Ensure overlay is disabled and erase any overlay pixels without
    // drawing the calibration UI (to avoid the blue splash). The overwrite
    // behavior is handled by cal_set_overlay(false) which will either
    // schedule the LVGL destroy or perform a neutral panel clear.
    cal_set_overlay(false);
#if HAVE_TOUCH_CAL_LVGL
    if (cal_lv_display && cal_overlay) {
        lv_async_call(cal_lvgl_destroy_overlay, NULL);
    }
#endif
}

cal_state_t cal_get_state(void)
{
    cal_state_t st;
    CAL_MUTEX_TAKE(100);
    st = current_state;
    CAL_MUTEX_GIVE();
    return st;
}

bool cal_is_valid(void)
{
    bool v;
    CAL_MUTEX_TAKE(100);
    v = transform.valid;
    CAL_MUTEX_GIVE();
    return v;
}

bool cal_process_touch(uint16_t raw_x, uint16_t raw_y)
{
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    return false; // no calibration processing
#endif
    // Only process during active calibration
    if (current_state < CAL_STATE_POINT_1 || current_state > CAL_STATE_POINT_3) {
        return false;
    }
    
    // Debounce
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_touch_time < TOUCH_DEBOUNCE_MS) {
        return true; // Consumed but ignored
    }
    last_touch_time = now;
    
    int point_idx = current_state - CAL_STATE_POINT_1;
    CAL_MUTEX_TAKE(100);
    
    ESP_LOGI(TAG, "Point %d captured: raw(%d, %d) -> display(%d, %d)",
             point_idx + 1, raw_x, raw_y,
             cal_points[point_idx].disp_x, cal_points[point_idx].disp_y);
    
    cal_points[point_idx].touch_x = raw_x;
    cal_points[point_idx].touch_y = raw_y;
    cal_points[point_idx].captured = true;

    // Update observed bounds so the default mapping can converge
    cal_update_bounds(raw_x, raw_y);
    
    // Move to next state
    if (current_state == CAL_STATE_POINT_1) {
        current_state = CAL_STATE_POINT_2;
    } else if (current_state == CAL_STATE_POINT_2) {
        current_state = CAL_STATE_POINT_3;
    } else if (current_state == CAL_STATE_POINT_3) {
        // All points captured - compute transform
        if (compute_transform()) {
            current_state = CAL_STATE_COMPLETE;
            ESP_LOGI(TAG, "Calibration complete!");
            
            // Auto-save
            if (cal_save() == ESP_OK) {
                ESP_LOGI(TAG, "Calibration saved to NVS");
            }
            /* Disable overlay after successful calibration */
            cal_set_overlay(false);
#if HAVE_TOUCH_CAL_LVGL
            if (cal_lv_display && cal_overlay) {
                lv_async_call(cal_lvgl_destroy_overlay, NULL);
            }
#endif
        } else {
            ESP_LOGE(TAG, "Transform computation failed - restarting calibration");
            cal_start();
            CAL_MUTEX_GIVE();
            return true;
        }
    }
    
    // Redraw screen for next point
    // If LVGL overlay is enabled and LVGL is registered, schedule an async update
#if HAVE_TOUCH_CAL_LVGL
    if (cal_get_overlay() && cal_lv_display) {
        // release the lock before scheduling LVGL async update
        CAL_MUTEX_GIVE();
        lv_async_call(cal_lvgl_update_overlay, NULL);
    } else {
        cal_draw_screen();
        CAL_MUTEX_GIVE();
    }
#else
    cal_draw_screen();
#endif
    return true;
}

bool cal_transform(uint16_t raw_x, uint16_t raw_y, uint16_t *disp_x, uint16_t *disp_y)
{
    if (!disp_x || !disp_y) return false;
    // Copy values we need under lock to avoid races with cal_process_touch
    touch_transform_t local_transform;
    uint16_t local_observed_min_x, local_observed_max_x, local_observed_min_y, local_observed_max_y;
    bool local_transform_valid;
    CAL_MUTEX_TAKE(100);
    local_transform = transform;
    local_observed_min_x = observed_min_x;
    local_observed_max_x = observed_max_x;
    local_observed_min_y = observed_min_y;
    local_observed_max_y = observed_max_y;
    local_transform_valid = transform.valid;
    CAL_MUTEX_GIVE();
#if defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1)
    // Simple mapping: normalize raw coordinates to display using default touch range
    *disp_x = raw_x * CAL_DISPLAY_WIDTH / CAL_TOUCH_WIDTH;
    *disp_y = raw_y * CAL_DISPLAY_HEIGHT / CAL_TOUCH_HEIGHT;
    return false;
#endif
    
    if (!local_transform_valid) {
        // Default transform: try to map raw observed range to display
        // If we have observed min/max bounds, use them; otherwise use
        // the legacy CAL_TOUCH_WIDTH/CAL_TOUCH_HEIGHT mapping.
        if ((local_observed_max_x > local_observed_min_x + 32) && (local_observed_max_y > local_observed_min_y + 32)) {
            // Normalize raw coordinates to 0..1
            float nx = (float)(raw_x - local_observed_min_x) / (float)(local_observed_max_x - local_observed_min_x);
            float ny = (float)(raw_y - local_observed_min_y) / (float)(local_observed_max_y - local_observed_min_y);
            // Map raw normalized axes to display axes without swapping.
            // With landscape orientation and our defaults the touch axes align
            // with the display axis; therefore use straight mapping.
            #if CAL_APP_SOFTWARE_SWAP_XY
            // Raw axes are native portrait; app expects landscape, so swap
            float fx = ny * (float)CAL_DISPLAY_WIDTH;
            float fy = nx * (float)CAL_DISPLAY_HEIGHT;
            #else
            float fx = nx * (float)CAL_DISPLAY_WIDTH;
            float fy = ny * (float)CAL_DISPLAY_HEIGHT;
            #endif
            // normalized clamps (block-form below)
                        if (fx < 0) {
                            fx = 0;
                        }
                        if (fx > CAL_DISPLAY_WIDTH - 1) {
                            fx = CAL_DISPLAY_WIDTH - 1;
                        }
                        if (fy < 0) {
                            fy = 0;
                        }
                        if (fy > CAL_DISPLAY_HEIGHT - 1) {
                            fy = CAL_DISPLAY_HEIGHT - 1;
                        }
            // normalized clamps (block-form above)
            *disp_x = (uint16_t)(fx + 0.5f);
            *disp_y = (uint16_t)(fy + 0.5f);
            return false;
        }
        // Legacy fallback: assume raw axes line up with display axes
        #if CAL_APP_SOFTWARE_SWAP_XY
        *disp_x = raw_y * CAL_DISPLAY_WIDTH / CAL_TOUCH_HEIGHT;
        *disp_y = raw_x * CAL_DISPLAY_HEIGHT / CAL_TOUCH_WIDTH;
        #else
        *disp_x = raw_x * CAL_DISPLAY_WIDTH / CAL_TOUCH_WIDTH;
        *disp_y = raw_y * CAL_DISPLAY_HEIGHT / CAL_TOUCH_HEIGHT;
        #endif
        return false;
    }
    
    // Apply calibrated transform
    float fx = local_transform.ax * raw_x + local_transform.bx * raw_y + local_transform.cx;
    float fy = local_transform.ay * raw_x + local_transform.by * raw_y + local_transform.cy;
    
    // Clamp to display bounds
    if (fx < 0) fx = 0;
    if (fx > CAL_DISPLAY_WIDTH - 1) fx = CAL_DISPLAY_WIDTH - 1;
    if (fy < 0) fy = 0;
    if (fy > CAL_DISPLAY_HEIGHT - 1) fy = CAL_DISPLAY_HEIGHT - 1;
    
    *disp_x = (uint16_t)(fx + 0.5f);
    *disp_y = (uint16_t)(fy + 0.5f);
    return true;
}

void cal_draw_screen(void)
{
#if CAL_FORCE_DISABLE_OVERLAY || (defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1))
    return;
#endif
#if HAVE_TOUCH_CAL_LVGL
    // Copy state under mutex and act outside lock
    CAL_MUTEX_TAKE(100);
    cal_state_t local_state = current_state;
    bool local_overlay = overlay_enabled;
    bool has_lv_display = (cal_lv_display != NULL);
    cal_point_t local_points[3];
    memcpy(local_points, cal_points, sizeof(local_points));
    CAL_MUTEX_GIVE();
    if (has_lv_display && local_overlay) {
        if (!cal_overlay) lv_async_call(cal_lvgl_create_overlay, NULL);
        lv_async_call(cal_lvgl_update_overlay, NULL);
        return;
    }
#endif
    if (!panel_handle) return;
    // If the overlay is disabled and we are idle, don't draw the
    // calibration UI. This prevents the calibration 'blue' background
    // from being force-painted during startup or when the app cancels
    // calibration. In the non-LVGL path, any overlay clean-up should
    // be performed by cal_set_overlay(false) (which uses a neutral
    // black fill by default) or the application can provide a custom
    // redraw to restore its UI.
    if (!local_overlay && local_state == CAL_STATE_IDLE) {
        return;
    }

    // Clear screen to dark blue (calibration background)
    draw_filled_rect(0, 0, CAL_DISPLAY_WIDTH, CAL_DISPLAY_HEIGHT, 0x0010);
    
    /* overlays are already disabled by CAL_FORCE_DISABLE_OVERLAY */
    if (local_state == CAL_STATE_COMPLETE) {
        // Show success screen
        draw_filled_rect(CAL_DISPLAY_WIDTH/2 - 150, CAL_DISPLAY_HEIGHT/2 - 50,
                        CAL_DISPLAY_WIDTH/2 + 150, CAL_DISPLAY_HEIGHT/2 + 50,
                        0x07E0); // Green box
        ESP_LOGI(TAG, "Calibration complete! Touch anywhere to continue.");
        return;
    }
    #if CAL_FORCE_DISABLE_OVERLAY || (defined(CAL_DISABLE_CALIBRATION) && (CAL_DISABLE_CALIBRATION == 1))
        (void)panel_handle;
        return;
    #endif
    
    if (local_state == CAL_STATE_IDLE) {
        // Show "Touch to calibrate" message area
        draw_filled_rect(CAL_DISPLAY_WIDTH/2 - 150, CAL_DISPLAY_HEIGHT/2 - 30,
                        CAL_DISPLAY_WIDTH/2 + 150, CAL_DISPLAY_HEIGHT/2 + 30,
                        0xFFFF); // White box
        return;
    }
    
    // Draw title area
    draw_filled_rect(10, 10, 200, 60, 0x001F); // Blue header
    
    // Draw all calibration points from local copy
    for (int i = 0; i < 3; i++) {
        cal_point_t *pt = &local_points[i];
        
        if (pt->captured) {
            // Draw completed point - green circle with checkmark
            draw_circle_outline(pt->disp_x, pt->disp_y, 30, CAL_COLOR_CAPTURED);
            draw_filled_rect(pt->disp_x - 5, pt->disp_y - 5, 
                           pt->disp_x + 5, pt->disp_y + 5, CAL_COLOR_CAPTURED);
        } else {
            // Draw uncaptured point - gray circle
            draw_circle_outline(pt->disp_x, pt->disp_y, 30, 0x4208); // Gray
        }
        
        // Draw point number
        draw_number(pt->disp_x - 8, pt->disp_y + 40, i + 1, 0xFFFF);
    }
    
    // Highlight current target with animated crosshair
    int current_idx = local_state - CAL_STATE_POINT_1;
    if (current_idx >= 0 && current_idx < 3) {
        cal_point_t target = local_points[current_idx];
        
        // Draw large crosshair at target
        draw_crosshair(target.disp_x, target.disp_y, 50, CAL_COLOR_TARGET);
        
        // Draw outer circle
        draw_circle_outline(target.disp_x, target.disp_y, 40, CAL_COLOR_TARGET);
        
        ESP_LOGI(TAG, "Touch point %d at approximately (%d, %d)", 
             current_idx + 1, target.disp_x, target.disp_y);
    }
    
    // Draw progress indicator at bottom
    int bar_width = CAL_DISPLAY_WIDTH - 40;
    int bar_x = 20;
    int bar_y = CAL_DISPLAY_HEIGHT - 50;
    int bar_height = 20;
    
    // Background bar
    draw_filled_rect(bar_x, bar_y, bar_x + bar_width, bar_y + bar_height, 0x2104);
    
    // Progress fill
    int progress = current_idx;
    if (progress > 0) {
        int fill_width = (bar_width * progress) / 3;
        draw_filled_rect(bar_x, bar_y, bar_x + fill_width, bar_y + bar_height, 0x07E0);
    }
}

esp_err_t cal_save(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save all coefficients as 32-bit integers (scaled by 10000 for precision)
    int32_t ax = (int32_t)(transform.ax * 10000.0f);
    int32_t bx = (int32_t)(transform.bx * 10000.0f);
    int32_t cx = (int32_t)(transform.cx * 10000.0f);
    int32_t ay = (int32_t)(transform.ay * 10000.0f);
    int32_t by = (int32_t)(transform.by * 10000.0f);
    int32_t cy = (int32_t)(transform.cy * 10000.0f);
    
    nvs_set_i32(handle, CAL_NVS_KEY_AX, ax);
    nvs_set_i32(handle, CAL_NVS_KEY_BX, bx);
    nvs_set_i32(handle, CAL_NVS_KEY_CX, cx);
    nvs_set_i32(handle, CAL_NVS_KEY_AY, ay);
    nvs_set_i32(handle, CAL_NVS_KEY_BY, by);
    nvs_set_i32(handle, CAL_NVS_KEY_CY, cy);
    nvs_set_u8(handle, CAL_NVS_KEY_VALID, transform.valid ? 1 : 0);
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return ret;
}

esp_err_t cal_load(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CAL_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    uint8_t valid = 0;
    ret = nvs_get_u8(handle, CAL_NVS_KEY_VALID, &valid);
    if (ret != ESP_OK || valid == 0) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    int32_t ax, bx, cx, ay, by, cy;
    nvs_get_i32(handle, CAL_NVS_KEY_AX, &ax);
    nvs_get_i32(handle, CAL_NVS_KEY_BX, &bx);
    nvs_get_i32(handle, CAL_NVS_KEY_CX, &cx);
    nvs_get_i32(handle, CAL_NVS_KEY_AY, &ay);
    nvs_get_i32(handle, CAL_NVS_KEY_BY, &by);
    nvs_get_i32(handle, CAL_NVS_KEY_CY, &cy);
    
    nvs_close(handle);
    
    transform.ax = ax / 10000.0f;
    transform.bx = bx / 10000.0f;
    transform.cx = cx / 10000.0f;
    transform.ay = ay / 10000.0f;
    transform.by = by / 10000.0f;
    transform.cy = cy / 10000.0f;
    transform.valid = true;
    
    ESP_LOGI(TAG, "Loaded calibration from NVS:");
    ESP_LOGI(TAG, "  X = %.4f*tx + %.4f*ty + %.2f", transform.ax, transform.bx, transform.cx);
    ESP_LOGI(TAG, "  Y = %.4f*tx + %.4f*ty + %.2f", transform.ay, transform.by, transform.cy);
    
    return ESP_OK;
}

esp_err_t cal_clear(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    
    transform.valid = false;
    ESP_LOGI(TAG, "Calibration cleared from NVS");
#if HAVE_TOUCH_CAL_LVGL
    if (cal_lv_display && cal_overlay) {
        lv_async_call(cal_lvgl_destroy_overlay, NULL);
    }
#endif
    return ESP_OK;
}

const touch_transform_t* cal_get_transform(void)
{
    return &transform;
}
