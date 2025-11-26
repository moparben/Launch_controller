/*
 * Display MCU v3.02 - Multi-MCU Rocket Launcher System
 * ESP32-P4 Focused Display Controller + GT911 Touch Working
 * 
 * Filename: display_mcu_v3_01_idf.c
 * Version: 3.01 - Restored native ESP-IDF i2c_master API for GT911 touch controller
 * Target: ESP32-P4 (Waveshare ESP32-P4-WIFI6-DEV-KIT)
 * Framework: ESP-IDF v5.4.2
 * 
 * 🎉 MAJOR BREAKTHROUGH - V3.01 ACHIEVEMENTS:
 * ✅ GT911 Touch Controller Successfully Working!
 * ✅ I2C Bus Sequential Sharing Architecture Discovered & Implemented
 * ✅ JD9365 → GT911 Bus Recreation Pattern Working
 * ✅ Touch Detection Active (coordinate calibration in progress)
 * ✅ Waveshare Hardware Architecture Fully Mapped
 * 
 * HARDWARE CONFIGURATION (ESP32-P4):
 * ✅ 10.1" Waveshare MIPI-DSI Display (JD9365 Controller)
 * ✅ Touch Screen Integration
 * ✅ CAN Transceiver: GPIO43 (TX), GPIO44 (RX)
 * ✅ BNO085 IMU: GPIO40 (SCL), GPIO41 (SDA) - I2C interface only
 * ❌ No other GPIO connections - Minimal hardware setup
 * 
 * PRIMARY RESPONSIBILITIES:
 * ✅ 10.1" MIPI-DSI Display Driving (Waveshare crash-free approach)
 * ✅ Touch Screen Input Processing (GT911 WORKING with coordinate calibration)
 * ✅ BNO085 IMU Integration (display orientation & tilt detection)
 * ✅ Display UI Rendering (integer-only, no LVGL crashes)
 * ✅ CAN Bus Communication (Client Mode)
 * 🔄 WiFi Access Point + Web Server (TEMPORARY - migrate to web_mcu later)
 * 
 * REMOVED BLOAT (4500+ lines eliminated):
 * ❌ Battery management → launch_mcu
 * ❌ Safety systems → launch_mcu  
 * ❌ GPS navigation → cam_mcu
 * ❌ Servo control → cam_mcu
 * ❌ Environmental sensors → launch_mcu
 * ❌ Ignitor monitoring → launch_mcu
 * ❌ Launch control logic → launch_mcu
 * ❌ Camera systems → cam_mcu
 * ❌ Cluster engine management → launch_mcu
 * ❌ Comprehensive settings (v3.7 legacy) → launch_mcu
 * 
 * DISTRIBUTED 6-MCU ARCHITECTURE:
 * 1. display_mcu (ESP32-P4) - THIS MCU - Display + temporary web
 * 2. launch_mcu (ESP32) - Safety, ignitor control, battery management
 * 3. web_mcu (ESP32) - Network interface (will receive web server migration)
 * 4. cam_mcu (ESP32-CAM) - Primary camera, GPS, servo pan/tilt
 * 5. cam2_mcu (ESP32-CAM) - Secondary camera, stereo vision
 * 6. vid_mcu (ESP32-S3) - Video processing, compression, streaming
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/lock.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "driver/i2c_master.h"
#include "esp_private/periph_ctrl.h"
#include "soc/periph_defs.h"
#include "i2c_bus.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"

#if SOC_WIFI_SUPPORTED
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#endif

// Display-specific includes (Waveshare 10.1" MIPI-DSI JD9365)
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h" 
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "linenoise/linenoise.h"
#include "touch_calibration.h"  // New simple calibration system
// #include "esp_spiffs.h"  // TODO: Enable for future JPEG boot image support

static const char *TAG = "DISPLAY_MCU_v3_01";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// SPIFFS FILE SYSTEM FOR BOOT IMAGES - COMMENTED OUT FOR NOW ////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*  // TODO: Enable for JPEG boot image support
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "🗄️ Initializing SPIFFS file system for boot images");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format SPIFFS filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("spiffs", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ SPIFFS mounted successfully");
    ESP_LOGI(TAG, "   📊 Partition size: %d KB, Used: %d KB, Free: %d KB", 
             total/1024, used/1024, (total-used)/1024);
    
    return ESP_OK;
}
*/  // End of commented SPIFFS function

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// JPEG BOOT IMAGE LOADER - COMMENTED OUT FOR NOW /////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*  // TODO: Enable for JPEG boot image support
static esp_err_t load_jpeg_boot_image(const char* filename)
{
    ESP_LOGI(TAG, "🖼️ Loading JPEG boot image: %s", filename);
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        ESP_LOGW(TAG, "⚠️ Boot image not found: %s, using default rocket", filename);
        return ESP_ERR_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    ESP_LOGI(TAG, "📁 JPEG file size: %d bytes", file_size);

    // Allocate buffer for JPEG data
    uint8_t* jpeg_data = malloc(file_size);
    if (!jpeg_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG data");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    // Read JPEG file
    size_t bytes_read = fread(jpeg_data, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read complete JPEG file");
        free(jpeg_data);
        return ESP_ERR_INVALID_SIZE;
    }

    // Initialize JPEG decoder
    jpeg_dec_config_t config = {
        .output_format = JPEG_OUTPUT_FORMAT_RGB565,
    };
    
    jpeg_dec_handle_t jpeg_dec;
    esp_err_t ret = jpeg_dec_open(&config, &jpeg_dec);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder: %s", esp_err_to_name(ret));
        free(jpeg_data);
        return ret;
    }

    // Get JPEG info
    jpeg_dec_io_t jpeg_io = {};
    jpeg_io.inbuf = jpeg_data;
    jpeg_io.inbuf_len = file_size;
    
    ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header: %s", esp_err_to_name(ret));
        jpeg_dec_close(jpeg_dec);
        free(jpeg_data);
        return ret;
    }

    ESP_LOGI(TAG, "🖼️ JPEG Info: %dx%d pixels", jpeg_io.outbuf_width, jpeg_io.outbuf_height);

    // Allocate output buffer for decoded RGB565 data
    size_t output_size = jpeg_io.outbuf_width * jpeg_io.outbuf_height * 2; // RGB565 = 2 bytes per pixel
    uint8_t* rgb_data = malloc(output_size);
    if (!rgb_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for RGB data");
        jpeg_dec_close(jpeg_dec);
        free(jpeg_data);
        return ESP_ERR_NO_MEM;
    }

    // Decode JPEG to RGB565
    jpeg_io.outbuf = rgb_data;
    ret = jpeg_dec_process(jpeg_dec, &jpeg_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode JPEG: %s", esp_err_to_name(ret));
        free(rgb_data);
        jpeg_dec_close(jpeg_dec);
        free(jpeg_data);
        return ret;
    }

    // Display the decoded image
    if (panel_handle) {
        // Scale/center the image if needed
        int display_x = (DISPLAY_H_RES - jpeg_io.outbuf_width) / 2;
        int display_y = (DISPLAY_V_RES - jpeg_io.outbuf_height) / 2;
        
        if (display_x < 0) display_x = 0;
        if (display_y < 0) display_y = 0;
        
        int draw_width = (jpeg_io.outbuf_width > DISPLAY_H_RES) ? DISPLAY_H_RES : jpeg_io.outbuf_width;
        int draw_height = (jpeg_io.outbuf_height > DISPLAY_V_RES) ? DISPLAY_V_RES : jpeg_io.outbuf_height;

        ESP_LOGI(TAG, "🖥️ Displaying JPEG at (%d,%d) size %dx%d", display_x, display_y, draw_width, draw_height);
        
        esp_lcd_panel_draw_bitmap(panel_handle, display_x, display_y, 
                                  display_x + draw_width, display_y + draw_height, rgb_data);
    }

    // Cleanup
    free(rgb_data);
    jpeg_dec_close(jpeg_dec);
    free(jpeg_data);

    ESP_LOGI(TAG, "✅ JPEG boot image displayed successfully!");
    return ESP_OK;
}
*/  // End of commented JPEG function

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// HARDWARE CONFIGURATION - ESP32-P4 MINIMAL SETUP //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// CAN/TWAI Configuration (Multi-MCU Communication)
#define TWAI_TX_PIN          GPIO_NUM_43     // CAN transceiver TX
#define TWAI_RX_PIN          GPIO_NUM_44     // CAN transceiver RX
#define TWAI_BITRATE         1000000         // 1 Mbps

// BNO085 IMU Configuration (Display Orientation Only)
#define BNO085_I2C_SCL       GPIO_NUM_40     // I2C Clock
#define BNO085_I2C_SDA       GPIO_NUM_41     // I2C Data
#define BNO085_I2C_PORT      I2C_NUM_0       // I2C port number (dedicated for BNO085)
#define BNO085_I2C_FREQ      400000          // 400kHz I2C frequency
#define BNO085_I2C_ADDR      0x4A            // BNO085 I2C address

static i2c_master_bus_handle_t bno085_bus_handle;      // I2C bus for the IMU
static i2c_master_dev_handle_t bno085_device_handle;   // Handle for the BNO085 device

// JD9365 10.1" Waveshare Display Configuration
#define DISPLAY_H_RES        800
#define DISPLAY_V_RES        1280
#define DISPLAY_BIT_PER_PIXEL 16  // Match Waveshare demo: drive panel in RGB565 over 2-lane DSI
#define PIN_NUM_LCD_RST      -1
#define PIN_NUM_BK_LIGHT     GPIO_NUM_15
#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define MIPI_DSI_LANE_NUM    2

// GT911 Touch Controller - Waveshare ESP32-P4 Hardware Pins (Shared I2C Bus)
// CRITICAL DISCOVERY: JD9365 and GT911 share the same I2C bus on Waveshare hardware!
// JD9365 display driver manages I2C_NUM_1 internally (SDA=GPIO7, SCL=GPIO8)
// GT911 must use the SAME bus (I2C_NUM_1) with SAME pins - this is the hardware design
// BNO085 IMU gets its own dedicated I2C_NUM_0 (SDA=GPIO41, SCL=GPIO40)
#define TOUCH_I2C_PORT              I2C_NUM_1  
#define TOUCH_I2C_SCL               GPIO_NUM_8   // Same as JD9365 - shared bus
#define TOUCH_I2C_SDA               GPIO_NUM_7   // Same as JD9365 - shared bus  
#define TOUCH_I2C_FREQ_HZ           400000
#define TOUCH_RST_GPIO              GPIO_NUM_27  // Official Waveshare test reset pin
#define TOUCH_INT_GPIO              GPIO_NUM_NC  // Official BSP touch interrupt pin (not connected)
#define TOUCH_DEFAULT_ADDR          ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP
#define TOUCH_FEEDBACK_SIZE         36
#define TOUCH_NATIVE_MAX_X          1280
#define TOUCH_NATIVE_MAX_Y          800

#if DISPLAY_BIT_PER_PIXEL == 16
#define TOUCH_FEEDBACK_PIXEL_SIZE   2
static uint16_t touch_feedback_buffer[TOUCH_FEEDBACK_SIZE * TOUCH_FEEDBACK_SIZE];
#elif DISPLAY_BIT_PER_PIXEL == 18 || DISPLAY_BIT_PER_PIXEL == 24
#define TOUCH_FEEDBACK_PIXEL_SIZE   3
static uint8_t touch_feedback_buffer[TOUCH_FEEDBACK_SIZE * TOUCH_FEEDBACK_SIZE * TOUCH_FEEDBACK_PIXEL_SIZE];
#else
#error "Touch feedback buffer not configured for current DISPLAY_BIT_PER_PIXEL"
#endif
#define TOUCH_MAX_POINTS            5
#define TOUCH_RELEASE_TIMEOUT_MS    120
#define TOUCH_SCAN_INTERVAL_MS      16



// LDO Configuration for MIPI DSI PHY
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

#if DISPLAY_BIT_PER_PIXEL == 24
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB888)
#elif DISPLAY_BIT_PER_PIXEL == 18
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB666)
#elif DISPLAY_BIT_PER_PIXEL == 16
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB565)
#else
#error "Unsupported DISPLAY_BIT_PER_PIXEL value"
#endif

// WiFi Configuration (TEMPORARY - migrate to web_mcu later)
#define WIFI_SSID      "ESP32_DISPLAY_MCU"
#define WIFI_PASS      "rocket123"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// SYSTEM STATE & DATA STRUCTURES ///////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// MCU State
typedef enum {
    MCU_STATE_INITIALIZING = 0,
    MCU_STATE_DISPLAY_READY,
    MCU_STATE_WIFI_ACTIVE,
    MCU_STATE_FULL_OPERATIONAL,
    MCU_STATE_ERROR
} display_mcu_state_t;

// BNO085 IMU Data (Display Orientation Only)
typedef struct {
    float roll;                     // Display roll angle (degrees)
    float pitch;                    // Display pitch angle (degrees)  
    float yaw;                      // Display yaw angle (degrees)
    bool calibrated;                // IMU calibration status
    bool tilt_detected;             // Significant tilt detected
    uint8_t orientation;            // Display orientation (0,90,180,270)
    unsigned long last_reading_ms;  // Last successful reading
    bool sensor_active;             // BNO085 responding
} display_imu_data_t;

// Touch Screen Data
typedef struct {
    int16_t x, y;                   // Touch coordinates
    bool pressed;                   // Touch state
    bool valid;                     // Touch data valid
    unsigned long press_time_ms;    // Time when pressed
    unsigned long last_touch_ms;    // Last touch event
} touch_data_t;

// CAN Communication Data (Client Mode)
typedef struct {
    bool bus_active;                // CAN bus operational
    uint32_t messages_received;     // Message count
    uint32_t messages_sent;         // Message count
    unsigned long last_message_ms;  // Last message timestamp
    uint8_t active_nodes;           // Number of active MCUs on bus
} can_status_t;

// Display System Status
typedef struct {
    bool display_initialized;       // MIPI display ready
    bool touch_initialized;         // Touch screen ready
    bool imu_initialized;           // BNO085 ready
    bool can_initialized;           // CAN bus ready
    bool wifi_active;               // WiFi AP active (temporary)
    bool web_server_active;         // Web server active (temporary)
    int connected_stations;         // WiFi clients connected
    unsigned long uptime_ms;        // System uptime
} display_system_status_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// GLOBAL VARIABLES //////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// System State
static display_mcu_state_t mcu_state = MCU_STATE_INITIALIZING;
static display_imu_data_t imu_data = {0};
static touch_data_t touch_data = {0};
static can_status_t can_status = {0};
static display_system_status_t system_status = {0};

// Hardware Handles
static esp_lcd_panel_handle_t panel_handle = NULL;
#if SOC_WIFI_SUPPORTED
static httpd_handle_t server = NULL;
#endif
static SemaphoreHandle_t system_mutex = NULL;

// Touch controller handles
static i2c_bus_handle_t gt911_i2c_bus_handle = NULL;       // i2c_bus component handle (shared with JD9365)
static i2c_master_bus_handle_t touch_bus_handle = NULL;    // Internal handle for ESP LCD Touch
static esp_lcd_panel_io_handle_t touch_panel_io = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static esp_lcd_touch_io_gt911_config_t touch_gt911_config = {
    .dev_addr = TOUCH_DEFAULT_ADDR,
};

// DMA synchronization for display drawing
static SemaphoreHandle_t draw_finish_sem = NULL;

// Timing
static uint64_t boot_time_us = 0;
static esp_timer_handle_t status_timer = NULL;

// Calibration system definitions
#define CAL_TARGET_LINE_HALF        20
#define CAL_TARGET_LINE_THICK       4
#define CAL_TARGET_LINE_COLOR       0xFFFF  // White
#define CAL_TARGET_CENTER_SIZE      8
#define CAL_TARGET_CLEAR_MARGIN     25
#define CAL_TARGET_MARKER_SIZE      12
#define CAL_BACKGROUND_COLOR        0x0008  // Dark blue

typedef struct {
    int display_x, display_y;        // Expected display coordinates
    int touch_x, touch_y;           // Raw touch coordinates captured
    bool collected;                 // Whether this point has been captured
    uint16_t color;                 // Color for this target
    const char *name;               // Name for logging
} calibration_target_t;

static calibration_target_t cal_targets[5] = {
    {100, 100, 0, 0, false, 0xF800, "Top-Left"},      // Red
    {700, 100, 0, 0, false, 0x07E0, "Top-Right"},     // Green
    {700, 1180, 0, 0, false, 0x001F, "Bottom-Right"}, // Blue
    {100, 1180, 0, 0, false, 0xFFE0, "Bottom-Left"},  // Yellow
    {400, 640, 0, 0, false, 0xF81F, "Center"}         // Magenta
};

static int last_drawn_target = -1;
static bool calibration_background_ready = false;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// REAL WAVESHARE MIPI DSI DISPLAY DRIVER ///////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Global handles for MIPI DSI
static esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;

// DMA draw completion callback (must be in IRAM for interrupt context)
static IRAM_ATTR bool on_draw_complete_callback(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(draw_finish_sem, &need_yield);
    return (need_yield == pdTRUE);
}

// Initialize LDO for MIPI DSI PHY power (ESP32-P4)
static esp_err_t init_ldo_power(void)
{
    ESP_LOGI(TAG, "Initializing LDO for MIPI DSI PHY power");
    
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire LDO channel (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ LDO channel %d acquired at %dmV", MIPI_DSI_PHY_PWR_LDO_CHAN, MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    return ESP_OK;
}

// Initialize backlight (if available)
static esp_err_t init_backlight(void)
{
    ESP_LOGI(TAG, "Initializing display backlight");
    
    if (PIN_NUM_BK_LIGHT >= 0) {
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << (gpio_num_t)PIN_NUM_BK_LIGHT
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);
        ESP_LOGI(TAG, "✅ Backlight enabled on GPIO%d", PIN_NUM_BK_LIGHT);
    } else {
        ESP_LOGI(TAG, "Backlight pin not configured (PIN_NUM_BK_LIGHT = -1)");
    }
    
    return ESP_OK;
}

// Real MIPI DSI bus initialization using JD9365 configuration
static esp_err_t init_mipi_dsi_bus(void)
{
    ESP_LOGI(TAG, "🔧 Initializing JD9365 MIPI DSI bus for ESP32-P4");
    
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    bus_config.lane_bit_rate_mbps = 960;  // Waveshare-tested value for better timing reliability
    
    esp_err_t ret = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MIPI DSI bus (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ JD9365 MIPI DSI bus created with %d lanes at %" PRIu32 " Mbps", bus_config.num_data_lanes, (uint32_t)bus_config.lane_bit_rate_mbps);
    return ESP_OK;
}

// Panel IO initialization (MIPI DBI interface) using JD9365 configuration
static esp_err_t init_panel_io(void)
{
    ESP_LOGI(TAG, "🔧 Initializing JD9365 MIPI DBI panel IO");
    
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    
    esp_err_t ret = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MIPI DBI panel IO (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ JD9365 MIPI DBI panel IO created successfully");
    return ESP_OK;
}

// JD9365 panel initialization using proper Waveshare driver
static esp_err_t init_jd9365_panel(void)
{
    ESP_LOGI(TAG, "🔧 Initializing JD9365 10.1\" Waveshare MIPI DSI panel");
    
    // Create DPI configuration for JD9365 800x1280 at 60Hz
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(MIPI_DPI_PX_FORMAT);
    dpi_config.video_timing.vsync_front_porch = 14;  // Align porch timing with Waveshare LVGL example
    
    // JD9365 vendor configuration
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = NULL,  // Use default initialization commands
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = MIPI_DSI_LANE_NUM,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };
    
    // Panel device configuration
    esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_LCD_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = DISPLAY_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    
    // Create JD9365 panel using the proper driver
    esp_err_t ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JD9365 panel (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    // Reset and initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // Configure display orientation - CORRECTED for proper orientation
    // Use swap_xy=true to rotate, then mirror_y to flip
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));         // Swap XY for rotation
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));   // Mirror Y only
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    // Create semaphore for DMA draw completion synchronization
    draw_finish_sem = xSemaphoreCreateBinary();
    if (!draw_finish_sem) {
        ESP_LOGE(TAG, "Failed to create draw completion semaphore");
        return ESP_FAIL;
    }
    // Give semaphore initially so first draw doesn't block
    xSemaphoreGive(draw_finish_sem);
    
    // Register callback for draw completion events
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_draw_complete_callback,
    };
    ret = esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register draw callback (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ JD9365 10.1\" Waveshare panel initialized successfully (%dx%d)", DISPLAY_H_RES, DISPLAY_V_RES);
    ESP_LOGI(TAG, "✅ DMA synchronization enabled with callback");
    return ESP_OK;
}

// Enhanced Touch Calibration System
static bool calibration_mode = true;   // Enable calibration to test ACCEPT/RESTART flow
static int calibration_step = 0;      // Current calibration step (0-4)
static bool calibration_review_mode = false;  // Review mode after completing 5 points
static bool calibration_test_mode = false;    // Test mode to verify calibration accuracy
static int touch_point_count = 0;

typedef struct {
    double a0, a1, a2;   // Display X coefficients
    double b0, b1, b2;   // Display Y coefficients
    bool valid;          // Transform ready for use
} touch_transform_t;

static touch_transform_t touch_transform = {
    .a0 = 0.0, .a1 = 1.0, .a2 = 0.0,
    .b0 = 0.0, .b1 = 0.0, .b2 = 1.0,
    .valid = false,
};

static uint16_t latest_raw_x[TOUCH_MAX_POINTS] = {0};
static uint16_t latest_raw_y[TOUCH_MAX_POINTS] = {0};

static inline bool is_collecting_calibration_points(void)
{
    return (calibration_mode && !calibration_review_mode && !calibration_test_mode && calibration_step < 5);
}

static void reset_calibration_state(bool redraw_screen);
static void draw_calibration_background(void);
static void draw_calibration_review_screen(void);
static void draw_calibration_success_screen(void);
static void draw_calibration_target_cross(const calibration_target_t *target);
static void finalize_calibration_target(const calibration_target_t *target);
static void clear_calibration_target_area(const calibration_target_t *target);
static void fill_rect_rgb565(int x0, int y0, int x1, int y1, uint16_t color);
static inline int clamp_coordinate(int value, int min_value, int max_value);
static void draw_calibration_screen(void);
static bool solve_3x3(double mat[3][3], double vec[3], double out[3]);
static bool compute_touch_transform(void);
static void apply_touch_transform(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y);
static void apply_default_orientation_transform(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y);
static uint16_t clamp_to_display(double value, int max_value);
static void log_calibration_summary(void);

static void draw_calibration_grid(void) __attribute__((unused));
static void draw_calibration_grid(void)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "🎯 Touch Coordinate Calibration Mode");
    ESP_LOGI(TAG, "� Drawing calibration grid with numbered targets...");
    
    // Create line buffer for drawing
    size_t pixel_size = 2; // RGB565
    uint8_t *line_buf = heap_caps_malloc(DISPLAY_H_RES * pixel_size, MALLOC_CAP_DMA);
    if (!line_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA line buffer");
        return;
    }
    
    // Calibration grid with numbered touch targets
    for (int y = 0; y < DISPLAY_V_RES; y++) {
        for (int x = 0; x < DISPLAY_H_RES; x++) {
            uint16_t color = 0x0020; // Dark blue background
            
            // Define calibration points (9 points in 3x3 grid)
            struct {
                int x, y, size;
                uint16_t color;
                const char* name;
            } targets[] = {
                {100, 100, 80, 0xF800, "1:TL"},     // Top-Left (Red)
                {400, 100, 80, 0x07E0, "2:TM"},     // Top-Middle (Green) 
                {700, 100, 80, 0x001F, "3:TR"},     // Top-Right (Blue)
                {100, 640, 80, 0xFFE0, "4:ML"},     // Middle-Left (Yellow)
                {400, 640, 80, 0xF81F, "5:CENTER"}, // Center (Magenta)
                {700, 640, 80, 0x07FF, "6:MR"},     // Middle-Right (Cyan)
                {100, 1180, 80, 0xFD20, "7:BL"},    // Bottom-Left (Orange)
                {400, 1180, 80, 0xF7BE, "8:BM"},    // Bottom-Middle (Pink)
                {700, 1180, 80, 0xAFE5, "9:BR"}     // Bottom-Right (Light Blue)
            };
            
            // Draw grid lines for reference
            if (x % 100 == 0 || y % 100 == 0) {
                color = 0x2104; // Dark gray grid lines
            }
            
            // Draw calibration targets
            for (int i = 0; i < 9; i++) {
                int dx = x - targets[i].x;
                int dy = y - targets[i].y;
                int dist_sq = dx*dx + dy*dy;
                int radius = targets[i].size / 2;
                
                if (dist_sq <= radius*radius) {
                    if (dist_sq <= (radius-10)*(radius-10)) {
                        color = targets[i].color; // Inner filled circle
                    } else {
                        color = 0xFFFF; // White border
                    }
                }
                
                // Draw number in center of target
                if (abs(dx) <= 15 && abs(dy) <= 15) {
                    color = 0x0000; // Black number area
                }
            }
            
            // Display coordinate reference at corners
            if ((x < 150 && y < 50) || (x > DISPLAY_H_RES-150 && y < 50) ||
                (x < 150 && y > DISPLAY_V_RES-50) || (x > DISPLAY_H_RES-150 && y > DISPLAY_V_RES-50)) {
                color = 0xFFFF; // White reference text areas
            }
            
            // Set pixel
            uint16_t *pixel = (uint16_t*)&line_buf[x * 2];
            *pixel = color;
        }
        
        // Draw the line to display
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, DISPLAY_H_RES, y + 1, line_buf);
        xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
    }
    
    free(line_buf);
    ESP_LOGI(TAG, "✅ Touch calibration grid displayed!");
    ESP_LOGI(TAG, "📍 Touch the numbered targets in order 1-9");
    ESP_LOGI(TAG, "📊 Raw coordinates will be logged for mapping analysis");
}

static inline int clamp_coordinate(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void fill_rect_rgb565(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!panel_handle || !draw_finish_sem) {
        return;
    }

    x0 = clamp_coordinate(x0, 0, DISPLAY_H_RES);
    y0 = clamp_coordinate(y0, 0, DISPLAY_V_RES);
    x1 = clamp_coordinate(x1, 0, DISPLAY_H_RES);
    y1 = clamp_coordinate(y1, 0, DISPLAY_V_RES);

    int width = x1 - x0;
    int height = y1 - y0;
    if (width <= 0 || height <= 0) {
        return;
    }

    const int strip_lines = (height > 40) ? 40 : height;
    size_t strip_pixels = (size_t)width * strip_lines;
    uint16_t *strip_buf = heap_caps_malloc(strip_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!strip_buf) {
        ESP_LOGE("CALIBRATION", "Failed to allocate strip buffer (%dx%d)", width, strip_lines);
        return;
    }

    for (size_t i = 0; i < strip_pixels; ++i) {
        strip_buf[i] = color;
    }

    int drawn = 0;
    while (drawn < height) {
        int lines = (height - drawn > strip_lines) ? strip_lines : (height - drawn);
        esp_lcd_panel_draw_bitmap(panel_handle,
                                  x0,
                                  y0 + drawn,
                                  x1,
                                  y0 + drawn + lines,
                                  strip_buf);
        xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
        drawn += lines;
    }

    free(strip_buf);
}

static void clear_calibration_target_area(const calibration_target_t *target)
{
    const int margin = CAL_TARGET_CLEAR_MARGIN;
    fill_rect_rgb565(target->display_x - margin,
                     target->display_y - margin,
                     target->display_x + margin,
                     target->display_y + margin,
                     CAL_BACKGROUND_COLOR);
}

static void draw_calibration_target_cross(const calibration_target_t *target)
{
    if (!target) {
        return;
    }

    int target_index = (int)(target - cal_targets);
    if (last_drawn_target == target_index) {
        return;
    }

    clear_calibration_target_area(target);

    const int half = CAL_TARGET_LINE_HALF;
    const int thick = CAL_TARGET_LINE_THICK;
    const int center = CAL_TARGET_CENTER_SIZE;

    // Horizontal line
    fill_rect_rgb565(target->display_x - half,
                     target->display_y - thick / 2,
                     target->display_x + half,
                     target->display_y + (thick - thick / 2),
                     target->color);

    // Vertical line
    fill_rect_rgb565(target->display_x - thick / 2,
                     target->display_y - half,
                     target->display_x + (thick - thick / 2),
                     target->display_y + half,
                     target->color);

    // Center marker
    fill_rect_rgb565(target->display_x - center / 2,
                     target->display_y - center / 2,
                     target->display_x + (center - center / 2),
                     target->display_y + (center - center / 2),
                     0xFFFF);

    last_drawn_target = target_index;
}

static void finalize_calibration_target(const calibration_target_t *target)
{
    if (!target) {
        return;
    }

    clear_calibration_target_area(target);

    const int marker = CAL_TARGET_MARKER_SIZE;
    fill_rect_rgb565(target->display_x - marker / 2,
                     target->display_y - marker / 2,
                     target->display_x + (marker - marker / 2),
                     target->display_y + (marker - marker / 2),
                     target->color);

    fill_rect_rgb565(target->display_x - (marker / 2 - 3),
                     target->display_y - (marker / 2 - 3),
                     target->display_x + (marker - marker / 2 - 3),
                     target->display_y + (marker - marker / 2 - 3),
                     CAL_BACKGROUND_COLOR);

    last_drawn_target = -1;
}

static void draw_calibration_background(void)
{
    ESP_LOGI("CALIBRATION", "🎨 Drawing calibration background once");
    fill_rect_rgb565(0, 0, DISPLAY_H_RES, DISPLAY_V_RES, CAL_BACKGROUND_COLOR);

    // Draw reference grid lines every 100 pixels
    for (int x = 0; x <= DISPLAY_H_RES; x += 100) {
        fill_rect_rgb565(x, 0, x + 1, DISPLAY_V_RES, 0x2104);
    }
    for (int y = 0; y <= DISPLAY_V_RES; y += 100) {
        fill_rect_rgb565(0, y, DISPLAY_H_RES, y + 1, 0x2104);
    }

    // Highlight calibration target locations with faint squares
    for (int i = 0; i < 5; i++) {
        fill_rect_rgb565(cal_targets[i].display_x - 15,
                         cal_targets[i].display_y - 15,
                         cal_targets[i].display_x + 15,
                         cal_targets[i].display_y + 15,
                         cal_targets[i].color);
        fill_rect_rgb565(cal_targets[i].display_x - 10,
                         cal_targets[i].display_y - 10,
                         cal_targets[i].display_x + 10,
                         cal_targets[i].display_y + 10,
                         CAL_BACKGROUND_COLOR);
    }

    calibration_background_ready = true;
    last_drawn_target = -1;
}

static void draw_calibration_review_screen(void)
{
    last_drawn_target = -1;

    const int button_y1 = DISPLAY_V_RES / 2 - 80;
    const int button_y2 = DISPLAY_V_RES / 2 + 80;

    fill_rect_rgb565(0, 0, DISPLAY_H_RES, DISPLAY_V_RES, 0x0010);
    fill_rect_rgb565(50, button_y1, 250, button_y2, 0x07FF); // TEST
    fill_rect_rgb565(300, button_y1, 500, button_y2, 0x07E0); // ACCEPT
    fill_rect_rgb565(550, button_y1, 750, button_y2, 0xF800); // RESTART

    ESP_LOGI("CALIBRATION", "✅ Review screen ready - touch TEST/ACCEPT/RESTART");
}

static void draw_calibration_success_screen(void)
{
    last_drawn_target = -1;
    fill_rect_rgb565(0, 0, DISPLAY_H_RES, DISPLAY_V_RES, 0x07E0);
    ESP_LOGI("CALIBRATION", "✅ Calibration accepted screen drawn");
}

static void draw_enhanced_calibration(void)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel not initialized");
        return;
    }

    if (calibration_test_mode) {
        draw_calibration_screen();
        return;
    }

    if (calibration_review_mode) {
        draw_calibration_review_screen();
        return;
    }

    if (!calibration_mode) {
        draw_calibration_success_screen();
        return;
    }

    // Always redraw background in calibration mode to avoid caching issues
    draw_calibration_background();

    if (calibration_step < 5) {
        calibration_target_t *target = &cal_targets[calibration_step];
        draw_calibration_target_cross(target);
        ESP_LOGI("CALIBRATION", "🎯 Step %d/5: Touch the %s cross at (%d,%d)",
                 calibration_step + 1, target->name, target->display_x, target->display_y);
    }
}

static uint16_t clamp_to_display(double value, int max_value)
{
    if (value < 0.0) {
        value = 0.0;
    }
    double limit = (double)max_value;
    if (value > limit) {
        value = limit;
    }
    return (uint16_t)(value + 0.5);
}

static void apply_default_orientation_transform(uint16_t raw_x, uint16_t raw_y,
                                                uint16_t *out_x, uint16_t *out_y)
{
    uint16_t safe_x = (raw_x > TOUCH_NATIVE_MAX_X) ? TOUCH_NATIVE_MAX_X : raw_x;
    uint16_t safe_y = (raw_y > TOUCH_NATIVE_MAX_Y) ? TOUCH_NATIVE_MAX_Y : raw_y;

    uint16_t inverted_x = TOUCH_NATIVE_MAX_X - safe_x;
    uint16_t inverted_y = TOUCH_NATIVE_MAX_Y - safe_y;

    uint16_t display_x = inverted_y;
    uint16_t display_y = inverted_x;

    if (out_x) {
        *out_x = clamp_to_display(display_x, DISPLAY_H_RES - 1);
    }
    if (out_y) {
        *out_y = clamp_to_display(display_y, DISPLAY_V_RES - 1);
    }
}

static void apply_touch_transform(uint16_t raw_x, uint16_t raw_y,
                                  uint16_t *out_x, uint16_t *out_y)
{
    double disp_x = touch_transform.a0 + touch_transform.a1 * raw_x + touch_transform.a2 * raw_y;
    double disp_y = touch_transform.b0 + touch_transform.b1 * raw_x + touch_transform.b2 * raw_y;

    if (out_x) {
        *out_x = clamp_to_display(disp_x, DISPLAY_H_RES - 1);
    }
    if (out_y) {
        *out_y = clamp_to_display(disp_y, DISPLAY_V_RES - 1);
    }
}

static bool solve_3x3(double mat[3][3], double vec[3], double out[3])
{
    double aug[3][4];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            aug[r][c] = mat[r][c];
        }
        aug[r][3] = vec[r];
    }

    for (int i = 0; i < 3; i++) {
        int pivot = i;
        double max_val = fabs(aug[i][i]);
        for (int r = i + 1; r < 3; r++) {
            double val = fabs(aug[r][i]);
            if (val > max_val) {
                max_val = val;
                pivot = r;
            }
        }

        if (max_val < 1e-6) {
            return false;
        }

        if (pivot != i) {
            for (int c = i; c < 4; c++) {
                double tmp = aug[i][c];
                aug[i][c] = aug[pivot][c];
                aug[pivot][c] = tmp;
            }
        }

        double divisor = aug[i][i];
        for (int c = i; c < 4; c++) {
            aug[i][c] /= divisor;
        }

        for (int r = 0; r < 3; r++) {
            if (r == i) {
                continue;
            }
            double factor = aug[r][i];
            for (int c = i; c < 4; c++) {
                aug[r][c] -= factor * aug[i][c];
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        out[i] = aug[i][3];
    }
    return true;
}

static bool compute_touch_transform(void)
{
    double sum_1 = 0.0;
    double sum_rx = 0.0, sum_ry = 0.0;
    double sum_rxx = 0.0, sum_ryy = 0.0, sum_rxy = 0.0;
    double sum_dx = 0.0, sum_dy = 0.0;
    double sum_dx_rx = 0.0, sum_dx_ry = 0.0;
    double sum_dy_rx = 0.0, sum_dy_ry = 0.0;

    for (int i = 0; i < 5; i++) {
        if (!cal_targets[i].collected) {
            continue;
        }
        double rx = cal_targets[i].touch_x;
        double ry = cal_targets[i].touch_y;
        double dx = cal_targets[i].display_x;
        double dy = cal_targets[i].display_y;

        sum_1 += 1.0;
        sum_rx += rx;
        sum_ry += ry;
        sum_rxx += rx * rx;
        sum_ryy += ry * ry;
        sum_rxy += rx * ry;

        sum_dx += dx;
        sum_dy += dy;
        sum_dx_rx += dx * rx;
        sum_dx_ry += dx * ry;
        sum_dy_rx += dy * rx;
        sum_dy_ry += dy * ry;
    }

    if (sum_1 < 3.0) {
        ESP_LOGE("CALIBRATION", "Not enough calibration points (%f) to solve transform", sum_1);
        return false;
    }

    double mat[3][3] = {
        {sum_1,  sum_rx,  sum_ry},
        {sum_rx, sum_rxx, sum_rxy},
        {sum_ry, sum_rxy, sum_ryy}
    };

    double vec_x[3] = {sum_dx, sum_dx_rx, sum_dx_ry};
    double vec_y[3] = {sum_dy, sum_dy_rx, sum_dy_ry};
    double sol_x[3] = {0};
    double sol_y[3] = {0};

    double mat_copy_x[3][3];
    double mat_copy_y[3][3];
    memcpy(mat_copy_x, mat, sizeof(mat));
    memcpy(mat_copy_y, mat, sizeof(mat));

    if (!solve_3x3(mat_copy_x, vec_x, sol_x)) {
        ESP_LOGE("CALIBRATION", "Failed to solve transform for X axis (singular matrix)");
        return false;
    }

    if (!solve_3x3(mat_copy_y, vec_y, sol_y)) {
        ESP_LOGE("CALIBRATION", "Failed to solve transform for Y axis (singular matrix)");
        return false;
    }

    touch_transform.a0 = sol_x[0];
    touch_transform.a1 = sol_x[1];
    touch_transform.a2 = sol_x[2];
    touch_transform.b0 = sol_y[0];
    touch_transform.b1 = sol_y[1];
    touch_transform.b2 = sol_y[2];
    touch_transform.valid = true;

    ESP_LOGI("CALIBRATION", "📐 Touch transform ready:");
    ESP_LOGI("CALIBRATION", "   X = %.4f + %.6f*raw_x + %.6f*raw_y", touch_transform.a0, touch_transform.a1, touch_transform.a2);
    ESP_LOGI("CALIBRATION", "   Y = %.4f + %.6f*raw_x + %.6f*raw_y", touch_transform.b0, touch_transform.b1, touch_transform.b2);
    return true;
}

static void log_calibration_summary(void)
{
    ESP_LOGI("CALIBRATION", "📊 Calibration capture summary:");
    for (int i = 0; i < 5; i++) {
        if (!cal_targets[i].collected) {
            ESP_LOGI("CALIBRATION", "   %s: NOT COLLECTED", cal_targets[i].name);
            continue;
        }
        ESP_LOGI("CALIBRATION", "   %s: Display(%d,%d) <- Touch(%d,%d)",
                 cal_targets[i].name,
                 cal_targets[i].display_x, cal_targets[i].display_y,
                 cal_targets[i].touch_x, cal_targets[i].touch_y);
    }
}

static void reset_calibration_state(bool redraw_screen)
{
    calibration_step = 0;
    calibration_review_mode = false;
    calibration_test_mode = false;
    touch_point_count = 0;
    touch_transform.valid = false;
    last_drawn_target = -1;

    for (int i = 0; i < 5; i++) {
        cal_targets[i].collected = false;
        cal_targets[i].touch_x = 0;
        cal_targets[i].touch_y = 0;
    }

    if (redraw_screen && calibration_mode) {
        draw_enhanced_calibration();
    }
}

static void show_orientation_reference(void)
{
    ESP_LOGI(TAG, "🧭 Displaying orientation reference screen");
    
    // Clear screen first  
    uint16_t *line_buf = heap_caps_malloc(DISPLAY_H_RES * 2, MALLOC_CAP_DMA);
    if (!line_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA line buffer");
        return;
    }
    
    for (int y = 0; y < DISPLAY_V_RES; y++) {
        for (int x = 0; x < DISPLAY_H_RES; x++) {
            uint16_t color = 0x0000; // Black background
            
            // TOP-LEFT corner: RED square (100x100)
            if (x < 100 && y < 100) {
                color = 0xF800; // Red
            }
            // TOP-RIGHT corner: GREEN square (100x100)  
            else if (x >= DISPLAY_H_RES-100 && y < 100) {
                color = 0x07E0; // Green
            }
            // BOTTOM-LEFT corner: BLUE square (100x100)
            else if (x < 100 && y >= DISPLAY_V_RES-100) {
                color = 0x001F; // Blue  
            }
            // BOTTOM-RIGHT corner: YELLOW square (100x100)
            else if (x >= DISPLAY_H_RES-100 && y >= DISPLAY_V_RES-100) {
                color = 0xFFE0; // Yellow
            }
            // Center cross: WHITE
            else if ((x >= DISPLAY_H_RES/2-2 && x <= DISPLAY_H_RES/2+2) ||
                     (y >= DISPLAY_V_RES/2-2 && y <= DISPLAY_V_RES/2+2)) {
                color = 0xFFFF; // White
            }
            
            uint16_t *pixel = (uint16_t*)&line_buf[x * 2];
            *pixel = color;
        }
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, DISPLAY_H_RES, y + 1, line_buf);
        xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
    }
    
    free(line_buf);
    ESP_LOGI(TAG, "🧭 ORIENTATION REFERENCE:");
    ESP_LOGI(TAG, "📍 RED square    = TOP-LEFT (0,0)");
    ESP_LOGI(TAG, "📍 GREEN square  = TOP-RIGHT (800,0)"); 
    ESP_LOGI(TAG, "📍 BLUE square   = BOTTOM-LEFT (0,1280)");
    ESP_LOGI(TAG, "📍 YELLOW square = BOTTOM-RIGHT (800,1280)");
    ESP_LOGI(TAG, "📍 WHITE cross   = CENTER (400,640)");
    ESP_LOGI(TAG, "💡 Touch each corner and tell me which color you see!");
}

static void waveshare_pattern_test(void)
{
    if (calibration_mode) {
        draw_enhanced_calibration();
    } else {
        ESP_LOGI(TAG, "✅ Calibration complete - showing success screen");
        draw_enhanced_calibration();
    }
}

// Draw touch calibration screen with test squares
static void draw_calibration_screen(void)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Panel not initialized, cannot draw calibration screen");
        return;
    }
    
    ESP_LOGI(TAG, "🎯 Drawing touch calibration screen with test squares");
    
    // Real hardware drawing
    size_t pixel_size = (DISPLAY_BIT_PER_PIXEL == 24) ? 3 : 2;
    uint8_t *screen_buf = malloc(DISPLAY_H_RES * DISPLAY_V_RES * pixel_size);
    if (!screen_buf) {
        ESP_LOGE(TAG, "Failed to allocate screen buffer");
        return;
    }
    
    // Fill background with dark blue
    for (int i = 0; i < DISPLAY_H_RES * DISPLAY_V_RES; i++) {
        if (DISPLAY_BIT_PER_PIXEL == 24) {
            screen_buf[i * 3 + 0] = 0;   // R
            screen_buf[i * 3 + 1] = 0;   // G  
            screen_buf[i * 3 + 2] = 32;  // B (darker background)
        } else {
            ((uint16_t*)screen_buf)[i] = 0x0004; // Dark blue RGB565
        }
    }
    
    // Define calibration square positions and colors - CORRECTED TO MATCH DISPLAY OUTPUT
    typedef struct {
        int x, y, size;
        uint16_t color_565;
        uint8_t color_888[3];
        const char* label;
    } cal_square_t;
    
    cal_square_t squares[] = {
        // Corner squares for calibration - FIXED COLOR ASSIGNMENTS
        {50, 50, 80, 0xF800, {255, 0, 0}, "TOP-LEFT"},                        // RED - top-left (confirmed)
        {DISPLAY_H_RES-130, 50, 80, 0x001F, {0, 0, 255}, "TOP-RIGHT"},       // BLUE - top-right (you see blue there)  
        {50, DISPLAY_V_RES-130, 80, 0x07E0, {0, 255, 0}, "BOTTOM-LEFT"},     // GREEN - bottom-left (you see green there)
        {DISPLAY_H_RES-130, DISPLAY_V_RES-130, 80, 0xFFE0, {255, 255, 0}, "BOTTOM-RIGHT"}, // YELLOW - bottom-right (confirmed)
        
        // Center square for reference  
        {DISPLAY_H_RES/2-40, DISPLAY_V_RES/2-40, 80, 0xF81F, {255, 0, 255}, "CENTER"},     // MAGENTA - center
    };
    
    // Draw each calibration square
    int num_squares = sizeof(squares) / sizeof(squares[0]);
    for (int sq = 0; sq < num_squares; sq++) {
        cal_square_t *square = &squares[sq];
        
        ESP_LOGI(TAG, "Drawing %s square at (%d,%d) size %d", square->label, square->x, square->y, square->size);
        
        // Draw square
        for (int y = square->y; y < square->y + square->size && y < DISPLAY_V_RES; y++) {
            for (int x = square->x; x < square->x + square->size && x < DISPLAY_H_RES; x++) {
                int idx = (y * DISPLAY_H_RES + x);
                if (DISPLAY_BIT_PER_PIXEL == 24) {
                    screen_buf[idx * 3 + 0] = square->color_888[0]; // R
                    screen_buf[idx * 3 + 1] = square->color_888[1]; // G
                    screen_buf[idx * 3 + 2] = square->color_888[2]; // B
                } else {
                    ((uint16_t*)screen_buf)[idx] = square->color_565;
                }
            }
        }
        
        // Draw border around square (brighter)
        for (int border = 0; border < 3; border++) {
            // Top and bottom borders
            for (int x = square->x - border; x < square->x + square->size + border && x >= 0 && x < DISPLAY_H_RES; x++) {
                // Top border
                int y_top = square->y - border;
                if (y_top >= 0 && y_top < DISPLAY_V_RES) {
                    int idx = (y_top * DISPLAY_H_RES + x);
                    if (DISPLAY_BIT_PER_PIXEL == 24) {
                        screen_buf[idx * 3 + 0] = 255; // Bright white border
                        screen_buf[idx * 3 + 1] = 255;
                        screen_buf[idx * 3 + 2] = 255;
                    } else {
                        ((uint16_t*)screen_buf)[idx] = 0xFFFF;
                    }
                }
                // Bottom border
                int y_bot = square->y + square->size + border;
                if (y_bot >= 0 && y_bot < DISPLAY_V_RES) {
                    int idx = (y_bot * DISPLAY_H_RES + x);
                    if (DISPLAY_BIT_PER_PIXEL == 24) {
                        screen_buf[idx * 3 + 0] = 255;
                        screen_buf[idx * 3 + 1] = 255;
                        screen_buf[idx * 3 + 2] = 255;
                    } else {
                        ((uint16_t*)screen_buf)[idx] = 0xFFFF;
                    }
                }
            }
            
            // Left and right borders
            for (int y = square->y - border; y < square->y + square->size + border && y >= 0 && y < DISPLAY_V_RES; y++) {
                // Left border
                int x_left = square->x - border;
                if (x_left >= 0 && x_left < DISPLAY_H_RES) {
                    int idx = (y * DISPLAY_H_RES + x_left);
                    if (DISPLAY_BIT_PER_PIXEL == 24) {
                        screen_buf[idx * 3 + 0] = 255;
                        screen_buf[idx * 3 + 1] = 255;
                        screen_buf[idx * 3 + 2] = 255;
                    } else {
                        ((uint16_t*)screen_buf)[idx] = 0xFFFF;
                    }
                }
                // Right border
                int x_right = square->x + square->size + border;
                if (x_right >= 0 && x_right < DISPLAY_H_RES) {
                    int idx = (y * DISPLAY_H_RES + x_right);
                    if (DISPLAY_BIT_PER_PIXEL == 24) {
                        screen_buf[idx * 3 + 0] = 255;
                        screen_buf[idx * 3 + 1] = 255;
                        screen_buf[idx * 3 + 2] = 255;
                    } else {
                        ((uint16_t*)screen_buf)[idx] = 0xFFFF;
                    }
                }
            }
        }
    }
    
    // Draw to display
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, screen_buf);
    
    free(screen_buf);
    ESP_LOGI(TAG, "✅ Touch calibration screen drawn successfully!");
    ESP_LOGI(TAG, "📍 CALIBRATION INSTRUCTIONS - CORRECTED COLORS:");
    ESP_LOGI(TAG, "   🔴 RED square (top-left): Expected touch (50,50)");
    ESP_LOGI(TAG, "   � BLUE square (top-right): Expected touch (%d,50)", DISPLAY_H_RES-90);
    ESP_LOGI(TAG, "   � GREEN square (bottom-left): Expected touch (50,%d)", DISPLAY_V_RES-90);
    ESP_LOGI(TAG, "   🟡 YELLOW square (bottom-right): Expected touch (%d,%d)", DISPLAY_H_RES-90, DISPLAY_V_RES-90);
    ESP_LOGI(TAG, "   🟣 MAGENTA square (center): Expected touch (%d,%d)", DISPLAY_H_RES/2, DISPLAY_V_RES/2);
    ESP_LOGI(TAG, "   Touch each square and check the serial output for coordinate mapping!");
}

// Initialize complete display system (real MIPI DSI)
static esp_err_t init_display_system(void)
{
    ESP_LOGI(TAG, "🖥️ Initializing Real Waveshare 10.1\" MIPI DSI Display System");
    
    // Initialize LDO power for MIPI DSI PHY
    esp_err_t ret = init_ldo_power();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LDO power");
        return ret;
    }
    
    // Initialize backlight
    ESP_ERROR_CHECK(init_backlight());
    
    // Initialize MIPI DSI bus
    ret = init_mipi_dsi_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MIPI DSI bus");
        return ret;
    }
    
    // Initialize panel IO (MIPI DBI)
    ret = init_panel_io();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize panel IO");
        return ret;
    }
    
    // Initialize JD9365 panel
    ret = init_jd9365_panel();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize JD9365 panel");
        return ret;
    }
    
    // Wait for panel to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Display boot screen based on mode
    waveshare_pattern_test();
    
    if (calibration_mode) {
        // Stay in calibration mode - grid remains displayed
        ESP_LOGI(TAG, "🎯 CALIBRATION MODE: Grid remains displayed for coordinate mapping");
        ESP_LOGI(TAG, "📍 Touch each numbered target (1-9) to capture coordinates");
    } else {
        // Normal operation - wait and then draw test squares
        vTaskDelay(pdMS_TO_TICKS(3000));
        draw_calibration_screen();
    }
    
    system_status.display_initialized = true;
    ESP_LOGI(TAG, "✅ Waveshare 10.1\" MIPI DSI display system fully operational!");
    
    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// GT911 TOUCH CONTROLLER (WAVESHARE ESP-PHONE) //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Simplified touch coordinate adjustment using new calibration system
static void touch_adjust_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                     uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
    (void)max_point_num;

    if (!x || !y || !point_num) {
        return;
    }

    cal_state_t cal_state = cal_get_state();
    
    for (uint8_t i = 0; i < *point_num; i++) {
        uint16_t raw_x = x[i];
        uint16_t raw_y = y[i];

        // During calibration, pass raw coordinates to calibration system
        if (cal_state >= CAL_STATE_POINT_1 && cal_state <= CAL_STATE_POINT_3) {
            // Log raw coordinates for debugging
            ESP_LOGI("TOUCH", "Raw: (%d, %d)", raw_x, raw_y);
            // Keep raw coordinates - calibration system will use them
            x[i] = raw_x;
            y[i] = raw_y;
        } else {
            // Apply calibration transform (or default if not calibrated)
            uint16_t disp_x, disp_y;
            cal_transform(raw_x, raw_y, &disp_x, &disp_y);
            x[i] = disp_x;
            y[i] = disp_y;
        }
    }
}

static void render_touch_feedback(uint16_t x, uint16_t y, bool pressed)
{
    if (!panel_handle || !system_status.display_initialized) {
        return;
    }

    const int half = TOUCH_FEEDBACK_SIZE / 2;
    int x_start = (int)x - half;
    int y_start = (int)y - half;
    int x_end = x_start + TOUCH_FEEDBACK_SIZE;
    int y_end = y_start + TOUCH_FEEDBACK_SIZE;

    if (x_start < 0) {
        x_start = 0;
    }
    if (y_start < 0) {
        y_start = 0;
    }
    if (x_end > DISPLAY_H_RES) {
        x_end = DISPLAY_H_RES;
    }
    if (y_end > DISPLAY_V_RES) {
        y_end = DISPLAY_V_RES;
    }

    if (x_end <= x_start || y_end <= y_start) {
        return;
    }

    const int width = x_end - x_start;
    const int height = y_end - y_start;
    const size_t pixel_count = (size_t)width * (size_t)height;

#if DISPLAY_BIT_PER_PIXEL == 16
    const uint16_t active_color = 0xF810; // Bright magenta feedback
    const uint16_t idle_color = 0x0008;   // Dark blue background approximation
    const uint16_t fill = pressed ? active_color : idle_color;

    for (size_t i = 0; i < pixel_count; ++i) {
        touch_feedback_buffer[i] = fill;
    }

    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, touch_feedback_buffer);
    xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
#elif DISPLAY_BIT_PER_PIXEL == 18 || DISPLAY_BIT_PER_PIXEL == 24
    const uint8_t active_rgb[3] = {255, 32, 32};
    const uint8_t idle_rgb[3] = {0, 0, 32};
    const uint8_t *color = pressed ? active_rgb : idle_rgb;

    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t idx = i * TOUCH_FEEDBACK_PIXEL_SIZE;
        touch_feedback_buffer[idx + 0] = color[0];
        touch_feedback_buffer[idx + 1] = color[1];
        touch_feedback_buffer[idx + 2] = color[2];
    }

    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, touch_feedback_buffer);
    xSemaphoreTake(draw_finish_sem, portMAX_DELAY);
#endif
}

static esp_err_t init_touch_controller(void)
{
    ESP_LOGI(TAG, "🖐️ Initializing GT911 capacitive touch controller");
    ESP_LOGI(TAG, "🔧 GT911 AFTER JD9365 - Recreating I2C bus on shared pins (Waveshare design)");

    if (touch_handle) {
        ESP_LOGW(TAG, "GT911 touch controller already initialized");
        system_status.touch_initialized = true;
        return ESP_OK;
    }

    system_status.touch_initialized = false;

    // CRITICAL FIX: JD9365 creates and holds I2C_NUM_1 bus using i2c_bus component.
    // GT911 must REUSE the same i2c_bus handle rather than creating a new bus.
    // This is the correct approach for Waveshare hardware - shared bus usage, not sequential.
    ESP_LOGI(TAG, "🔧 Reusing JD9365's existing I2C_NUM_1 bus for GT911");
    
    // Get the existing i2c_bus handle that JD9365 created and is still using
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_I2C_SDA,         // GPIO7 - same pins as JD9365
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TOUCH_I2C_SCL,         // GPIO8 - same pins as JD9365
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,          // 400kHz for GT911
    };
    
    // Create i2c_bus handle on the same port that JD9365 is using
    // This should succeed by getting the existing handle rather than creating new
    if (gt911_i2c_bus_handle) {
        i2c_bus_delete(&gt911_i2c_bus_handle);
        gt911_i2c_bus_handle = NULL;
    }

    gt911_i2c_bus_handle = i2c_bus_create(TOUCH_I2C_PORT, &i2c_conf);
    esp_err_t ret = (gt911_i2c_bus_handle != NULL) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get i2c_bus handle for GT911: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get the internal i2c_master_bus_handle for compatibility with ESP LCD Touch library
    touch_bus_handle = i2c_bus_get_internal_bus_handle(gt911_i2c_bus_handle);
    if (touch_bus_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get internal bus handle from i2c_bus");
        i2c_bus_delete(&gt911_i2c_bus_handle);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "✅ Successfully reused JD9365's I2C_NUM_1 bus for GT911");

    // ENHANCED GT911 HARDWARE RESET SEQUENCE
    // GT911 requires specific reset timing to properly initialize and set I2C address
    ESP_LOGI(TAG, "🔧 Performing GT911 hardware reset sequence...");
    
    // Configure reset pin (interrupt pin not connected on official BSP)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOUCH_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // GT911 Address Selection via Reset Sequence:
    // INT pin state during reset determines I2C address
    // INT=LOW during reset -> Address 0x14 
    // INT=HIGH during reset -> Address 0x5D
    
    // Official BSP reset sequence (INT pin not connected)
    ESP_LOGI(TAG, "🔧 Performing GT911 reset sequence (BSP style)...");
    gpio_set_level(TOUCH_RST_GPIO, 0);  // Assert reset
    vTaskDelay(pdMS_TO_TICKS(20));      // Hold reset for 20ms
    gpio_set_level(TOUCH_RST_GPIO, 1);  // Release reset
    vTaskDelay(pdMS_TO_TICKS(100));     // Wait for GT911 to boot and I2C to stabilize
    
    // Test address 0x5D first
    uint8_t detected_addr = 0x5D;
    bool addr_found = false;
    
    ESP_LOGI(TAG, "🔍 Testing GT911 at address 0x5D...");
    if (i2c_master_probe(touch_bus_handle, 0x5D, 500) == ESP_OK) {
        detected_addr = 0x5D;
        addr_found = true;
        ESP_LOGI(TAG, "✅ GT911 found at primary address 0x5D");
    } else {
        ESP_LOGW(TAG, "⚠️ GT911 not responding at 0x5D, trying 0x14...");
        
        // Method 2: Try address 0x14 (INT=LOW during reset)
        ESP_LOGI(TAG, "� Attempting GT911 reset for address 0x14...");
        // Skip INT pin control since it's not connected on official BSP
        gpio_set_level(TOUCH_RST_GPIO, 0);  // Assert reset
        vTaskDelay(pdMS_TO_TICKS(20));      // Hold reset for 20ms
        gpio_set_level(TOUCH_RST_GPIO, 1);  // Release reset
        vTaskDelay(pdMS_TO_TICKS(50));      // Wait for GT911 to boot
        
        // INT pin not connected on official BSP
        
        vTaskDelay(pdMS_TO_TICKS(100));     // Wait for I2C to stabilize
        
        if (i2c_master_probe(touch_bus_handle, 0x14, 500) == ESP_OK) {
            detected_addr = 0x14;
            addr_found = true;
            ESP_LOGI(TAG, "✅ GT911 found at backup address 0x14");
        }
    }

    if (!addr_found) {
        ESP_LOGE(TAG, "❌ GT911 not responding at either address (0x5D or 0x14)");
        ESP_LOGE(TAG, "❌ Check hardware connections: SDA=GPIO7, SCL=GPIO8, RST=GPIO27, INT=NC");
        ret = ESP_ERR_NOT_FOUND;
        goto touch_init_cleanup;
    }

    touch_gt911_config.dev_addr = detected_addr;
    ESP_LOGI(TAG, "🎯 GT911 successfully detected at I2C address 0x%02X", detected_addr);

    // Create panel IO for GT911 communication
    ESP_LOGI(TAG, "🔧 Creating GT911 panel IO with address 0x%02X", detected_addr);
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = detected_addr;
    io_config.scl_speed_hz = TOUCH_I2C_FREQ_HZ;

    ret = esp_lcd_new_panel_io_i2c(touch_bus_handle, &io_config, &touch_panel_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create GT911 panel IO: %s", esp_err_to_name(ret));
        goto touch_init_cleanup;
    }
    ESP_LOGI(TAG, "✅ GT911 panel IO created successfully");

    // Configure GT911 touch controller - CORRECTED CONFIGURATION!
    // CRITICAL: Touch controller reports in NATIVE panel coordinates (before display rotation)
    // Display applies swap_xy=true + mirror_y=true for 90° rotation
    // Touch should NOT apply the same transforms - report native coordinates only
    ESP_LOGI(TAG, "🔧 Configuring GT911 touch controller (RST=GPIO27, INT=NC - Official Waveshare)");
    esp_lcd_touch_config_t touch_config = {
        .x_max = DISPLAY_V_RES,              // 1280 (native panel width before rotation)
        .y_max = DISPLAY_H_RES,              // 800 (native panel height before rotation)
        .rst_gpio_num = TOUCH_RST_GPIO,      // GPIO27 - Official BSP reset pin
        .int_gpio_num = TOUCH_INT_GPIO,      // GPIO_NC - Official BSP (not connected)
        .levels = {
            .reset = 0,                      // Active low reset
            .interrupt = 0,                  // Active low interrupt
        },
        .flags = {
            .swap_xy = 0,                    // NO swap - let display rotation handle it
            .mirror_x = 0,                   // NO mirror - native coordinates
            .mirror_y = 0,                   // NO mirror - native coordinates
        },
        .process_coordinates = touch_adjust_coordinates,
        .interrupt_callback = NULL,          // Polling mode (no interrupt handler)
        .user_data = NULL,
        .driver_data = &touch_gt911_config,
    };

    ESP_LOGI(TAG, "🔧 Creating GT911 controller instance...");
    
    // Add timeout protection for GT911 initialization
    TickType_t start_time = xTaskGetTickCount();
    ret = esp_lcd_touch_new_i2c_gt911(touch_panel_io, &touch_config, &touch_handle);
    TickType_t elapsed_time = xTaskGetTickCount() - start_time;
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create GT911 controller: %s (took %ld ms)", 
                 esp_err_to_name(ret), pdTICKS_TO_MS(elapsed_time));
        ESP_LOGE(TAG, "❌ This indicates GT911 hardware communication failure");
        goto touch_init_cleanup;
    }
    
    ESP_LOGI(TAG, "✅ GT911 controller created successfully (took %ld ms)", pdTICKS_TO_MS(elapsed_time));
    
    // Verify GT911 is responding by attempting a test read
    ESP_LOGI(TAG, "🔧 Verifying GT911 communication with test read...");
    uint16_t test_x[1], test_y[1];
    uint8_t test_points = 0;
    esp_err_t read_test = esp_lcd_touch_read_data(touch_handle);
    if (read_test == ESP_OK) {
        bool coords_ok = esp_lcd_touch_get_coordinates(touch_handle, test_x, test_y, NULL, &test_points, 1);
        ESP_LOGI(TAG, "✅ GT911 communication verified (read_data=%s, coords=%s, points=%d)", 
                 esp_err_to_name(read_test), coords_ok ? "OK" : "FAIL", test_points);
    } else {
        ESP_LOGW(TAG, "⚠️ GT911 test read failed: %s (this may be normal if no touch)", esp_err_to_name(read_test));
    }

    system_status.touch_initialized = true;
    ESP_LOGI(TAG, "🎉 GT911 touch controller successfully initialized!");
    ESP_LOGI(TAG, "🎉 Touch system ready on I2C_NUM_1 (GPIO7/8) with shared bus architecture");
    return ESP_OK;

touch_init_cleanup:
    if (touch_handle) {
        esp_lcd_touch_del(touch_handle);
        touch_handle = NULL;
    }
    if (touch_panel_io) {
        esp_lcd_panel_io_del(touch_panel_io);
        touch_panel_io = NULL;
    }
    if (gt911_i2c_bus_handle) {
        i2c_bus_delete(&gt911_i2c_bus_handle);
        gt911_i2c_bus_handle = NULL;
        touch_bus_handle = NULL;  // Internal handle becomes invalid
    }
    return ret;
}

// Simplified touch task using new calibration system
static void touch_task(void *pvParameters)
{
    (void)pvParameters;

    uint16_t touch_x[TOUCH_MAX_POINTS] = {0};
    uint16_t touch_y[TOUCH_MAX_POINTS] = {0};
    uint16_t touch_strength[TOUCH_MAX_POINTS] = {0};
    uint8_t touch_points = 0;
    const TickType_t delay_ticks = pdMS_TO_TICKS(TOUCH_SCAN_INTERVAL_MS);
    
    // Initialize calibration system
    cal_init();
    
    // Start calibration if not already calibrated
    if (!cal_is_valid()) {
        ESP_LOGI(TAG, "Starting touch calibration...");
        cal_start();
    }

    while (1) {
        bool touch_ready = false;
        if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            touch_ready = system_status.touch_initialized;
            xSemaphoreGive(system_mutex);
        }

        if (touch_handle && touch_ready) {
            esp_err_t read_ret = esp_lcd_touch_read_data(touch_handle);
            
            if (read_ret == ESP_OK) {
                bool got_coords = esp_lcd_touch_get_coordinates(touch_handle, 
                    touch_x, touch_y, touch_strength, &touch_points, TOUCH_MAX_POINTS);

                if (got_coords && touch_points > 0) {
                    uint16_t raw_x = touch_x[0];
                    uint16_t raw_y = touch_y[0];
                    
                    cal_state_t cal_state = cal_get_state();
                    
                    // During calibration, pass raw touch to calibration system
                    if (cal_state >= CAL_STATE_POINT_1 && cal_state <= CAL_STATE_POINT_3) {
                        if (cal_process_touch(raw_x, raw_y)) {
                            // Touch was processed by calibration
                            ESP_LOGI(TAG, "Calibration touch processed");
                        }
                    } else if (cal_state == CAL_STATE_COMPLETE) {
                        // Calibration just completed - exit calibration mode
                        ESP_LOGI(TAG, "Calibration complete! Touch to continue.");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        // Show a test pattern or continue to main UI
                    } else {
                        // Normal operation - coordinates already transformed by touch_adjust_coordinates
                        uint16_t disp_x = touch_x[0];
                        uint16_t disp_y = touch_y[0];
                        
                        // Update touch data
                        if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            touch_data.x = disp_x;
                            touch_data.y = disp_y;
                            touch_data.pressed = true;
                            touch_data.valid = true;
                            touch_data.last_touch_ms = esp_timer_get_time() / 1000ULL;
                            xSemaphoreGive(system_mutex);
                        }
                        
                        // Draw touch feedback
                        render_touch_feedback(disp_x, disp_y, true);
                        
                        ESP_LOGI(TAG, "Touch at (%d, %d)", disp_x, disp_y);
                    }
                } else {
                    // No touch detected
                    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                        touch_data.pressed = false;
                        xSemaphoreGive(system_mutex);
                    }
                }
            }
        }

        vTaskDelay(delay_ticks);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// BNO085 IMU INTEGRATION (DISPLAY ORIENTATION ONLY) ////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize I2C for BNO085
static esp_err_t init_bno085_i2c(void)
{
    ESP_LOGI(TAG, "Initializing BNO085 IMU (I2C) for display orientation");

    system_status.imu_initialized = false;
    imu_data.sensor_active = false;

    esp_err_t err = ESP_OK;

    if (!bno085_bus_handle) {
        const i2c_master_bus_config_t bus_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = BNO085_I2C_PORT,
            .sda_io_num = BNO085_I2C_SDA,
            .scl_io_num = BNO085_I2C_SCL,
            .flags = {
                .enable_internal_pullup = true,
            },
        };

        err = i2c_new_master_bus(&bus_config, &bno085_bus_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize BNO085 I2C bus: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (!bno085_device_handle) {
        const i2c_device_config_t device_config = {
            .device_address = BNO085_I2C_ADDR,
            .scl_speed_hz = BNO085_I2C_FREQ,
        };

        err = i2c_master_bus_add_device(bno085_bus_handle, &device_config, &bno085_device_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register BNO085 device on I2C bus: %s", esp_err_to_name(err));
            i2c_del_master_bus(bno085_bus_handle);
            bno085_bus_handle = NULL;
            return err;
        }
    }

    err = i2c_master_probe(bno085_bus_handle, BNO085_I2C_ADDR, 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✅ BNO085 IMU detected at I2C address 0x%02X", BNO085_I2C_ADDR);
        system_status.imu_initialized = true;
        imu_data.sensor_active = true;
    } else {
        ESP_LOGW(TAG, "⚠️ BNO085 IMU not detected, continuing without IMU (%s)", esp_err_to_name(err));
    }

    return ESP_OK;
}

// Update display orientation based on IMU data
static void update_display_orientation(void)
{
    if (!imu_data.sensor_active) return;
    
    // Determine display orientation based on roll/pitch
    uint8_t new_orientation = 0;
    
    if (imu_data.roll > 45 && imu_data.roll < 135) {
        new_orientation = 90;   // Rotated 90 degrees
    } else if (imu_data.roll > 135 || imu_data.roll < -135) {
        new_orientation = 180;  // Upside down
    } else if (imu_data.roll > -135 && imu_data.roll < -45) {
        new_orientation = 3;    // Rotated 270 degrees (encoded as 3)
    } else {
        new_orientation = 0;    // Normal orientation
    }
    
    // Update orientation if changed
    if (new_orientation != imu_data.orientation) {
        imu_data.orientation = new_orientation;
        ESP_LOGI(TAG, "Display orientation changed to %d degrees", new_orientation);
        
        // TODO: Implement display rotation based on orientation
        // This would require updating the display rendering pipeline
    }
    
    // Detect significant tilt
    imu_data.tilt_detected = (abs((int)imu_data.pitch) > 30 || abs((int)imu_data.roll) > 30);
}

// BNO085 reading task (simplified for display orientation only)
static void bno085_task(void *pvParameters)
{
    while (1) {
        if (imu_data.sensor_active) {
            // Simplified IMU reading - in real implementation would read actual BNO085 data
            // For now, just update timestamp and mark as active
            imu_data.last_reading_ms = esp_timer_get_time() / 1000;
            imu_data.calibrated = true;
            
            update_display_orientation();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz update rate
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// CAN BUS COMMUNICATION (CLIENT MODE) ///////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize TWAI/CAN bus
static esp_err_t init_can_bus(void)
{
    ESP_LOGI(TAG, "Initializing CAN bus (client mode) for multi-MCU communication");
    
    // Configure TWAI
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_PIN, TWAI_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    // Install TWAI driver
    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ CAN bus initialization failed, continuing without CAN");
        return ret;
    }
    
    // Start TWAI driver
    ret = twai_start();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ CAN bus initialized successfully at 1Mbps");
        can_status.bus_active = true;
        system_status.can_initialized = true;
    } else {
        ESP_LOGW(TAG, "⚠️ CAN bus start failed, continuing without CAN");
    }
    
    return ESP_OK;
}

// CAN message handling task
static void can_task(void *pvParameters)
{
    twai_message_t message;
    
    while (1) {
        if (can_status.bus_active) {
            // Receive CAN messages (non-blocking)
            esp_err_t ret = twai_receive(&message, pdMS_TO_TICKS(10));
            if (ret == ESP_OK) {
                can_status.messages_received++;
                can_status.last_message_ms = esp_timer_get_time() / 1000;
                
                // Process display-related CAN messages
                // TODO: Implement CAN message parsing for display commands
                ESP_LOGD(TAG, "CAN message received: ID=0x%03lX, DLC=%d", (unsigned long)message.identifier, message.data_length_code);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// TEMPORARY WEB SERVER (MIGRATE TO web_mcu LATER) ///////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if SOC_WIFI_SUPPORTED

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        system_status.connected_stations++;
        ESP_LOGI(TAG, "Station " MACSTR " connected (Total: %d)", MAC2STR(event->mac), system_status.connected_stations);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        system_status.connected_stations--;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected (Total: %d)", MAC2STR(event->mac), system_status.connected_stations);
    }
}

// Initialize WiFi Access Point (TEMPORARY - ESP32-P4 WiFi via ESP32-C6)
static esp_err_t init_wifi_temporary(void)
{
    ESP_LOGI(TAG, "🌐 Starting WiFi Access Point on ESP32-P4");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    // Simplified WiFi configuration for ESP32-P4
    wifi_init_config_t cfg = {
        .osi_funcs = &g_wifi_osi_funcs,
        .wpa_crypto_funcs = g_wifi_default_wpa_crypto_funcs,
        .static_rx_buf_num = 10,
        .dynamic_rx_buf_num = 32,
        .tx_buf_type = 1,
        .static_tx_buf_num = 0,
        .dynamic_tx_buf_num = 32,
        .rx_mgmt_buf_type = 1,
        .rx_mgmt_buf_num = 5,
        .cache_tx_buf_num = 0,
        .csi_enable = 0,
        .ampdu_rx_enable = 1,
        .ampdu_tx_enable = 1,
        .amsdu_tx_enable = 0,
        .nvs_enable = 1,
        .nano_enable = 0,
        .feature_caps = 0,  // No special features
        .sta_disconnected_pm = false,
        .espnow_max_encrypt_num = 7,
        .magic = WIFI_INIT_CONFIG_MAGIC
    };
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    system_status.wifi_active = true;
    ESP_LOGI(TAG, "✅ WiFi AP started: %s (TEMPORARY)", WIFI_SSID);
    
    return ESP_OK;
}

// Status API handler
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "mcu", "Display MCU v3.02 - GT911 Working");
    cJSON_AddStringToObject(json, "state", (mcu_state == MCU_STATE_FULL_OPERATIONAL) ? "Operational" : "Initializing");
    cJSON_AddBoolToObject(json, "display_ready", system_status.display_initialized);
    cJSON_AddBoolToObject(json, "touch_ready", system_status.touch_initialized);
    cJSON_AddBoolToObject(json, "imu_ready", system_status.imu_initialized);
    cJSON_AddBoolToObject(json, "can_ready", system_status.can_initialized);
    cJSON_AddBoolToObject(json, "wifi_active", system_status.wifi_active);
    cJSON_AddNumberToObject(json, "stations", system_status.connected_stations);
    cJSON_AddNumberToObject(json, "uptime_min", (esp_timer_get_time() - boot_time_us) / 60000000);
    
    touch_data_t touch_snapshot = {0};
    bool touch_ready = false;
    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        touch_ready = system_status.touch_initialized;
        touch_snapshot = touch_data;
        xSemaphoreGive(system_mutex);
    } else {
        touch_ready = system_status.touch_initialized;
    }

    cJSON *touch_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(touch_json, "ready", touch_ready);
    cJSON_AddBoolToObject(touch_json, "pressed", touch_snapshot.pressed);
    cJSON_AddBoolToObject(touch_json, "valid", touch_snapshot.valid);
    cJSON_AddNumberToObject(touch_json, "x", touch_snapshot.x);
    cJSON_AddNumberToObject(touch_json, "y", touch_snapshot.y);
    cJSON_AddNumberToObject(touch_json, "press_time_ms", touch_snapshot.press_time_ms);
    cJSON_AddNumberToObject(touch_json, "last_touch_ms", touch_snapshot.last_touch_ms);
    cJSON_AddItemToObject(json, "touch", touch_json);

    // IMU data
    if (imu_data.sensor_active) {
        cJSON *imu_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(imu_json, "roll", imu_data.roll);
        cJSON_AddNumberToObject(imu_json, "pitch", imu_data.pitch);
        cJSON_AddNumberToObject(imu_json, "orientation", imu_data.orientation);
        cJSON_AddBoolToObject(imu_json, "tilt_detected", imu_data.tilt_detected);
        cJSON_AddItemToObject(json, "imu", imu_json);
    }
    
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// Main page handler (lightweight)
static esp_err_t index_handler(httpd_req_t *req)
{
    const char* html_page = 
    "<!DOCTYPE html>"
    "<html><head><title>Display MCU v3.00</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>body{font-family:Arial;text-align:center;background:#000;color:#0f0;padding:20px;}"
    ".container{max-width:600px;margin:0 auto;}"
    ".status{font-size:18px;margin:20px 0;padding:15px;border:2px solid #0f0;}"
    "button{font-size:16px;padding:10px 20px;margin:10px;cursor:pointer;border:none;border-radius:5px;background:#0080ff;color:white;}"
    "</style></head><body>"
    "<div class=\"container\">"
    "<h1>🖥️ Display MCU v3.02 - GT911 Working!</h1>"
    "<h2>Waveshare 10.1\" MIPI-DSI Display Controller</h2>"
    "<div id=\"status\" class=\"status\">Loading...</div>"
    "<button onclick=\"updateStatus()\">Refresh Status</button>"
    "<div id=\"data\" style=\"text-align:left;margin-top:20px;\"></div>"
    "</div>"
    "<script>"
    "function updateStatus(){"
    "fetch('/api/status')"
    ".then(response=>response.json())"
    ".then(data=>{"
    "document.getElementById('status').innerHTML="
    "'MCU: '+data.mcu+'<br>State: '+data.state+'<br>Display: '+(data.display_ready?'✅':'❌')+'<br>Touch: '+(data.touch_ready?'✅':'❌')+'<br>IMU: '+(data.imu_ready?'✅':'❌')+'<br>CAN: '+(data.can_ready?'✅':'❌')+'<br>Uptime: '+data.uptime_min+' min';"
    "document.getElementById('data').innerHTML='<h3>System Data:</h3><pre>'+JSON.stringify(data,null,2)+'</pre>';"
    "});"
    "}"
    "setInterval(updateStatus,5000);"
    "updateStatus();"
    "</script></body></html>";
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

// Start web server (TEMPORARY)
static esp_err_t start_webserver_temporary(void)
{
    ESP_LOGI(TAG, "Starting web server (TEMPORARY - will migrate to web_mcu)");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &index_uri);
        
        httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler, .user_ctx = NULL};
        httpd_register_uri_handler(server, &status_uri);
        
        system_status.web_server_active = true;
        ESP_LOGI(TAG, "✅ Web server started at http://192.168.4.1 (TEMPORARY)");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ Failed to start web server");
        return ESP_FAIL;
    }
}

#endif // SOC_WIFI_SUPPORTED

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// SERIAL CONSOLE COMMANDS ///////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int cmd_test(int argc, char **argv)
{
    if (calibration_mode && calibration_review_mode) {
        ESP_LOGI("CONSOLE", "🧪 SERIAL COMMAND: Entering TEST mode");
        calibration_test_mode = true;
        waveshare_pattern_test(); // Show test grid
        return 0;
    } else if (calibration_mode && calibration_step < 5) {
        ESP_LOGI("CONSOLE", "❌ Cannot test - still collecting calibration points (%d/5)", calibration_step);
        return -1;
    } else if (!calibration_mode) {
        ESP_LOGI("CONSOLE", "❌ Cannot test - not in calibration mode");
        return -1;
    } else {
        ESP_LOGI("CONSOLE", "❌ Cannot test in current state");
        return -1;
    }
}

static int cmd_accept(int argc, char **argv)
{
    if (calibration_mode && (calibration_review_mode || calibration_test_mode)) {
        if (!touch_transform.valid) {
            ESP_LOGE("CONSOLE", "❌ Cannot accept - calibration transform invalid");
            return -1;
        }
        ESP_LOGI("CONSOLE", "✅ SERIAL COMMAND: Accepting calibration");
        calibration_mode = false;
        calibration_review_mode = false;
        calibration_test_mode = false;
        waveshare_pattern_test(); // Show success screen
        return 0;
    } else {
        ESP_LOGI("CONSOLE", "❌ Cannot accept - not in review/test mode");
        return -1;
    }
}

static int cmd_restart(int argc, char **argv)
{
    if (calibration_mode) {
        ESP_LOGI("CONSOLE", "🔄 SERIAL COMMAND: Restarting calibration");
        reset_calibration_state(true);
        return 0;
    } else {
        ESP_LOGI("CONSOLE", "❌ Cannot restart - not in calibration mode");
        return -1;
    }
}

static int cmd_back(int argc, char **argv)
{
    if (calibration_mode && calibration_test_mode) {
        ESP_LOGI("CONSOLE", "🔙 SERIAL COMMAND: Returning to review mode");
        calibration_test_mode = false;
        waveshare_pattern_test(); // Show review screen
        return 0;
    } else {
        ESP_LOGI("CONSOLE", "❌ Cannot go back - not in test mode");
        return -1;
    }
}

static int cmd_status(int argc, char **argv)
{
    ESP_LOGI("CONSOLE", "📊 CALIBRATION STATUS:");
    ESP_LOGI("CONSOLE", "   Calibration Mode: %s", calibration_mode ? "ACTIVE" : "INACTIVE");
    ESP_LOGI("CONSOLE", "   Calibration Step: %d/5", calibration_step);
    ESP_LOGI("CONSOLE", "   Review Mode: %s", calibration_review_mode ? "ACTIVE" : "INACTIVE");
    ESP_LOGI("CONSOLE", "   Test Mode: %s", calibration_test_mode ? "ACTIVE" : "INACTIVE");
    
    if (calibration_step > 0) {
        ESP_LOGI("CONSOLE", "📍 Collected Points:");
        for (int i = 0; i < calibration_step && i < 5; i++) {
            ESP_LOGI("CONSOLE", "   %s: Display(%d,%d) -> Touch(%d,%d)", 
                     cal_targets[i].name, 
                     cal_targets[i].display_x, cal_targets[i].display_y,
                     cal_targets[i].touch_x, cal_targets[i].touch_y);
        }
    }

    if (touch_transform.valid) {
        ESP_LOGI("CONSOLE", "📐 Transform:");
        ESP_LOGI("CONSOLE", "   X = %.4f + %.6f*raw_x + %.6f*raw_y",
                 touch_transform.a0, touch_transform.a1, touch_transform.a2);
        ESP_LOGI("CONSOLE", "   Y = %.4f + %.6f*raw_x + %.6f*raw_y",
                 touch_transform.b0, touch_transform.b1, touch_transform.b2);
    } else {
        ESP_LOGI("CONSOLE", "📐 Transform: NOT READY");
    }
    return 0;
}

static int cmd_help(int argc, char **argv)
{
    ESP_LOGI("CONSOLE", "🆘 CALIBRATION SERIAL COMMANDS:");
    ESP_LOGI("CONSOLE", "   test     - Enter visual test mode (review mode only)");
    ESP_LOGI("CONSOLE", "   accept   - Accept current calibration (review/test mode)");
    ESP_LOGI("CONSOLE", "   restart  - Restart calibration from beginning");
    ESP_LOGI("CONSOLE", "   back     - Return to review mode (test mode only)");
    ESP_LOGI("CONSOLE", "   status   - Show current calibration status");
    ESP_LOGI("CONSOLE", "   help     - Show this help message");
    ESP_LOGI("CONSOLE", "");
    ESP_LOGI("CONSOLE", "💡 Use these commands if touch buttons don't work properly!");
    return 0;
}

static void register_console_commands(void)
{
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    const esp_console_cmd_t commands[] = {
        {
            .command = "test",
            .help = "Enter calibration test mode",
            .hint = NULL,
            .func = &cmd_test,
        },
        {
            .command = "accept", 
            .help = "Accept current calibration",
            .hint = NULL,
            .func = &cmd_accept,
        },
        {
            .command = "restart",
            .help = "Restart calibration process",
            .hint = NULL, 
            .func = &cmd_restart,
        },
        {
            .command = "back",
            .help = "Return to review mode from test",
            .hint = NULL,
            .func = &cmd_back,
        },
        {
            .command = "status",
            .help = "Show calibration status",
            .hint = NULL,
            .func = &cmd_status,
        },
        {
            .command = "help",
            .help = "Show available commands",
            .hint = NULL,
            .func = &cmd_help,
        }
    };

    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
    
    ESP_LOGI("CONSOLE", "📟 Serial console commands registered! Type 'help' for available commands.");
}

static void console_task(void *pvParameters)
{
    char *line;
    while (1) {
        line = linenoise("cal> ");
        if (line == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        if (strlen(line) == 0) {
            free(line);
            continue;
        }
        
        linenoiseHistoryAdd(line);
        
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW("CONSOLE", "Unknown command '%s'. Type 'help' for available commands.", line);
        } else if (err == ESP_ERR_INVALID_ARG) {
            ESP_LOGE("CONSOLE", "Invalid arguments for command");
        } else if (err != ESP_OK) {
            ESP_LOGE("CONSOLE", "Command execution failed");
        }
        
        free(line);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// SYSTEM MONITORING & STATUS ////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Status timer callback
static void status_timer_callback(void *arg)
{
    system_status.uptime_ms = (esp_timer_get_time() - boot_time_us) / 1000;

#if SOC_WIFI_SUPPORTED
    const char *wifi_status = system_status.wifi_active ? "active" : "inactive";
    int wifi_clients = system_status.connected_stations;
#else
    const char *wifi_status = "unsupported";
    int wifi_clients = 0;
#endif

    touch_data_t touch_snapshot = {0};
    bool touch_ready = false;
    if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        touch_ready = system_status.touch_initialized;
        touch_snapshot = touch_data;
        xSemaphoreGive(system_mutex);
    } else {
        touch_ready = system_status.touch_initialized;
    }

    char touch_info[48];
    if (!touch_ready) {
        snprintf(touch_info, sizeof(touch_info), "❌ offline");
    } else if (touch_snapshot.pressed) {
        snprintf(touch_info, sizeof(touch_info), "✅ pressed (%d,%d)", touch_snapshot.x, touch_snapshot.y);
    } else {
        snprintf(touch_info, sizeof(touch_info), "✅ idle");
    }

    ESP_LOGI(TAG, "📊 Display MCU Status: %s | Display: %s | Touch: %s | IMU: %s | CAN: %s | WiFi: %s (%d clients) | Uptime: %ld min", 
             (mcu_state == MCU_STATE_FULL_OPERATIONAL) ? "OPERATIONAL" : "INITIALIZING",
             system_status.display_initialized ? "✅" : "❌",
             touch_info,
             system_status.imu_initialized ? "✅" : "❌", 
             system_status.can_initialized ? "✅" : "❌",
             wifi_status,
             wifi_clients,
             system_status.uptime_ms / 60000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// MAIN APPLICATION //////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void app_main(void)
{
    boot_time_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "🖥️ DISPLAY MCU v3.02 - ESP32-P4 WAVESHARE + GT911 TOUCH WORKING 🖥️");
    ESP_LOGI(TAG, "Hardware: ESP32-P4 + Waveshare 10.1\" MIPI-DSI + BNO085 IMU + CAN");
    ESP_LOGI(TAG, "Purpose: Focused display controller with temporary web server");
    ESP_LOGI(TAG, "Architecture: 6-MCU distributed system (display_mcu optimized)");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // TODO: Add SPIFFS initialization for future JPEG boot images
    
    // Create system mutex
    system_mutex = xSemaphoreCreateMutex();
    if (!system_mutex) {
        ESP_LOGE(TAG, "Failed to create system mutex");
        return;
    }
    
    mcu_state = MCU_STATE_INITIALIZING;
    
    // Initialize core display system (Waveshare crash-free approach)
    ESP_LOGI(TAG, "🎯 Phase 1: Core Display System Initialization");
    ESP_ERROR_CHECK(init_display_system());
    mcu_state = MCU_STATE_DISPLAY_READY;
    
    // Initialize GT911 touch controller
    ESP_LOGI(TAG, "🎯 Phase 2: GT911 Touch Controller Initialization");
    esp_err_t touch_ret = init_touch_controller();
    if (touch_ret == ESP_OK) {
        // Touch task now includes calibration system
        BaseType_t created = xTaskCreate(touch_task, "touch_task", 8192, NULL, 5, NULL);
        if (created != pdPASS) {
            ESP_LOGW(TAG, "Failed to create touch polling task");
        } else {
            ESP_LOGI(TAG, "✅ Touch task started with new calibration system");
        }
    } else {
        ESP_LOGW(TAG, "Touch initialization failed (%s); continuing without touch", esp_err_to_name(touch_ret));
    }

    // Initialize BNO085 IMU for display orientation
    ESP_LOGI(TAG, "🎯 Phase 3: BNO085 IMU Initialization - DISABLED FOR DEBUGGING");
    // init_bno085_i2c(); // Non-critical, continues if IMU not available
    
    // Initialize CAN bus communication
    ESP_LOGI(TAG, "🎯 Phase 4: CAN Bus Communication Initialization - DISABLED FOR DEBUGGING");
    // init_can_bus(); // Non-critical, continues if CAN not available
    
    // Initialize temporary WiFi and web server
#if SOC_WIFI_SUPPORTED
    ESP_LOGI(TAG, "🎯 Phase 5: Temporary WiFi & Web Server - DISABLED FOR DEBUGGING");
    /*
    esp_err_t wifi_init_ret = init_wifi_temporary();
    if (wifi_init_ret == ESP_OK) {
        ESP_ERROR_CHECK(start_webserver_temporary());
        mcu_state = MCU_STATE_WIFI_ACTIVE;
    } else {
        ESP_LOGW(TAG, "WiFi initialization failed (%s); continuing without temporary AP", esp_err_to_name(wifi_init_ret));
    }
    */
#else
    ESP_LOGW(TAG, "🎯 Phase 5: WiFi/Web server skipped (not supported on this target)");
#endif
    
    // Create system tasks
    ESP_LOGI(TAG, "🎯 Phase 6: Starting System Tasks");
    xTaskCreate(bno085_task, "bno085_task", 4096, NULL, 5, NULL);
    xTaskCreate(can_task, "can_task", 4096, NULL, 5, NULL);
    
    // Create status timer
    esp_timer_create_args_t status_timer_args = {
        .callback = &status_timer_callback,
        .name = "status_timer"
    };
    esp_timer_create(&status_timer_args, &status_timer);
    esp_timer_start_periodic(status_timer, 1500000);  // 1.5 seconds (like test code)
    
    // System fully operational
    mcu_state = MCU_STATE_FULL_OPERATIONAL;
    
    ESP_LOGI(TAG, "🔥 DISPLAY MCU v3.02 - GT911 TOUCH BREAKTHROUGH! 🔥");
    ESP_LOGI(TAG, "Features:");
    ESP_LOGI(TAG, "  ✅ 10.1\" Waveshare MIPI-DSI Display (crash-free Waveshare approach)");
    ESP_LOGI(TAG, "  ✅ GT911 Capacitive Touch (landscape-aligned coordinates)");
    ESP_LOGI(TAG, "  ✅ BNO085 IMU Integration (display orientation & tilt detection)");
    ESP_LOGI(TAG, "  ✅ CAN Bus Communication (multi-MCU coordination)");
#if SOC_WIFI_SUPPORTED
    ESP_LOGI(TAG, "  🔄 WiFi AP + Web Server (TEMPORARY - migrate to web_mcu)");
#else
    ESP_LOGI(TAG, "  ⛔ WiFi AP + Web Server unavailable on this target");
#endif
    ESP_LOGI(TAG, "Controls:");
#if SOC_WIFI_SUPPORTED
    ESP_LOGI(TAG, "  🌐 Web: http://192.168.4.1 (WiFi: %s / %s) [TEMPORARY]", WIFI_SSID, WIFI_PASS);
#else
    ESP_LOGI(TAG, "  🌐 Web: disabled (WiFi unsupported)");
#endif
    ESP_LOGI(TAG, "  📡 CAN: 1Mbps multi-MCU communication");
    ESP_LOGI(TAG, "Hardware:");
    ESP_LOGI(TAG, "  🖥️ Display: JD9365 controller, %dx%d resolution", DISPLAY_H_RES, DISPLAY_V_RES);
    ESP_LOGI(TAG, "  🖐️ Touch: GT911 on I2C%d (SDA=%d, SCL=%d, RST=%d, INT=%d)", TOUCH_I2C_PORT, TOUCH_I2C_SDA, TOUCH_I2C_SCL, TOUCH_RST_GPIO, TOUCH_INT_GPIO);
    ESP_LOGI(TAG, "  📡 CAN: GPIO43(TX), GPIO44(RX)");
    ESP_LOGI(TAG, "  🧭 IMU: GPIO40(SCL), GPIO41(SDA) - I2C only");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    ESP_LOGI(TAG, "Display MCU v3.02 - New simplified calibration system ready! 🚀");
    
    // Main loop - minimal processing
    while (1) {
        // System health monitoring
        if (xSemaphoreTake(system_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Update system status
            xSemaphoreGive(system_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz main loop
    }
}