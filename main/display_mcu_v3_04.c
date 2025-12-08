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
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/lock.h>
#include "driver/gpio.h"
#include "i2c_bus.h"
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
#include "display_tag.h"
// Our simple calibration system
#include "touch_calibration.h"
#include "cmds_splash.h"
#include "cmds_cal.h"
#include "cmds_panel.h"

static const char *TAG = DISPLAY_TAG;

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
// Queue used to hold temporary allocated buffers (like rotation buffers)
// until the panel driver has finished DMA transferring the color data.
static QueueHandle_t rotbuf_free_q = NULL;
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
    else {
        // Free any buffers queued for release by the on_color_trans_done ISR
        void *buf_to_free = NULL;
        if (rotbuf_free_q) {
            if (xQueueReceive(rotbuf_free_q, &buf_to_free, 0) == pdTRUE) {
                if (buf_to_free) {
                    heap_caps_free(buf_to_free);
                }
            }
        }
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
    // If hardware swap was applied, LVGL coordinates are in application space
    // (1280x800) whereas the DPI panel is in hardware pixel space (800x1280).
    // We must rotate the px_map buffer and transform coordinates before drawing.
    extern bool panel_hw_swap_xy; // from display_helpers_idf.c
    esp_err_t r = ESP_OK;
    if (!panel_hw_swap_xy) {
        r = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    } else {
        // LVGL coordinates
        int lx1 = area->x1;
        int ly1 = area->y1;
        int lx2_excl = area->x2 + 1;
        int ly2_excl = area->y2 + 1;
        // LVGL display width used for mapping
        int lvw = lv_display_get_horizontal_resolution(disp);
        // Transform to hardware coordinates (90 deg clockwise): hx = ly; hy = (lvw - 1) - lx
        int hx1 = ly1;
        int hx2_excl = ly2_excl;
        int hy1 = lvw - lx2_excl; // start y
        int hy2_excl = lvw - lx1; // end y (exclusive)
        int rot_w = hx2_excl - hx1; // equals ly2_excl - ly1
        int rot_h = hy2_excl - hy1; // equals lx2_excl - lx1
            if (rot_w <= 0 || rot_h <= 0) {
            ESP_LOGW(TAG, "example_lvgl_flush_cb: invalid rotated dims: rot_w=%d rot_h=%d", rot_w, rot_h);
            r = ESP_ERR_INVALID_ARG;
        } else {
            size_t pixel_size = sizeof(lv_color_t);
            int src_w = lx2_excl - lx1;
            // Basic sanity checks on computed hardware coordinates
            if (hx1 < 0 || hy1 < 0 || hx2_excl > DISPLAY_NATIVE_WIDTH || hy2_excl > DISPLAY_NATIVE_HEIGHT) {
                ESP_LOGE(TAG, "example_lvgl_flush_cb: rotated bounds out of range: hx=(%d,%d) hy=(%d,%d) native=(%d,%d)", hx1, hx2_excl, hy1, hy2_excl, DISPLAY_NATIVE_WIDTH, DISPLAY_NATIVE_HEIGHT);
                r = ESP_ERR_INVALID_ARG;
            }
            size_t rot_size = (size_t)rot_w * rot_h * pixel_size;
            // Try to allocate a temp buffer for rotated pixels, prefer internal DMA memory
            void *rot_buf = heap_caps_malloc(rot_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!rot_buf) {
                rot_buf = heap_caps_malloc(rot_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!rot_buf) {
                ESP_LOGW(TAG, "example_lvgl_flush_cb: unable to allocate rotated buffer size=%u", (unsigned)rot_size);
                r = ESP_ERR_NO_MEM;
            } else {
                ESP_LOGD(TAG, "example_lvgl_flush_cb: rot_w=%d rot_h=%d src_w=%d rot_size=%u px_map=%p rot_buf=%p", rot_w, rot_h, src_w, (unsigned)rot_size, (void*)px_map, (void*)rot_buf);
                if (rot_size > (size_t)DISPLAY_NATIVE_WIDTH * DISPLAY_NATIVE_HEIGHT * pixel_size) {
                    ESP_LOGE(TAG, "example_lvgl_flush_cb: rotated buffer size exceeds panel native size: rot_size=%u native_max=%u", (unsigned)rot_size, (unsigned)(DISPLAY_NATIVE_WIDTH * DISPLAY_NATIVE_HEIGHT * pixel_size));
                    heap_caps_free(rot_buf);
                    r = ESP_ERR_INVALID_ARG;
                }
                // rotate pixels from px_map -> rot_buf
                uint16_t *src = (uint16_t *)px_map;
                uint16_t *dst = (uint16_t *)rot_buf;
                int src_w = lx2_excl - lx1;
                for (int sy = ly1; sy < ly2_excl; sy++) {
                    for (int sx = lx1; sx < lx2_excl; sx++) {
                        int src_x = sx - lx1;
                        int src_y = sy - ly1;
                        int dst_row = (lx2_excl - 1) - sx; // row index in rotated buffer
                        int dst_col = sy - ly1; // column index in rotated buffer
                        int dst_index = dst_row * rot_w + dst_col;
                        int src_index = src_y * src_w + src_x;
                        dst[dst_index] = src[src_index];
                    }
                }
                // Call draw with transformed coords and rotated buffer
                ESP_LOGD(TAG, "example_lvgl_flush_cb: invoking esp_lcd_panel_draw_bitmap panel=%p hx=(%d,%d)-(%d,%d) rot_buf=%p", (void*)panel, hx1, hy1, hx2_excl, hy2_excl, rot_buf);
                r = esp_lcd_panel_draw_bitmap(panel, hx1, hy1, hx2_excl, hy2_excl, rot_buf);
                // Do not free the buffer yet - wait until the panel indicates the
                // DMA transfer is complete via the on_color_trans_done callback.
                if (rotbuf_free_q) {
                    // Queue the pointer for later freeing in the LVGL wait function
                    if (xQueueSend(rotbuf_free_q, &rot_buf, 0) != pdTRUE) {
                        // Queue full - worst case free immediately (leak/overflow prevention)
                        ESP_LOGW(TAG, "rotbuf_free_q full, freeing rot_buf immediately");
                        heap_caps_free(rot_buf);
                    }
                } else {
                    heap_caps_free(rot_buf);
                }
            }
        }
    }
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "example_lvgl_flush_cb: esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(r));
    }
    // Some panel drivers may not reliably call the color transfer "done" callback
    // (on_color_trans_done) for every draw operation. To avoid LVGL blocking in
    // wait_for_flushing(), ensure we always signal completion via the draw
    // semaphore or directly notify LVGL if no semaphore exists.
    if (draw_finish_sem) {
        // Do not give the semaphore from the flush callback - the right place
        // to notify LVGL is from the panel IO event callback (on_color_trans_done).
        // This ensures the panel DMA has finished before LVGL continues.
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

// Panel callback: notify LVGL that color DMA transfer has completed. This
// callback runs in ISR context so it uses xSemaphoreGiveFromISR.
IRAM_ATTR static bool lvgl_panel_on_color_trans_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    BaseType_t need_yield = pdFALSE;
    if (sem) {
        xSemaphoreGiveFromISR(sem, &need_yield);
    }
    return (need_yield == pdTRUE);
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
    /* Early plain printf to ensure we see startup even if logging subsystem
     * hasn't been fully initialized or if log level filtering hides tags. */
    printf("app_main: starting Display MCU v3.04 (LVGL with rotation)\n");
    fflush(stdout);
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

    /* Heartbeat LED (GPIO2) to indicate that app_main has been reached. Use
     * an available pin on the dev board; you can change to a different pin
     * if your board uses a different LED. */
    gpio_config_t hb_cfg = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL<<GPIO_NUM_2), .intr_type = GPIO_INTR_DISABLE, .pull_down_en = 0, .pull_up_en = 0 };
    gpio_config(&hb_cfg);
    gpio_set_level(GPIO_NUM_2, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_NUM_2, 0);

    // Initialize display from existing code
    extern esp_err_t init_display(void);
    ESP_ERROR_CHECK(init_display());

#if DISPLAY_ROTATE_90
    // Enable hardware rotation for 90° clockwise
    esp_err_t rc_swap = esp_lcd_panel_swap_xy(panel_handle, true);
    if (rc_swap == ESP_OK) {
        ESP_LOGI(TAG, "Display rotated 90° clockwise via panel_swap_xy (hardware)");
        extern bool panel_hw_swap_xy; // defined in display_helpers_idf.c
        panel_hw_swap_xy = true;
    } else {
        ESP_LOGW(TAG, "Panel swap_xy returned %s - hardware rotation may be unsupported or failed, falling back to LVGL rotation", esp_err_to_name(rc_swap));
    }
#endif

    // Draw initial test pattern
    extern void draw_test_pattern(void);
    draw_test_pattern();

    // Create rotation buffer free queue used to hold temp buffers until the
    // panel's on_color_trans_done notifies completion. We keep a modest queue
    // size to avoid unbounded memory usage.
    rotbuf_free_q = xQueueCreate(8, sizeof(void *));
    if (!rotbuf_free_q) {
        ESP_LOGW(TAG, "rotbuf_free_q creation failed; temp rotation buffers will not be freed reliably");
    }

    // Register panel IO event callbacks to signal LVGL via the draw_finish_sem
    if (panel_handle) {
        esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = lvgl_panel_on_color_trans_done,
        };
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, draw_finish_sem));
    } else {
        ESP_LOGW(TAG, "Panel handle is NULL; cannot register on_color_trans_done callback");
    }

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
        // We rely on hardware panel swap for rotation. Keep LVGL rotation
        // disabled to avoid double-swapping which causes coordinate/refresh
        // mismatch and visual artifacts.
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
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

    // allocate draw buffers - prefer internal DMA-capable memory to avoid
    // PSRAM DMA artefacts. Fall back to SPIRAM and 8-bit allocations when
    // internal DMA-capable memory is not available.
    size_t draw_buffer_sz = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 10) * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf1) {
        buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf1) {
        buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_8BIT);
    }
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
            /* Register dev console commands for splash control (if console is available) */
            register_splash_console_cmd();
            /* Register calibration console command to allow disabling overlay at runtime */
            register_cal_console_cmd();
            /* Register panel control commands */
            register_panel_console_cmd();
            /* Register dev console commands for splash control (if console is available) */
            register_splash_console_cmd();

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
