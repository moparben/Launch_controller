/*
 * ESP32-P4-Nano WiFi Test - Based on Waveshare Documentation
 * Version: 1.01
 * Target: ESP32-P4-Nano with ESP32-C6 WiFi module
 * 
 * This version follows the working WiFi station example from Waveshare docs
 * and addresses the ESP32-C6 SDIO communication issues.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "esp_http_server.h"

static const char *TAG = "ESP32P4_NANO_WIFI";

// GPIO Configuration for ESP32-P4-Nano
#define LED_ARM_PIN GPIO_NUM_15         // ARM LED
#define LED_LAUNCH_PIN GPIO_NUM_16      // LAUNCH LED
#define BUTTON_ARM_PIN GPIO_NUM_0       // ARM button (BOOT button)

// WiFi Configuration - Change these to your network
#define WIFI_SSID "YOUR_WIFI_SSID"      // Replace with your WiFi network name
#define WIFI_PASS "YOUR_WIFI_PASSWORD"  // Replace with your WiFi password
#define WIFI_MAXIMUM_RETRY 5

// WiFi Event Group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static httpd_handle_t server = NULL;

// System Status
typedef enum {
    SYSTEM_INITIALIZING = 0,
    SYSTEM_WIFI_CONNECTING,
    SYSTEM_READY,
    SYSTEM_ERROR
} system_state_t;

static system_state_t system_state = SYSTEM_INITIALIZING;

// Basic GPIO initialization
static void gpio_init(void) {
    ESP_LOGI(TAG, "Initializing GPIO for ESP32-P4-Nano");
    
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
    io_conf.pin_bit_mask = (1ULL << BUTTON_ARM_PIN);
    io_conf.pull_up_en = 1;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Set initial LED states - both off during initialization
    gpio_set_level(LED_ARM_PIN, 0);    // ARM LED off
    gpio_set_level(LED_LAUNCH_PIN, 0); // LAUNCH LED off

    ESP_LOGI(TAG, "GPIO initialization completed");
}

// LED status indication
static void update_leds(system_state_t state) {
    switch(state) {
        case SYSTEM_INITIALIZING:
            gpio_set_level(LED_ARM_PIN, 0);
            gpio_set_level(LED_LAUNCH_PIN, 0);
            break;
        case SYSTEM_WIFI_CONNECTING:
            // Blink ARM LED to show WiFi connecting
            static bool blink_state = false;
            gpio_set_level(LED_ARM_PIN, blink_state ? 1 : 0);
            gpio_set_level(LED_LAUNCH_PIN, 0);
            blink_state = !blink_state;
            break;
        case SYSTEM_READY:
            gpio_set_level(LED_ARM_PIN, 0);
            gpio_set_level(LED_LAUNCH_PIN, 1); // LAUNCH LED on when ready
            break;
        case SYSTEM_ERROR:
            gpio_set_level(LED_ARM_PIN, 1);    // ARM LED on for error
            gpio_set_level(LED_LAUNCH_PIN, 0);
            break;
    }
}

// WiFi Event Handler - Based on Waveshare documentation
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, attempting connection...");
        system_state = SYSTEM_WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry WiFi connection (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAXIMUM_RETRY);
            system_state = SYSTEM_ERROR;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        system_state = SYSTEM_READY;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi Station Mode - Based on Waveshare working example
static void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "Initializing WiFi for ESP32-P4-Nano with ESP32-C6 co-processor");
    
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished. Connecting to SSID: %s", WIFI_SSID);

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

// HTTP GET handler for root page
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char* resp_str = 
        "<!DOCTYPE html><html><head><title>ESP32-P4-Nano Test</title></head>"
        "<body style='font-family: Arial, sans-serif; margin: 40px;'>"
        "<h1>ESP32-P4-Nano WiFi Test</h1>"
        "<h2>Status: WiFi Connected!</h2>"
        "<p><strong>Board:</strong> ESP32-P4-Nano</p>"
        "<p><strong>WiFi Module:</strong> ESP32-C6 via SDIO</p>"
        "<p><strong>Version:</strong> 1.01</p>"
        "<p><strong>System State:</strong> READY</p>"
        "<hr>"
        "<p>This confirms that the ESP32-C6 SDIO communication is working properly!</p>"
        "</body></html>";
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP GET handler for status API
static esp_err_t status_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    const char* resp_str = "{"
        "\"status\":\"connected\","
        "\"board\":\"ESP32-P4-Nano\","
        "\"wifi_module\":\"ESP32-C6\","
        "\"version\":\"1.01\","
        "\"system_state\":\"READY\""
        "}";
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
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

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// System monitoring task
static void system_monitor_task(void *pvParameters)
{
    while (1) {
        // Update LED status
        update_leds(system_state);
        
        // Log system status every 10 seconds
        static int log_counter = 0;
        if (++log_counter >= 50) { // 50 * 200ms = 10 seconds
            ESP_LOGI(TAG, "System Status: %d, Free heap: %lu bytes", 
                     system_state, esp_get_free_heap_size());
            
            // Print WiFi status
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                ESP_LOGI(TAG, "Connected to: %s, RSSI: %d", ap_info.ssid, ap_info.rssi);
            }
            
            log_counter = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(200)); // 200ms delay
    }
}

// Main application entry point
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-P4-Nano WiFi Test v1.01");
    ESP_LOGI(TAG, "Target: ESP32-P4 with ESP32-C6 WiFi");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize GPIO
    gpio_init();
    ESP_LOGI(TAG, "GPIO initialized");

    // Initialize WiFi
    system_state = SYSTEM_WIFI_CONNECTING;
    wifi_init_sta();

    // Start HTTP server if WiFi connected
    if (system_state == SYSTEM_READY) {
        server = start_webserver();
        if (server) {
            ESP_LOGI(TAG, "HTTP server started successfully");
            ESP_LOGI(TAG, "Visit http://[device_ip]/ to test connection");
        }
    }

    // Create system monitoring task
    xTaskCreate(system_monitor_task, "system_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP32-P4-Nano initialization complete!");
    ESP_LOGI(TAG, "System is now running...");
}