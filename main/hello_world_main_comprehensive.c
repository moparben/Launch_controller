/*
 * Comprehensive Waveshare ESP32-P4 Pin Scanner
 * Tests many different pin combinations and backlight configurations
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

static const char *TAG = "waveshare_scan";

// Display dimensions
#define LCD_H_RES           240
#define LCD_V_RES           320
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (20 * 1000 * 1000)

// Status LEDs
#define STATUS_LED_PIN      48
#define DEBUG_LED_PIN       47

// Pin configuration structure
typedef struct {
    int sclk;
    int mosi;
    int dc;
    int rst;
    int cs;
    int backlight;
    int backlight_active;  // 0 = active low, 1 = active high
    const char* name;
} pin_config_t;

// Comprehensive Waveshare ESP32-P4 pin configurations to test
static pin_config_t pin_configs[] = {
    // Configuration 1: Standard SPI2 pins
    {12, 11, 13, 14, 10, 9, 1, "Config1: SPI2-Standard"},
    {12, 11, 13, 14, 10, 46, 1, "Config2: SPI2-Alt-BL46"},
    {12, 11, 13, 14, 10, 21, 1, "Config3: SPI2-Alt-BL21"},
    
    // Configuration 4-6: Alternative SPI2 arrangements
    {7, 6, 4, 5, 15, 46, 1, "Config4: SPI2-V2"},
    {7, 6, 4, 5, 15, 9, 1, "Config5: SPI2-V2-BL9"},
    {7, 6, 4, 5, 15, 21, 1, "Config6: SPI2-V2-BL21"},
    
    // Configuration 7-9: Common ESP32 LCD pins
    {18, 23, 2, 4, 5, 15, 1, "Config7: Common-LCD"},
    {18, 23, 2, 4, 5, 22, 1, "Config8: Common-LCD-BL22"},
    {14, 13, 12, 27, 15, 2, 1, "Config9: Alt-Common"},
    
    // Configuration 10-12: Waveshare specific variations
    {6, 7, 4, 5, 15, 46, 0, "Config10: WS-InvertBL"},
    {12, 11, 10, 9, 8, 13, 1, "Config11: WS-Sequential"},
    {8, 9, 10, 11, 12, 13, 1, "Config12: WS-Reverse"},
    
    // Configuration 13-15: More ESP32-P4 specific pins
    {20, 21, 22, 23, 19, 18, 1, "Config13: High-Pins"},
    {1, 2, 3, 8, 9, 10, 1, "Config14: Low-Pins"},
    {16, 17, 18, 19, 20, 21, 1, "Config15: Mid-Pins"},
    
    // Configuration 16-18: Try different CS/DC arrangements
    {12, 11, 4, 5, 13, 46, 1, "Config16: CS-DC-Swap"},
    {12, 11, 5, 4, 13, 46, 1, "Config17: RST-DC-Swap"},
    {11, 12, 13, 14, 10, 46, 1, "Config18: SCLK-MOSI-Swap"},
};

#define NUM_CONFIGS (sizeof(pin_configs) / sizeof(pin_config_t))

esp_lcd_panel_handle_t panel_handle = NULL;
bool display_initialized = false;

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
        // Flash both LEDs to show we're starting
        for (int i = 0; i < 3; i++) {
            gpio_set_level(STATUS_LED_PIN, 1);
            gpio_set_level(DEBUG_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(STATUS_LED_PIN, 0);
            gpio_set_level(DEBUG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        ESP_LOGI(TAG, "✅ Status LEDs ready");
    }
}

static void cleanup_display_resources(void)
{
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
        panel_handle = NULL;
    }
    spi_bus_free(LCD_HOST);
    display_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(500)); // Give time for cleanup
}

static void test_backlight_configurations(pin_config_t* config)
{
    ESP_LOGI(TAG, "💡 Testing backlight configurations for pin %d", config->backlight);
    
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << config->backlight
    };
    gpio_config(&bk_gpio_config);
    
    // Test both polarities
    for (int polarity = 0; polarity < 2; polarity++) {
        ESP_LOGI(TAG, "🔦 Backlight pin %d, polarity %s", config->backlight, polarity ? "HIGH" : "LOW");
        
        gpio_set_level(config->backlight, polarity);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Also try PWM-like pulsing
        for (int pulse = 0; pulse < 10; pulse++) {
            gpio_set_level(config->backlight, polarity);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(config->backlight, !polarity);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        gpio_set_level(config->backlight, polarity);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Set to configured polarity
    gpio_set_level(config->backlight, config->backlight_active);
}

static esp_err_t init_display_with_config(int config_index)
{
    if (config_index >= NUM_CONFIGS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pin_config_t* config = &pin_configs[config_index];
    
    ESP_LOGI(TAG, "🧪 Testing %s", config->name);
    ESP_LOGI(TAG, "📍 Pins: SCLK=%d, MOSI=%d, DC=%d, RST=%d, CS=%d, BL=%d(active_%s)",
             config->sclk, config->mosi, config->dc, config->rst, config->cs, 
             config->backlight, config->backlight_active ? "HIGH" : "LOW");

    // Test backlight first
    test_backlight_configurations(config);
    
    esp_err_t ret;

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = config->sclk,
        .mosi_io_num = config->mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ SPI init failed: %s", esp_err_to_name(ret));
        return ret;
    }

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
    };

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ Panel IO init failed: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return ret;
    }

    // Initialize LCD panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ Panel init failed: %s", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return ret;
    }

    // Reset and initialize
    esp_lcd_panel_reset(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_lcd_panel_init(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Try multiple display configurations
    ESP_LOGI(TAG, "🎨 Drawing test patterns...");
    
    for (int invert = 0; invert < 2; invert++) {
        esp_lcd_panel_invert_color(panel_handle, invert);
        esp_lcd_panel_disp_on_off(panel_handle, true);
        
        // Draw bright full-screen colors
        uint16_t test_colors[] = {0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0x0000};
        int num_colors = sizeof(test_colors) / sizeof(test_colors[0]);
        
        for (int c = 0; c < num_colors; c++) {
            uint16_t *color_buffer = malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
            if (color_buffer) {
                for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
                    color_buffer[i] = test_colors[c];
                }
                
                esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, color_buffer);
                free(color_buffer);
                
                ESP_LOGI(TAG, "🎨 Drew color 0x%04X (invert=%d) - LOOK AT DISPLAY NOW!", test_colors[c], invert);
                
                // Flash status LED to indicate when to look
                for (int flash = 0; flash < 5; flash++) {
                    gpio_set_level(STATUS_LED_PIN, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gpio_set_level(STATUS_LED_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                
                vTaskDelay(pdMS_TO_TICKS(3000)); // Long pause to check display
            }
        }
    }

    ESP_LOGI(TAG, "✅ Configuration %s completed all tests", config->name);
    display_initialized = true;
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "🔬 COMPREHENSIVE Waveshare ESP32-P4 Display Scanner");
    ESP_LOGI(TAG, "📡 Testing %d different pin configurations", NUM_CONFIGS);
    ESP_LOGI(TAG, "👀 WATCH YOUR DISPLAY - Look for ANY change in brightness or color!");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_status_leds();
    
    // Test all configurations with extended patterns
    for (int i = 0; i < NUM_CONFIGS; i++) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🔄 ===== TESTING CONFIGURATION %d/%d =====", i + 1, NUM_CONFIGS);
        ESP_LOGI(TAG, "📋 %s", pin_configs[i].name);
        ESP_LOGI(TAG, "⏰ Watch display for next 30 seconds!");
        
        // Clean up previous attempt
        cleanup_display_resources();
        
        // Blink pattern to show which config we're testing
        for (int blink = 0; blink <= i && blink < 10; blink++) {
            gpio_set_level(STATUS_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(150));
            gpio_set_level(STATUS_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        
        ret = init_display_with_config(i);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "🎉 Configuration %s may be working!", pin_configs[i].name);
            ESP_LOGI(TAG, "💡 If you see ANYTHING on the display, this is the right config!");
            
            // Extended testing time
            vTaskDelay(pdMS_TO_TICKS(10000));
        } else {
            ESP_LOGW(TAG, "❌ Configuration %s failed", pin_configs[i].name);
        }
        
        ESP_LOGI(TAG, "⏭️  Moving to next configuration in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    ESP_LOGI(TAG, "🏁 All configurations tested!");
    ESP_LOGI(TAG, "❓ Did you see ANY change on the display during testing?");
    ESP_LOGI(TAG, "📝 If yes, note which configuration number was active!");
    
    // Final status - continuous slow blink
    while (1) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}