/******************************************************************************
 * ESP32-P4-Nano Display Controller with WiFi
 * Rocket Launcher System - Complete Display + WiFi Implementation
 * 
 * Based on ESP32-P4-Nano documentation and working examples
 * Features:
 * - MIPI-DSI 10.1" display (JD9365 controller)  
 * - ESP32-C6 WiFi 6 via SDIO
 * - GPIO controls for rocket launcher
 * - HTTP server for remote control
 * 
 * Hardware: Waveshare ESP32-P4-Nano
 * Target: esp32p4
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_mac.h"

// Display includes for ESP32-P4-Nano
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_heap_caps.h"

static const char *TAG = "ESP32-P4-Nano-RocketLauncher";

// Configuration defines
#define ROCKET_LAUNCHER_WIFI_SSID      "RocketLauncher-P4-Nano"
#define ROCKET_LAUNCHER_WIFI_PASS      "rocket123"
#define ROCKET_LAUNCHER_WIFI_CHANNEL   1
#define ROCKET_LAUNCHER_MAX_STA_CONN   4

// GPIO Configuration for ESP32-P4-Nano
#define ROCKET_LED_RED_GPIO    GPIO_NUM_15
#define ROCKET_LED_GREEN_GPIO  GPIO_NUM_16
#define ROCKET_FIRE_GPIO       GPIO_NUM_17
#define ROCKET_SAFETY_GPIO     GPIO_NUM_18

// Display Configuration - ESP32-P4-Nano MIPI-DSI
#define DISPLAY_WIDTH     1024
#define DISPLAY_HEIGHT    600
#define DISPLAY_LANES     2
#define DISPLAY_BIT_RATE  1500  // 1.5 Gbps per lane

// System status
typedef struct {
    bool wifi_connected;
    bool display_ready;
    bool safety_enabled;
    uint32_t boot_time;
    uint32_t fire_count;
} system_status_t;

static system_status_t g_system_status = {0};
static esp_lcd_panel_handle_t panel_handle = NULL;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// Initialize GPIO for rocket launcher controls
static void gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO controls...");
    
    // Configure LED GPIOs
    gpio_config_t led_conf = {
        .pin_bit_mask = ((1ULL << ROCKET_LED_RED_GPIO) | 
                         (1ULL << ROCKET_LED_GREEN_GPIO)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    
    // Configure fire control GPIO
    gpio_config_t fire_conf = {
        .pin_bit_mask = (1ULL << ROCKET_FIRE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&fire_conf));
    
    // Configure safety input GPIO
    gpio_config_t safety_conf = {
        .pin_bit_mask = (1ULL << ROCKET_SAFETY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&safety_conf));
    
    // Initialize states
    gpio_set_level(ROCKET_LED_RED_GPIO, 1);    // Red LED on (not ready)
    gpio_set_level(ROCKET_LED_GREEN_GPIO, 0);  // Green LED off
    gpio_set_level(ROCKET_FIRE_GPIO, 0);       // Fire control off
    
    ESP_LOGI(TAG, "GPIO initialization complete");
}

// Initialize MIPI-DSI display for ESP32-P4-Nano
static esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4-Nano MIPI-DSI display...");
    
    // MIPI-DSI bus configuration for ESP32-P4-Nano
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = DISPLAY_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DISPLAY_BIT_RATE,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    
    // JD9365 panel configuration for 10.1" display
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_panel_io_dbi_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    
    // JD9365 panel configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    
    // Create JD9365 panel for ESP32-P4-Nano 10.1" display
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365_10_1(mipi_dbi_io, &panel_config, &panel_handle));
    
    // Initialize the panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    g_system_status.display_ready = true;
    ESP_LOGI(TAG, "MIPI-DSI display initialized successfully");
    
    return ESP_OK;
}

// Display update function
static void display_update_status(void)
{
    if (!g_system_status.display_ready || !panel_handle) {
        return;
    }
    
    // Simple color fill to show display is working
    // Create buffer for display data
    size_t buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2; // RGB565
    uint16_t *display_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    
    if (display_buffer) {
        // Fill with status colors
        uint16_t color = g_system_status.wifi_connected ? 0x07E0 : 0xF800; // Green or Red
        
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            display_buffer[i] = color;
        }
        
        // Draw to display
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 
                                  DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                  display_buffer);
        
        free(display_buffer);
    }
}

// Initialize WiFi Access Point using ESP32-C6 via SDIO
static void wifi_init_softap(void)
{
    ESP_LOGI(TAG, "Initializing WiFi AP via ESP32-C6 SDIO...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // WiFi configuration for ESP32-P4-Nano (ESP32-C6 via SDIO)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Configure AP settings
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ROCKET_LAUNCHER_WIFI_SSID,
            .ssid_len = strlen(ROCKET_LAUNCHER_WIFI_SSID),
            .channel = ROCKET_LAUNCHER_WIFI_CHANNEL,
            .password = ROCKET_LAUNCHER_WIFI_PASS,
            .max_connection = ROCKET_LAUNCHER_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    if (strlen(ROCKET_LAUNCHER_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Get MAC address to verify ESP32-C6 communication
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    ESP_LOGI(TAG, "WiFi AP MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    g_system_status.wifi_connected = true;
    ESP_LOGI(TAG, "WiFi AP '%s' started successfully", ROCKET_LAUNCHER_WIFI_SSID);
}

// HTTP handlers
static esp_err_t rocket_status_handler(httpd_req_t *req)
{
    char resp[512];
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    
    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html>"
        "<html><head><title>Rocket Launcher P4-Nano Status</title></head>"
        "<body><h1>ESP32-P4-Nano Rocket Launcher</h1>"
        "<p>WiFi Status: %s</p>"
        "<p>Display Status: %s</p>"
        "<p>Safety: %s</p>"
        "<p>MAC Address: %02x:%02x:%02x:%02x:%02x:%02x</p>"
        "<p>Fire Count: %lu</p>"
        "<p>Uptime: %lu seconds</p>"
        "<a href='/fire'>FIRE ROCKET</a>"
        "</body></html>",
        g_system_status.wifi_connected ? "Connected" : "Disconnected",
        g_system_status.display_ready ? "Ready" : "Not Ready", 
        g_system_status.safety_enabled ? "Enabled" : "DISABLED",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        g_system_status.fire_count,
        (esp_timer_get_time() - g_system_status.boot_time) / 1000000);
    
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t rocket_fire_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "ROCKET FIRE COMMAND RECEIVED!");
    
    // Safety check
    g_system_status.safety_enabled = gpio_get_level(ROCKET_SAFETY_GPIO);
    
    if (!g_system_status.safety_enabled) {
        ESP_LOGW(TAG, "FIRE ABORTED - Safety not enabled!");
        httpd_resp_send(req, "FIRE ABORTED - Safety switch not enabled!", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Fire sequence
    ESP_LOGI(TAG, "Initiating fire sequence...");
    gpio_set_level(ROCKET_LED_RED_GPIO, 1);
    gpio_set_level(ROCKET_LED_GREEN_GPIO, 0);
    
    // Fire pulse
    gpio_set_level(ROCKET_FIRE_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500)); // 500ms fire pulse
    gpio_set_level(ROCKET_FIRE_GPIO, 0);
    
    g_system_status.fire_count++;
    
    // Status LEDs
    gpio_set_level(ROCKET_LED_RED_GPIO, 0);
    gpio_set_level(ROCKET_LED_GREEN_GPIO, 1);
    
    ESP_LOGI(TAG, "Fire sequence complete! Total fires: %lu", g_system_status.fire_count);
    
    httpd_resp_send(req, "ROCKET FIRED SUCCESSFULLY!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void)
{
    ESP_LOGI(TAG, "Starting HTTP server...");
    
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t status_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = rocket_status_handler,
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t fire_uri = {
            .uri       = "/fire",
            .method    = HTTP_GET,
            .handler   = rocket_fire_handler,
        };
        httpd_register_uri_handler(server, &fire_uri);
        
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    }
    return server;
}

// System monitoring task
static void system_monitor_task(void *pvParameters)
{
    while (1) {
        // Update system status
        g_system_status.safety_enabled = gpio_get_level(ROCKET_SAFETY_GPIO);
        
        // Update status LEDs
        if (g_system_status.wifi_connected && g_system_status.display_ready) {
            // System ready - green LED blink
            gpio_set_level(ROCKET_LED_GREEN_GPIO, 1);
            gpio_set_level(ROCKET_LED_RED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(ROCKET_LED_GREEN_GPIO, 0);
        } else {
            // System not ready - red LED solid
            gpio_set_level(ROCKET_LED_RED_GPIO, 1);
            gpio_set_level(ROCKET_LED_GREEN_GPIO, 0);
        }
        
        // Update display
        display_update_status();
        
        // Log status
        ESP_LOGI(TAG, "Status - WiFi:%s Display:%s Safety:%s Fires:%lu",
                g_system_status.wifi_connected ? "OK" : "FAIL",
                g_system_status.display_ready ? "OK" : "FAIL", 
                g_system_status.safety_enabled ? "ON" : "OFF",
                g_system_status.fire_count);
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second interval
    }
}

// Main application entry point
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4-Nano Rocket Launcher Controller Starting...");
    g_system_status.boot_time = esp_timer_get_time();
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize hardware
    gpio_init();
    
    // Initialize display (MIPI-DSI)
    if (display_init() == ESP_OK) {
        ESP_LOGI(TAG, "Display system ready");
    } else {
        ESP_LOGW(TAG, "Display initialization failed - continuing without display");
    }
    
    // Initialize WiFi (ESP32-C6 via SDIO)
    wifi_init_softap();
    
    // Start HTTP server
    httpd_handle_t server = start_webserver();
    if (server) {
        ESP_LOGI(TAG, "Web server running - connect to WiFi '%s' and browse to http://192.168.4.1", 
                 ROCKET_LAUNCHER_WIFI_SSID);
    }
    
    // Start system monitoring
    xTaskCreate(system_monitor_task, "system_monitor", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "ESP32-P4-Nano Rocket Launcher Controller ready!");
    ESP_LOGI(TAG, "WiFi SSID: %s", ROCKET_LAUNCHER_WIFI_SSID);
    ESP_LOGI(TAG, "WiFi Password: %s", ROCKET_LAUNCHER_WIFI_PASS);
    ESP_LOGI(TAG, "Connect and browse to: http://192.168.4.1");
}