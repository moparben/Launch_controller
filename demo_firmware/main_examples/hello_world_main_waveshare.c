/*
 * Rocket Launcher Display Controller - Waveshare ESP32-P4 Specific
 * Corrected GPIO pins for Waveshare ESP32-P4 development board
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
#include "nvs_flash.h"

static const char *TAG = "rocket_waveshare";

// Waveshare ESP32-P4 LCD pins (common configurations to try)
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (20 * 1000 * 1000)
#define LCD_BK_LIGHT_ON     1
#define LCD_BK_LIGHT_OFF    0

// Waveshare ESP32-P4 typical display pins - Configuration 1
#define PIN_NUM_SCLK_1      12   // SPI Clock
#define PIN_NUM_MOSI_1      11   // SPI MOSI 
#define PIN_NUM_LCD_DC_1    13   // Data/Command
#define PIN_NUM_LCD_RST_1   14   // Reset
#define PIN_NUM_LCD_CS_1    10   // Chip Select
#define PIN_NUM_BK_LIGHT_1  9    // Backlight

// Alternative Waveshare configuration - Configuration 2
#define PIN_NUM_SCLK_2      7    // SPI Clock
#define PIN_NUM_MOSI_2      6    // SPI MOSI 
#define PIN_NUM_LCD_DC_2    4    // Data/Command
#define PIN_NUM_LCD_RST_2   5    // Reset
#define PIN_NUM_LCD_CS_2    15   // Chip Select
#define PIN_NUM_BK_LIGHT_2  46   // Backlight

// Status LEDs
#define STATUS_LED_PIN      48   // Primary status LED
#define DEBUG_LED_PIN       47   // Secondary debug LED

// Display dimensions
#define LCD_H_RES           240
#define LCD_V_RES           320

// Global state
esp_lcd_panel_handle_t panel_handle = NULL;
bool display_initialized = false;
int current_config = 0;

// Pin configuration structure
typedef struct {
    int sclk;
    int mosi;
    int dc;
    int rst;
    int cs;
    int backlight;
    const char* name;
} pin_config_t;

static pin_config_t pin_configs[] = {
    {PIN_NUM_SCLK_1, PIN_NUM_MOSI_1, PIN_NUM_LCD_DC_1, PIN_NUM_LCD_RST_1, PIN_NUM_LCD_CS_1, PIN_NUM_BK_LIGHT_1, "Config1-Standard"},
    {PIN_NUM_SCLK_2, PIN_NUM_MOSI_2, PIN_NUM_LCD_DC_2, PIN_NUM_LCD_RST_2, PIN_NUM_LCD_CS_2, PIN_NUM_BK_LIGHT_2, "Config2-Alt"},
    // Add more configurations as needed
};

#define NUM_CONFIGS (sizeof(pin_configs) / sizeof(pin_config_t))

// Initialize status LEDs
static void init_status_leds(void)
{
    ESP_LOGI(TAG, "Initializing status LEDs on pins %d and %d", STATUS_LED_PIN, DEBUG_LED_PIN);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << STATUS_LED_PIN) | (1ULL << DEBUG_LED_PIN)),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure status LEDs: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Status LEDs configured successfully");
        // Test LEDs
        gpio_set_level(STATUS_LED_PIN, 1);
        gpio_set_level(DEBUG_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(STATUS_LED_PIN, 0);
        gpio_set_level(DEBUG_LED_PIN, 0);
    }
}

// Blink status LED in patterns
static void blink_status(int pattern)
{
    for (int i = 0; i < pattern; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Cleanup previous display resources
static void cleanup_display_resources(void)
{
    if (panel_handle) {
        ESP_LOGI(TAG, "Cleaning up previous display resources");
        esp_lcd_panel_del(panel_handle);
        panel_handle = NULL;
    }
    
    // Deinitialize SPI bus if it was initialized
    spi_bus_free(LCD_HOST);
    
    display_initialized = false;
}

// Initialize display with specific pin configuration
static esp_err_t init_display_with_config(int config_index)
{
    if (config_index >= NUM_CONFIGS) {
        ESP_LOGE(TAG, "Invalid config index: %d", config_index);
        return ESP_ERR_INVALID_ARG;
    }
    
    pin_config_t* config = &pin_configs[config_index];
    current_config = config_index;
    
    ESP_LOGI(TAG, "🔧 Trying %s pinout:", config->name);
    ESP_LOGI(TAG, "   SCLK=%d, MOSI=%d, DC=%d, RST=%d, CS=%d, BL=%d",
             config->sclk, config->mosi, config->dc, config->rst, config->cs, config->backlight);

    esp_err_t ret;

    // Configure backlight first
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << config->backlight
    };
    ret = gpio_config(&bk_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure backlight GPIO %d: %s", config->backlight, esp_err_to_name(ret));
        return ret;
    }
    
    // Turn on backlight
    gpio_set_level(config->backlight, LCD_BK_LIGHT_ON);
    ESP_LOGI(TAG, "✅ Backlight enabled on pin %d", config->backlight);

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = config->sclk,
        .mosi_io_num = config->mosi,
        .miso_io_num = -1,  // Not used for display
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ SPI bus initialized");

    // Initialize LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->dc,
        .cs_gpio_num = config->cs,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return ret;
    }
    ESP_LOGI(TAG, "✅ LCD panel IO initialized");

    // Initialize LCD panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return ret;
    }
    ESP_LOGI(TAG, "✅ ST7789 panel created");

    // Reset and initialize panel
    ret = esp_lcd_panel_reset(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize panel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Try different display orientations and settings
    ESP_LOGI(TAG, "🔄 Configuring display settings...");
    
    // Try multiple orientation and inversion combinations
    for (int invert = 0; invert < 2; invert++) {
        for (int mirror_x = 0; mirror_x < 2; mirror_x++) {
            for (int mirror_y = 0; mirror_y < 2; mirror_y++) {
                esp_lcd_panel_invert_color(panel_handle, invert);
                esp_lcd_panel_mirror(panel_handle, mirror_x, mirror_y);
                esp_lcd_panel_disp_on_off(panel_handle, true);
                
                ESP_LOGI(TAG, "📺 Testing: invert=%d, mirror_x=%d, mirror_y=%d", invert, mirror_x, mirror_y);
                
                // Draw a simple test pattern
                uint16_t *test_buffer = malloc(LCD_H_RES * 50 * sizeof(uint16_t));
                if (test_buffer) {
                    // Fill with bright white
                    for (int i = 0; i < LCD_H_RES * 50; i++) {
                        test_buffer[i] = 0xFFFF; // White
                    }
                    
                    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, 50, test_buffer);
                    free(test_buffer);
                    
                    // Wait to see if anything appears
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    
                    ESP_LOGI(TAG, "🔍 White bar drawn - do you see anything?");
                    
                    // Try bright red stripe
                    test_buffer = malloc(LCD_H_RES * 50 * sizeof(uint16_t));
                    if (test_buffer) {
                        for (int i = 0; i < LCD_H_RES * 50; i++) {
                            test_buffer[i] = 0xF800; // Red
                        }
                        esp_lcd_panel_draw_bitmap(panel_handle, 0, 50, LCD_H_RES, 100, test_buffer);
                        free(test_buffer);
                        
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ESP_LOGI(TAG, "🔴 Red bar drawn - do you see anything?");
                    }
                }
            }
        }
    }

    ESP_LOGI(TAG, "✅ Display initialization completed for %s!", config->name);
    display_initialized = true;
    blink_status(1); // Success
    return ESP_OK;
}

// Test all pin configurations
static esp_err_t test_all_configurations(void)
{
    for (int i = 0; i < NUM_CONFIGS; i++) {
        ESP_LOGI(TAG, "🧪 Testing configuration %d/%d: %s", i + 1, NUM_CONFIGS, pin_configs[i].name);
        
        // Clean up any previous attempt
        cleanup_display_resources();
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        esp_err_t ret = init_display_with_config(i);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "🎉 SUCCESS with %s configuration!", pin_configs[i].name);
            
            // Give extra time to observe
            for (int j = 0; j < 10; j++) {
                ESP_LOGI(TAG, "⏱️  Configuration %s active - check display (%d/10)", pin_configs[i].name, j + 1);
                gpio_set_level(STATUS_LED_PIN, j % 2); // Blink to indicate active
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "❌ Configuration %s failed: %s", pin_configs[i].name, esp_err_to_name(ret));
            blink_status(i + 2); // Error pattern
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait between attempts
    }
    
    return ESP_FAIL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Rocket Display Controller - Waveshare ESP32-P4 Pin Scanner");
    ESP_LOGI(TAG, "🔍 Scanning for correct display pins...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize status LEDs
    init_status_leds();
    
    // Test all pin configurations
    ESP_LOGI(TAG, "🔧 Starting automatic pin configuration detection...");
    ret = test_all_configurations();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Found working configuration: %s", pin_configs[current_config].name);
        
        // Main loop with working display
        int loop_count = 0;
        while (1) {
            // Animate display to show it's working
            uint16_t *color_buffer = malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
            if (color_buffer) {
                // Cycle through colors
                uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF};
                int color_count = sizeof(colors) / sizeof(colors[0]);
                uint16_t current_color = colors[loop_count % color_count];
                
                for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
                    color_buffer[i] = current_color;
                }
                
                esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, color_buffer);
                free(color_buffer);
                
                ESP_LOGI(TAG, "🌈 Color cycle %d - displaying color 0x%04X", loop_count, current_color);
            }
            
            loop_count++;
            gpio_set_level(STATUS_LED_PIN, loop_count % 2);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    } else {
        ESP_LOGE(TAG, "💥 No working pin configuration found!");
        ESP_LOGI(TAG, "📋 Please check your Waveshare ESP32-P4 board documentation for correct LCD pins");
        
        // Error indication loop
        while (1) {
            blink_status(5); // 5 blinks = no config found
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}