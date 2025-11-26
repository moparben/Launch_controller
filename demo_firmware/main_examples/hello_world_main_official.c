/*
 * Waveshare ESP32-P4-WIFI6-DEV-KIT Official Display Test
 * Using official Waveshare display components with proper ESP-IDF API
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
#include "esp_heap_caps.h"
#include "nvs_flash.h"

// Include both display drivers
#include "esp_lcd_jd9365_10_1.h"    // 10.1" display
#include "esp_lcd_ili9881c.h"       // 7" display

static const char *TAG = "waveshare_display";

// Display configurations from Waveshare headers
#define LCD_10_1_H_RES          800   // From JD9365 config
#define LCD_10_1_V_RES          1280
#define LCD_7_H_RES             720   // From ILI9881C config  
#define LCD_7_V_RES             1280

// Current display configuration (will be set during detection)
static int current_h_res = LCD_10_1_H_RES;
static int current_v_res = LCD_10_1_V_RES;
static int display_type = 0;  // 0 = 10.1", 1 = 7"

#define LCD_BIT_PER_PIXEL       16  // RGB565 for better performance

// Status LEDs
#define STATUS_LED_PIN          48
#define DEBUG_LED_PIN           47

static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
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
        for (int i = 0; i < 3; i++) {
            gpio_set_level(STATUS_LED_PIN, 1);
            gpio_set_level(DEBUG_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(STATUS_LED_PIN, 0);
            gpio_set_level(DEBUG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    } else {
        ESP_LOGW(TAG, "⚠️ Could not initialize status LEDs: %s", esp_err_to_name(ret));
    }
}

static void signal_success(void)
{
    // Flash both LEDs rapidly to indicate success
    for (int i = 0; i < 6; i++) {
        gpio_set_level(STATUS_LED_PIN, i & 1);
        gpio_set_level(DEBUG_LED_PIN, !(i & 1));
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    gpio_set_level(STATUS_LED_PIN, 0);
    gpio_set_level(DEBUG_LED_PIN, 0);
}

static void signal_error(void)
{
    // Flash error pattern
    for (int i = 0; i < 10; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t init_mipi_dsi_bus_and_io(void)
{
    ESP_LOGI(TAG, "🚌 Creating MIPI DSI bus and IO interface");
    
    // Create MIPI DSI bus (use JD9365 config as default)
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    esp_err_t ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create MIPI DSI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DSI bus created");

    // Create MIPI DBI IO interface  
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create MIPI DBI IO: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DBI IO created");
    
    return ESP_OK;
}

static esp_err_t init_10_1_display(void)
{
    ESP_LOGI(TAG, "🖥️ Attempting 10.1\" Display (JD9365) - 800x1280");
    
    // Create DPI configuration
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    
    // Create vendor configuration
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = NULL,  // Use default commands
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };
    
    // Create panel device configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    esp_err_t ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 10.1\" display creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_reset(lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 10.1\" display reset failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    ret = esp_lcd_panel_init(lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 10.1\" display init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    ret = esp_lcd_panel_disp_on_off(lcd_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 10.1\" display turn on failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    current_h_res = LCD_10_1_H_RES;
    current_v_res = LCD_10_1_V_RES;
    display_type = 0;
    ESP_LOGI(TAG, "✅ 10.1\" Display (JD9365) initialized successfully!");
    return ESP_OK;
}

static esp_err_t init_7_display(void)
{
    ESP_LOGI(TAG, "🖥️ Attempting 7\" Display (ILI9881C) - 720x1280");
    
    // Create DPI configuration
    esp_lcd_dpi_panel_config_t dpi_config = ILI9881C_720_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    
    // Create vendor configuration
    ili9881c_vendor_config_t vendor_config = {
        .init_cmds = NULL,  // Use default commands
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };
    
    // Create panel device configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    esp_err_t ret = esp_lcd_new_panel_ili9881c(mipi_dbi_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 7\" display creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_reset(lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 7\" display reset failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    ret = esp_lcd_panel_init(lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 7\" display init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    ret = esp_lcd_panel_disp_on_off(lcd_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 7\" display turn on failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
        return ret;
    }
    
    current_h_res = LCD_7_H_RES;
    current_v_res = LCD_7_V_RES;
    display_type = 1;
    ESP_LOGI(TAG, "✅ 7\" Display (ILI9881C) initialized successfully!");
    return ESP_OK;
}

static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "📺 Auto-detecting Waveshare Display...");
    
    // First initialize MIPI DSI bus and IO interface
    esp_err_t ret = init_mipi_dsi_bus_and_io();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize MIPI DSI bus/IO");
        return ret;
    }
    
    // Try 10.1" display first (most common)
    ret = init_10_1_display();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Detected: 10.1\" Waveshare DSI LCD (800x1280)");
        signal_success();
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🔄 10.1\" failed, trying 7\" display...");
    
    // Try 7" display
    ret = init_7_display();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Detected: 7\" Waveshare DSI LCD (720x1280)");
        signal_success();
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "❌ No compatible display detected!");
    signal_error();
    return ESP_FAIL;
}

static void allocate_frame_buffer(void)
{
    size_t pixel_size = LCD_BIT_PER_PIXEL / 8;  // 2 bytes for RGB565
    size_t frame_buffer_size = current_h_res * current_v_res * pixel_size;
    
    ESP_LOGI(TAG, "💾 Allocating frame buffer for %dx%d display", current_h_res, current_v_res);
    ESP_LOGI(TAG, "📏 Buffer size: %d bytes (RGB565)", frame_buffer_size);
    
    frame_buffer = heap_caps_aligned_calloc(64, 1, frame_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (frame_buffer) {
        ESP_LOGI(TAG, "✅ Frame buffer allocated successfully in SPIRAM");
    } else {
        ESP_LOGW(TAG, "⚠️ SPIRAM failed, trying internal RAM...");
        frame_buffer = heap_caps_aligned_calloc(64, 1, frame_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (frame_buffer) {
            ESP_LOGI(TAG, "✅ Frame buffer allocated in internal RAM");
        } else {
            ESP_LOGE(TAG, "❌ Failed to allocate frame buffer!");
        }
    }
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void draw_test_pattern(uint8_t pattern)
{
    if (!frame_buffer || !lcd_panel) return;
    
    ESP_LOGI(TAG, "🎨 Drawing test pattern %d on %s display", 
             pattern, display_type == 0 ? "10.1\"" : "7\"");
    
    uint16_t *fb16 = (uint16_t *)frame_buffer;
    
    switch (pattern) {
        case 0: { // Red screen
            uint16_t red = rgb888_to_rgb565(0xFF, 0x00, 0x00);
            for (int i = 0; i < current_h_res * current_v_res; i++) {
                fb16[i] = red;
            }
            break;
        }
        
        case 1: { // Green screen
            uint16_t green = rgb888_to_rgb565(0x00, 0xFF, 0x00);
            for (int i = 0; i < current_h_res * current_v_res; i++) {
                fb16[i] = green;
            }
            break;
        }
        
        case 2: { // Blue screen
            uint16_t blue = rgb888_to_rgb565(0x00, 0x00, 0xFF);
            for (int i = 0; i < current_h_res * current_v_res; i++) {
                fb16[i] = blue;
            }
            break;
        }
        
        case 3: { // White screen
            uint16_t white = rgb888_to_rgb565(0xFF, 0xFF, 0xFF);
            for (int i = 0; i < current_h_res * current_v_res; i++) {
                fb16[i] = white;
            }
            break;
        }
        
        case 4: { // Rainbow gradient
            for (int y = 0; y < current_v_res; y++) {
                for (int x = 0; x < current_h_res; x++) {
                    uint8_t r = (x * 255) / current_h_res;
                    uint8_t g = (y * 255) / current_v_res;
                    uint8_t b = ((x + y) * 255) / (current_h_res + current_v_res);
                    fb16[y * current_h_res + x] = rgb888_to_rgb565(r, g, b);
                }
            }
            break;
        }
        
        case 5: { // Checkerboard
            for (int y = 0; y < current_v_res; y++) {
                for (int x = 0; x < current_h_res; x++) {
                    bool checker = ((x / 50) + (y / 50)) % 2;
                    uint16_t color = checker ? 
                        rgb888_to_rgb565(0xFF, 0xFF, 0xFF) : 
                        rgb888_to_rgb565(0x00, 0x00, 0x00);
                    fb16[y * current_h_res + x] = color;
                }
            }
            break;
        }
    }
    
    // Send frame buffer to display
    esp_err_t ret = esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, current_h_res, current_v_res, frame_buffer);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Pattern %d displayed successfully", pattern);
        // Quick flash to indicate success
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(STATUS_LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "❌ Failed to display pattern %d: %s", pattern, esp_err_to_name(ret));
        signal_error();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Waveshare ESP32-P4-WIFI6-DEV-KIT Display Test");
    ESP_LOGI(TAG, "🎯 Using Official Waveshare Components");
    ESP_LOGI(TAG, "📱 Supports: 10.1\" (1280x800) and 7\" (800x480) displays");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize components
    init_status_leds();
    
    ESP_LOGI(TAG, "📺 Step 1: Auto-detect and Initialize Display");
    ret = init_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "💥 Display initialization failed!");
        ESP_LOGE(TAG, "🔍 Check your display connections:");
        ESP_LOGE(TAG, "   - Ensure display is properly connected to MIPI DSI port");
        ESP_LOGE(TAG, "   - Verify power connections (5V)");
        ESP_LOGE(TAG, "   - Check if display is compatible (Waveshare 7\" or 10.1\")");
        
        // Flash error pattern continuously
        while (1) {
            signal_error();
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    
    ESP_LOGI(TAG, "💾 Step 2: Allocate Frame Buffer");
    allocate_frame_buffer();
    if (!frame_buffer) {
        ESP_LOGE(TAG, "💥 Frame buffer allocation failed!");
        while (1) {
            signal_error();
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    
    ESP_LOGI(TAG, "🎨 Step 3: Display Test Patterns");
    ESP_LOGI(TAG, "👀 WATCH YOUR DISPLAY - You should see changing colors and patterns!");
    ESP_LOGI(TAG, "🔧 Display Type: %s (%dx%d)", 
             display_type == 0 ? "10.1\" JD9365" : "7\" ILI9881C",
             current_h_res, current_v_res);
    ESP_LOGI(TAG, "📝 Note: Display is in portrait mode (height > width)");
    
    // Display test patterns in a loop
    int pattern = 0;
    const char* pattern_names[] = {
        "Red Screen", "Green Screen", "Blue Screen", 
        "White Screen", "Rainbow Gradient", "Checkerboard"
    };
    
    while (1) {
        ESP_LOGI(TAG, "🖼️ Pattern %d/6: %s", pattern + 1, pattern_names[pattern]);
        draw_test_pattern(pattern);
        
        // Wait with status indication
        for (int i = 0; i < 50; i++) {  // 5 second delay
            gpio_set_level(DEBUG_LED_PIN, i & 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        gpio_set_level(DEBUG_LED_PIN, 0);
        
        pattern = (pattern + 1) % 6;
        
        if (pattern == 0) {
            ESP_LOGI(TAG, "🔄 Cycling through all patterns again...");
            ESP_LOGI(TAG, "💡 If you see changing patterns, your display is working perfectly!");
        }
    }
}