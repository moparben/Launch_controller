/*
 * ESP32-P4 Simple Rocket Launcher with WiFi
 * Simplified version to ensure system starts properly
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

// WiFi includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// HTTP Server includes
#include "esp_http_server.h"

// JSON includes
#include "cJSON.h"

static const char *TAG = "rocket_simple";

// WiFi Configuration - UPDATE THESE!
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Status LEDs
#define STATUS_LED_PIN     GPIO_NUM_15
#define SUCCESS_LED_PIN    GPIO_NUM_16
#define ERROR_LED_PIN      GPIO_NUM_17

// Display backlight (basic control)
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_21

// System status
typedef struct {
    char device_name[64];
    char firmware_version[32];
    char ip_address[16];
    int launch_count;
    bool system_ready;
    bool wifi_connected;
    bool display_ready;
} system_status_t;

static system_status_t system_status = {0};
static httpd_handle_t server = NULL;

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define ESP_MAXIMUM_RETRY  5

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(system_status.ip_address, sizeof(system_status.ip_address), 
                 IPSTR, IP2STR(&event->ip_info.ip));
        system_status.wifi_connected = true;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_name", system_status.device_name);
    cJSON_AddStringToObject(json, "firmware_version", system_status.firmware_version);
    cJSON_AddStringToObject(json, "ip_address", system_status.ip_address);
    cJSON_AddNumberToObject(json, "launch_count", system_status.launch_count);
    cJSON_AddBoolToObject(json, "system_ready", system_status.system_ready);
    cJSON_AddBoolToObject(json, "wifi_connected", system_status.wifi_connected);
    cJSON_AddBoolToObject(json, "display_ready", system_status.display_ready);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t launch_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "🚀 ROCKET LAUNCH INITIATED!");
    
    // Flash LEDs for launch sequence
    for (int i = 0; i < 5; i++) {
        gpio_set_level(SUCCESS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(SUCCESS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    system_status.launch_count++;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "success");
    cJSON_AddStringToObject(json, "message", "🚀 Rocket launched successfully!");
    cJSON_AddNumberToObject(json, "launch_count", system_status.launch_count);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Status endpoint
        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        // Launch endpoint
        httpd_uri_t launch_uri = {
            .uri = "/api/launch",
            .method = HTTP_POST,
            .handler = launch_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &launch_uri);
        
        ESP_LOGI(TAG, "🌐 HTTP Server started");
        ESP_LOGI(TAG, "📋 Endpoints:");
        ESP_LOGI(TAG, "   GET  http://%s/api/status", system_status.ip_address);
        ESP_LOGI(TAG, "   POST http://%s/api/launch", system_status.ip_address);
        
        return server;
    }

    ESP_LOGE(TAG, "❌ Error starting HTTP server!");
    return NULL;
}

static void init_leds(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << STATUS_LED_PIN) | 
                          (1ULL << SUCCESS_LED_PIN) | 
                          (1ULL << ERROR_LED_PIN) |
                          (1ULL << DISPLAY_BACKLIGHT_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Turn on display backlight
    gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);
    ESP_LOGI(TAG, "💡 Display backlight enabled");
}

static esp_err_t init_wifi(void)
{
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
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "📡 WiFi init finished.");

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
        ESP_LOGI(TAG, "✅ Connected to AP SSID:%s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "❌ Failed to connect to SSID:%s", WIFI_SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "❓ UNEXPECTED EVENT");
        return ESP_FAIL;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 ESP32-P4 SIMPLE ROCKET LAUNCHER STARTING...");
    ESP_LOGI(TAG, "📺 Waveshare ESP32-P4-WIFI6-DEV-KIT");
    ESP_LOGI(TAG, "🎯 Simplified System - WiFi + HTTP API");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS Flash initialized");

    // Initialize system status
    strcpy(system_status.device_name, "ESP32-P4 Simple Rocket Launcher");
    strcpy(system_status.firmware_version, "v1.0-simple");
    system_status.launch_count = 0;
    system_status.system_ready = false;
    system_status.display_ready = true; // Assume basic backlight works
    system_status.wifi_connected = false;
    strcpy(system_status.ip_address, "0.0.0.0");
    
    // Initialize LEDs and backlight
    init_leds();
    
    // Initialize WiFi
    ESP_LOGI(TAG, "📡 Initializing WiFi...");
    ESP_LOGI(TAG, "🔧 Make sure to update WIFI_SSID and WIFI_PASSWORD in the code!");
    ret = init_wifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi initialization failed!");
        gpio_set_level(ERROR_LED_PIN, 1);
        return;
    }
    
    // Start web server
    ESP_LOGI(TAG, "🌐 Starting HTTP Server...");
    start_webserver();
    
    system_status.system_ready = true;
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎯 ═══════════════════════════════════════════");
    ESP_LOGI(TAG, "🎯  SIMPLE ROCKET LAUNCHER SYSTEM READY!");
    ESP_LOGI(TAG, "🎯 ═══════════════════════════════════════════");
    ESP_LOGI(TAG, "📺 Display Backlight: ✅ READY");
    ESP_LOGI(TAG, "📶 WiFi Connection:   ✅ CONNECTED (%s)", system_status.ip_address);
    ESP_LOGI(TAG, "🚀 Launch System:     ✅ READY");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "🌐 Control Panel:");
    ESP_LOGI(TAG, "   📊 Status:  http://%s/api/status", system_status.ip_address);
    ESP_LOGI(TAG, "   🚀 Launch:  curl -X POST http://%s/api/launch", system_status.ip_address);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🚀 READY TO LAUNCH ROCKETS!");
    
    // Main status loop
    while (1) {
        // Heartbeat LED
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Status update every 30 seconds
        static int counter = 0;
        if (++counter >= 60) { // 30 seconds at 500ms intervals
            ESP_LOGI(TAG, "💓 System Status: WiFi:%s Launches:%d", 
                     system_status.wifi_connected ? "✅" : "❌",
                     system_status.launch_count);
            counter = 0;
        }
    }
}