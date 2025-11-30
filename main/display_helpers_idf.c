/*
 * IDF-specific helper definitions for display controller
 * Provides placeholder implementations for panel_handle, touch_handle,
 * cut-down display init and touch functions. This file intentionally does
 * NOT use 'display_mcu*' prefix to avoid CMake's duplicate checks.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

#include "display_tag.h"

static const char *TAG = DISPLAY_TAG;

// Define the globals expected by the display code
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
SemaphoreHandle_t draw_finish_sem = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;

// Placeholder functions used by display_mcu_v3_04.c to avoid link errors during build
esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "init_display placeholder called");
    // In production, set up the panel and mipi IO handles here.
    draw_finish_sem = xSemaphoreCreateBinary();
    return ESP_OK;
}

esp_err_t init_touch(void)
{
    ESP_LOGI(TAG, "init_touch placeholder called");
    return ESP_OK;
}

void draw_test_pattern(void)
{
    ESP_LOGD(TAG, "draw_test_pattern placeholder called");
}

void touch_task(void *pvParameters)
{
    (void) pvParameters;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
