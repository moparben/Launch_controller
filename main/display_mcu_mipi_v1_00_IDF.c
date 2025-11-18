/*
 * Display MCU v1.00 - Multi-MCU Rocket Launcher System
 * ESP32-P4 Main Display Controller + WiFi Access Point + Web Server + MIPI-DSI Display
 * 
 * Filename: display_mcu_mipi_v1_00_IDF.c
 * Version: 1.00 MIPI-DSI
 * Target: ESP32-P4 (Waveshare ESP32-P4-WIFI6-DEV-KIT)
 * Display: Waveshare 10.1" MIPI-DSI (JD9365 controller)
 * Framework: ESP-IDF v5.4.2
 * 
 * Features:
 * - MIPI-DSI LCD Display (JD9365) for real-time status visualization
 * - WiFi Access Point mode for remote control coordination (ESP32-C6 via SDIO)
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
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "cJSON.h"
#include "nvs_flash.h"

// MIPI-DSI Display includes
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9365_10_1.h"

static const char *TAG = "DISPLAY_MCU_v1.00_MIPI";

// GPIO Definitions for ESP32-P4 with Display
#define LED_ARM_PIN     GPIO_NUM_15     // Red LED - Armed status
#define LED_LAUNCH_PIN  GPIO_NUM_16     // Green LED - Launch ready
#define BUTTON_ARM_PIN  GPIO_NUM_0      // ARM button (BOOT button)
#define BUTTON_RESET_PIN GPIO_NUM_1     // RESET button

// MIPI-DSI Display Configuration (10.1" JD9365)
#define LCD_WIDTH       1024
#define LCD_HEIGHT      600
#define LCD_BIT_PER_PIXEL 16

// MIPI-DSI configuration
#define MIPI_DSI_DPI_CLK_MHZ  52
#define MIPI_DSI_LCD_PIXEL_CLK_HZ (MIPI_DSI_DPI_CLK_MHZ * 1000 * 1000)
#define MIPI_DSI_LANE_NUM         2

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
static bool armed = false;
static bool launch_enabled = false;

// Display globals
esp_lcd_panel_handle_t lcd_panel = NULL;
static uint16_t *lcd_buffer = NULL;

// Timers
static esp_timer_handle_t display_timer;
static esp_timer_handle_t status_timer;

// Event groups
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// Web server
static httpd_handle_t server = NULL;

// Color definitions for RGB565
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_ORANGE  0xFD20
#define COLOR_PURPLE  0x780F

// Function to convert RGB888 to RGB565
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Display initialization
esp_err_t mipi_dsi_init(void) {
    ESP_LOGI(TAG, "Initializing MIPI-DSI 10.1\" display...");
    
    // Allocate frame buffer
    lcd_buffer = heap_caps_aligned_alloc(64, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (lcd_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LCD buffer in SPIRAM");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LCD buffer allocated: %p", lcd_buffer);

    // MIPI-DSI bus configuration
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    // DBI panel IO configuration
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    // LCD panel configuration
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = GPIO_NUM_NC, // Use software reset instead
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(mipi_dbi_io, &lcd_dev_config, &lcd_panel));

    // Initialize the LCD panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    ESP_LOGI(TAG, "MIPI-DSI display initialized successfully");
    return ESP_OK;
}

// Clear display with color
void lcd_clear(uint16_t color) {
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        lcd_buffer[i] = color;
    }
    esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, lcd_buffer);
    ESP_LOGI(TAG, "LCD Clear: Filled screen with color 0x%04X", color);
}

// Draw text at position (simple implementation)
void draw_text(int x, int y, const char* text, uint16_t color, uint16_t bg_color) {
    // Simple character drawing - you can implement bitmap fonts later
    int char_width = 8;
    int char_height = 16;
    int text_len = strlen(text);
    
    // Clear background first
    for (int cy = y; cy < y + char_height && cy < LCD_HEIGHT; cy++) {
        for (int cx = x; cx < x + (text_len * char_width) && cx < LCD_WIDTH; cx++) {
            if (cy * LCD_WIDTH + cx < LCD_WIDTH * LCD_HEIGHT) {
                lcd_buffer[cy * LCD_WIDTH + cx] = bg_color;
            }
        }
    }
    
    // For now, just draw colored blocks for each character
    for (int i = 0; i < text_len && x + i * char_width < LCD_WIDTH; i++) {
        for (int cy = y; cy < y + char_height && cy < LCD_HEIGHT; cy++) {
            for (int cx = x + i * char_width; cx < x + (i + 1) * char_width && cx < LCD_WIDTH; cx++) {
                // Simple pattern to represent characters
                if ((cy - y) % 2 == 0 || (cx - x) % 2 == 0) {
                    if (cy * LCD_WIDTH + cx < LCD_WIDTH * LCD_HEIGHT) {
                        lcd_buffer[cy * LCD_WIDTH + cx] = color;
                    }
                }
            }
        }
    }
}

// Update display with current status
void update_display(void) {
    // Clear screen with black background
    lcd_clear(COLOR_BLACK);
    
    // Title
    draw_text(300, 50, "ESP32-P4 ROCKET LAUNCHER", COLOR_WHITE, COLOR_BLACK);
    draw_text(400, 80, "Display MCU v1.00", COLOR_YELLOW, COLOR_BLACK);
    
    // Status section
    draw_text(50, 150, "SYSTEM STATUS:", COLOR_WHITE, COLOR_BLACK);
    
    switch (system_state) {
        case SYSTEM_IDLE:
            draw_text(250, 150, "IDLE", COLOR_GREEN, COLOR_BLACK);
            break;
        case SYSTEM_ARMED:
            draw_text(250, 150, "ARMED", COLOR_ORANGE, COLOR_BLACK);
            break;
        case SYSTEM_LAUNCHING:
            draw_text(250, 150, "LAUNCHING", COLOR_RED, COLOR_BLACK);
            break;
        case SYSTEM_ERROR:
            draw_text(250, 150, "ERROR", COLOR_RED, COLOR_BLACK);
            break;
    }
    
    // Armed status
    draw_text(50, 200, "ARMED:", COLOR_WHITE, COLOR_BLACK);
    draw_text(150, 200, armed ? "YES" : "NO", armed ? COLOR_RED : COLOR_GREEN, COLOR_BLACK);
    
    // Launch enabled
    draw_text(50, 250, "LAUNCH READY:", COLOR_WHITE, COLOR_BLACK);
    draw_text(200, 250, launch_enabled ? "YES" : "NO", launch_enabled ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);
    
    // WiFi status
    draw_text(50, 300, "WIFI AP:", COLOR_WHITE, COLOR_BLACK);
    draw_text(150, 300, "ACTIVE", COLOR_GREEN, COLOR_BLACK);
    draw_text(50, 330, "SSID: ESP32_ROCKET_LAUNCHER", COLOR_YELLOW, COLOR_BLACK);
    draw_text(50, 360, "PASSWORD: rocket123", COLOR_YELLOW, COLOR_BLACK);
    draw_text(50, 390, "IP: 192.168.4.1", COLOR_YELLOW, COLOR_BLACK);
    
    // Controls
    draw_text(50, 450, "CONTROLS:", COLOR_WHITE, COLOR_BLACK);
    draw_text(50, 480, "WEB: http://192.168.4.1", COLOR_BLUE, COLOR_BLACK);
    draw_text(50, 510, "BUTTON: ARM/DISARM ONLY", COLOR_PURPLE, COLOR_BLACK);
    
    // Update the display
    esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, lcd_buffer);
}

// Display color test
void display_color_test(void) {
    ESP_LOGI(TAG, "Starting MIPI-DSI color test...");
    
    // Red
    lcd_clear(COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Green  
    lcd_clear(COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Blue
    lcd_clear(COLOR_BLUE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // White
    lcd_clear(COLOR_WHITE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "MIPI-DSI color test complete, starting normal display");
}

// Initialize display
esp_err_t lcd_init(void) {
    esp_err_t ret = mipi_dsi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIPI-DSI initialization failed");
        return ret;
    }
    
    // Run color test
    display_color_test();
    
    // Show initial display
    update_display();
    
    return ESP_OK;
}

// GPIO initialization
void gpio_init(void) {
    gpio_config_t io_conf = {};
    
    // Output pins (LEDs)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_ARM_PIN) | (1ULL << LED_LAUNCH_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Input pins (Buttons)
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_ARM_PIN) | (1ULL << BUTTON_RESET_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    
    // Set initial LED states
    gpio_set_level(LED_ARM_PIN, 0);
    gpio_set_level(LED_LAUNCH_PIN, 1); // Launch LED on when not armed
    
    ESP_LOGI(TAG, "GPIO initialized");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
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

// Initialize WiFi AP
void wifi_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi AP via ESP32-C6 SDIO interface...");
    
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
    
    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", WIFI_SSID, WIFI_PASS);
}

// HTTP handlers
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char* resp_str = 
        "<!DOCTYPE html><html><head><title>ESP32-P4 Rocket Launcher</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}"
        ".container{max-width:800px;margin:0 auto;text-align:center}"
        ".status{background:#333;padding:20px;border-radius:10px;margin:20px 0}"
        ".button{padding:15px 30px;margin:10px;border:none;border-radius:5px;font-size:18px;cursor:pointer}"
        ".arm{background:#ff6b35;color:white}.disarm{background:#4CAF50;color:white}"
        ".launch{background:#ff0000;color:white;font-weight:bold}"
        ".disabled{background:#666;color:#999;cursor:not-allowed}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>🚀 ESP32-P4 ROCKET LAUNCHER 🚀</h1>"
        "<div class='status'>"
        "<h2>System Status</h2>"
        "<p id='status'>IDLE</p>"
        "<p>Armed: <span id='armed'>NO</span></p>"
        "<p>Launch Ready: <span id='ready'>NO</span></p>"
        "</div>"
        "<button class='button arm' onclick='arm()'>ARM SYSTEM</button>"
        "<button class='button disarm' onclick='disarm()'>DISARM</button><br>"
        "<button class='button launch disabled' id='launchBtn' onclick='launch()'>🚀 LAUNCH 🚀</button>"
        "</div>"
        "<script>"
        "function updateStatus(){fetch('/status').then(r=>r.json()).then(d=>{"
        "document.getElementById('status').textContent=d.state;"
        "document.getElementById('armed').textContent=d.armed?'YES':'NO';"
        "document.getElementById('ready').textContent=d.ready?'YES':'NO';"
        "document.getElementById('launchBtn').className='button launch '+(d.ready?'':'disabled');"
        "})}"
        "function arm(){fetch('/arm',{method:'POST'}).then(updateStatus)}"
        "function disarm(){fetch('/disarm',{method:'POST'}).then(updateStatus)}"
        "function launch(){fetch('/launch',{method:'POST'}).then(updateStatus)}"
        "setInterval(updateStatus,1000);updateStatus();"
        "</script></body></html>";
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    
    const char* state_str = "UNKNOWN";
    switch (system_state) {
        case SYSTEM_IDLE: state_str = "IDLE"; break;
        case SYSTEM_ARMED: state_str = "ARMED"; break;
        case SYSTEM_LAUNCHING: state_str = "LAUNCHING"; break;
        case SYSTEM_ERROR: state_str = "ERROR"; break;
    }
    
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddBoolToObject(json, "armed", armed);
    cJSON_AddBoolToObject(json, "ready", launch_enabled);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t arm_post_handler(httpd_req_t *req) {
    armed = true;
    launch_enabled = true;
    system_state = SYSTEM_ARMED;
    gpio_set_level(LED_ARM_PIN, 1);
    gpio_set_level(LED_LAUNCH_PIN, 1);
    ESP_LOGI(TAG, "System ARMED via web interface");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t disarm_post_handler(httpd_req_t *req) {
    armed = false;
    launch_enabled = false;
    system_state = SYSTEM_IDLE;
    gpio_set_level(LED_ARM_PIN, 0);
    gpio_set_level(LED_LAUNCH_PIN, 1);
    ESP_LOGI(TAG, "System DISARMED via web interface");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t launch_post_handler(httpd_req_t *req) {
    if (!armed || !launch_enabled) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "System not armed");
        return ESP_FAIL;
    }
    
    system_state = SYSTEM_LAUNCHING;
    ESP_LOGI(TAG, "🚀 LAUNCH INITIATED via web interface! 🚀");
    
    // Launch sequence
    for (int i = 3; i > 0; i--) {
        ESP_LOGI(TAG, "Launch in %d...", i);
        gpio_set_level(LED_LAUNCH_PIN, i % 2);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "🚀🚀🚀 LAUNCH! 🚀🚀🚀");
    gpio_set_level(LED_LAUNCH_PIN, 1);
    
    // Reset system after launch
    vTaskDelay(pdMS_TO_TICKS(2000));
    armed = false;
    launch_enabled = false;
    system_state = SYSTEM_IDLE;
    gpio_set_level(LED_ARM_PIN, 0);
    
    httpd_resp_send(req, "LAUNCHED", 8);
    return ESP_OK;
}

// Start web server
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        httpd_uri_t arm_uri = {
            .uri = "/arm",
            .method = HTTP_POST,
            .handler = arm_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &arm_uri);
        
        httpd_uri_t disarm_uri = {
            .uri = "/disarm",
            .method = HTTP_POST,
            .handler = disarm_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &disarm_uri);
        
        httpd_uri_t launch_uri = {
            .uri = "/launch",
            .method = HTTP_POST,
            .handler = launch_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &launch_uri);
        
        return server;
    }
    
    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// Timer callbacks
static void display_timer_callback(void* arg) {
    update_display();
}

static void status_timer_callback(void* arg) {
    // Disabled periodic status logging for cleaner serial output
    // ESP_LOGI(TAG, "Status: %s | Armed: %s | Ready: %s", 
    //          system_state == SYSTEM_IDLE ? "IDLE" : 
    //          system_state == SYSTEM_ARMED ? "ARMED" : 
    //          system_state == SYSTEM_LAUNCHING ? "LAUNCHING" : "ERROR",
    //          armed ? "YES" : "NO", 
    //          launch_enabled ? "YES" : "NO");
}

// Button handling task
void button_task(void *pvParameters) {
    int button_state = 0;
    int last_button_state = 0;
    
    while (1) {
        button_state = gpio_get_level(BUTTON_ARM_PIN);
        
        // Button pressed (active low with pullup)
        if (button_state == 0 && last_button_state == 1) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (gpio_get_level(BUTTON_ARM_PIN) == 0) {
                if (armed) {
                    // Disarm
                    armed = false;
                    launch_enabled = false;
                    system_state = SYSTEM_IDLE;
                    gpio_set_level(LED_ARM_PIN, 0);
                    gpio_set_level(LED_LAUNCH_PIN, 1);
                    ESP_LOGI(TAG, "System DISARMED via button");
                } else {
                    // Arm
                    armed = true;
                    launch_enabled = true;
                    system_state = SYSTEM_ARMED;
                    gpio_set_level(LED_ARM_PIN, 1);
                    gpio_set_level(LED_LAUNCH_PIN, 1);
                    ESP_LOGI(TAG, "System ARMED via button");
                }
            }
        }
        
        last_button_state = button_state;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Serial command task (simplified for display MCU)
void serial_command_task(void *pvParameters) {
    char command[64];
    int pos = 0;
    
    while (1) {
        int c = getchar();
        if (c != EOF && c != '\n' && c != '\r') {
            if (pos < sizeof(command) - 1) {
                command[pos++] = c;
            }
        } else if (c == '\n' || c == '\r') {
            command[pos] = '\0';
            pos = 0;
            
            if (strlen(command) > 0) {
                if (strcmp(command, "help") == 0) {
                    printf("ESP32-P4 Display MCU v1.00 Commands:\n");
                    printf("  help     - Show this help\n");
                    printf("  status   - Show system status\n");
                    printf("  arm      - Arm the system\n");
                    printf("  disarm   - Disarm the system\n");
                    printf("  display  - Update display\n");
                    printf("  wifi_info - Show WiFi information\n");
                } else if (strcmp(command, "status") == 0) {
                    printf("System Status: %s\n", 
                           system_state == SYSTEM_IDLE ? "IDLE" : 
                           system_state == SYSTEM_ARMED ? "ARMED" : 
                           system_state == SYSTEM_LAUNCHING ? "LAUNCHING" : "ERROR");
                    printf("Armed: %s\n", armed ? "YES" : "NO");
                    printf("Launch Ready: %s\n", launch_enabled ? "YES" : "NO");
                } else if (strcmp(command, "arm") == 0) {
                    armed = true;
                    launch_enabled = true;
                    system_state = SYSTEM_ARMED;
                    gpio_set_level(LED_ARM_PIN, 1);
                    gpio_set_level(LED_LAUNCH_PIN, 1);
                    printf("System ARMED\n");
                } else if (strcmp(command, "disarm") == 0) {
                    armed = false;
                    launch_enabled = false;
                    system_state = SYSTEM_IDLE;
                    gpio_set_level(LED_ARM_PIN, 0);
                    gpio_set_level(LED_LAUNCH_PIN, 1);
                    printf("System DISARMED\n");
                } else if (strcmp(command, "display") == 0) {
                    update_display();
                    printf("Display updated\n");
                } else if (strcmp(command, "wifi_info") == 0) {
                    printf("WiFi AP Information:\n");
                    printf("  SSID: %s\n", WIFI_SSID);
                    printf("  Password: %s\n", WIFI_PASS);
                    printf("  Channel: %d\n", WIFI_CHANNEL);
                    printf("  IP: 192.168.4.1\n");
                    printf("  Web Interface: http://192.168.4.1\n");
                } else {
                    printf("Unknown command: %s (type 'help' for commands)\n", command);
                }
                printf("ESP32-P4> ");
                fflush(stdout);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "🚀 ESP32-P4 Display MCU v1.00 Starting... 🚀");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize GPIO
    gpio_init();
    
    // Initialize MIPI-DSI Display
    lcd_init();
    
    // Initialize WiFi (ESP32-C6 via SDIO)
    wifi_init();
    
    // Start web server
    start_webserver();
    
    // Create status timer (disabled output)
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
    esp_timer_start_periodic(display_timer, 2000000); // 2 seconds
    
    // Create tasks
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(serial_command_task, "serial_task", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "🔥 DISPLAY MCU v1.00 READY FOR MULTI-MCU OPERATION! 🔥");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  🖥️  Display: Real-time status visualization (MIPI-DSI 10.1\")");
    ESP_LOGI(TAG, "  🌐 Web: http://192.168.4.1 (WiFi: %s / %s)", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "  🔧 Serial: Full diagnostic toolkit (type 'help' for commands)");
    ESP_LOGI(TAG, "  🔘 Button GPIO0: Arm/Disarm system (launch via web only)");
    ESP_LOGI(TAG, "  📡 WiFi: ESP32-C6 SDIO interface");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    ESP_LOGI(TAG, "All systems go! Display MCU v1.00 active and ready! 🖥️🚀");
    printf("ESP32-P4> ");
    fflush(stdout);
}