/*
ESP32-P4 Rocket Launcher Display Controller
Hardware Test Version - Display Only
No WiFi (ESP32-P4 doesn't have built-in WiFi)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char* TAG = "ROCKET_DISPLAY";

// Display Configuration
#define LCD_HOST           SPI2_HOST
#define PIN_NUM_MISO       -1
#define PIN_NUM_MOSI       11  // DIN
#define PIN_NUM_CLK        12  // CLK  
#define PIN_NUM_CS         10  // CS
#define PIN_NUM_DC         13  // DC
#define PIN_NUM_RST        14  // RST
#define PIN_NUM_BCKL       15  // Backlight

// Display dimensions
#define LCD_H_RES          240
#define LCD_V_RES          320

// LED Configuration
#define LED_PIN            GPIO_NUM_2
#define LED_BLINK_PERIOD   1000  // ms

// Global variables
static esp_lcd_panel_handle_t panel_handle = NULL;
static TimerHandle_t led_timer = NULL;
static bool led_state = false;

// Test data structure
typedef struct {
    int launch_status;
    float voltage;
    int signal_strength;
    uint32_t uptime_seconds;
} rocket_data_t;

static rocket_data_t rocket_data = {
    .launch_status = 0,
    .voltage = 12.6,
    .signal_strength = 85,
    .uptime_seconds = 0
};

// LED Timer Callback
static void led_timer_callback(TimerHandle_t timer)
{
    led_state = !led_state;
    gpio_set_level(LED_PIN, led_state);
    ESP_LOGI(TAG, "💡 LED %s", led_state ? "ON" : "OFF");
}

// Initialize LED
static esp_err_t init_led(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Create LED blink timer
    led_timer = xTimerCreate("led_timer", 
                            pdMS_TO_TICKS(LED_BLINK_PERIOD), 
                            pdTRUE, 
                            NULL, 
                            led_timer_callback);
    
    if (led_timer) {
        xTimerStart(led_timer, 0);
        ESP_LOGI(TAG, "✅ LED initialized with blink timer");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "❌ Failed to create LED timer");
    return ESP_FAIL;
}

// Initialize Display
static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "🖥️  Initializing ESP32-P4 Display...");
    
    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 20 * 1000 * 1000, // 20MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Initialize backlight
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BCKL
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level(PIN_NUM_BCKL, 1)); // Turn on backlight

    ESP_LOGI(TAG, "✅ Display initialized - %dx%d ST7789", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

// Draw text on display
static void draw_text(int x, int y, const char* text, uint16_t color)
{
    if (!panel_handle) return;
    
    // Simple text rendering - just draw colored pixels for now
    // In a real implementation you'd use a font library
    int len = strlen(text);
    for (int i = 0; i < len && (x + i * 8) < LCD_H_RES; i++) {
        for (int py = 0; py < 8 && (y + py) < LCD_V_RES; py++) {
            for (int px = 0; px < 6 && (x + i * 8 + px) < LCD_H_RES; px++) {
                // Simple block character representation
                if ((text[i] >= '0' && text[i] <= '9') || 
                    (text[i] >= 'A' && text[i] <= 'Z') || 
                    (text[i] >= 'a' && text[i] <= 'z')) {
                    esp_lcd_panel_draw_bitmap(panel_handle, 
                                            x + i * 8 + px, y + py, 
                                            x + i * 8 + px + 1, y + py + 1, 
                                            &color);
                }
            }
        }
    }
}

// Update display with rocket data
static void update_display(void)
{
    if (!panel_handle) return;

    // Clear screen - fill with black
    uint16_t black = 0x0000;
    for (int y = 0; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, &black);
        }
    }

    // Colors
    uint16_t white = 0xFFFF;
    uint16_t green = 0x07E0;
    uint16_t red = 0xF800;
    uint16_t yellow = 0xFFE0;

    // Title
    draw_text(50, 20, "ROCKET LAUNCHER", white);
    draw_text(60, 40, "ESP32-P4 DISPLAY", yellow);

    // Status indicators
    char status_text[32];
    snprintf(status_text, sizeof(status_text), "STATUS: %s", 
             rocket_data.launch_status ? "READY" : "STANDBY");
    draw_text(20, 80, status_text, rocket_data.launch_status ? green : red);

    // Voltage display
    char voltage_text[32];
    snprintf(voltage_text, sizeof(voltage_text), "VOLTAGE: %.1fV", rocket_data.voltage);
    draw_text(20, 110, voltage_text, white);

    // Signal strength
    char signal_text[32];
    snprintf(signal_text, sizeof(signal_text), "SIGNAL: %d%%", rocket_data.signal_strength);
    draw_text(20, 140, signal_text, white);

    // Uptime
    char uptime_text[32];
    snprintf(uptime_text, sizeof(uptime_text), "UPTIME: %lds", rocket_data.uptime_seconds);
    draw_text(20, 170, uptime_text, white);

    // Hardware info
    draw_text(20, 220, "HARDWARE: ESP32-P4", green);
    draw_text(20, 250, "DISPLAY: ST7789", green);

    ESP_LOGI(TAG, "📊 Display updated - Status:%d V:%.1f S:%d%% T:%lds", 
             rocket_data.launch_status, rocket_data.voltage, 
             rocket_data.signal_strength, rocket_data.uptime_seconds);
}

// Test data update task
static void data_update_task(void *arg)
{
    while (1) {
        // Simulate changing data
        rocket_data.uptime_seconds++;
        
        // Simulate voltage fluctuation
        rocket_data.voltage = 12.0 + (float)(esp_random() % 100) / 100.0;
        
        // Simulate signal strength changes
        rocket_data.signal_strength = 70 + (esp_random() % 30);
        
        // Toggle launch status occasionally
        if (rocket_data.uptime_seconds % 10 == 0) {
            rocket_data.launch_status = !rocket_data.launch_status;
        }
        
        update_display();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 ESP32-P4 Rocket Launcher Display Controller Starting...");
    
    // Initialize components
    ESP_ERROR_CHECK(init_led());
    ESP_ERROR_CHECK(init_display());
    
    // Initial display update
    update_display();
    
    // Create data update task
    xTaskCreate(data_update_task, "data_update", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "✅ ESP32-P4 Display Controller Ready!");
    ESP_LOGI(TAG, "📱 Display: %dx%d ST7789 LCD", LCD_H_RES, LCD_V_RES);
    ESP_LOGI(TAG, "💡 Status LED: GPIO %d", LED_PIN);
    
    // Main loop - just keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "❤️  ESP32-P4 Running - Uptime: %ld seconds", rocket_data.uptime_seconds);
    }
}