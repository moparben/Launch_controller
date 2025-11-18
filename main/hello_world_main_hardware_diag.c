/*
 * Hardware Diagnostic Scanner for Waveshare ESP32-P4
 * This will help identify the actual display controller and pins
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
#include "driver/i2c.h"
#include "nvs_flash.h"

static const char *TAG = "hardware_diag";

#define STATUS_LED_PIN      48
#define DEBUG_LED_PIN       47

// Common display pins to test
static int test_pins[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 46, 47, 48
};
#define NUM_TEST_PINS (sizeof(test_pins) / sizeof(test_pins[0]))

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
    }
}

static void blink_pattern(int count)
{
    for (int i = 0; i < count; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        gpio_set_level(DEBUG_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_PIN, 0);
        gpio_set_level(DEBUG_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void test_gpio_connectivity(void)
{
    ESP_LOGI(TAG, "🔍 Testing GPIO connectivity and pull states");
    
    for (int i = 0; i < NUM_TEST_PINS; i++) {
        int pin = test_pins[i];
        
        // Skip status LEDs
        if (pin == STATUS_LED_PIN || pin == DEBUG_LED_PIN) continue;
        
        // Configure as input with pull-up
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << pin),
            .pull_down_en = 0,
            .pull_up_en = 1,
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            int level_pullup = gpio_get_level(pin);
            
            // Configure with pull-down
            io_conf.pull_up_en = 0;
            io_conf.pull_down_en = 1;
            gpio_config(&io_conf);
            vTaskDelay(pdMS_TO_TICKS(10));
            int level_pulldown = gpio_get_level(pin);
            
            ESP_LOGI(TAG, "📍 GPIO%d: PullUp=%d, PullDown=%d %s", 
                     pin, level_pullup, level_pulldown,
                     (level_pullup != level_pulldown) ? "✅ Floating" : "🔗 Connected");
        }
        
        // Reset to input only
        io_conf.pull_up_en = 0;
        io_conf.pull_down_en = 0;
        gpio_config(&io_conf);
    }
}

static void test_i2c_devices(void)
{
    ESP_LOGI(TAG, "🔍 Scanning for I2C devices (maybe display uses I2C?)");
    
    // Test common I2C pin combinations
    int sda_pins[] = {21, 22, 4, 5};
    int scl_pins[] = {22, 21, 5, 4};
    int num_combos = 4;
    
    for (int combo = 0; combo < num_combos; combo++) {
        int sda = sda_pins[combo];
        int scl = scl_pins[combo];
        
        ESP_LOGI(TAG, "📡 Testing I2C on SDA=%d, SCL=%d", sda, scl);
        
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = sda,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_io_num = scl,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 100000,
        };
        
        esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
        if (ret == ESP_OK) {
            ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
            if (ret == ESP_OK) {
                // Scan I2C addresses
                for (int addr = 1; addr < 127; addr++) {
                    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                    i2c_master_start(cmd);
                    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
                    i2c_master_stop(cmd);
                    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
                    i2c_cmd_link_delete(cmd);
                    
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "🎯 Found I2C device at address 0x%02X!", addr);
                        blink_pattern(3); // Signal found device
                    }
                }
                i2c_driver_delete(I2C_NUM_0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void test_voltage_levels(void)
{
    ESP_LOGI(TAG, "🔍 Testing for 3.3V/5V detection on potential power pins");
    
    // These pins might be connected to power rails
    int power_test_pins[] = {1, 2, 3, 46, 47, 21, 22};
    int num_power_pins = sizeof(power_test_pins) / sizeof(power_test_pins[0]);
    
    for (int i = 0; i < num_power_pins; i++) {
        int pin = power_test_pins[i];
        
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << pin),
            .pull_down_en = 1,  // Pull down to test if pin is driven high
            .pull_up_en = 0,
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            int level = gpio_get_level(pin);
            if (level == 1) {
                ESP_LOGI(TAG, "⚡ GPIO%d appears to be driven HIGH (possible power/VCC)", pin);
                blink_pattern(2);
            }
        }
    }
}

static void generate_test_signals(void)
{
    ESP_LOGI(TAG, "🎯 Generating test signals on likely display pins");
    
    // Common display pin combinations for Waveshare
    int signal_pins[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 21, 46};
    int num_signal_pins = sizeof(signal_pins) / sizeof(signal_pins[0]);
    
    // Configure all as output
    for (int i = 0; i < num_signal_pins; i++) {
        int pin = signal_pins[i];
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << pin),
            .pull_down_en = 0,
            .pull_up_en = 0,
        };
        gpio_config(&io_conf);
    }
    
    ESP_LOGI(TAG, "🌟 Generating clock patterns - watch for ANY display activity!");
    
    // Generate various patterns that might trigger display activity
    for (int cycle = 0; cycle < 20; cycle++) {
        ESP_LOGI(TAG, "🔄 Test pattern cycle %d/20", cycle + 1);
        
        // Clock pattern on pin 12 (common SCLK)
        for (int clk = 0; clk < 1000; clk++) {
            gpio_set_level(12, 1);
            esp_rom_delay_us(1);
            gpio_set_level(12, 0);
            esp_rom_delay_us(1);
        }
        
        // Data pattern on pin 11 (common MOSI)
        for (int data = 0; data < 8; data++) {
            gpio_set_level(11, data & 1);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Toggle potential DC/CS pins
        gpio_set_level(13, cycle & 1);  // DC
        gpio_set_level(10, !(cycle & 1));  // CS
        gpio_set_level(14, 1);  // RST high
        
        // Toggle potential backlight pins
        gpio_set_level(9, cycle & 1);
        gpio_set_level(46, cycle & 1);
        gpio_set_level(21, cycle & 1);
        
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Blink status to show progress
        gpio_set_level(STATUS_LED_PIN, cycle & 1);
    }
    
    ESP_LOGI(TAG, "🏁 Test signal generation complete");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🔬 HARDWARE DIAGNOSTIC SCANNER");
    ESP_LOGI(TAG, "📋 This will help identify your display controller and pinout");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_status_leds();
    blink_pattern(5); // Startup signal
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 Phase 1: GPIO Connectivity Test");
    test_gpio_connectivity();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 Phase 2: I2C Device Scan");
    test_i2c_devices();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 Phase 3: Voltage Level Detection");
    test_voltage_levels();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 Phase 4: Signal Generation Test");
    ESP_LOGI(TAG, "👀 WATCH YOUR DISPLAY - Look for ANY activity during next 30 seconds!");
    generate_test_signals();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✅ Hardware diagnostic complete!");
    ESP_LOGI(TAG, "📊 Check the log above for:");
    ESP_LOGI(TAG, "   - Floating vs Connected GPIO pins");
    ESP_LOGI(TAG, "   - Any I2C devices found");
    ESP_LOGI(TAG, "   - Power rails detected");
    ESP_LOGI(TAG, "   - Any display activity during signal tests");
    
    // Continuous slow blink to show we're done
    while (1) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}