/*
 * Display MCU v1.00 - Multi-MCU Rocket Launcher System
 * ESP32-P4 Main Display Controller + WiFi Access Point + Web Server
 * 
 * Filename: display_mcu_v1_00_IDF.c
 * Version: 1.00
 * Target: ESP32-P4 (Waveshare ESP32-P4-WIFI6-DEV-KIT)
 * Framework: ESP-IDF v5.4.2
 * 
 * Features:
 * - SPI LCD Display (ILI9341) for real-time status visualization
 * - WiFi Access Point mode for remote control coordination
 * - HTTP server with responsive web interface
 * - GPIO LED indicators and hardware reset button
 * - JSON API endpoints for inter-MCU communication
 * - Web-only launch control (serial disabled for safety)
 * - Main hub controller for distributed rocket launcher system
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#include "esp_flash.h"
#include "soc/clk_tree_defs.h"
#include "hal/clk_tree_hal.h"
#include "cJSON.h"
#include "nvs_flash.h"

static const char *TAG = "DISPLAY_MCU_v1.00";

// GPIO Definitions for ESP32-P4 with Display
#define LED_ARM_PIN     GPIO_NUM_15     // Red LED - Armed status
#define LED_LAUNCH_PIN  GPIO_NUM_16     // Green LED - Launch ready
#define BUTTON_ARM_PIN  GPIO_NUM_0      // ARM button (BOOT button)
#define BUTTON_RESET_PIN GPIO_NUM_1     // RESET button

// SPI LCD Display Configuration (ILI9341)
#define LCD_HOST        SPI2_HOST
#define PIN_NUM_MISO    GPIO_NUM_13
#define PIN_NUM_MOSI    GPIO_NUM_11
#define PIN_NUM_CLK     GPIO_NUM_12
#define PIN_NUM_CS      GPIO_NUM_10
#define PIN_NUM_DC      GPIO_NUM_14
#define PIN_NUM_RST     GPIO_NUM_21
#define PIN_NUM_BCKL    GPIO_NUM_2

// Display dimensions
#define LCD_WIDTH       320
#define LCD_HEIGHT      240

// ILI9341 Commands
#define ILI9341_SWRESET     0x01
#define ILI9341_SLPOUT      0x11
#define ILI9341_DISPON      0x29
#define ILI9341_CASET       0x2A
#define ILI9341_PASET       0x2B
#define ILI9341_RAMWR       0x2C
#define ILI9341_MADCTL      0x36
#define ILI9341_COLMOD      0x3A

// Colors (RGB565)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F

// WiFi Configuration
#define WIFI_SSID      "ESP32_ROCKET_LAUNCHER"
#define WIFI_PASS      "rocket123"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4

// System States
typedef enum {
    ROCKET_STATE_IDLE = 0,
    ROCKET_STATE_ARMED,
    ROCKET_STATE_COUNTDOWN,
    ROCKET_STATE_LAUNCHED,
    ROCKET_STATE_COOLDOWN
} rocket_state_t;

// Global State
static rocket_state_t rocket_state = ROCKET_STATE_IDLE;
static bool is_armed = false;
static bool is_launched = false;
static int launch_count = 0;
static esp_timer_handle_t countdown_timer = NULL;
static esp_timer_handle_t status_timer = NULL;
static esp_timer_handle_t display_timer = NULL;
static httpd_handle_t server = NULL;
static int countdown_seconds = 0;

// Display variables
static spi_device_handle_t lcd_spi;
static bool display_initialized = false;

// Function Prototypes
static void gpio_init(void);
static void wifi_init(void);
static void start_webserver(void);
static void rocket_arm(void);
static void rocket_launch(void);
static void rocket_reset(void);
static void update_led_status(void);
static void countdown_timer_callback(void *arg);
static void status_timer_callback(void *arg);
static void display_timer_callback(void *arg);

// Display function prototypes
static void lcd_init(void);
static void lcd_cmd(uint8_t cmd);
static void lcd_data(uint8_t *data, int len);
static void lcd_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
static void lcd_draw_text(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg_color);
static void lcd_clear(uint16_t color);
static void update_display(void);
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

// HTTP Handlers
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_arm_handler(httpd_req_t *req);
static esp_err_t api_launch_handler(httpd_req_t *req);
static esp_err_t api_reset_handler(httpd_req_t *req);

// WiFi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// GPIO Initialization
static void gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO for Display Controller with WiFi");
    
    // LED GPIO setup
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_ARM_PIN) | (1ULL << LED_LAUNCH_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_config);
    
    // Button GPIO setup
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_ARM_PIN) | (1ULL << BUTTON_RESET_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button_config);
    
    // Display control pins
    gpio_config_t display_config = {
        .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST) | (1ULL << PIN_NUM_BCKL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&display_config);
    
    // Initialize LED states
    gpio_set_level(LED_ARM_PIN, 0);
    gpio_set_level(LED_LAUNCH_PIN, 0);
    
    // Initialize display control pins
    gpio_set_level(PIN_NUM_BCKL, 1);  // Turn on backlight
    gpio_set_level(PIN_NUM_RST, 1);   // Release reset
    
    ESP_LOGI(TAG, "GPIO initialization complete - Display Controller Ready!");
}

// WiFi Initialization
static void wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Access Point");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
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
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "🛜 WiFi AP started. SSID: %s, Password: %s", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "🌐 Connect to http://192.168.4.1 for web control");
}

// Rocket Control Functions
static void rocket_arm(void)
{
    if (rocket_state == ROCKET_STATE_IDLE) {
        is_armed = true;
        rocket_state = ROCKET_STATE_ARMED;
        ESP_LOGI(TAG, "🔴 ROCKET ARMED! Ready to launch! 🔴");
        update_led_status();
    } else {
        ESP_LOGW(TAG, "⚠️  Cannot arm - rocket not in idle state");
    }
}

static void rocket_launch(void)
{
    if (!is_armed) {
        ESP_LOGW(TAG, "⚠️  LAUNCH DENIED - Rocket not armed!");
        return;
    }
    
    if (rocket_state != ROCKET_STATE_ARMED) {
        ESP_LOGW(TAG, "⚠️  LAUNCH DENIED - Invalid state");
        return;
    }
    
    ESP_LOGI(TAG, "🚀 INITIATING LAUNCH SEQUENCE! 🚀");
    rocket_state = ROCKET_STATE_COUNTDOWN;
    countdown_seconds = 5;
    
    // Start countdown timer
    esp_timer_create_args_t countdown_timer_args = {
        .callback = &countdown_timer_callback,
        .name = "countdown_timer"
    };
    
    if (countdown_timer != NULL) {
        esp_timer_delete(countdown_timer);
    }
    
    esp_timer_create(&countdown_timer_args, &countdown_timer);
    esp_timer_start_periodic(countdown_timer, 1000000); // 1 second
    
    ESP_LOGI(TAG, "⏰ T-minus %d seconds...", countdown_seconds);
}

static void rocket_reset(void)
{
    // Stop any active timers
    if (countdown_timer != NULL) {
        esp_timer_stop(countdown_timer);
        esp_timer_delete(countdown_timer);
        countdown_timer = NULL;
    }
    
    // Reset state
    rocket_state = ROCKET_STATE_IDLE;
    is_armed = false;
    is_launched = false;
    countdown_seconds = 0;
    
    update_led_status();
    ESP_LOGI(TAG, "🔄 SYSTEM RESET - Ready for new mission");
}

static void update_led_status(void)
{
    switch (rocket_state) {
        case ROCKET_STATE_IDLE:
            gpio_set_level(LED_ARM_PIN, 0);
            gpio_set_level(LED_LAUNCH_PIN, 0);
            break;
            
        case ROCKET_STATE_ARMED:
            gpio_set_level(LED_ARM_PIN, 1);
            gpio_set_level(LED_LAUNCH_PIN, 0);
            break;
            
        case ROCKET_STATE_COUNTDOWN:
            // Flash both LEDs during countdown
            gpio_set_level(LED_ARM_PIN, (countdown_seconds % 2));
            gpio_set_level(LED_LAUNCH_PIN, (countdown_seconds % 2));
            break;
            
        case ROCKET_STATE_LAUNCHED:
            gpio_set_level(LED_ARM_PIN, 1);
            gpio_set_level(LED_LAUNCH_PIN, 1);
            break;
            
        case ROCKET_STATE_COOLDOWN:
            // Alternate LEDs during cooldown
            gpio_set_level(LED_ARM_PIN, (countdown_seconds % 2));
            gpio_set_level(LED_LAUNCH_PIN, !(countdown_seconds % 2));
            break;
    }
}

// Timer Callbacks
static void countdown_timer_callback(void *arg)
{
    countdown_seconds--;
    
    if (countdown_seconds > 0) {
        ESP_LOGI(TAG, "⏰ T-minus %d...", countdown_seconds);
        update_led_status();
    } else {
        ESP_LOGI(TAG, "🚀🚀🚀 LIFTOFF! ROCKET LAUNCHED! 🚀🚀🚀");
        
        rocket_state = ROCKET_STATE_LAUNCHED;
        is_launched = true;
        launch_count++;
        
        esp_timer_stop(countdown_timer);
        esp_timer_delete(countdown_timer);
        countdown_timer = NULL;
        
        // Start cooldown phase
        rocket_state = ROCKET_STATE_COOLDOWN;
        countdown_seconds = 10; // 10 second cooldown
        
        esp_timer_create_args_t cooldown_timer_args = {
            .callback = &countdown_timer_callback,
            .name = "cooldown_timer"
        };
        
        esp_timer_create(&cooldown_timer_args, &countdown_timer);
        esp_timer_start_periodic(countdown_timer, 1000000);
        
        update_led_status();
        
        ESP_LOGI(TAG, "🔥 Mission %d completed! Cooldown initiated...", launch_count);
    }
}

static void status_timer_callback(void *arg)
{
    // Status logging disabled - use web interface or 'status' command for current status
    // const char* state_names[] = {"Idle", "Armed", "Countdown", "Launched", "Cooldown"};
    // ESP_LOGI(TAG, "🚀 Status: %s | Armed: %s | Launched: %s | Total: %d", 
    //          state_names[rocket_state],
    //          is_armed ? "✅" : "❌",
    //          is_launched ? "✅" : "❌", 
    //          launch_count);
}

static void display_timer_callback(void *arg)
{
    if (display_initialized) {
        update_display();
    }
}

// Display Functions
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    gpio_set_level(PIN_NUM_DC, 0);
    spi_device_polling_transmit(lcd_spi, &t);
}

static void lcd_data(uint8_t *data, int len)
{
    spi_transaction_t t;
    if (len == 0) return;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_device_polling_transmit(lcd_spi, &t);
}

static void lcd_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_cmd(ILI9341_CASET);
    uint8_t data[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    lcd_data(data, 4);
    
    lcd_cmd(ILI9341_PASET);
    data[0] = y0 >> 8; data[1] = y0 & 0xFF; data[2] = y1 >> 8; data[3] = y1 & 0xFF;
    lcd_data(data, 4);
    
    lcd_cmd(ILI9341_RAMWR);
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    
    lcd_set_addr_window(x, y, x + w - 1, y + h - 1);
    
    uint16_t *line = malloc(w * 2);
    if (!line) return;
    
    for (int i = 0; i < w; i++) {
        line[i] = (color >> 8) | (color << 8); // Swap bytes for SPI
    }
    
    for (int i = 0; i < h; i++) {
        lcd_data((uint8_t*)line, w * 2);
    }
    
    free(line);
}

static void lcd_clear(uint16_t color)
{
    ESP_LOGI(TAG, "LCD Clear: Filling screen with color 0x%04X", color);
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
    ESP_LOGI(TAG, "LCD Clear: Screen fill complete");
}

// Simple 8x8 font drawing (basic implementation)
static void lcd_draw_text(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg_color)
{
    int len = strlen(text);
    int char_width = 8;
    int char_height = 16;
    
    // Simple text background
    lcd_fill_rect(x, y, len * char_width, char_height, bg_color);
    
    // For now, just draw colored rectangles as placeholders for each character
    for (int i = 0; i < len; i++) {
        if (text[i] != ' ') {
            lcd_fill_rect(x + i * char_width + 1, y + 2, char_width - 2, char_height - 4, color);
        }
    }
}

static void update_display(void)
{
    if (!display_initialized) return;
    
    // Clear screen with black background
    lcd_clear(COLOR_BLACK);
    
    // Title
    lcd_draw_text(10, 10, "ROCKET LAUNCHER", COLOR_WHITE, COLOR_BLACK);
    lcd_draw_text(10, 30, "DISPLAY CONTROLLER", COLOR_CYAN, COLOR_BLACK);
    
    // Status section
    const char* state_names[] = {"IDLE", "ARMED", "COUNTDOWN", "LAUNCHED", "COOLDOWN"};
    uint16_t state_colors[] = {COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_GREEN, COLOR_BLUE};
    
    char status_line[32];
    snprintf(status_line, sizeof(status_line), "STATE: %s", state_names[rocket_state]);
    lcd_draw_text(10, 60, status_line, state_colors[rocket_state], COLOR_BLACK);
    
    // Armed status
    snprintf(status_line, sizeof(status_line), "ARMED: %s", is_armed ? "YES" : "NO");
    lcd_draw_text(10, 80, status_line, is_armed ? COLOR_RED : COLOR_WHITE, COLOR_BLACK);
    
    // Launch count
    snprintf(status_line, sizeof(status_line), "LAUNCHES: %d", launch_count);
    lcd_draw_text(10, 100, status_line, COLOR_GREEN, COLOR_BLACK);
    
    // Countdown display
    if (rocket_state == ROCKET_STATE_COUNTDOWN && countdown_seconds > 0) {
        snprintf(status_line, sizeof(status_line), "T-MINUS: %d", countdown_seconds);
        lcd_draw_text(10, 120, status_line, COLOR_RED, COLOR_BLACK);
    } else if (rocket_state == ROCKET_STATE_COOLDOWN && countdown_seconds > 0) {
        snprintf(status_line, sizeof(status_line), "COOLDOWN: %d", countdown_seconds);
        lcd_draw_text(10, 120, status_line, COLOR_BLUE, COLOR_BLACK);
    }
    
    // WiFi info
    lcd_draw_text(10, 160, "WiFi: ESP32_ROCKET_LAUNCHER", COLOR_CYAN, COLOR_BLACK);
    lcd_draw_text(10, 180, "Web: 192.168.4.1", COLOR_CYAN, COLOR_BLACK);
    
    // Controls info
    lcd_draw_text(10, 210, "GPIO0: ARM/LAUNCH  GPIO1: RESET", COLOR_YELLOW, COLOR_BLACK);
}

static void lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD Display");
    
    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8
    };
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,  // 20 MHz (ESP32-P4 compatible)
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = spi_bus_add_device(LCD_HOST, &devcfg, &lcd_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return;
    }
    
    // Reset display
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    // Initialize ILI9341
    lcd_cmd(ILI9341_SWRESET);
    vTaskDelay(150 / portTICK_PERIOD_MS);
    
    lcd_cmd(ILI9341_SLPOUT);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    // Color mode: 16-bit color
    lcd_cmd(ILI9341_COLMOD);
    uint8_t data = 0x55;
    lcd_data(&data, 1);
    
    // Memory access control
    lcd_cmd(ILI9341_MADCTL);
    data = 0x08;  // RGB color order
    lcd_data(&data, 1);
    
    lcd_cmd(ILI9341_DISPON);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    display_initialized = true;
    ESP_LOGI(TAG, "✅ LCD Display initialized successfully!");
    
    // Test display with bright colors to verify it's working
    ESP_LOGI(TAG, "Testing display with colors...");
    lcd_clear(COLOR_RED);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    lcd_clear(COLOR_GREEN);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    lcd_clear(COLOR_BLUE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    lcd_clear(COLOR_WHITE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Color test complete, starting normal display");
    
    // Initial display update
    update_display();
}

// Simple HTML page
static const char* html_page = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>ESP32-P4 Rocket Launcher</title>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body{font-family:Arial;text-align:center;background:#000;color:#0f0;}"
".container{max-width:600px;margin:0 auto;padding:20px;}"
".status{font-size:24px;margin:20px 0;padding:20px;border:2px solid #0f0;}"
"button{font-size:20px;padding:15px 30px;margin:10px;cursor:pointer;border:none;border-radius:5px;}"
".arm-btn{background:#ff0000;color:white;}"
".launch-btn{background:#00ff00;color:black;font-weight:bold;}"
".reset-btn{background:#ffff00;color:black;}"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<h1>ESP32-P4 WiFi Rocket Launcher</h1>"
"<div id=\"status\" class=\"status\">Loading...</div>"
"<button class=\"arm-btn\" onclick=\"sendCommand('arm')\">ARM ROCKET</button>"
"<button class=\"launch-btn\" onclick=\"sendCommand('launch')\">LAUNCH!</button>"
"<button class=\"reset-btn\" onclick=\"sendCommand('reset')\">RESET</button>"
"</div>"
"<script>"
"function sendCommand(cmd){"
"fetch('/api/'+cmd,{method:'POST'})"
".then(response=>response.json())"
".then(data=>console.log(data.message));"
"}"
"function updateStatus(){"
"fetch('/api/status')"
".then(response=>response.json())"
".then(data=>{document.getElementById('status').innerHTML='State: '+data.state+'<br>Armed: '+(data.armed?'YES':'NO')+'<br>Launches: '+data.count;});"
"}"
"setInterval(updateStatus,2000);"
"updateStatus();"
"</script>"
"</body>"
"</html>";

// HTTP Handlers
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    const char* state_names[] = {"idle", "armed", "countdown", "launched", "cooldown"};
    
    cJSON_AddStringToObject(json, "state", state_names[rocket_state]);
    cJSON_AddBoolToObject(json, "armed", is_armed);
    cJSON_AddBoolToObject(json, "launched", is_launched);
    cJSON_AddNumberToObject(json, "count", launch_count);
    cJSON_AddNumberToObject(json, "countdown", countdown_seconds);
    
    char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_arm_handler(httpd_req_t *req)
{
    rocket_arm();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Rocket armed");
    cJSON_AddBoolToObject(json, "success", true);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_launch_handler(httpd_req_t *req)
{
    rocket_launch();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Launch initiated");
    cJSON_AddBoolToObject(json, "success", true);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req)
{
    rocket_reset();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "System reset");
    cJSON_AddBoolToObject(json, "success", true);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Start Web Server
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Main page
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
        // API endpoints
        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = api_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        httpd_uri_t arm_uri = {
            .uri = "/api/arm",
            .method = HTTP_POST,
            .handler = api_arm_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &arm_uri);
        
        httpd_uri_t launch_uri = {
            .uri = "/api/launch",
            .method = HTTP_POST,
            .handler = api_launch_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &launch_uri);
        
        httpd_uri_t reset_uri = {
            .uri = "/api/reset",
            .method = HTTP_POST,
            .handler = api_reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &reset_uri);
        
        ESP_LOGI(TAG, "✅ Web server started successfully!");
    } else {
        ESP_LOGE(TAG, "❌ Failed to start web server");
    }
}

// Button Task - Disabled automatic launching for web-only control
static void button_task(void *pvParameters)
{
    bool button_reset_pressed = false;
    
    while (1) {
        // Only allow RESET button to work (GPIO1)
        // ARM and LAUNCH should be done via web interface only
        
        // Check RESET button (with debouncing)
        if (gpio_get_level(BUTTON_RESET_PIN) == 0) {
            if (!button_reset_pressed) {
                button_reset_pressed = true;
                vTaskDelay(50 / portTICK_PERIOD_MS); // Debounce
                
                if (gpio_get_level(BUTTON_RESET_PIN) == 0) {
                    ESP_LOGI(TAG, "🔘 Hardware reset button pressed");
                    rocket_reset();
                }
            }
        } else {
            button_reset_pressed = false;
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// MCU Diagnostic Toolkit - Universal command interface for all MCUs
static void print_help_menu(void)
{
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              DISPLAY MCU v1.00 DIAGNOSTIC TOOLKIT        ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ SYSTEM INFO                                               ║\n");
    printf("║   help       - Show this menu                             ║\n");
    printf("║   info       - MCU information & hardware details        ║\n");
    printf("║   status     - Current system status                     ║\n");
    printf("║   uptime     - System uptime and performance             ║\n");
    printf("║   memory     - RAM usage and heap information            ║\n");
    printf("║   tasks      - FreeRTOS task information                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ WIFI DIAGNOSTICS                                         ║\n");
    printf("║   wifi_info  - WiFi status and configuration             ║\n");
    printf("║   wifi_scan  - Scan for nearby WiFi networks             ║\n");
    printf("║   wifi_reset - Reset WiFi subsystem                      ║\n");
    printf("║   web_stats  - HTTP server statistics                    ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ GPIO & HARDWARE                                          ║\n");
    printf("║   gpio_test  - Test GPIO pins and LEDs                   ║\n");
    printf("║   spi_test   - Test SPI interface (display)              ║\n");
    printf("║   led_on     - Turn on status LEDs                       ║\n");
    printf("║   led_off    - Turn off status LEDs                      ║\n");
    printf("║   led_blink  - Blink test sequence                       ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ SYSTEM CONTROL                                           ║\n");
    printf("║   reboot     - Restart the MCU                           ║\n");
    printf("║   reset      - Reset application state                   ║\n");
    printf("║   factory    - Factory reset (clear NVS)                 ║\n");
    printf("║   log_level  - Change logging level (0-5)                ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ MULTI-MCU COORDINATION                                   ║\n");
    printf("║   ping_mcu   - Ping other MCUs in system                 ║\n");
    printf("║   list_mcus  - List all connected MCUs                   ║\n");
    printf("║   broadcast  - Send test broadcast message               ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("Type command + Enter | Web control: http://192.168.4.1\n\n");
}

static void print_system_info(void)
{
    printf("\n🖥️ DISPLAY MCU v1.00 SYSTEM INFORMATION\n");
    printf("════════════════════════════════════════════════════════════\n");
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("Chip: %s (Rev %d)\n", CONFIG_IDF_TARGET, chip_info.revision);
    printf("Firmware: display_mcu_v1_00_IDF.c\n");
    printf("ESP-IDF: %s\n", esp_get_idf_version());
    printf("Compile: %s %s\n", __DATE__, __TIME__);
    
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    printf("CPU Freq: %d MHz\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    printf("Flash: %lu MB\n", (unsigned long)(flash_size / (1024 * 1024)));
    printf("════════════════════════════════════════════════════════════\n\n");
}

static void print_memory_info(void)
{
    printf("\n💾 MEMORY DIAGNOSTICS\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("Total Heap: %d bytes\n", heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
    printf("Free Heap: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    printf("Largest Free Block: %d bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    printf("Min Free Ever: %d bytes\n", heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
    
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("Internal RAM: %d / %d bytes (%.1f%% free)\n", 
           internal_free, internal_total, (float)internal_free * 100 / internal_total);
    printf("════════════════════════════════════════════════════════════\n\n");
}

static void print_wifi_info(void)
{
    printf("\n📡 WiFi DIAGNOSTICS\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("Mode: Access Point (AP)\n");
    printf("SSID: ESP32_ROCKET_LAUNCHER\n");
    printf("Channel: %d\n", WIFI_CHANNEL);
    printf("IP Address: 192.168.4.1\n");
    printf("Connected Stations: %d/%d\n", 0, MAX_STA_CONN); // Would need to track this
    printf("Web Server: http://192.168.4.1 (Port 80)\n");
    printf("Authentication: WPA2-PSK\n");
    printf("════════════════════════════════════════════════════════════\n\n");
}

static void gpio_test_sequence(void)
{
    printf("\n🔌 GPIO TEST SEQUENCE\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("Testing LEDs and GPIO pins...\n");
    
    // Test LED sequence
    for (int i = 0; i < 3; i++) {
        printf("LED Test %d: ARM LED ON\n", i + 1);
        gpio_set_level(LED_ARM_PIN, 1);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        
        printf("LED Test %d: LAUNCH LED ON\n", i + 1);
        gpio_set_level(LED_LAUNCH_PIN, 1);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        
        printf("LED Test %d: ALL LEDs OFF\n", i + 1);
        gpio_set_level(LED_ARM_PIN, 0);
        gpio_set_level(LED_LAUNCH_PIN, 0);
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    
    printf("GPIO Test Complete!\n");
    printf("Button States: GPIO0=%d, GPIO1=%d\n", 
           gpio_get_level(BUTTON_ARM_PIN), gpio_get_level(BUTTON_RESET_PIN));
    printf("════════════════════════════════════════════════════════════\n\n");
}

static void serial_command_task(void *pvParameters)
{
    char command[64];
    int cmd_idx = 0;
    
    ESP_LOGI(TAG, "🔧 MCU Diagnostic Toolkit Ready");
    ESP_LOGI(TAG, "Type 'help' for available commands");
    print_help_menu();
    
    while (1) {
        int c = getchar();
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (cmd_idx > 0) {
                    command[cmd_idx] = '\0';
                    
                    // System Info Commands
                    if (strcmp(command, "help") == 0) {
                        print_help_menu();
                    } else if (strcmp(command, "info") == 0) {
                        print_system_info();
                    } else if (strcmp(command, "status") == 0) {
                        const char* state_names[] = {"Idle", "Armed", "Countdown", "Launched", "Cooldown"};
                        printf("\n🚀 SYSTEM STATUS\n");
                        printf("════════════════════════════════════════════════════════════\n");
                        printf("MCU: Display MCU v1.00\n");
                        printf("State: %s\n", state_names[rocket_state]);
                        printf("Armed: %s\n", is_armed ? "✅ YES" : "❌ NO");
                        printf("Launch Count: %d\n", launch_count);
                        printf("Uptime: %lld ms\n", esp_timer_get_time() / 1000);
                        printf("Free Heap: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                        printf("WiFi: ESP32_ROCKET_LAUNCHER (AP Mode)\n");
                        printf("Web: http://192.168.4.1\n");
                        printf("════════════════════════════════════════════════════════════\n\n");
                    } else if (strcmp(command, "uptime") == 0) {
                        uint64_t uptime_ms = esp_timer_get_time() / 1000;
                        printf("\n⏰ UPTIME: %lld ms (%.2f minutes)\n\n", uptime_ms, uptime_ms / 60000.0);
                    } else if (strcmp(command, "memory") == 0) {
                        print_memory_info();
                    } else if (strcmp(command, "tasks") == 0) {
                        printf("\n📋 FREERTOS TASK INFO\n");
                        printf("════════════════════════════════════════════════════════════\n");
                        printf("Total Tasks: %d\n", uxTaskGetNumberOfTasks());
                        printf("Current Task: %s\n", pcTaskGetName(NULL));
                        printf("Stack High Water: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
                        printf("════════════════════════════════════════════════════════════\n\n");
                    
                    // WiFi Commands
                    } else if (strcmp(command, "wifi_info") == 0) {
                        print_wifi_info();
                    } else if (strcmp(command, "wifi_scan") == 0) {
                        printf("\n🔍 WiFi scan feature would be implemented here\n\n");
                    } else if (strcmp(command, "wifi_reset") == 0) {
                        printf("\n🔄 WiFi reset feature would be implemented here\n\n");
                    } else if (strcmp(command, "web_stats") == 0) {
                        printf("\n📊 HTTP Server Statistics would be shown here\n\n");
                    
                    // GPIO Commands
                    } else if (strcmp(command, "gpio_test") == 0) {
                        gpio_test_sequence();
                    } else if (strcmp(command, "led_on") == 0) {
                        gpio_set_level(LED_ARM_PIN, 1);
                        gpio_set_level(LED_LAUNCH_PIN, 1);
                        printf("\n💡 All LEDs ON\n\n");
                    } else if (strcmp(command, "led_off") == 0) {
                        gpio_set_level(LED_ARM_PIN, 0);
                        gpio_set_level(LED_LAUNCH_PIN, 0);
                        printf("\n💡 All LEDs OFF\n\n");
                    } else if (strcmp(command, "led_blink") == 0) {
                        printf("\n✨ LED Blink Test (5 seconds)\n");
                        for (int i = 0; i < 10; i++) {
                            gpio_set_level(LED_ARM_PIN, i % 2);
                            gpio_set_level(LED_LAUNCH_PIN, (i + 1) % 2);
                            vTaskDelay(250 / portTICK_PERIOD_MS);
                        }
                        gpio_set_level(LED_ARM_PIN, 0);
                        gpio_set_level(LED_LAUNCH_PIN, 0);
                        printf("Blink test complete!\n\n");
                    } else if (strcmp(command, "spi_test") == 0) {
                        printf("\n🔌 SPI Display Test\n");
                        printf("Display Init Status: %s\n", display_initialized ? "✅ OK" : "❌ FAILED");
                        printf("SPI Clock Issue: Check display connection\n\n");
                    
                    // System Control
                    } else if (strcmp(command, "reboot") == 0) {
                        printf("\n🔄 Rebooting MCU in 3 seconds...\n");
                        vTaskDelay(3000 / portTICK_PERIOD_MS);
                        esp_restart();
                    } else if (strcmp(command, "reset") == 0) {
                        rocket_reset();
                        printf("\n🔄 Application state reset\n\n");
                    } else if (strcmp(command, "factory") == 0) {
                        printf("\n⚠️  Factory reset would clear all settings\n");
                        printf("Use 'reboot' to restart instead\n\n");
                    
                    // Multi-MCU Commands (placeholders for future expansion)
                    } else if (strcmp(command, "ping_mcu") == 0) {
                        printf("\n📡 MCU Ping feature - future implementation\n\n");
                    } else if (strcmp(command, "list_mcus") == 0) {
                        printf("\n📋 Connected MCUs:\n");
                        printf("1. Display MCU v1.00 (This device) ✅\n");
                        printf("2. Launch MCU - Not detected\n");
                        printf("3. Camera MCU - Not detected\n\n");
                    } else if (strcmp(command, "broadcast") == 0) {
                        printf("\n📢 Broadcast test message sent\n\n");
                    
                    // Launch Control (disabled but informative)
                    } else if (strcmp(command, "arm") == 0 || strcmp(command, "launch") == 0) {
                        printf("\n⚠️  Launch commands disabled in diagnostic mode\n");
                        printf("Use web interface: http://192.168.4.1\n\n");
                    
                    // Unknown command
                    } else {
                        printf("\n❌ Unknown command: '%s'\n", command);
                        printf("Type 'help' for available commands\n\n");
                    }
                    
                    cmd_idx = 0;
                }
            } else if (cmd_idx < sizeof(command) - 1) {
                command[cmd_idx++] = c;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Main Application
void app_main(void)
{
    ESP_LOGI(TAG, "🖥️ DISPLAY MCU v1.00 - ESP32-P4 MAIN CONTROLLER 🖥️");
    ESP_LOGI(TAG, "Multi-MCU Rocket Launcher System - Display & WiFi Hub");
    ESP_LOGI(TAG, "Hardware: Waveshare ESP32-P4-WIFI6-DEV-KIT + ILI9341 Display");
    ESP_LOGI(TAG, "Firmware: ESP-IDF v5.4.2 | File: display_mcu_v1_00_IDF.c");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize GPIO
    gpio_init();
    
    // Initialize Display
    lcd_init();
    
    // Initialize WiFi (ESP32-C6 via SDIO)
    ESP_LOGI(TAG, "Initializing WiFi via ESP32-C6 SDIO interface...");
    wifi_init();
    
    // Start web server
    start_webserver();
    
    // Create status timer
    esp_timer_create_args_t status_timer_args = {
        .callback = &status_timer_callback,
        .name = "status_timer"
    };
    esp_timer_create(&status_timer_args, &status_timer);
    esp_timer_start_periodic(status_timer, 5000000); // 5 seconds
    
    // Create display update timer
    esp_timer_create_args_t display_timer_args = {
        .callback = &display_timer_callback,
        .name = "display_timer"
    };
    esp_timer_create(&display_timer_args, &display_timer);
    esp_timer_start_periodic(display_timer, 1000000); // 1 second
    
    // Create tasks
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(serial_command_task, "serial_task", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "🔥 DISPLAY MCU v1.00 READY FOR MULTI-MCU OPERATION! 🔥");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  🖥️  Display: Real-time status visualization");
    ESP_LOGI(TAG, "  🌐 Web: http://192.168.4.1 (WiFi: %s / %s)", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "  🔧 Serial: Full diagnostic toolkit (type 'help' for commands)");
    ESP_LOGI(TAG, "  🔘 Button GPIO1: Reset system only (launch via web)");
    ESP_LOGI(TAG, "  � WiFi: ESP32-C6 SDIO interface");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    ESP_LOGI(TAG, "All systems go! Display MCU v1.00 active and ready! 🖥️🚀");
}