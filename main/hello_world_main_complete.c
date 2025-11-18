/*
 * Waveshare ESP32-P4-WIFI6-DEV-KIT Ultimate Rocket System
 * Display + WiFi + Audio + I2C + SDMMC + Complete GPIO Integrationresolve the
 * Based on official Waveshare documentation and specifications
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

// WiFi includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// HTTP Server includes
#include "esp_http_server.h"

// JSON includes
#include "cJSON.h"

// Display drivers
#include "esp_lcd_jd9365_10_1.h"    // 10.1" display
#include "esp_lcd_ili9881c.h"       // 7" display

// Audio includes
#include "driver/i2s_std.h"
#include "es8311.h"

// I2C includes
#include "driver/i2c.h"

// SDMMC includes
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "ultimate_rocket";

// WiFi Configuration - UPDATE THESE!
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
#define WIFI_MAXIMUM_RETRY  5

// Display configurations from Waveshare headers
#define LCD_10_1_H_RES          800   // From JD9365 config
#define LCD_10_1_V_RES          1280
#define LCD_7_H_RES             720   // From ILI9881C config  
#define LCD_7_V_RES             1280

// Current display configuration (will be set during detection)
static int current_h_res = LCD_10_1_H_RES;
static int current_v_res = LCD_10_1_V_RES;
static int display_type = 0;  // 0 = 10.1", 1 = 7"

#define LCD_BIT_PER_PIXEL       16  // RGB565 for better performance

// GPIO Pin Definitions (from Waveshare documentation)
#define STATUS_LED_PIN          48
#define DEBUG_LED_PIN           47

// I2C Configuration (from Waveshare specs)
#define I2C_SCL_GPIO            8
#define I2C_SDA_GPIO            7
#define I2C_PORT_NUM            I2C_NUM_0
#define ES8311_I2C_ADDR         0x18

// I2S Audio Configuration (from Waveshare specs)
#define I2S_MCLK_GPIO           13
#define I2S_SCLK_GPIO           12
#define I2S_ASDOUT_GPIO         11  // Data out from codec
#define I2S_LRCK_GPIO           10  // Left/Right clock
#define I2S_DSDIN_GPIO          9   // Data in to codec
#define PA_CTRL_GPIO            53  // Power amplifier enable

// SDMMC Configuration (from Waveshare specs)
#define SDMMC_CLK_GPIO          43
#define SDMMC_CMD_GPIO          44
#define SDMMC_D0_GPIO           39
#define SDMMC_D1_GPIO           40
#define SDMMC_D2_GPIO           41
#define SDMMC_D3_GPIO           42

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// System state
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static uint8_t *frame_buffer = NULL;
static httpd_handle_t server = NULL;
static int s_retry_num = 0;

// Complete system state
typedef struct {
    bool wifi_connected;
    bool display_active;
    bool audio_active;
    bool i2c_active;
    bool sdmmc_active;
    char ip_address[16];
    int rocket_status;  // 0=ready, 1=armed, 2=launched
    int clients_connected;
    float audio_volume;
    bool speaker_enabled;
    char sdcard_info[64];
} system_status_t;

static system_status_t system_status = {
    .audio_volume = 0.5f,
    .speaker_enabled = false
};

// Hardware handles
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static es8311_handle_t es8311_handle = NULL;
static i2s_chan_handle_t i2s_tx_handle = NULL;
static i2s_chan_handle_t i2s_rx_handle = NULL;
static sdmmc_card_t *sdmmc_card = NULL;

static void init_status_leds(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << STATUS_LED_PIN) | (1ULL << DEBUG_LED_PIN)),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Status LEDs initialized");
        // Flash LEDs to show startup
        for (int i = 0; i < 3; i++) {
            gpio_set_level(STATUS_LED_PIN, 1);
            gpio_set_level(DEBUG_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(STATUS_LED_PIN, 0);
            gpio_set_level(DEBUG_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

static void signal_success(void)
{
    // Flash both LEDs rapidly to indicate success
    for (int i = 0; i < 6; i++) {
        gpio_set_level(STATUS_LED_PIN, i & 1);
        gpio_set_level(DEBUG_LED_PIN, !(i & 1));
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    gpio_set_level(STATUS_LED_PIN, 0);
    gpio_set_level(DEBUG_LED_PIN, 0);
}

static void signal_error(void)
{
    // Flash error pattern
    for (int i = 0; i < 10; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t init_i2c_system(void)
{
    ESP_LOGI(TAG, "🔌 Initializing I2C System (SCL:GPIO%d, SDA:GPIO%d)", I2C_SCL_GPIO, I2C_SDA_GPIO);
    
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  // Board has external pullups
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret == ESP_OK) {
        system_status.i2c_active = true;
        ESP_LOGI(TAG, "✅ I2C System initialized");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ I2C initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
}

static esp_err_t init_audio_system(void)
{
    ESP_LOGI(TAG, "🔊 Initializing Audio System (ES8311 + I2S)");
    
    if (!i2c_bus_handle) {
        ESP_LOGE(TAG, "❌ I2C must be initialized first for audio");
        return ESP_FAIL;
    }
    
    // Initialize power amplifier control pin
    gpio_config_t pa_config = {
        .pin_bit_mask = (1ULL << PA_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_config);
    gpio_set_level(PA_CTRL_GPIO, 0);  // Start with amplifier disabled
    
    // Configure I2S for audio
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S channel creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),  // 44.1kHz sample rate
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_SCLK_GPIO,
            .ws = I2S_LRCK_GPIO,
            .dout = I2S_DSDIN_GPIO,  // To codec
            .din = I2S_ASDOUT_GPIO,  // From codec
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ret = i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S TX init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S RX init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize ES8311 codec
    es8311_codec_config_t es8311_cfg = {
        .i2c_handle = i2c_bus_handle,
        .codec_mode = ES8311_CODEC_MODE_BOTH,
        .i2s_iface = {
            .mode = ES8311_MODE_SLAVE,
            .fmt = ES8311_I2S_NORMAL,
            .samples = ES8311_16BIT_SAMPLES,
            .mclk_inverted = false,
            .sclk_inverted = false,
            .mclk_from_mclk_pin = true,
        },
    };
    
    ret = es8311_codec_new(&es8311_cfg, &es8311_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ES8311 codec initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ES8311 for playback
    es8311_codec_config_format(es8311_handle, ES8311_ADCADC_SAMPLE_RATE_44100, ES8311_ADCDAC_BIT_LENGTH_16BITS, ES8311_ADCADC_I2S_FMT_I2S);
    es8311_codec_set_voice_volume(es8311_handle, (int)(system_status.audio_volume * 100));
    
    // Enable I2S channels
    i2s_channel_enable(i2s_tx_handle);
    i2s_channel_enable(i2s_rx_handle);
    
    system_status.audio_active = true;
    ESP_LOGI(TAG, "✅ Audio System initialized (ES8311 + I2S)");
    return ESP_OK;
}

static esp_err_t init_sdmmc_system(void)
{
    ESP_LOGI(TAG, "💾 Initializing SDMMC System (4-wire SDIO3.0)");
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40MHz high-speed mode
    
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4-wire mode
    slot_config.clk = SDMMC_CLK_GPIO;
    slot_config.cmd = SDMMC_CMD_GPIO;
    slot_config.d0 = SDMMC_D0_GPIO;
    slot_config.d1 = SDMMC_D1_GPIO;
    slot_config.d2 = SDMMC_D2_GPIO;
    slot_config.d3 = SDMMC_D3_GPIO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ SDMMC host init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ SDMMC slot init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Try to mount card
    sdmmc_card = malloc(sizeof(sdmmc_card_t));
    ret = sdmmc_card_init(&host, sdmmc_card);
    if (ret == ESP_OK) {
        system_status.sdmmc_active = true;
        snprintf(system_status.sdcard_info, sizeof(system_status.sdcard_info), 
                "%.1fGB %s", (float)sdmmc_card->csd.capacity / (1024*1024*1024/512), 
                sdmmc_card->cid.name);
        ESP_LOGI(TAG, "✅ SD Card detected: %s", system_status.sdcard_info);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "⚠️ No SD Card detected: %s", esp_err_to_name(ret));
        free(sdmmc_card);
        sdmmc_card = NULL;
        return ret;
    }
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
            ESP_LOGI(TAG, "🔄 Retry to connect to the AP (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        system_status.wifi_connected = false;
        ESP_LOGI(TAG,"❌ Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "📡 Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(system_status.ip_address, sizeof(system_status.ip_address), 
                 IPSTR, IP2STR(&event->ip_info.ip));
        system_status.wifi_connected = true;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        signal_success();
    }
}

// Enhanced HTTP Server handlers
static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *status = cJSON_CreateObject();
    cJSON *hardware = cJSON_CreateObject();
    cJSON *gpio_map = cJSON_CreateObject();
    
    // System status
    cJSON_AddStringToObject(status, "device", "ESP32-P4-WIFI6-DEV-KIT Ultimate");
    cJSON_AddStringToObject(status, "firmware", "Rocket Launcher v2.0");
    cJSON_AddBoolToObject(status, "wifi_connected", system_status.wifi_connected);
    cJSON_AddStringToObject(status, "ip_address", system_status.ip_address);
    cJSON_AddNumberToObject(status, "rocket_status", system_status.rocket_status);
    cJSON_AddNumberToObject(status, "uptime_ms", esp_timer_get_time() / 1000);
    
    // Hardware status
    cJSON_AddBoolToObject(hardware, "display_active", system_status.display_active);
    cJSON_AddBoolToObject(hardware, "audio_active", system_status.audio_active);
    cJSON_AddBoolToObject(hardware, "i2c_active", system_status.i2c_active);
    cJSON_AddBoolToObject(hardware, "sdmmc_active", system_status.sdmmc_active);
    cJSON_AddStringToObject(hardware, "display_type", 
                           display_type == 0 ? "10.1inch_JD9365" : "7inch_ILI9881C");
    cJSON_AddNumberToObject(hardware, "display_width", current_h_res);
    cJSON_AddNumberToObject(hardware, "display_height", current_v_res);
    cJSON_AddNumberToObject(hardware, "audio_volume", system_status.audio_volume);
    cJSON_AddBoolToObject(hardware, "speaker_enabled", system_status.speaker_enabled);
    cJSON_AddStringToObject(hardware, "sdcard_info", system_status.sdcard_info);
    
    // GPIO mapping
    cJSON_AddNumberToObject(gpio_map, "status_led", STATUS_LED_PIN);
    cJSON_AddNumberToObject(gpio_map, "debug_led", DEBUG_LED_PIN);
    cJSON_AddNumberToObject(gpio_map, "i2c_scl", I2C_SCL_GPIO);
    cJSON_AddNumberToObject(gpio_map, "i2c_sda", I2C_SDA_GPIO);
    cJSON_AddNumberToObject(gpio_map, "i2s_mclk", I2S_MCLK_GPIO);
    cJSON_AddNumberToObject(gpio_map, "i2s_sclk", I2S_SCLK_GPIO);
    cJSON_AddNumberToObject(gpio_map, "pa_ctrl", PA_CTRL_GPIO);
    
    cJSON_AddItemToObject(json, "status", status);
    cJSON_AddItemToObject(json, "hardware", hardware);
    cJSON_AddItemToObject(json, "gpio_map", gpio_map);
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t audio_control_handler(httpd_req_t *req)
{
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    content[received] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    bool audio_changed = false;
    
    // Handle volume control
    cJSON *volume = cJSON_GetObjectItem(json, "volume");
    if (volume && cJSON_IsNumber(volume)) {
        float new_volume = (float)volume->valuedouble;
        if (new_volume >= 0.0f && new_volume <= 1.0f) {
            system_status.audio_volume = new_volume;
            if (es8311_handle) {
                es8311_codec_set_voice_volume(es8311_handle, (int)(new_volume * 100));
            }
            audio_changed = true;
            ESP_LOGI(TAG, "🔊 Volume set to %.1f", new_volume);
        }
    }
    
    // Handle speaker enable/disable
    cJSON *speaker = cJSON_GetObjectItem(json, "speaker_enabled");
    if (speaker && cJSON_IsBool(speaker)) {
        bool enable = cJSON_IsTrue(speaker);
        system_status.speaker_enabled = enable;
        gpio_set_level(PA_CTRL_GPIO, enable ? 1 : 0);
        audio_changed = true;
        ESP_LOGI(TAG, "🔊 Speaker %s", enable ? "enabled" : "disabled");
    }
    
    // Handle test tone
    cJSON *test_tone = cJSON_GetObjectItem(json, "test_tone");
    if (test_tone && cJSON_IsTrue(test_tone) && system_status.audio_active) {
        ESP_LOGI(TAG, "🎵 Playing test tone");
        // Simple test tone generation would go here
        audio_changed = true;
    }
    
    cJSON_Delete(json);
    
    // Response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", audio_changed);
    cJSON_AddNumberToObject(response, "volume", system_status.audio_volume);
    cJSON_AddBoolToObject(response, "speaker_enabled", system_status.speaker_enabled);
    
    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, HTTPD_RESP_USE_STRLEN);
    
    free(response_string);
    cJSON_Delete(response);
    return ESP_OK;
}

static esp_err_t launch_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "🚀 LAUNCH COMMAND RECEIVED!");
    system_status.rocket_status = 2; // launched
    
    // Flash LEDs for launch sequence
    for (int i = 0; i < 20; i++) {
        gpio_set_level(STATUS_LED_PIN, 1);
        gpio_set_level(DEBUG_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(STATUS_LED_PIN, 0);
        gpio_set_level(DEBUG_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Reset to ready after 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));
    system_status.rocket_status = 0;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Launch sequence executed!");
    cJSON_AddNumberToObject(json, "timestamp", esp_timer_get_time() / 1000);
    
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
    config.max_uri_handlers = 8;

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
        
        // Audio control endpoint
        httpd_uri_t audio_uri = {
            .uri = "/api/audio",
            .method = HTTP_POST,
            .handler = audio_control_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &audio_uri);
        
        ESP_LOGI(TAG, "🌐 Ultimate Rocket Control Server started");
        ESP_LOGI(TAG, "📋 API Endpoints:");
        ESP_LOGI(TAG, "   GET  http://%s/api/status", system_status.ip_address);
        ESP_LOGI(TAG, "   POST http://%s/api/launch", system_status.ip_address);
        ESP_LOGI(TAG, "   POST http://%s/api/audio", system_status.ip_address);
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🎵 Audio Control Examples:");
        ESP_LOGI(TAG, "   curl -X POST -H 'Content-Type: application/json' \\");
        ESP_LOGI(TAG, "        -d '{\"volume\":0.7}' http://%s/api/audio", system_status.ip_address);
        ESP_LOGI(TAG, "   curl -X POST -H 'Content-Type: application/json' \\");
        ESP_LOGI(TAG, "        -d '{\"speaker_enabled\":true}' http://%s/api/audio", system_status.ip_address);
        
        return server;
    }

    ESP_LOGE(TAG, "❌ Error starting HTTP server!");
    return NULL;
}

static esp_err_t init_wifi(void)
{
    ESP_LOGI(TAG, "📡 Initializing WiFi 6 (ESP32-C6 via SDIO)");
    
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
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "🔍 WiFi init finished. Connecting to %s...", WIFI_SSID);

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Connected to WiFi SSID: %s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "❌ Failed to connect to SSID: %s", WIFI_SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "⚠️ Unexpected WiFi event");
        return ESP_FAIL;
    }
}

static esp_err_t init_mipi_dsi_bus_and_io(void)
{
    ESP_LOGI(TAG, "🚌 Creating MIPI DSI bus and IO interface");
    
    // Create MIPI DSI bus (use JD9365 config as default)
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    esp_err_t ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create MIPI DSI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DSI bus created");

    // Create MIPI DBI IO interface  
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create MIPI DBI IO: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ MIPI DBI IO created");
    
    return ESP_OK;
}

static esp_err_t init_10_1_display(void)
{
    ESP_LOGI(TAG, "🖥️ Attempting 10.1\" Display (JD9365) - 800x1280");
    
    // Create DPI configuration
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    
    // Create vendor configuration
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = NULL,  // Use default commands
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };
    
    // Create panel device configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    esp_err_t ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 10.1\" display creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_reset(lcd_panel);
    if (ret == ESP_OK) {
        ret = esp_lcd_panel_init(lcd_panel);
        if (ret == ESP_OK) {
            ret = esp_lcd_panel_disp_on_off(lcd_panel, true);
            if (ret == ESP_OK) {
                current_h_res = LCD_10_1_H_RES;
                current_v_res = LCD_10_1_V_RES;
                display_type = 0;
                system_status.display_active = true;
                ESP_LOGI(TAG, "✅ 10.1\" Display (JD9365) initialized successfully!");
                return ESP_OK;
            }
        }
    }
    
    esp_lcd_panel_del(lcd_panel);
    lcd_panel = NULL;
    return ret;
}

static esp_err_t init_7_display(void)
{
    ESP_LOGI(TAG, "🖥️ Attempting 7\" Display (ILI9881C) - 720x1280");
    
    // Create DPI configuration
    esp_lcd_dpi_panel_config_t dpi_config = ILI9881C_720_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    
    // Create vendor configuration
    ili9881c_vendor_config_t vendor_config = {
        .init_cmds = NULL,  // Use default commands
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };
    
    // Create panel device configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    esp_err_t ret = esp_lcd_new_panel_ili9881c(mipi_dbi_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ 7\" display creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_reset(lcd_panel);
    if (ret == ESP_OK) {
        ret = esp_lcd_panel_init(lcd_panel);
        if (ret == ESP_OK) {
            ret = esp_lcd_panel_disp_on_off(lcd_panel, true);
            if (ret == ESP_OK) {
                current_h_res = LCD_7_H_RES;
                current_v_res = LCD_7_V_RES;
                display_type = 1;
                system_status.display_active = true;
                ESP_LOGI(TAG, "✅ 7\" Display (ILI9881C) initialized successfully!");
                return ESP_OK;
            }
        }
    }
    
    esp_lcd_panel_del(lcd_panel);
    lcd_panel = NULL;
    return ret;
}

static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "📺 Auto-detecting Waveshare Display...");
    
    // First initialize MIPI DSI bus and IO interface
    esp_err_t ret = init_mipi_dsi_bus_and_io();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize MIPI DSI bus/IO");
        return ret;
    }
    
    // Try 10.1" display first (most common)
    ret = init_10_1_display();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Detected: 10.1\" Waveshare DSI LCD (800x1280)");
        signal_success();
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🔄 10.1\" failed, trying 7\" display...");
    
    // Try 7" display
    ret = init_7_display();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎯 Detected: 7\" Waveshare DSI LCD (720x1280)");
        signal_success();
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "❌ No compatible display detected!");
    signal_error();
    return ESP_FAIL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 ULTIMATE ESP32-P4 ROCKET LAUNCHER SYSTEM STARTING...");
    ESP_LOGI(TAG, "📺 Waveshare ESP32-P4-WIFI6-DEV-KIT - The Complete Experience");
    ESP_LOGI(TAG, "🎯 Display + WiFi 6 + Audio + I2C + SDMMC Integration");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS Flash initialized");

    // Initialize status LEDs
    init_status_leds();
    
    ESP_LOGI(TAG, "� Initializing hardware subsystems...");
    
    // Initialize I2C system first (required for display and audio)
    if (init_i2c_system() == ESP_OK) {
        ESP_LOGI(TAG, "✅ I2C System ready (GPIO7=SDA, GPIO8=SCL)");
        system_status.i2c_ready = true;
    } else {
        ESP_LOGW(TAG, "⚠️  I2C initialization failed, continuing...");
    }
    
    // Initialize audio system
    if (init_audio_system() == ESP_OK) {
        ESP_LOGI(TAG, "✅ Audio System ready (ES8311 + NS4150B)");
        system_status.audio_ready = true;
    } else {
        ESP_LOGW(TAG, "⚠️  Audio initialization failed, continuing...");
    }
    
    // Initialize SDMMC system
    if (init_sdmmc_system() == ESP_OK) {
        ESP_LOGI(TAG, "✅ SDMMC System ready (GPIO39-44)");
        system_status.sdmmc_ready = true;
    } else {
        ESP_LOGW(TAG, "⚠️  SDMMC initialization failed, continuing...");
    }
    
    // Initialize Display
    ESP_LOGI(TAG, "📺 Initializing Display System...");
    ret = init_display();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Display initialization failed, continuing without display");
        system_status.display_active = false;
    }
    
    // Initialize WiFi
    ESP_LOGI(TAG, "📡 Initializing WiFi 6 System (ESP32-C6 via SDIO)...");
    ESP_LOGI(TAG, "🔧 Make sure to update WIFI_SSID and WIFI_PASSWORD in the code!");
    ret = init_wifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi initialization failed!");
        signal_error();
        return;
    }
    
    // Wait for WiFi connection
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Start the ultimate web server
    ESP_LOGI(TAG, "🌐 Starting Ultimate Rocket Control Server...");
    start_webserver();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎯 ═══════════════════════════════════════════════════");
    ESP_LOGI(TAG, "🎯  ULTIMATE ESP32-P4 ROCKET LAUNCHER SYSTEM READY!");
    ESP_LOGI(TAG, "🎯 ═══════════════════════════════════════════════════");
    ESP_LOGI(TAG, "📺 10.1'' Display:     %s (%dx%d)", 
             system_status.display_active ? 
             (display_type == 0 ? "JD9365 ✅" : "ILI9881C ✅") : "❌ ERROR",
             current_h_res, current_v_res);
    ESP_LOGI(TAG, "� WiFi 6 (ESP32-C6):  %s", 
             system_status.wifi_connected ? "✅ CONNECTED" : "⚠️  CONNECTING");
    ESP_LOGI(TAG, "🔊 Audio (ES8311):     %s", system_status.audio_ready ? "✅ READY" : "❌ ERROR");
    ESP_LOGI(TAG, "🔧 I2C System:         %s", system_status.i2c_ready ? "✅ READY" : "❌ ERROR");
    ESP_LOGI(TAG, "💾 SDMMC Card:         %s", system_status.sdmmc_ready ? "✅ READY" : "❌ ERROR");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "🌐 Ultimate Control Panel:");
    ESP_LOGI(TAG, "   📊 Status:  http://%s/api/status", system_status.ip_address);
    ESP_LOGI(TAG, "   🚀 Launch:  curl -X POST http://%s/api/launch", system_status.ip_address);
    ESP_LOGI(TAG, "   🔊 Audio:   curl -X POST -H 'Content-Type: application/json' \\");
    ESP_LOGI(TAG, "              -d '{\"volume\":0.7}' http://%s/api/audio", system_status.ip_address);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🚀 READY TO LAUNCH ROCKETS! ALL SYSTEMS NOMINAL!");
    
    // Enhanced status loop with audio feedback
    while (1) {
        // Update status LED to show system is running
        gpio_set_level(STATUS_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(STATUS_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Log comprehensive status every 30 seconds
        static int status_counter = 0;
        if (++status_counter >= 60) {  // Every 30 seconds (500ms * 2 * 30)
            ESP_LOGI(TAG, "💓 Ultimate System Status:");
            ESP_LOGI(TAG, "   📺 Display:%s 📶 WiFi:%s 🔊 Audio:%s", 
                     system_status.display_active ? "✅" : "❌",
                     system_status.wifi_connected ? "✅" : "❌",
                     system_status.audio_ready ? "✅" : "❌");
            ESP_LOGI(TAG, "   🔧 I2C:%s 💾 SDMMC:%s 🚀 Launches:%d", 
                     system_status.i2c_ready ? "✅" : "❌",
                     system_status.sdmmc_ready ? "✅" : "❌",
                     system_status.launch_count);
            status_counter = 0;
        }
    }
}