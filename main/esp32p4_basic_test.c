/**
 * ESP32-P4-Nano Basic Hardware Test
 * Minimal firmware to test basic functionality
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

static const char *TAG = "ESP32P4_TEST";

// GPIO definitions for ESP32-P4-Nano
#define LED_GPIO GPIO_NUM_26        // Onboard LED (if available)
#define BOOT_BUTTON_GPIO GPIO_NUM_0 // Boot button

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4-Nano Basic Hardware Test ===");
    ESP_LOGI(TAG, "Chip: %s", esp_get_idf_version());
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // Configure LED GPIO
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_config);
    ESP_LOGI(TAG, "LED GPIO %d configured", LED_GPIO);

    // Configure boot button
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button_config);
    ESP_LOGI(TAG, "Boot button GPIO %d configured", BOOT_BUTTON_GPIO);

    ESP_LOGI(TAG, "=== Hardware Test Started ===");
    ESP_LOGI(TAG, "Press and hold boot button to see button state");
    ESP_LOGI(TAG, "LED should be blinking every 500ms");

    int blink_count = 0;
    bool led_state = false;

    while (1) {
        // Toggle LED
        led_state = !led_state;
        gpio_set_level(LED_GPIO, led_state);
        
        // Read boot button
        int button_state = gpio_get_level(BOOT_BUTTON_GPIO);
        
        // Print status every 10 blinks (5 seconds)
        if (++blink_count % 10 == 0) {
            ESP_LOGI(TAG, "Alive! Blink count: %d, Button: %s, LED: %s", 
                     blink_count, 
                     button_state ? "Released" : "Pressed",
                     led_state ? "ON" : "OFF");
            
            // Print memory info
            ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "Minimum free heap: %lu bytes", esp_get_minimum_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms delay
    }
}