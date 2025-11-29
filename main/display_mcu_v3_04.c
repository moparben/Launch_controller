/**
 * Display MCU v3.04 - LVGL integration with rotation support
 *
 * Based on display_mcu_v3_03.c with hardware rotation support for
 * JD9365 MIPI-DSI panel and touch coordinate transformation.
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

// BUILD_DISPLAY_V3_03 is defined via CMake to select app_main in this file
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
// Our simple calibration system
#include "touch_calibration.h"

static const char *TAG = "DISPLAY_v3.04";

// LVGL API mutex - protect all calls into LVGL from different tasks
static _lock_t lvgl_api_lock;

// Display dimensions (application coordinate space: portrait)
#define DISPLAY_ROTATE_90 1

#if DISPLAY_ROTATE_90
#define DISPLAY_WIDTH       1280
#define DISPLAY_HEIGHT      800
#else
#define DISPLAY_WIDTH       800
#define DISPLAY_HEIGHT      1280
#endif
// Native physical panel resolution (portrait)
#define DISPLAY_NATIVE_WIDTH     800
#define DISPLAY_NATIVE_HEIGHT    1280

// Globals from the original file use the same names so we can reuse
extern esp_lcd_panel_handle_t panel_handle;
extern esp_lcd_panel_io_handle_t mipi_dbi_io;
extern SemaphoreHandle_t draw_finish_sem;
extern esp_lcd_touch_handle_t touch_handle;
extern void touch_task(void *pvParameters);

/* Panel callback that notifies LVGL that the flush is done - not used by
 * the manual fallback; left for future use by an ISR-safe registration.
 */

// Forward declarations of helper functions used by LVGL glue
static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void example_increase_lvgl_tick(void *arg);
static void example_lvgl_port_task(void *arg);
static void lvgl_touch_read_cb(lv_indev_t * drv, lv_indev_data_t *data);

/* Wait callback for LVGL that uses the draw finish semaphore directly.
 * This will be executed in the LVGL thread while it is inside
 * `wait_for_flushing()` so it must not attempt to grab the lvgl_api_lock.
 */
static void example_lvgl_flush_wait_cb(lv_display_t *disp)
{
    ESP_LOGD(TAG, "example_lvgl_flush_wait_cb: entering (disp=%p)", (void*)disp);
    if (!draw_finish_sem) {
        /* No semaphore available -> fall back to a short wait */
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    /* Wait for the draw completion reported by the panel driver ISR */
    if (xSemaphoreTake(draw_finish_sem, portMAX_DELAY) != pdTRUE) {
        /* If the wait fails for some reason, give the task a small delay */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGD(TAG, "example_lvgl_flush_wait_cb: leaving (disp=%p)", (void*)disp);
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    if (!panel) {
        ESP_LOGW(TAG, "example_lvgl_flush_cb called with NULL panel");
        return;
    }
    ESP_LOGD(TAG, "example_lvgl_flush_cb: area=(%d,%d)-(%d,%d) panel=%p px_map=%p", (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2, (void*)panel, (void*)px_map);
    if (!area || !px_map) {
        ESP_LOGW(TAG, "example_lvgl_flush_cb: invalid arguments (area/px_map)");
        return;
    }
    esp_err_t r = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "example_lvgl_flush_cb: esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(r));
    }
    // Some panel drivers may not reliably call the color transfer "done" callback
    // (on_color_trans_done) for every draw operation. To avoid LVGL blocking in
    // wait_for_flushing(), ensure we always signal completion via the draw
    // semaphore or directly notify LVGL if no semaphore exists.
    if (draw_finish_sem) {
        ESP_LOGD(TAG, "example_lvgl_flush_cb: giving draw_finish_sem to notify LVGL");
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGive(draw_finish_sem);
        (void)xHigherPriorityTaskWoken;
    } else {
        // As a last resort, directly notify LVGL that this flush is done.
        _lock_acquire(&lvgl_api_lock);
        lv_display_flush_ready(disp);
        _lock_release(&lvgl_api_lock);
    }
    ESP_LOGD(TAG, "example_lvgl_flush_cb: done (r=%d)", r);
}

static void example_increase_lvgl_tick(void *arg)
{
    LV_UNUSED(arg);
    lv_tick_inc(2);
}

static void example_lvgl_port_task(void *arg)
{
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void lvgl_touch_read_cb(lv_indev_t * drv, lv_indev_data_t *data)
{
    (void) drv;
    if (!touch_handle) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    uint16_t touch_x[5], touch_y[5];
    uint16_t touch_strength[5];
    uint8_t touch_cnt = 0;
    esp_err_t touch_ret = esp_lcd_touch_read_data(touch_handle);
    if (touch_ret != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    bool touched = esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 5);
    if (touched && touch_cnt > 0) {
        uint16_t raw_x = touch_x[0];
        uint16_t raw_y = touch_y[0];
        uint16_t disp_x = 0, disp_y = 0;
        cal_transform(raw_x, raw_y, &disp_x, &disp_y);
        data->point.x = disp_x;
        data->point.y = disp_y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    // no buffering (just done)
}

// New entry point that creates LVGL display and indev after init_display/init_touch
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Display MCU v3.04 (LVGL with rotation)");

    // Initialize LVGL API lock
    _lock_init(&lvgl_api_lock);

    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize display from existing code
    extern esp_err_t init_display(void);
    ESP_ERROR_CHECK(init_display());

#if DISPLAY_ROTATE_90
    // Enable hardware rotation for 90° clockwise
    esp_lcd_panel_swap_xy(panel_handle, true);
    ESP_LOGI(TAG, "Display rotated 90° clockwise (hardware)");
#endif

    // Draw initial test pattern
    extern void draw_test_pattern(void);
    draw_test_pattern();

    // Initialize touch
    extern esp_err_t init_touch(void);
    ESP_ERROR_CHECK(init_touch());

    // Initialize LVGL port if available, else fallback to manual LVGL setup
#if HAVE_ESP_LVGL_PORT
    // Initialize LVGL port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* Add DSI display to LVGL using esp_lvgl_port helper. This will
     * create LVGL buffers optimised for the chosen configuration */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = mipi_dbi_io,
        .panel_handle = panel_handle,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        // Use a moderate draw buffer size (10% of screen) to balance RAM and perf
        .buffer_size = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 10),
        .double_buffer = false,
        .mipi_dsi = true,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = DISPLAY_ROTATE_90, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = false, .buff_spiram = false }
    };

    _lock_acquire(&lvgl_api_lock);
    lv_display_t *display = lvgl_port_add_disp_dsi(&disp_cfg);
    _lock_release(&lvgl_api_lock);
    if (!display) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
    }

    /* For DSI displays, the port will automatically register callbacks to
     * notify LVGL about flush completion. We don't need a dedicated semaphore.
     */

    /* Add touch input in the LVGL port so LVGL can handle input events */
    lvgl_port_touch_cfg_t touch_cfg = { .disp = display, .handle = touch_handle };
    _lock_acquire(&lvgl_api_lock);
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    _lock_release(&lvgl_api_lock);
    if (touch_indev) {
        _lock_acquire(&lvgl_api_lock);
        lv_obj_t *cursor = lv_obj_create(lv_display_get_layer_sys(display));
        if (cursor) {
            lv_obj_set_size(cursor, 12, 12);
            lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(cursor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
            lv_indev_set_cursor(touch_indev, cursor);
        }
        _lock_release(&lvgl_api_lock);
    }
    if (!touch_indev) {
        ESP_LOGW(TAG, "lvgl_port_add_touch failed; falling back to manual indev driver");
        lv_indev_t *indev = lv_indev_create();
        if (indev) {
            _lock_acquire(&lvgl_api_lock);
            /* Associate display before any cursor manipulation to avoid deref of NULL disp */
            lv_indev_set_display(indev, display);
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
            /* Create a small pointer graphic (cursor) on the system layer and attach it to the indev */
            lv_obj_t *cursor = lv_obj_create(lv_display_get_layer_sys(display));
            if (cursor) {
                lv_obj_set_size(cursor, 12, 12);
                lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
                lv_obj_clear_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(cursor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
                lv_indev_set_cursor(indev, cursor);
            }
            _lock_release(&lvgl_api_lock);
        }
    }
#else
    // Manual LVGL initialization (no esp_lvgl_port available)
    ESP_LOGI(TAG, "esp_lvgl_port not found, falling back to manual LVGL init");
    lv_init();

    // create a lvgl display
    lv_display_t *display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    // associate the panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // set color format
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    // allocate draw buffers
    size_t draw_buffer_sz = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 10) * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    if (!buf1) buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_8BIT);
    void *buf2 = NULL; // single buffer to save RAM
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);
    /* Register a flush wait callback that waits on the draw completion semaphore
     * This avoids the notifier task deadlock and makes using the driver ISR
     * semaphore the canonical path for waiting on draw completion.
     */
    lv_display_set_flush_wait_cb(display, example_lvgl_flush_wait_cb);

    // Instead of registering an ISR callback for DPI event (which requires
    // callbacks to be IRAM safe), use our semaphore-driven task to notify
    // LVGL that the flush is complete. This avoids relying on ISR-safe
    // attributes and makes the fallback path more robust.
    ESP_LOGI(TAG, "Manual LVGL fallback: using draw semaphore + task for flush notification");

    // Register touch input for LVGL manually (v9 API)
    lv_indev_t *indev = lv_indev_create();
    if (indev) {
        _lock_acquire(&lvgl_api_lock);
        lv_indev_set_display(indev, display);
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
        /* Create & attach a cursor object to the input device */
        lv_obj_t *cursor = lv_obj_create(lv_display_get_layer_sys(display));
        if (cursor) {
            lv_obj_set_size(cursor, 12, 12);
            lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(cursor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
            lv_indev_set_cursor(indev, cursor);
        }
        _lock_release(&lvgl_api_lock);
    }

    // Use esp_timer as LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2 * 1000));

    // Create LVGL task (create UI first to avoid concurrent access to LVGL)
#endif

    // Create a simple UI
    // Initialize our lightweight UI manager inspired by Brookesia
    // Call UI creation before starting the LVGL task so we avoid concurrent
    // access to LVGL object tree between the creator and lv_timer_handler.
    ESP_LOGI(TAG, "Creating UI before starting LVGL task");
    if (!display) {
        ESP_LOGE(TAG, "display is NULL - cannot initialize UI");
    } else {
        lv_obj_t *screen = lv_display_get_screen_active(display);
        ESP_LOGI(TAG, "Display pointer=%p, active_screen=%p, width=%d, height=%d", (void*)display, (void*)screen, (int)lv_display_get_horizontal_resolution(display), (int)lv_display_get_vertical_resolution(display));
        _lock_acquire(&lvgl_api_lock);
            ui_manager_init(display);
            _lock_release(&lvgl_api_lock);

    #if HAVE_TOUCH_CAL_LVGL && defined(CAL_OVERLAY_AUTO_REG) && (CAL_OVERLAY_AUTO_REG == 1)
        /*
         * Only register the LVGL display with the calibration overlay if overlays
         * are not force-disabled at build time (CAL_FORCE_DISABLE_OVERLAY).
         *
         * This prevents accidental overlays being created in production builds.
         */
        #if defined(CAL_FORCE_DISABLE_OVERLAY) && (CAL_FORCE_DISABLE_OVERLAY == 1)
            ESP_LOGW(TAG, "CAL_OVERLAY_AUTO_REG was enabled at build but overlays are force-disabled (CAL_FORCE_DISABLE_OVERLAY=1); skipping registration.");
        #else
            ESP_LOGI(TAG, "Registering LVGL display for calibration overlay (CAL_OVERLAY_AUTO_REG active).\n");
            #if !defined(CAL_DISABLE_CALIBRATION) || (CAL_DISABLE_CALIBRATION == 0)
                cal_register_lvgl_display(display);
            #else
                ESP_LOGW(TAG, "CAL_OVERLAY_AUTO_REG requested but CAL_DISABLE_CALIBRATION=1; not registering.");
            #endif
        #endif
    #endif
    }

    // If we're using the manual fallback (no esp_lvgl_port), we already set the
    // flush wait callback to use the driver semaphore. The notifier task is
    // removed to prevent re-entrancy issues and deadlocks.
    #if !HAVE_ESP_LVGL_PORT
        /* Using display flush wait callback via `example_lvgl_flush_wait_cb`.
         * The separate notifier task was removed to prevent re-entrancy
         * issues and deadlocks. For normal runtime we use the wait_cb.
         */
    #endif
    // Create LVGL task after UI has been created
        ESP_LOGI(TAG, "Starting LVGL task");
        xTaskCreate(example_lvgl_port_task, "LVGL", 8192, NULL, 2, NULL);

    // Make sure calibration overlay is explicitly disabled at startup. Some
    // older builds may have left calibration active in NVS or early init
    // calls may have started it; ensure we cancel any calibration session.
    #if !defined(CAL_DISABLE_CALIBRATION) || (CAL_DISABLE_CALIBRATION == 0)
        cal_cancel();
        // Ensure the regular UI is shown after canceling calibration
        // so we don't draw the calibration 'blue' background. This
        // will restore the LVGL UI screen (if present) via
        // ui_manager_show_home().
        ui_manager_show_home();
    #else
        ESP_LOGW(TAG, "Skipping cal_cancel() since CAL_DISABLE_CALIBRATION=1");
    #endif

    // Start the calibration/touch task from existing code
    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);

    // Loop forever
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
