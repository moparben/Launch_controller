/*
 * Waveshare ESP32-P4 MIPI DSI Display Test
 * Based on JD9365 display controller
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
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "waveshare_mipi";

// Waveshare 10.1inch DSI LCD (C) Display Configuration
#define EXAMPLE_LCD_H_RES               1280
#define EXAMPLE_LCD_V_RES               800
#define EXAMPLE_LCD_BIT_PER_PIXEL       24

// MIPI DSI Configuration (2-lane DSI as per Waveshare specs)
#define EXAMPLE_MIPI_DSI_LANE_NUM       2
#define EXAMPLE_MIPI_DSI_LANE_BITRATE   800   // Adjusted for 1280x800@60Hz
#define EXAMPLE_MIPI_DPI_CLK_MHZ        60    // For 60Hz refresh rate

// Display timing for 1280x800 @ 60Hz (Waveshare DSI LCD C)
#define EXAMPLE_LCD_HSYNC               20
#define EXAMPLE_LCD_HBP                 80
#define EXAMPLE_LCD_HFP                 80
#define EXAMPLE_LCD_VSYNC               3
#define EXAMPLE_LCD_VBP                 10
#define EXAMPLE_LCD_VFP                 10

// Power and GPIO Configuration
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_PIN_NUM_LCD_RST         -1  // Hardware reset, set to actual pin if connected
#define EXAMPLE_PIN_NUM_BK_LIGHT        -1  // Backlight control, set to actual pin if connected

// Status LEDs
#define STATUS_LED_PIN                  48
#define DEBUG_LED_PIN                   47

static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
static uint8_t *frame_buffer = NULL;

static void init_status_leds(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << STATUS_LED_PIN) | (1ULL << DEBUG_LED_PIN)),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Status LEDs initialized");
        // Flash LEDs to show startup
        for (int i = 0; i < 5; i++) {
            gpio_set_level(STATUS_LED_PIN, 1);
            gpio_set_level(DEBUG_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(STATUS_LED_PIN, 0);
            gpio_set_level(DEBUG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

static void enable_mipi_dsi_phy_power(void)
{
    ESP_LOGI(TAG, "🔌 Enabling MIPI DSI PHY power (LDO channel %d, %dmV)", 
             EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN, EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ MIPI DSI PHY power enabled");
        gpio_set_level(STATUS_LED_PIN, 1); // Signal success
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(STATUS_LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "❌ Failed to enable MIPI DSI PHY power: %s", esp_err_to_name(ret));
    }
}

static esp_err_t init_mipi_dsi_display(void)
{
    ESP_LOGI(TAG, "📺 Initializing MIPI DSI Display (JD9365 compatible)");
    ESP_LOGI(TAG, "📐 Resolution: %dx%d, %d lanes, %d Mbps", 
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, EXAMPLE_MIPI_DSI_LANE_NUM, EXAMPLE_MIPI_DSI_LANE_BITRATE);
    
    // Create MIPI DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = EXAMPLE_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = EXAMPLE_MIPI_DSI_LANE_BITRATE,
    };
    
    esp_err_t ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create MIPI DSI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DSI bus created");
    
    // Create DBI interface for sending commands
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create DBI interface: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DBI interface created");
    
    // Create DPI panel for display data
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = EXAMPLE_MIPI_DPI_CLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .video_timing = {
            .h_size = EXAMPLE_LCD_H_RES,
            .v_size = EXAMPLE_LCD_V_RES,
            .hsync_back_porch = EXAMPLE_LCD_HBP,
            .hsync_pulse_width = EXAMPLE_LCD_HSYNC,
            .hsync_front_porch = EXAMPLE_LCD_HFP,
            .vsync_back_porch = EXAMPLE_LCD_VBP,
            .vsync_pulse_width = EXAMPLE_LCD_VSYNC,
            .vsync_front_porch = EXAMPLE_LCD_VFP,
        },
    };
    
    ret = esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create DPI panel: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DPI panel created");
    
    // Initialize and reset the display
    ret = esp_lcd_panel_reset(mipi_dpi_panel);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Display reset successful");
    } else {
        ESP_LOGW(TAG, "⚠️ Display reset failed: %s", esp_err_to_name(ret));
    }
    
    ret = esp_lcd_panel_init(mipi_dpi_panel);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Display initialization successful");
        // Flash both LEDs rapidly to indicate success
        for (int i = 0; i < 10; i++) {
            gpio_set_level(STATUS_LED_PIN, i & 1);
            gpio_set_level(DEBUG_LED_PIN, !(i & 1));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        ESP_LOGE(TAG, "❌ Display initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Turn on display
    ret = esp_lcd_panel_disp_on_off(mipi_dpi_panel, true);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Display turned ON");
    } else {
        ESP_LOGE(TAG, "❌ Failed to turn on display: %s", esp_err_to_name(ret));
    }
    
    return ESP_OK;
}

static void allocate_frame_buffer(void)
{
    ESP_LOGI(TAG, "💾 Allocating frame buffer (%dx%dx%d = %d bytes)", 
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, EXAMPLE_LCD_BIT_PER_PIXEL / 8,
             EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * (EXAMPLE_LCD_BIT_PER_PIXEL / 8));
    
    size_t frame_buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * (EXAMPLE_LCD_BIT_PER_PIXEL / 8);
    frame_buffer = heap_caps_aligned_calloc(64, 1, frame_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (frame_buffer) {
        ESP_LOGI(TAG, "✅ Frame buffer allocated successfully");
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate frame buffer");
    }
}

static void draw_test_pattern(uint8_t pattern)
{
    if (!frame_buffer || !mipi_dpi_panel) return;
    
    ESP_LOGI(TAG, "🎨 Drawing test pattern %d", pattern);
    
    size_t pixel_size = EXAMPLE_LCD_BIT_PER_PIXEL / 8;  // 3 bytes for RGB888
    
    switch (pattern) {
        case 0: // Red screen
            for (int y = 0; y < EXAMPLE_LCD_V_RES; y++) {
                for (int x = 0; x < EXAMPLE_LCD_H_RES; x++) {
                    size_t offset = (y * EXAMPLE_LCD_H_RES + x) * pixel_size;
                    frame_buffer[offset] = 0xFF;     // R
                    frame_buffer[offset + 1] = 0x00; // G
                    frame_buffer[offset + 2] = 0x00; // B
                }
            }
            break;
            
        case 1: // Green screen
            for (int y = 0; y < EXAMPLE_LCD_V_RES; y++) {
                for (int x = 0; x < EXAMPLE_LCD_H_RES; x++) {
                    size_t offset = (y * EXAMPLE_LCD_H_RES + x) * pixel_size;
                    frame_buffer[offset] = 0x00;     // R
                    frame_buffer[offset + 1] = 0xFF; // G
                    frame_buffer[offset + 2] = 0x00; // B
                }
            }
            break;
            
        case 2: // Blue screen
            for (int y = 0; y < EXAMPLE_LCD_V_RES; y++) {
                for (int x = 0; x < EXAMPLE_LCD_H_RES; x++) {
                    size_t offset = (y * EXAMPLE_LCD_H_RES + x) * pixel_size;
                    frame_buffer[offset] = 0x00;     // R
                    frame_buffer[offset + 1] = 0x00; // G
                    frame_buffer[offset + 2] = 0xFF; // B
                }
            }
            break;
            
        case 3: // White screen
            memset(frame_buffer, 0xFF, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * pixel_size);
            break;
            
        case 4: // Color bars
            for (int y = 0; y < EXAMPLE_LCD_V_RES; y++) {
                for (int x = 0; x < EXAMPLE_LCD_H_RES; x++) {
                    size_t offset = (y * EXAMPLE_LCD_H_RES + x) * pixel_size;
                    int bar = x / (EXAMPLE_LCD_H_RES / 8);
                    uint8_t colors[8][3] = {
                        {0xFF, 0xFF, 0xFF}, // White
                        {0xFF, 0xFF, 0x00}, // Yellow
                        {0x00, 0xFF, 0xFF}, // Cyan
                        {0x00, 0xFF, 0x00}, // Green
                        {0xFF, 0x00, 0xFF}, // Magenta
                        {0xFF, 0x00, 0x00}, // Red
                        {0x00, 0x00, 0xFF}, // Blue
                        {0x00, 0x00, 0x00}, // Black
                    };
                    frame_buffer[offset] = colors[bar][0];     // R
                    frame_buffer[offset + 1] = colors[bar][1]; // G
                    frame_buffer[offset + 2] = colors[bar][2]; // B
                }
            }
            break;
    }
    
    // Send frame buffer to display
    esp_err_t ret = esp_lcd_panel_draw_bitmap(mipi_dpi_panel, 0, 0, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, frame_buffer);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Pattern %d displayed", pattern);
        // Flash LED to indicate successful draw
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "❌ Failed to display pattern %d: %s", pattern, esp_err_to_name(ret));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Waveshare ESP32-P4 MIPI DSI Display Test");
    ESP_LOGI(TAG, "🎯 Testing Waveshare 10.1inch DSI LCD (C)");
    ESP_LOGI(TAG, "📱 Expected: 1280x800 MIPI DSI display, IPS panel");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize components
    init_status_leds();
    
    ESP_LOGI(TAG, "🔌 Step 1: Enable MIPI DSI PHY Power");
    enable_mipi_dsi_phy_power();
    
    ESP_LOGI(TAG, "📺 Step 2: Initialize MIPI DSI Display");
    ret = init_mipi_dsi_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "💥 Display initialization failed! Check hardware connections.");
        // Flash error pattern
        while (1) {
            gpio_set_level(STATUS_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(STATUS_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    ESP_LOGI(TAG, "💾 Step 3: Allocate Frame Buffer");
    allocate_frame_buffer();
    
    ESP_LOGI(TAG, "🎨 Step 4: Display Test Patterns");
    ESP_LOGI(TAG, "👀 WATCH YOUR DISPLAY - You should see colored screens!");
    
    // Display test patterns in a loop
    int pattern = 0;
    while (1) {
        ESP_LOGI(TAG, "🖼️ Displaying pattern %d/5", pattern + 1);
        draw_test_pattern(pattern);
        
        // Wait and flash status LED
        for (int i = 0; i < 50; i++) {  // 5 second delay
            gpio_set_level(DEBUG_LED_PIN, i & 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        pattern = (pattern + 1) % 5;
        
        if (pattern == 0) {
            ESP_LOGI(TAG, "🔄 Cycling through patterns again...");
        }
    }
}