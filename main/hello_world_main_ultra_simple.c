#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <esp_mac.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <cJSON.h>

static const char *TAG = "ROCKET_LAUNCHER";

/* WiFi credentials - Update these with your network details */
#define WIFI_SSID      "ESP32_P4_ROCKET"
#define WIFI_PASS      "supersecret123"
#define WIFI_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* GPIO pins for LEDs and buttons */
#define LED_ROCKET_STATUS    GPIO_NUM_15
#define LED_WIFI_STATUS      GPIO_NUM_16
#define BUTTON_LAUNCH        GPIO_NUM_0
#define BUTTON_RESET         GPIO_NUM_1

/* Rocket launcher state */
typedef struct {
    bool launched;
    bool armed;
    uint32_t launch_count;
    char status[64];
    uint64_t last_launch_time;
} rocket_state_t;

static rocket_state_t rocket_state = {
    .launched = false,
    .armed = false,
    .launch_count = 0,
    .status = "Ready",
    .last_launch_time = 0
};

static int s_retry_num = 0;

void init_gpio(void)
{
    ESP_LOGI(TAG, "Initializing GPIO");
    
    // Initialize LED pins
    gpio_reset_pin(LED_ROCKET_STATUS);
    gpio_reset_pin(LED_WIFI_STATUS);
    gpio_set_direction(LED_ROCKET_STATUS, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_WIFI_STATUS, GPIO_MODE_OUTPUT);
    
    // Initialize button pins
    gpio_reset_pin(BUTTON_LAUNCH);
    gpio_reset_pin(BUTTON_RESET);
    gpio_set_direction(BUTTON_LAUNCH, GPIO_MODE_INPUT);
    gpio_set_direction(BUTTON_RESET, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_LAUNCH, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BUTTON_RESET, GPIO_PULLUP_ONLY);
    
    // Turn on status LED to show system is ready
    gpio_set_level(LED_ROCKET_STATUS, 1);
    ESP_LOGI(TAG, "GPIO initialization complete");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        gpio_set_level(LED_WIFI_STATUS, 0);
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        gpio_set_level(LED_WIFI_STATUS, 1);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
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
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

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
        ESP_LOGI(TAG, "Connected to AP SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* HTTP GET handler for rocket status */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString(rocket_state.status);
    cJSON *launched = cJSON_CreateBool(rocket_state.launched);
    cJSON *armed = cJSON_CreateBool(rocket_state.armed);
    cJSON *launch_count = cJSON_CreateNumber(rocket_state.launch_count);
    cJSON *uptime = cJSON_CreateNumber(esp_timer_get_time() / 1000000);
    
    cJSON_AddItemToObject(json, "status", status);
    cJSON_AddItemToObject(json, "launched", launched);
    cJSON_AddItemToObject(json, "armed", armed);
    cJSON_AddItemToObject(json, "launch_count", launch_count);
    cJSON_AddItemToObject(json, "uptime_seconds", uptime);
    
    const char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/* HTTP POST handler for rocket launch */
static esp_err_t launch_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "🚀 LAUNCH REQUEST RECEIVED! 🚀");
    
    if (!rocket_state.armed) {
        strcpy(rocket_state.status, "Launch failed - not armed");
        ESP_LOGW(TAG, "Launch attempt failed - rocket not armed");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Rocket not armed");
        return ESP_FAIL;
    }
    
    // Launch sequence!
    strcpy(rocket_state.status, "LAUNCHING!");
    rocket_state.launched = true;
    rocket_state.launch_count++;
    rocket_state.last_launch_time = esp_timer_get_time();
    
    // Visual launch sequence
    for (int i = 0; i < 10; i++) {
        gpio_set_level(LED_ROCKET_STATUS, i % 2);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    gpio_set_level(LED_ROCKET_STATUS, 1);
    
    strcpy(rocket_state.status, "Launch complete!");
    ESP_LOGI(TAG, "🎆 ROCKET LAUNCHED! Total launches: %lu", rocket_state.launch_count);
    
    cJSON *json = cJSON_CreateObject();
    cJSON *message = cJSON_CreateString("Rocket launched successfully! 🚀");
    cJSON *count = cJSON_CreateNumber(rocket_state.launch_count);
    
    cJSON_AddItemToObject(json, "message", message);
    cJSON_AddItemToObject(json, "launch_count", count);
    
    const char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/* HTTP POST handler for arming the rocket */
static esp_err_t arm_post_handler(httpd_req_t *req)
{
    rocket_state.armed = true;
    strcpy(rocket_state.status, "Armed and ready");
    ESP_LOGI(TAG, "💣 ROCKET ARMED! Ready for launch");
    
    // Flash LED to indicate armed status
    for (int i = 0; i < 6; i++) {
        gpio_set_level(LED_ROCKET_STATUS, i % 2);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    gpio_set_level(LED_ROCKET_STATUS, 1);
    
    cJSON *json = cJSON_CreateObject();
    cJSON *message = cJSON_CreateString("Rocket armed! Ready for launch 💣");
    
    cJSON_AddItemToObject(json, "message", message);
    
    const char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/* HTTP POST handler for resetting the rocket */
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    rocket_state.launched = false;
    rocket_state.armed = false;
    strcpy(rocket_state.status, "Reset - ready");
    ESP_LOGI(TAG, "🔄 ROCKET RESET! Ready for next mission");
    
    // Reset sequence
    gpio_set_level(LED_ROCKET_STATUS, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    gpio_set_level(LED_ROCKET_STATUS, 1);
    
    cJSON *json = cJSON_CreateObject();
    cJSON *message = cJSON_CreateString("Rocket reset! Ready for next mission 🔄");
    
    cJSON_AddItemToObject(json, "message", message);
    
    const char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

static const httpd_uri_t status_uri = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t launch_uri = {
    .uri       = "/launch",
    .method    = HTTP_POST,
    .handler   = launch_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t arm_uri = {
    .uri       = "/arm",
    .method    = HTTP_POST,
    .handler   = arm_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t reset_uri = {
    .uri       = "/reset",
    .method    = HTTP_POST,
    .handler   = reset_post_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &launch_uri);
        httpd_register_uri_handler(server, &arm_uri);
        httpd_register_uri_handler(server, &reset_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void button_task(void *pvParameter)
{
    while (1) {
        // Check launch button
        if (gpio_get_level(BUTTON_LAUNCH) == 0) {  // Button pressed (active low)
            ESP_LOGI(TAG, "Launch button pressed!");
            if (rocket_state.armed) {
                ESP_LOGI(TAG, "Launching via button press!");
                // Simulate launch via button
                strcpy(rocket_state.status, "LAUNCHING via button!");
                rocket_state.launched = true;
                rocket_state.launch_count++;
                rocket_state.last_launch_time = esp_timer_get_time();
                
                // Visual feedback
                for (int i = 0; i < 10; i++) {
                    gpio_set_level(LED_ROCKET_STATUS, i % 2);
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                gpio_set_level(LED_ROCKET_STATUS, 1);
                strcpy(rocket_state.status, "Launch complete!");
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Debounce
        }
        
        // Check reset button
        if (gpio_get_level(BUTTON_RESET) == 0) {  // Button pressed (active low)
            ESP_LOGI(TAG, "Reset button pressed!");
            rocket_state.launched = false;
            rocket_state.armed = false;
            strcpy(rocket_state.status, "Reset - ready");
            gpio_set_level(LED_ROCKET_STATUS, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            gpio_set_level(LED_ROCKET_STATUS, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Debounce
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Check buttons every 100ms
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 ESP32-P4 ROCKET LAUNCHER SYSTEM ULTRA SIMPLE 🚀");
    ESP_LOGI(TAG, "Starting Waveshare ESP32-P4-WIFI6-DEV-KIT Rocket Launcher");
    ESP_LOGI(TAG, "Version: Ultra Simple - WiFi + HTTP Control");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize GPIO
    init_gpio();
    
    // Initialize WiFi
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    
    // Start HTTP server
    static httpd_handle_t server = NULL;
    server = start_webserver();
    
    if (server) {
        ESP_LOGI(TAG, "🌐 HTTP server started successfully!");
        ESP_LOGI(TAG, "API Endpoints:");
        ESP_LOGI(TAG, "  GET  /status - Get rocket status");
        ESP_LOGI(TAG, "  POST /arm    - Arm the rocket");
        ESP_LOGI(TAG, "  POST /launch - Launch the rocket! 🚀");
        ESP_LOGI(TAG, "  POST /reset  - Reset the system");
        ESP_LOGI(TAG, "🔥 ROCKET LAUNCHER READY FOR ACTION! 🔥");
    }
    
    // Create button monitoring task
    xTaskCreate(&button_task, "button_task", 2048, NULL, 5, NULL);
    
    // Main loop
    while (1) {
        ESP_LOGI(TAG, "Rocket Status: %s | Launched: %d | Armed: %d | Count: %lu", 
                 rocket_state.status, rocket_state.launched, rocket_state.armed, rocket_state.launch_count);
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Status update every 10 seconds
    }
}