/*
 * ESP32-P4 Basic Display MCU v1.00 - Minimal Test Version
 * Basic functionality to test ESP32-P4 boot and WiFi
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi_remote.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "cJSON.h"
#include "nvs_flash.h"

static const char *TAG = "BASIC_MCU_v1.00";

// GPIO Definitions for ESP32-P4
#define LED_ARM_PIN     GPIO_NUM_15     // Red LED - Armed status
#define LED_LAUNCH_PIN  GPIO_NUM_16     // Green LED - Launch ready
#define BUTTON_ARM_PIN  GPIO_NUM_0      // ARM button (BOOT button)
#define BUTTON_RESET_PIN GPIO_NUM_1     // RESET button

// WiFi Configuration
#define WIFI_SSID "ESP32_ROCKET_LAUNCHER"
#define WIFI_PASS "rocket123"
#define WIFI_CHANNEL 1
#define MAX_STA_CONN 4

// System Status
typedef enum {
    SYSTEM_IDLE = 0,
    SYSTEM_ARMED,
    SYSTEM_LAUNCHING,
    SYSTEM_ERROR
} system_state_t;

static system_state_t system_state = SYSTEM_IDLE;
// HTTP server handle will be declared when needed
static EventGroupHandle_t wifi_event_group;

// Basic GPIO initialization
esp_err_t gpio_init() {
    ESP_LOGI(TAG, "Initializing GPIO pins");
    
    // Configure LED pins as output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_ARM_PIN) | (1ULL << LED_LAUNCH_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Configure button pins as input
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_ARM_PIN) | (1ULL << BUTTON_RESET_PIN);
    io_conf.pull_up_en = 1;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Set initial LED states
    gpio_set_level(LED_ARM_PIN, 0);    // ARM LED off
    gpio_set_level(LED_LAUNCH_PIN, 1); // LAUNCH LED on (system ready)

    ESP_LOGI(TAG, "GPIO initialization completed");
    return ESP_OK;
}

// LED test function
void led_test() {
    ESP_LOGI(TAG, "Running LED test sequence");
    
    for (int i = 0; i < 5; i++) {
        // ARM LED on, LAUNCH LED off
        gpio_set_level(LED_ARM_PIN, 1);
        gpio_set_level(LED_LAUNCH_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // ARM LED off, LAUNCH LED on
        gpio_set_level(LED_ARM_PIN, 0);
        gpio_set_level(LED_LAUNCH_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    ESP_LOGI(TAG, "LED test completed");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected");
    }
}

// Initialize WiFi Access Point
esp_err_t wifi_init() {
    ESP_LOGI(TAG, "Initializing WiFi Access Point");
    
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s channel:%d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
    
    return ESP_OK;
}

// HTTP GET handler for root page
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char* resp_str = 
        "<!DOCTYPE html><html><head><title>ESP32-P4 Rocket Launcher</title></head>"
        "<body><h1>ESP32-P4 Display Controller</h1>"
        "<p>Status: System Ready</p>"
        "<p>This is a basic test version</p>"
        "<p>System State: IDLE</p>"
        "</body></html>";
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP GET handler for status
static esp_err_t status_get_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString("ready");
    cJSON *state = cJSON_CreateString("IDLE");
    cJSON *version = cJSON_CreateString("Basic_v1.00");
    
    cJSON_AddItemToObject(json, "status", status);
    cJSON_AddItemToObject(json, "state", state);
    cJSON_AddItemToObject(json, "version", version);
    
    const char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free((void *)json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Start HTTP server
esp_err_t start_webserver() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t status = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler
        };
        httpd_register_uri_handler(server, &status);

        ESP_LOGI(TAG, "HTTP server started successfully");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting HTTP server!");
    return ESP_FAIL;
}

// System info task
void system_info_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "=== ESP32-P4 Basic System Status ===");
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "System state: %d", system_state);
        ESP_LOGI(TAG, "Uptime: %llu ms", esp_timer_get_time() / 1000);
        
        // Flash LED to indicate system is alive
        gpio_set_level(LED_LAUNCH_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_LAUNCH_PIN, 1);
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second interval
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32-P4 Basic Display MCU Starting ===");
    
    // Print chip information
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);
    
    ESP_LOGI(TAG, "ESP32-P4 chip with %d CPU core(s), %s%s%s%s, %luMB %s flash",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "",
             flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG, "Minimum free heap size: %lu bytes", esp_get_minimum_free_heap_size());

    // Initialize components
    ESP_ERROR_CHECK(gpio_init());
    
    // Run LED test
    led_test();
    
    // Initialize WiFi Remote (for ESP32-C6 via SDIO)
    ESP_LOGI(TAG, "Initializing WiFi Remote for ESP32-C6");
    ESP_ERROR_CHECK(wifi_init());
    
    // Start web server
    start_webserver();
    
    // Create system monitoring task
    xTaskCreate(system_info_task, "system_info", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "=== Basic system initialization completed ===");
    ESP_LOGI(TAG, "Connect to WiFi: %s (password: %s)", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "Open browser: http://192.168.4.1");
}