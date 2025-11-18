/*
 * Rocket Launcher Display Controller - ESP32-P4 Safe Version
 * Minimal boot test without WiFi - displays status on screen
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"

static const char *TAG = "rocket_safe";

// Display pins (adjust these for your actual hardware)
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (20 * 1000 * 1000)
#define LCD_BK_LIGHT_ON     1
#define LCD_BK_LIGHT_OFF    0

#define PIN_NUM_SCLK        6
#define PIN_NUM_MOSI        7
#define PIN_NUM_MISO        -1  // Not used
#define PIN_NUM_LCD_DC      4
#define PIN_NUM_LCD_RST     5
#define PIN_NUM_LCD_CS      15
#define PIN_NUM_BK_LIGHT    46

// Status LED
#define STATUS_LED_PIN      48

// Display handle
esp_lcd_panel_handle_t panel_handle = NULL;

// Initialize status LED
static void init_status_led(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << STATUS_LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "✅ Status LED initialized on pin %d", STATUS_LED_PIN);
}

// Initialize display with safe error handling
static esp_err_t init_display_safe(void)
{
    ESP_LOGI(TAG, "🖥️ Initializing ST7789 display...");
    
    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 320 * sizeof(uint16_t) + 8,
    };
    
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ SPI bus initialized");
    
    // Configure LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ LCD panel IO init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ LCD panel IO initialized");
    
    // Configure LCD panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ST7789 panel init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ ST7789 panel created");
    
    // Reset and initialize panel
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    ESP_LOGI(TAG, "✅ Display initialized successfully!");
    return ESP_OK;
}

// Initialize backlight
static void init_backlight(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_NUM_BK_LIGHT)
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON);
    
    ESP_LOGI(TAG, "✅ Backlight enabled on pin %d", PIN_NUM_BK_LIGHT);
}

// Draw simple test pattern
static void draw_test_pattern(void)
{
    if (panel_handle == NULL) {
        ESP_LOGW(TAG, "⚠️ Panel not initialized, skipping test pattern");
        return;
    }
    
    ESP_LOGI(TAG, "🎨 Drawing test pattern...");
    
    // Create a simple color buffer (red screen)
    uint16_t *color_buf = malloc(240 * 50 * sizeof(uint16_t));
    if (color_buf == NULL) {
        ESP_LOGE(TAG, "❌ Failed to allocate color buffer");
        return;
    }
    
    // Fill with red color (RGB565 format: 0xF800)
    for (int i = 0; i < 240 * 50; i++) {
        color_buf[i] = 0xF800; // Red
    }
    
    // Draw red rectangle at top of screen
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 240, 50, color_buf);
    
    // Fill with green color (RGB565 format: 0x07E0)
    for (int i = 0; i < 240 * 50; i++) {
        color_buf[i] = 0x07E0; // Green
    }
    
    // Draw green rectangle in middle
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 135, 240, 185, color_buf);
    
    free(color_buf);
    ESP_LOGI(TAG, "✅ Test pattern drawn");
}

// Main application
void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Rocket Display Controller - ESP32-P4 Safe Mode");
    ESP_LOGI(TAG, "📊 Starting minimal display test...");
    
    // Initialize status LED first
    init_status_led();
    
    // Blink LED to show we're alive
    for (int i = 0; i < 5; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    ESP_LOGI(TAG, "✅ System alive - LED test complete");
    
    // Initialize backlight
    init_backlight();
    
    // Initialize display with error handling
    esp_err_t ret = init_display_safe();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Display initialization failed, continuing with LED-only mode");
        
        // Keep system running with LED status
        int counter = 0;
        while (1) {
            gpio_set_level(STATUS_LED_PIN, counter % 2);
            ESP_LOGI(TAG, "💔 Display failed - heartbeat %d", counter++);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    // Draw test pattern
    draw_test_pattern();
    
    ESP_LOGI(TAG, "🎯 System Ready! Display should show red/green test pattern");
    ESP_LOGI(TAG, "📊 Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Keep system running with status updates
    int counter = 0;
    while (1) {
        counter++;
        
        // Blink status LED
        gpio_set_level(STATUS_LED_PIN, counter % 2);
        
        // Log status every 10 seconds
        if (counter % 10 == 0) {
            ESP_LOGI(TAG, "🚀 System Status: Count=%d, Heap=%lu bytes, Uptime=%llu sec", 
                     counter, esp_get_free_heap_size(), esp_timer_get_time() / 1000000);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}