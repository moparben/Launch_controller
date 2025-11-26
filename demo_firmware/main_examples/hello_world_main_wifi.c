/*
 * ESP32-P4 WiFi Rocket Launcher System
 * Complete version with HTTP server for remote control
 * 
 * Features:
 * - GPIO LED indicators (pins 15, 16) 
 * - Button controls (pins 0, 1)
 * - Serial command interface
 * - WiFi Station + Access Point modes
 * - HTTP server for web control
 * - JSON API endpoints
 * - Multi-stage rocket launch sequence
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#include "cJSON.h"
#include "nvs_flash.h"

static const char *TAG = "ROCKET_LAUNCHER_WIFI";

// GPIO Definitions for ESP32-P4
#define LED_ARM_PIN     GPIO_NUM_15     // Red LED - Armed status
#define LED_LAUNCH_PIN  GPIO_NUM_16     // Green LED - Launch ready
#define BUTTON_ARM_PIN  GPIO_NUM_0      // ARM button (BOOT button)
#define BUTTON_RESET_PIN GPIO_NUM_1     // RESET button

// WiFi Configuration
#define WIFI_SSID      "ESP32_ROCKET_LAUNCHER"
#define WIFI_PASS      "rocket123"
#define WIFI_CHANNEL   6
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
static httpd_handle_t server = NULL;
static int countdown_seconds = 0;

// WiFi Event Group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

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
    ESP_LOGI(TAG, "Initializing GPIO for WiFi Rocket Launcher");
    
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
    
    // Initialize LED states
    gpio_set_level(LED_ARM_PIN, 0);
    gpio_set_level(LED_LAUNCH_PIN, 0);
    
    ESP_LOGI(TAG, "GPIO initialization complete - WiFi Rocket Ready!");
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
                .required = true,
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
    const char* state_names[] = {"Idle", "Armed", "Countdown", "Launched", "Cooldown"};
    ESP_LOGI(TAG, "🚀 Status: %s | Armed: %s | Launched: %s | Total: %d", 
             state_names[rocket_state],
             is_armed ? "✅" : "❌",
             is_launched ? "✅" : "❌", 
             launch_count);
}

// HTTP Handlers
static const char* html_page = R"(
<!DOCTYPE html>
<html>
<head>
    <title>🚀 ESP32-P4 Rocket Launcher</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; background: #000; color: #0f0; }
        .container { max-width: 600px; margin: 0 auto; padding: 20px; }
        .status { font-size: 24px; margin: 20px 0; padding: 20px; border: 2px solid #0f0; }
        button { font-size: 20px; padding: 15px 30px; margin: 10px; cursor: pointer; border: none; border-radius: 5px; }
        .arm-btn { background: #ff0000; color: white; }
        .launch-btn { background: #00ff00; color: black; font-weight: bold; }
        .reset-btn { background: #ffff00; color: black; }
        .countdown { font-size: 48px; color: #ff0000; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 ESP32-P4 WiFi Rocket Launcher</h1>
        <div id="status" class="status">Loading...</div>
        <div id="countdown" class="countdown"></div>
        <button class="arm-btn" onclick="sendCommand('arm')">🔴 ARM ROCKET</button>
        <button class="launch-btn" onclick="sendCommand('launch')">🚀 LAUNCH!</button>
        <button class="reset-btn" onclick="sendCommand('reset')">🔄 RESET</button>
    </div>
    
    <script>
        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').innerHTML = 
                        `State: ${data.state}<br>Armed: ${data.armed ? '✅' : '❌'}<br>Launches: ${data.count}`;
                    if (data.countdown > 0) {
                        document.getElementById('countdown').innerHTML = `T-${data.countdown}`;
                    } else {
                        document.getElementById('countdown').innerHTML = '';
                    }
                });
        }
        
        function sendCommand(cmd) {
            fetch(`/api/${cmd}`, {method: 'POST'})
                .then(response => response.json())
                .then(data => {
                    console.log(data.message);
                    updateStatus();
                });
        }
        
        setInterval(updateStatus, 1000);
        updateStatus();
    </script>
</body>
</html>
)";

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

// Button Task
static void button_task(void *pvParameters)
{
    bool button_arm_pressed = false;
    bool button_reset_pressed = false;
    
    while (1) {
        // Check ARM button (with debouncing)
        if (gpio_get_level(BUTTON_ARM_PIN) == 0) {
            if (!button_arm_pressed) {
                button_arm_pressed = true;
                vTaskDelay(50 / portTICK_PERIOD_MS); // Debounce
                
                if (gpio_get_level(BUTTON_ARM_PIN) == 0) {
                    if (is_armed) {
                        rocket_launch();
                    } else {
                        rocket_arm();
                    }
                }
            }
        } else {
            button_arm_pressed = false;
        }
        
        // Check RESET button (with debouncing)
        if (gpio_get_level(BUTTON_RESET_PIN) == 0) {
            if (!button_reset_pressed) {
                button_reset_pressed = true;
                vTaskDelay(50 / portTICK_PERIOD_MS); // Debounce
                
                if (gpio_get_level(BUTTON_RESET_PIN) == 0) {
                    rocket_reset();
                }
            }
        } else {
            button_reset_pressed = false;
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// Serial Command Task
static void serial_command_task(void *pvParameters)
{
    char command[64];
    int cmd_idx = 0;
    
    ESP_LOGI(TAG, "Serial command interface ready!");
    ESP_LOGI(TAG, "Commands: 'arm', 'launch', 'reset', 'status'");
    
    while (1) {
        int c = getchar();
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (cmd_idx > 0) {
                    command[cmd_idx] = '\0';
                    
                    if (strcmp(command, "arm") == 0) {
                        rocket_arm();
                    } else if (strcmp(command, "launch") == 0) {
                        rocket_launch();
                    } else if (strcmp(command, "reset") == 0) {
                        rocket_reset();
                    } else if (strcmp(command, "status") == 0) {
                        const char* state_names[] = {"Idle", "Armed", "Countdown", "Launched", "Cooldown"};
                        printf("🚀 Status: %s | Armed: %s | Launched: %s | Total: %d\n", 
                               state_names[rocket_state],
                               is_armed ? "✅" : "❌",
                               is_launched ? "✅" : "❌", 
                               launch_count);
                    } else {
                        printf("❌ Unknown command: %s\n", command);
                        printf("Available: arm, launch, reset, status\n");
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
    ESP_LOGI(TAG, "🚀 ESP32-P4 WiFi ROCKET LAUNCHER SYSTEM 🚀");
    ESP_LOGI(TAG, "Starting Waveshare ESP32-P4-WIFI6-DEV-KIT WiFi Rocket Launcher");
    ESP_LOGI(TAG, "Version: Complete - WiFi + HTTP Server + GPIO Control");
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
    
    // Initialize WiFi
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
    
    // Create tasks
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(serial_command_task, "serial_task", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "🔥 WiFi ROCKET LAUNCHER READY FOR ACTION! 🔥");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  🌐 Web: http://192.168.4.1 (WiFi: %s / %s)", WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "  📱 Serial: 'arm', 'launch', 'reset', 'status'");
    ESP_LOGI(TAG, "  🔘 Button GPIO0: Arm/Launch");
    ESP_LOGI(TAG, "  🔘 Button GPIO1: Reset system");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    const char* state_names[] = {"Ready", "Armed", "Countdown", "Launched", "Cooldown"};
    ESP_LOGI(TAG, "🚀 Rocket Status: %s | Armed: %s | Launched: %s | Total: %d", 
             state_names[rocket_state],
             is_armed ? "✅" : "❌",
             is_launched ? "✅" : "❌", 
             launch_count);
    ESP_LOGI(TAG, "All systems go! WiFi rocket launcher active! 🚀");
}