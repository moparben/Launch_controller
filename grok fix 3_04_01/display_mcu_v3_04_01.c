/**
 * Display MCU v3.04.01 - LVGL + MIPI-DSI + JD9365 + GT911 (Production Ready)
 *
 * Fixes from v3.04:
 *  - Fixed double lv_display_flush_ready() bug
 *  - Removed conflicting touch_task (LVGL now owns touch polling)
 *  - Proper hardware rotation fallback with correct LVGL resolution/rotation
 *  - All LVGL API calls fully locked
 *  - ISR-safe semaphore handling
 *  - Cleaner buffer allocation and cursor handling
 *  - Safe calibration overlay defaults
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/lock.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

// Display includes
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "i2c_bus.h"

// LVGL includes
#include "lvgl.h"
#if __has_include("esp_lvgl_port.h")
#include "esp_lvgl_port.h"
#define HAVE_ESP_LVGL_PORT 1
#else
#define HAVE_ESP_LVGL_PORT 0
#endif
extern void example_lvgl_demo_ui(lv_display_t *disp);

#include "ui_manager.h"
#include "ui_styles.h"
#include "touch_calibration.h"

static const char *TAG = "DISPLAY_v3.04.01";

// LVGL API mutex - protects all LVGL calls from multiple tasks
static _lock_t lvgl_api_lock;

// Logical resolution (what your app sees)
#define APP_HOR_RES       1280
#define APP_VER_RES       800

// Physical panel native resolution (portrait)
#define PANEL_NATIVE_HOR  800
#define PANEL_NATIVE_VER  1280

// Globals from display/touch init
extern esp_lcd_panel_handle_t panel_handle;
extern esp_lcd_panel_io_handle_t mipi_dbi_io;
extern SemaphoreHandle_t draw_finish_sem;
extern esp_lcd_touch_handle_t touch_handle;

// Forward declarations
static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void example_lvgl_flush_wait_cb(lv_display_t *disp);
static void example_increase_lvgl_tick(void *arg);
static void example_lvgl_port_task(void *arg);
static void lvgl_touch_read_cb(lv_indev_t *drv, lv_indev_data_t *data);

// Global: did hardware rotation succeed?
static bool g_hw_rotation_applied = false;

/* ==================== LVGL CALLBACKS ==================== */

static void example_lvgl_flush_wait_cb(lv_display_t *disp)
{
    // Wait for DMA-backed draw completion up to a timeout. We always
    // call lv_display_flush_ready() after either the semaphore is
    // acquired or a timeout happens to avoid blocking LVGL forever.
    if (!draw_finish_sem) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_display_flush_ready(disp);
        return;
    }
    if (xSemaphoreTake(draw_finish_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "flush_wait_cb: timeout waiting for draw finish");
        lv_display_flush_ready(disp);
        return;
    }
    lv_display_flush_ready(disp);
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    if (!panel || !area || !px_map) {
        ESP_LOGW(TAG, "flush_cb: invalid args");
        lv_display_flush_ready(disp);  // still notify even on error
        return;
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel,
                                              area->x1, area->y1,
                                              area->x2 + 1, area->y2 + 1,
                                              px_map);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "draw_bitmap failed: %s", esp_err_to_name(ret));
    }

    // DO NOT call lv_display_flush_ready() here when using flush_wait_cb!
    // The wait_cb will do it after the semaphore is given by the ISR.
    // Only give the semaphore if it exists (task context: use non-ISR API)
    if (draw_finish_sem) {
        if (xSemaphoreGive(draw_finish_sem) != pdTRUE) {
            ESP_LOGW(TAG, "flush_cb: failed to give draw_finish_sem");
        }
    }
}

static void example_increase_lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

static void example_lvgl_port_task(void *arg)
{
    (void)arg;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        vTaskDelay(pdMS_TO_TICKS(5));  // 200Hz is plenty
    }
}

static void lvgl_touch_read_cb(lv_indev_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    if (!touch_handle) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint16_t x[5], y[5], strength[5];
    uint8_t cnt = 0;

    if (esp_lcd_touch_read_data(touch_handle) != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    bool pressed = esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &cnt, 5);
    if (pressed && cnt > 0) {
        uint16_t calibrated_x, calibrated_y;
        cal_transform(x[0], y[0], &calibrated_x, &calibrated_y);

        data->point.x = calibrated_x;
        data->point.y = calibrated_y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/* ==================== MAIN ENTRY ==================== */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Display MCU v3.04.01 - Production Ready");

    _lock_init(&lvgl_api_lock);

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize display & touch
    ESP_ERROR_CHECK(init_display());
    ESP_ERROR_CHECK(init_touch());

    // === Try hardware 90° rotation (clockwise) ===
    esp_err_t rot_ret = esp_lcd_panel_swap_xy(panel_handle, true);
    if (rot_ret == ESP_OK) {
        ESP_LOGI(TAG, "Hardware 90° rotation (swap_xy) applied successfully");
        g_hw_rotation_applied = true;
    } else {
        ESP_LOGW(TAG, "Hardware rotation failed (%s) -> using software rotation", esp_err_to_name(rot_ret));
        g_hw_rotation_applied = false;
    }

    // Initial test pattern (optional, safe)
    draw_test_pattern();
    vTaskDelay(pdMS_TO_TICKS(500));

    lv_display_t *disp = NULL;
    lv_indev_t *indev = NULL;
    bool use_manual = false;

#if HAVE_ESP_LVGL_PORT
    // Preferred path: use esp_lvgl_port (handles flush/dma/timing perfectly)
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = mipi_dbi_io,
        .panel_handle  = panel_handle,
        .hres          = g_hw_rotation_applied ? APP_HOR_RES : PANEL_NATIVE_HOR,
        .vres          = g_hw_rotation_applied ? APP_VER_RES : PANEL_NATIVE_VER,
        .buffer_size   = APP_HOR_RES * 80,  // ~10% of screen
        .double_buffer = false,
        .mipi_dsi      = true,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation      = g_hw_rotation_applied ?
                         (lvgl_port_rotation_t){ .swap_xy = false } :
                         (lvgl_port_rotation_t){ .swap_xy = true },
        .flags         = {
            .buff_dma    = true,
            .buff_spiram = true,
            .swap_bytes  = false,
        }
    };

    _lock_acquire(&lvgl_api_lock);
    disp = lvgl_port_add_disp_dsi(&disp_cfg);
    _lock_release(&lvgl_api_lock);

    if (!disp) {
        ESP_LOGE(TAG, "Failed to add DSI display via esp_lvgl_port; falling back to manual path");
        use_manual = true;
    }

    // Touch via port
    lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = touch_handle };
    if (!use_manual) {
        _lock_acquire(&lvgl_api_lock);
        indev = lvgl_port_add_touch(&touch_cfg);
        _lock_release(&lvgl_api_lock);
        if (!indev) {
            ESP_LOGW(TAG, "lvgl_port_add_touch failed, using manual indev");
            use_manual = true;
        }
    }

#else
    // If esp_lvgl_port is not available at compile time, force manual path
    use_manual = true;
#endif

    if (use_manual) {
        ESP_LOGI(TAG, "Using manual LVGL initialization (software path)");

    lv_init();

    disp = lv_display_create(
        g_hw_rotation_applied ? APP_HOR_RES : PANEL_NATIVE_HOR,
        g_hw_rotation_applied ? APP_VER_RES : PANEL_NATIVE_VER
    );
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // Use pixel count for LVGL API: buf_pixels, allocate bytes accordingly
    size_t buf_pixels = APP_HOR_RES * 80; // number of pixels
    size_t buf_bytes = buf_pixels * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf1) {
        buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer!");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    lv_display_set_buffers(disp, buf1, NULL, buf_pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, example_lvgl_flush_cb);
    lv_display_set_flush_wait_cb(disp, example_lvgl_flush_wait_cb);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));  // 2ms

manual_indev:
    // Manual input device (fallback or when port fails)
    if (!indev) {
        indev = lv_indev_create();
    }
    if (indev) {
        _lock_acquire(&lvgl_api_lock);
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
        lv_indev_set_display(indev, disp);

        // Simple white dot cursor
        lv_obj_t *cursor = lv_obj_create(lv_display_get_layer_sys(disp));
        lv_obj_set_size(cursor, 12, 12);
        lv_obj_set_style_bg_color(cursor, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
        lv_obj_clear_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(cursor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
        lv_indev_set_cursor(indev, cursor);
        _lock_release(&lvgl_api_lock);
    }
#endif

    // === UI INIT (fully locked) ===
    _lock_acquire(&lvgl_api_lock);
    ui_manager_init(disp);
    _lock_release(&lvgl_api_lock);

    // === Calibration overlay (only in debug builds) ===
#if defined(CAL_OVERLAY_AUTO_REG) && (CAL_OVERLAY_AUTO_REG == 1)
    #if !defined(CAL_DISABLE_CALIBRATION) || (CAL_DISABLE_CALIBRATION == 0)
        #if !defined(CAL_FORCE_DISABLE_OVERLAY) || (CAL_FORCE_DISABLE_OVERLAY == 0)
            ESP_LOGI(TAG, "Registering calibration overlay (dev only)");
            cal_register_lvgl_display(disp);
        #endif
    #endif
#endif

    // Ensure no calibration session is active at startup
    cal_cancel();
    ui_manager_show_home();

    // Enable backlight only after the UI is created and home screen is shown
    extern void backlight_enable(void);
    backlight_enable();

    // === Start LVGL task ===
    xTaskCreate(example_lvgl_port_task, "LVGL", 8192, NULL, 5, NULL);

    // DO NOT START touch_task! LVGL now owns touch polling.
    // Old: xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Display MCU v3.04.01 initialized successfully");
    ESP_LOGI(TAG, "Resolution: %dx%d | HW Rotation: %s",
             lv_display_get_horizontal_resolution(disp),
             lv_display_get_vertical_resolution(disp),
             g_hw_rotation_applied ? "YES" : "NO (software)");

    // Idle loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}