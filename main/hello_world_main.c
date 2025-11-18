/*
 * Rocket Launcher Display Controller - ESP-IDF Version
 * Ported from Arduino rocket_v_8 display_mcu_v_8_00_001
 * ESP32-P4 + WiFi AP + Web Server + Studio Integration
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG = "rocket_display";

// WiFi Configuration (matches Arduino version)
#define WIFI_SSID "RocketDisplay_v8"
#define WIFI_PASS "rocket123"
#define MAX_STA_CONN 4

// HTTP Server handle
static httpd_handle_t server = NULL;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "📱 Station %02x:%02x:%02x:%02x:%02x:%02x connected, AID=%d", 
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "📱 Station %02x:%02x:%02x:%02x:%02x:%02x disconnected, AID=%d", 
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

// Generate diagnostics JSON (matches Arduino diagJSON function)
static char* generate_diagnostics_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *uptime = cJSON_CreateNumber(esp_timer_get_time() / 1000); // Convert to milliseconds
    cJSON *heap = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    
    // Heap information
    cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap, "total", esp_get_minimum_free_heap_size());
    
    // WiFi information
    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_get_ip_info(ap_netif, &ip_info);
        char ip_str[16];
        esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
        cJSON_AddStringToObject(wifi, "ap_ssid", WIFI_SSID);
        cJSON_AddStringToObject(wifi, "ap_ip", ip_str);
    }
    cJSON_AddBoolToObject(wifi, "sta", false); // STA not implemented yet
    cJSON_AddStringToObject(wifi, "sta_ssid", "");
    cJSON_AddStringToObject(wifi, "sta_ip", "0.0.0.0");
    cJSON_AddNumberToObject(wifi, "rssi", -127);
    cJSON_AddNumberToObject(wifi, "ch", 6);
    
    // Add objects to root
    cJSON_AddItemToObject(root, "uptime", uptime);
    cJSON_AddItemToObject(root, "heap", heap);
    cJSON_AddItemToObject(root, "wifi", wifi);
    cJSON_AddStringToObject(root, "system", "rocket_display_controller");
    cJSON_AddStringToObject(root, "version", "ESP-IDF v8.00.001");
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

// HTTP GET handler for diagnostics JSON
static esp_err_t diag_json_get_handler(httpd_req_t *req)
{
    char* json_resp = generate_diagnostics_json();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_send(req, json_resp, strlen(json_resp));
    
    free(json_resp);
    return ret;
}

// HTTP GET handler for launcher status (matches Studio frontend expectations)
static esp_err_t launcher_status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *launchers = cJSON_CreateArray();
    
    // Create sample launcher data (matches our Node.js test)
    cJSON *launcher1 = cJSON_CreateObject();
    cJSON_AddStringToObject(launcher1, "id", "esp32-p4-001");
    cJSON_AddStringToObject(launcher1, "name", "Primary Launch Pad");
    cJSON_AddStringToObject(launcher1, "status", "armed");
    cJSON_AddBoolToObject(launcher1, "safetyEnabled", true);
    
    cJSON *telemetry1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(telemetry1, "fuelLevel", 95);
    cJSON_AddNumberToObject(telemetry1, "temperature", 22.5);
    cJSON_AddStringToObject(telemetry1, "lastUpdate", "2024-12-28 15:30:00");
    cJSON_AddItemToObject(launcher1, "telemetry", telemetry1);
    
    cJSON_AddItemToArray(launchers, launcher1);
    
    // Add to root response
    cJSON_AddStringToObject(root, "timestamp", "2024-12-28T15:30:00Z");
    cJSON_AddStringToObject(root, "system", "rocket_display_controller");
    cJSON_AddItemToObject(root, "launchers", launchers);
    
    char *json_string = cJSON_Print(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    cJSON_Delete(root);
    return ret;
}

// HTTP GET handler for root
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html_resp = 
        "<!DOCTYPE html>"
        "<html><head><title>Rocket Display Controller</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{background:#111;color:#eee;font-family:Arial;padding:20px}</style>"
        "</head><body>"
        "<h1>🚀 Rocket Launcher Display Controller</h1>"
        "<p><strong>ESP-IDF Version</strong> - Ready for Studio Dashboard</p>"
        "<p>📡 <a href='/api/status'>Launcher Status API</a></p>"
        "<p>📊 <a href='/api/diag'>Diagnostics JSON</a></p>"
        "<p>🎯 <strong>Studio Dashboard:</strong> Connect to this AP and access rocket controls</p>"
        "</body></html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, html_resp, strlen(html_resp));
}

// Start HTTP server
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    
    ESP_LOGI(TAG, "Starting HTTP server on port 80");
    if (httpd_start(&server, &config) == ESP_OK) {
        
        // Register URI handlers
        httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root);
        
        httpd_uri_t diag_json = {
            .uri       = "/api/diag",
            .method    = HTTP_GET,
            .handler   = diag_json_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &diag_json);
        
        httpd_uri_t launcher_status = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = launcher_status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &launcher_status);
        
        ESP_LOGI(TAG, "✅ Web server started successfully");
        ESP_LOGI(TAG, "🌐 Access at: http://192.168.4.1");
        ESP_LOGI(TAG, "📡 API endpoints:");
        ESP_LOGI(TAG, "   → http://192.168.4.1/api/status (launcher data)");
        ESP_LOGI(TAG, "   → http://192.168.4.1/api/diag (diagnostics)");
        
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "❌ Failed to start HTTP server");
    return ESP_FAIL;
}

// Initialize WiFi AP mode (matches Arduino startApRobust function)
static esp_err_t wifi_init_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // Use simplified WiFi configuration for ESP32-P4  
    wifi_init_config_t cfg = {
        .osi_funcs = &g_wifi_osi_funcs,
        .wpa_crypto_funcs = g_wifi_default_wpa_crypto_funcs,
        .static_rx_buf_num = 10,
        .dynamic_rx_buf_num = 32,
        .tx_buf_type = 1,
        .static_tx_buf_num = 0,
        .dynamic_tx_buf_num = 32,
        .rx_mgmt_buf_type = 0,
        .cache_tx_buf_num = 0,
        .csi_enable = 0,
        .ampdu_rx_enable = 1,
        .ampdu_tx_enable = 1,
        .amsdu_tx_enable = 0,
        .nvs_enable = 1,
        .nano_enable = 0,
        .rx_ba_win = 6,
        .wifi_task_core_id = 0,
        .beacon_max_len = 752,
        .mgmt_sbuf_num = 32,
        .feature_caps = 0,
        .sta_disconnected_pm = false,
        .espnow_max_encrypt_num = 7,
        .magic = WIFI_INIT_CONFIG_MAGIC
    };
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 6,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "✅ WiFi AP started successfully");
    ESP_LOGI(TAG, "📶 SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔐 Password: %s", WIFI_PASS);
    ESP_LOGI(TAG, "🌐 IP: 192.168.4.1");
    
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Rocket Launcher Display Controller Starting...");
    ESP_LOGI(TAG, "📋 ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "🎯 Ported from Arduino rocket_v_8 display_mcu_v_8_00_001");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS Flash initialized");

    // Initialize WiFi AP
    ESP_ERROR_CHECK(wifi_init_ap());

    // Start web server
    ESP_ERROR_CHECK(start_webserver());

    ESP_LOGI(TAG, "🚀 Rocket Launcher Display Controller Ready!");
    ESP_LOGI(TAG, "💡 Studio Dashboard can connect and control launchers");
    ESP_LOGI(TAG, "📊 System Status:");
    ESP_LOGI(TAG, "   Free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "   WiFi AP: %s (192.168.4.1)", WIFI_SSID);
    ESP_LOGI(TAG, "   Web Server: Running on port 80");
    
    // Keep the system running and log periodic status
    int counter = 0;
    while (1) {
        counter++;
        if (counter % 12 == 0) { // Every minute
            ESP_LOGI(TAG, "🚀 System Status: Count=%d, Heap=%u bytes, Uptime=%llu sec", 
                     counter, (unsigned int)esp_get_free_heap_size(), (unsigned long long)(esp_timer_get_time() / 1000000));
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}
