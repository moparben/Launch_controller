/**
 * Display MCU v3.03 - LVGL integration
 *
 * Based on display_mcu_v3_02_clean.c with LVGL integration for UI and
 * LVGL input device support using the GT9xx touch controller and the
 * JD9365 MIPI-DSI panel.
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
extern void example_lvgl_demo_ui(lv_display_t *disp);

// Our simple calibration system
#include "touch_calibration.h"

static const char *TAG = "DISPLAY_v3.03";

// Display dimensions (application coordinate space: portrait)
#define DISPLAY_WIDTH       800
#define DISPLAY_HEIGHT      1280
// Native physical panel resolution (portrait)
#define DISPLAY_NATIVE_WIDTH     800
#define DISPLAY_NATIVE_HEIGHT    1280

// Globals from the original file use the same names so we can reuse
extern esp_lcd_panel_handle_t panel_handle;
extern SemaphoreHandle_t draw_finish_sem;
extern esp_lcd_touch_handle_t touch_handle;

// Forward declarations of helper functions used by LVGL glue
static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void example_increase_lvgl_tick(void *arg);
static void example_lvgl_port_task(void *arg);
static bool lvgl_touch_read_cb(lv_indev_drv_t * drv, lv_indev_data_t *data);

// Task that waits for the draw finish semaphore then notifies LVGL
static void lvgl_flush_notify_task(void *arg)
{
    lv_display_t *disp = (lv_display_t *)arg;
    while (1) {
        if (xSemaphoreTake(draw_finish_sem, portMAX_DELAY) == pdTRUE) {
            if (disp) lv_display_flush_ready(disp);
        }
    }
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    if (!panel) return;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static void example_increase_lvgl_tick(void *arg)
{
    LV_UNUSED(arg);
    lv_tick_inc(2);
}

static void example_lvgl_port_task(void *arg)
{
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool lvgl_touch_read_cb(lv_indev_drv_t * drv, lv_indev_data_t *data)
{
    (void) drv;
    if (!touch_handle) {
        data->state = LV_INDEV_STATE_REL;
        return false;
    }
    uint16_t touch_x[5], touch_y[5];
    uint16_t touch_strength[5];
    uint8_t touch_cnt = 0;
    esp_lcd_touch_read_data(touch_handle);
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
    return false; // no buffering
}

// New entry point that creates LVGL display and indev after init_display/init_touch
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Display MCU v3.03 (LVGL)");

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

    // Draw initial test pattern
    extern void draw_test_pattern(void);
    draw_test_pattern();

    // Initialize touch
    extern esp_err_t init_touch(void);
    ESP_ERROR_CHECK(init_touch());

    // Initialize LVGL
    lv_init();
    lv_display_t *display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    size_t draw_buffer_sz = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 10) * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    // Register a simple semaphore-to-LVGL path - wait on draw finish semaphore and call lv_display_flush_ready
    if (draw_finish_sem) {
        xTaskCreate(lvgl_flush_notify_task, "lvgl_notify", 2048, display, 6, NULL);
    }

    // Register LVGL event callback in the panel driver to get notified on refresh if needed
    // We won't call lv_display_flush_ready from ISR, the semaphore/notify task will handle it.

    // Create LVGL tick
    const esp_timer_create_args_t lvgl_tick_timer_args = { .callback = &example_increase_lvgl_tick, .name = "lvgl_tick" };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2 * 1000));

    // Create LVGL task
    xTaskCreate(example_lvgl_port_task, "LVGL", 4096, NULL, 5, NULL);

    // Register input device using the GT9xx touch driver read
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
    if (indev) {
        lv_indev_set_cursor(indev, NULL); // us if we had a cursor object
    }

    // Create a simple UI
    example_lvgl_demo_ui(display);

    // Start the calibration/touch task from existing code
    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);

    // Loop forever
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
