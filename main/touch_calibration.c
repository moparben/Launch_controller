/**
 * Simple Touch Calibration System - Implementation
 * 
 * Clean 3-point calibration with affine transform
 */

#include "touch_calibration.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
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

static void draw_filled_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!panel_handle || !draw_finish_sem) return;
    
    // Clamp coordinates
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CAL_DISPLAY_WIDTH) x1 = CAL_DISPLAY_WIDTH;
    if (y1 > CAL_DISPLAY_HEIGHT) y1 = CAL_DISPLAY_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;
    
    int width = x1 - x0;
    int height = y1 - y0;
    
    // Allocate line buffer
    uint16_t *line = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) return;
    
    // Fill line with color
    for (int i = 0; i < width; i++) {
        line[i] = color;
    }
    
    // Draw line by line
    for (int y = y0; y < y1; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, x0, y, x1, y + 1, line);
        xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
    }
    
    free(line);
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

static uint16_t observed_min_x = 0xFFFF;
static uint16_t observed_max_x = 0x0000;
static uint16_t observed_min_y = 0xFFFF;
static uint16_t observed_max_y = 0x0000;

void cal_update_bounds(uint16_t raw_x, uint16_t raw_y)
{
    if (raw_x < observed_min_x) observed_min_x = raw_x;
    if (raw_x > observed_max_x) observed_max_x = raw_x;
    if (raw_y < observed_min_y) observed_min_y = raw_y;
    if (raw_y > observed_max_y) observed_max_y = raw_y;
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
    ESP_LOGI(TAG, "Initializing calibration system");
    current_state = CAL_STATE_IDLE;
    
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
}

void cal_start(void)
{
    ESP_LOGI(TAG, "Starting calibration sequence");
    
    // Reset all points
    for (int i = 0; i < 3; i++) {
        cal_points[i].touch_x = 0;
        cal_points[i].touch_y = 0;
        cal_points[i].captured = false;
    }
    
    transform.valid = false;
    current_state = CAL_STATE_POINT_1;
    last_touch_time = 0;
    
    // Draw initial calibration screen
    cal_draw_screen();
}

void cal_cancel(void)
{
    ESP_LOGI(TAG, "Calibration cancelled");
    current_state = CAL_STATE_IDLE;
}

cal_state_t cal_get_state(void)
{
    return current_state;
}

bool cal_is_valid(void)
{
    return transform.valid;
}

bool cal_process_touch(uint16_t raw_x, uint16_t raw_y)
{
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
        } else {
            ESP_LOGE(TAG, "Transform computation failed - restarting calibration");
            cal_start();
            return true;
        }
    }
    
    // Redraw screen for next point
    cal_draw_screen();
    return true;
}

bool cal_transform(uint16_t raw_x, uint16_t raw_y, uint16_t *disp_x, uint16_t *disp_y)
{
    if (!disp_x || !disp_y) return false;
    
    if (!transform.valid) {
        // Default transform: try to map raw observed range to display
        // If we have observed min/max bounds, use them; otherwise use
        // the legacy CAL_TOUCH_WIDTH/CAL_TOUCH_HEIGHT mapping.
        if (has_observed_bounds()) {
            // Normalize raw coordinates to 0..1
            float nx = (float)(raw_x - observed_min_x) / (float)(observed_max_x - observed_min_x);
            float ny = (float)(raw_y - observed_min_y) / (float)(observed_max_y - observed_min_y);
            // We assume touch axes are swapped from display (90deg CCW)
            float fx = ny * (float)CAL_DISPLAY_WIDTH;
            float fy = (1.0f - nx) * (float)CAL_DISPLAY_HEIGHT;
            if (fx < 0) fx = 0; if (fx > CAL_DISPLAY_WIDTH - 1) fx = CAL_DISPLAY_WIDTH - 1;
            if (fy < 0) fy = 0; if (fy > CAL_DISPLAY_HEIGHT - 1) fy = CAL_DISPLAY_HEIGHT - 1;
            *disp_x = (uint16_t)(fx + 0.5f);
            *disp_y = (uint16_t)(fy + 0.5f);
            return false;
        }
        // Legacy fallback
        *disp_x = raw_y * CAL_DISPLAY_WIDTH / CAL_TOUCH_WIDTH;
        *disp_y = (CAL_TOUCH_WIDTH - raw_x) * CAL_DISPLAY_HEIGHT / CAL_TOUCH_WIDTH;
        return false;
    }
    
    // Apply calibrated transform
    float fx = transform.ax * raw_x + transform.bx * raw_y + transform.cx;
    float fy = transform.ay * raw_x + transform.by * raw_y + transform.cy;
    
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
    if (!panel_handle) return;
    
    // Clear screen to dark blue
    draw_filled_rect(0, 0, CAL_DISPLAY_WIDTH, CAL_DISPLAY_HEIGHT, 0x0010);
    
    if (current_state == CAL_STATE_COMPLETE) {
        // Show success screen
        draw_filled_rect(CAL_DISPLAY_WIDTH/2 - 150, CAL_DISPLAY_HEIGHT/2 - 50,
                        CAL_DISPLAY_WIDTH/2 + 150, CAL_DISPLAY_HEIGHT/2 + 50,
                        0x07E0); // Green box
        ESP_LOGI(TAG, "Calibration complete! Touch anywhere to continue.");
        return;
    }
    
    if (current_state == CAL_STATE_IDLE) {
        // Show "Touch to calibrate" message area
        draw_filled_rect(CAL_DISPLAY_WIDTH/2 - 150, CAL_DISPLAY_HEIGHT/2 - 30,
                        CAL_DISPLAY_WIDTH/2 + 150, CAL_DISPLAY_HEIGHT/2 + 30,
                        0xFFFF); // White box
        return;
    }
    
    // Draw title area
    draw_filled_rect(10, 10, 200, 60, 0x001F); // Blue header
    
    // Draw all calibration points
    for (int i = 0; i < 3; i++) {
        cal_point_t *pt = &cal_points[i];
        
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
    int current_idx = current_state - CAL_STATE_POINT_1;
    if (current_idx >= 0 && current_idx < 3) {
        cal_point_t *target = &cal_points[current_idx];
        
        // Draw large crosshair at target
        draw_crosshair(target->disp_x, target->disp_y, 50, CAL_COLOR_TARGET);
        
        // Draw outer circle
        draw_circle_outline(target->disp_x, target->disp_y, 40, CAL_COLOR_TARGET);
        
        ESP_LOGI(TAG, "Touch point %d at approximately (%d, %d)", 
                 current_idx + 1, target->disp_x, target->disp_y);
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
    return ESP_OK;
}

const touch_transform_t* cal_get_transform(void)
{
    return &transform;
}
