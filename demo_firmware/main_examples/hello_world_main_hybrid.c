/*
 * Rocket Launcher Display Controller - ESP32-P4 Hybrid Version
 * Supports both standalone ESP32-P4 and ESP32-P4 + ESP32-C6 WiFi configurations
 * Auto-detects WiFi capability and adapts functionality accordingly
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

// Include WiFi headers - always include but handle errors gracefully
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "rocket_hybrid";

// Display pins - Updated for typical ESP32-P4 dev board pinout
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (10 * 1000 * 1000)  // Reduced for stability
#define LCD_BK_LIGHT_ON     1
#define LCD_BK_LIGHT_OFF    0

// Common ESP32-P4 development board GPIO assignments
#define PIN_NUM_SCLK        12   // SPI Clock
#define PIN_NUM_MOSI        11   // SPI MOSI 
#define PIN_NUM_MISO        -1   // Not used for display
#define PIN_NUM_LCD_DC      10   // Data/Command
#define PIN_NUM_LCD_RST     9    // Reset
#define PIN_NUM_LCD_CS      8    // Chip Select
#define PIN_NUM_BK_LIGHT    13   // Backlight

// Status LEDs - use multiple pins for debugging
#define STATUS_LED_PIN      47   // Primary status LED
#define DEBUG_LED_PIN       48   // Secondary debug LED

// Display dimensions
#define LCD_H_RES           240
#define LCD_V_RES           320

// Global state
esp_lcd_panel_handle_t panel_handle = NULL;
bool wifi_available = false;
bool display_initialized = false;

// WiFi configuration (if available)
#define WIFI_SSID           "RocketDisplay_v8"
#define WIFI_PASS           "rocket123"
#define WIFI_CHANNEL        1
#define MAX_STA_CONN        4

// Function prototypes
static void init_status_leds(void);
static esp_err_t init_display(void);
static esp_err_t test_display_pattern(void);
static bool detect_wifi_capability(void);
static void blink_status(int pattern);

#ifdef CONFIG_ESP32_WIFI_ENABLED
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t init_wifi_ap(void);
static esp_err_t start_webserver(void);
static esp_err_t status_get_handler(httpd_req_t *req);
#endif

// Initialize status LEDs with detailed logging
static void init_status_leds(void)
{
    ESP_LOGI(TAG, "Initializing status LEDs on pins %d and %d", STATUS_LED_PIN, DEBUG_LED_PIN);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << STATUS_LED_PIN) | (1ULL << DEBUG_LED_PIN)),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure status LEDs: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Status LEDs configured successfully");
        // Test LEDs
        gpio_set_level(STATUS_LED_PIN, 1);
        gpio_set_level(DEBUG_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(STATUS_LED_PIN, 0);
        gpio_set_level(DEBUG_LED_PIN, 0);
    }
}

// Blink status LED in different patterns
static void blink_status(int pattern)
{
    for (int i = 0; i < pattern; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Initialize display with comprehensive error checking
static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing ST7789 display...");
    ESP_LOGI(TAG, "Display pins: SCLK=%d, MOSI=%d, DC=%d, RST=%d, CS=%d, BL=%d",
             PIN_NUM_SCLK, PIN_NUM_MOSI, PIN_NUM_LCD_DC, 
             PIN_NUM_LCD_RST, PIN_NUM_LCD_CS, PIN_NUM_BK_LIGHT);

    esp_err_t ret;

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };

    ESP_LOGI(TAG, "Installing SPI bus...");
    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        blink_status(2); // 2 blinks = SPI error
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully");

    // Initialize LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ESP_LOGI(TAG, "Installing LCD panel IO...");
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        blink_status(3); // 3 blinks = IO error
        return ret;
    }
    ESP_LOGI(TAG, "LCD panel IO installed successfully");

    // Initialize LCD panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ESP_LOGI(TAG, "Installing LCD panel driver...");
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        blink_status(4); // 4 blinks = panel error
        return ret;
    }
    ESP_LOGI(TAG, "LCD panel driver installed successfully");

    // Reset and initialize panel
    ESP_LOGI(TAG, "Resetting LCD panel...");
    ret = esp_lcd_panel_reset(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initializing LCD panel...");
    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize panel: %s", esp_err_to_name(ret));
        blink_status(5); // 5 blinks = init error
        return ret;
    }

    // Set orientation and turn on display
    ESP_LOGI(TAG, "Configuring LCD panel orientation and display...");
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    // Configure backlight
    ESP_LOGI(TAG, "Configuring backlight on pin %d", PIN_NUM_BK_LIGHT);
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON);

    ESP_LOGI(TAG, "Display initialization completed successfully!");
    display_initialized = true;
    blink_status(1); // 1 blink = success
    return ESP_OK;
}

// Test display with a pattern
static esp_err_t test_display_pattern(void)
{
    if (!display_initialized || panel_handle == NULL) {
        ESP_LOGE(TAG, "Display not initialized, cannot draw test pattern");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Drawing test pattern...");
    
    // Create a simple test pattern buffer
    uint16_t *test_buffer = malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    if (test_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate display buffer");
        return ESP_ERR_NO_MEM;
    }

    // Fill with color pattern - Red, Green, Blue stripes
    for (int y = 0; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            int idx = y * LCD_H_RES + x;
            if (y < LCD_V_RES / 3) {
                test_buffer[idx] = 0xF800; // Red
            } else if (y < 2 * LCD_V_RES / 3) {
                test_buffer[idx] = 0x07E0; // Green
            } else {
                test_buffer[idx] = 0x001F; // Blue
            }
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, test_buffer);
    free(test_buffer);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Test pattern drawn successfully!");
        gpio_set_level(DEBUG_LED_PIN, 1); // Turn on debug LED to indicate success
    } else {
        ESP_LOGE(TAG, "Failed to draw test pattern: %s", esp_err_to_name(ret));
        blink_status(6); // 6 blinks = draw error
    }

    return ret;
}

// Detect WiFi capability (check if ESP32-C6 companion chip is present)
static bool detect_wifi_capability(void)
{
    ESP_LOGI(TAG, "Detecting WiFi capability...");
    
    // Try to initialize WiFi to see if it's available
    ESP_LOGI(TAG, "Testing WiFi initialization...");
    
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi initialization failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "No WiFi companion chip detected - running in display-only mode");
        return false;
    }

    ESP_LOGI(TAG, "WiFi companion chip detected! ESP32-C6 is available");
    return true;
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

// Initialize WiFi AP mode
static esp_err_t init_wifi_ap(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Access Point: %s", WIFI_SSID);

    esp_netif_create_default_wifi_ap();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

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

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s channel:%d", WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
    return ESP_OK;
}

// HTTP status endpoint handler
static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString("operational");
    cJSON *display = cJSON_CreateString(display_initialized ? "ready" : "error");
    cJSON *wifi = cJSON_CreateString("esp32_c6_companion");
    cJSON *uptime = cJSON_CreateNumber(esp_timer_get_time() / 1000000);

    cJSON_AddItemToObject(json, "status", status);
    cJSON_AddItemToObject(json, "display", display);
    cJSON_AddItemToObject(json, "wifi_mode", wifi);
    cJSON_AddItemToObject(json, "uptime_seconds", uptime);

    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Start web server
static esp_err_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t status_uri = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Rocket Display Controller v8 - ESP32-P4 Hybrid ===");
    ESP_LOGI(TAG, "Chip: ESP32-P4, MAC: %s", "Will be detected");
    ESP_LOGI(TAG, "Build date: %s %s", __DATE__, __TIME__);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize status LEDs first for debugging
    init_status_leds();
    
    // Detect WiFi capability
    wifi_available = detect_wifi_capability();
    ESP_LOGI(TAG, "WiFi capability: %s", wifi_available ? "Available (ESP32-C6 companion)" : "Not available (standalone ESP32-P4)");

    // Initialize display
    ESP_LOGI(TAG, "Starting display initialization...");
    ret = init_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        // Continue running for debugging even if display fails
    } else {
        // Test display with pattern
        vTaskDelay(pdMS_TO_TICKS(1000));
        test_display_pattern();
    }

    // Initialize WiFi if available
    if (wifi_available) {
        ESP_LOGI(TAG, "Starting WiFi services...");
        ret = init_wifi_ap();
        if (ret == ESP_OK) {
            ret = start_webserver();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Web server started - connect to %s and visit http://192.168.4.1/api/status", WIFI_SSID);
            }
        }
    }

    // Main loop
    ESP_LOGI(TAG, "Entering main loop...");
    int loop_count = 0;
    while (1) {
        // Blink status LED to show we're alive
        gpio_set_level(STATUS_LED_PIN, loop_count % 2);
        
        // Log status every 10 seconds
        if (loop_count % 10 == 0) {
            ESP_LOGI(TAG, "System status: Display=%s, WiFi=%s, Loop=%d",
                     display_initialized ? "OK" : "ERROR",
                     wifi_available ? "C6_COMPANION" : "DISABLED",
                     loop_count);
        }
        
        loop_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}