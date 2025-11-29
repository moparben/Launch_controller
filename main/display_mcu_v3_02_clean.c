/**
 * Display MCU v3.02 - Clean Minimal Version
 * 
 * ESP32-P4 + Waveshare 10.1" MIPI-DSI Display + GT911 Touch
 * 
 * This is a clean rewrite focusing on:
 * 1. Reliable display initialization (MIPI-DSI + JD9365)
 * 2. Working touch input (GT911)
 * 3. Simple 3-point calibration system
 * 4. DMA2D for efficient buffer copies
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

// Display includes
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "i2c_bus.h"

// Our simple calibration system
#include "touch_calibration.h"

static const char *TAG = "DISPLAY_v3.02";

// ============================================================================
// Hardware Configuration
// ============================================================================

// Display dimensions (application coordinate space: portrait)
// NOTE: The physical JD9365 panel is native 800x1280 (portrait). For now
// operate the application in portrait mode so the display and touch map
// directly to native panel coordinates.
#define DISPLAY_WIDTH       800
#define DISPLAY_HEIGHT      1280

// Native physical panel resolution (portrait) used in DPI config and
// touch raw coordinate defaults:
#define DISPLAY_NATIVE_WIDTH     800
#define DISPLAY_NATIVE_HEIGHT    1280

// MIPI-DSI Configuration
#define MIPI_DSI_LANE_BITRATE_MBPS  1000
#define MIPI_DSI_PHY_LDO_CHANNEL    3
#define MIPI_DSI_PHY_LDO_VOLTAGE_MV 2500

// GPIO Configuration
#define GPIO_BACKLIGHT      15
#define GPIO_I2C_SDA        7
#define GPIO_I2C_SCL        8
#define GPIO_TOUCH_RST      27
// Optional LCD reset pin - set to your board's LCD reset if available; set to -1 to skip
#define GPIO_LCD_RST        -1

// Touch configuration
#define TOUCH_I2C_ADDR      0x5D

#ifndef ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG
#define ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG (0x8140)
#endif

// ============================================================================
// Global Handles
// ============================================================================

esp_lcd_panel_handle_t panel_handle = NULL;
static bool panel_hw_swap_supported = false;
SemaphoreHandle_t draw_finish_sem = NULL;

static esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;
// ============================================================================
// DMA Draw Complete Callback
// ============================================================================

static IRAM_ATTR bool on_draw_complete(esp_lcd_panel_handle_t panel,
                                        esp_lcd_dpi_panel_event_data_t *edata,
                                        void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    if (sem) {
        xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
        // log for debug when ISR callback fires
        ESP_EARLY_LOGI("DISPLAY_v3.02", "on_draw_complete: xSemaphoreGiveFromISR called (sem=%p)", (void*)sem);
    } else {
        ESP_EARLY_LOGI("DISPLAY_v3.02", "on_draw_complete called but sem==NULL");
    }
    return xHigherPriorityTaskWoken == pdTRUE;
}
// stray line removed
// ============================================================================
// Display Initialization
// ============================================================================

esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing MIPI-DSI display...");
    
    // Create draw completion semaphore
    draw_finish_sem = xSemaphoreCreateBinary();
    if (!draw_finish_sem) {
        ESP_LOGE(TAG, "Failed to create draw semaphore");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(draw_finish_sem);  // Start with semaphore available
    
    // Initialize LDO for MIPI-DSI PHY
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_LDO_CHANNEL,
        .voltage_mv = MIPI_DSI_PHY_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));
    
    // Configure backlight GPIO
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << GPIO_BACKLIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(GPIO_BACKLIGHT, 1);
    ESP_LOGI(TAG, "Backlight enabled on GPIO%d", GPIO_BACKLIGHT);
    
    // Create MIPI-DSI bus
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));
    ESP_LOGI(TAG, "MIPI-DSI bus created");
    
    // Create DBI panel IO
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &mipi_dbi_io));
    ESP_LOGI(TAG, "MIPI-DBI panel IO created");
    
    // Create JD9365 panel
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 80,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            // Use native panel orientation for DPI timings (portrait) and
            // rotate the panel afterward so the app sees landscape.
            .h_size = DISPLAY_NATIVE_WIDTH,
            .v_size = DISPLAY_NATIVE_HEIGHT,
            .hsync_back_porch = 140,
            .hsync_pulse_width = 40,
            .hsync_front_porch = 40,
            .vsync_back_porch = 16,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 16,
        },
        .flags = {
            .use_dma2d = true,  // Enable DMA2D for efficient buffer copies
        },
    };
    
    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };
    
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "panel handle %p, reset pin: %d", panel_handle, panel_config.reset_gpio_num);
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "Panel init completed, now enabling display output");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    vTaskDelay(pdMS_TO_TICKS(100)); // give panel some time to power up
    ESP_LOGI(TAG, "JD9365 panel initialized (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Register DMA completion callback
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = on_draw_complete,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &callbacks, draw_finish_sem));
    ESP_LOGI(TAG, "DMA callbacks registered");
    // Ensure there's no axis swap for portrait. If the panel driver is in a
    // swapped state, request swap off. For most JD9365 revisions this will
    // either succeed or return ESP_ERR_NOT_SUPPORTED - either is fine.
    esp_err_t rc = esp_lcd_panel_swap_xy(panel_handle, false);
    if (rc == ESP_OK) {
        // hardware supports swap; we don't need software mapping
        panel_hw_swap_supported = true;
        ESP_LOGI(TAG, "Panel swap_xy supported by driver");
    } else {
        panel_hw_swap_supported = false;
        ESP_LOGW(TAG, "Panel swap_xy failed (not supported): %s", esp_err_to_name(rc));
    }
    // Reset any mirroring; for portrait defaults we prefer no mirroring
    rc = esp_lcd_panel_mirror(panel_handle, false, false);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Panel mirror adjustment failed: %s", esp_err_to_name(rc));
    }
    
    ESP_LOGI(TAG, "Display initialization complete!");
    return ESP_OK;
}

// Helper: draw a rect in application coordinates (DISPLAY_WIDTH x DISPLAY_HEIGHT)
// If hardware swap isn't supported, and software swap is enabled, perform
// a simple mapping to the hardware coordinates (native portrait) so our
// application remains in landscape coordinate space.
static void display_draw_filled_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!panel_handle || !draw_finish_sem) return;
    // Clamp coordinates
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DISPLAY_WIDTH) x1 = DISPLAY_WIDTH;
    if (y1 > DISPLAY_HEIGHT) y1 = DISPLAY_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;

#if CAL_APP_SOFTWARE_SWAP_XY
    // If the panel driver didn't swap axes, draw rotated rectangles by
    // mapping app coords (x,y) -> hw coords (y,x) and drawing per hardware row
    // (avoids needing to transpose actual image buffers)
    // Check if hardware supports swap - if so, we can draw directly
    if (!panel_hw_swap_supported) {
        int hw_x0 = y0;
        int hw_x1 = y1;
        int hw_y0 = x0;
        int hw_y1 = x1;
        int hw_width = hw_x1 - hw_x0;
        uint16_t *hw_line = heap_caps_malloc(hw_width * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!hw_line) return;
        for (int i = 0; i < hw_width; i++) hw_line[i] = color;
        for (int y = hw_y0; y < hw_y1; y++) {
            esp_err_t r = esp_lcd_panel_draw_bitmap(panel_handle, hw_x0, y, hw_x1, y + 1, hw_line);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "display_draw_filled_rect: draw failed: %s", esp_err_to_name(r));
                break;
            }
            if (xSemaphoreTake(draw_finish_sem, portMAX_DELAY) != pdTRUE) {
                ESP_LOGW(TAG, "display_draw_filled_rect: wait for draw finish timed out");
            }
        }
        free(hw_line);
        return;
    }
#endif

    int width = x1 - x0;
    uint16_t *line = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) return;
    for (int i = 0; i < width; i++) line[i] = color;
    for (int y = y0; y < y1; y++) {
        esp_err_t r = esp_lcd_panel_draw_bitmap(panel_handle, x0, y, x1, y + 1, line);
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "display_draw_filled_rect: draw failed: %s", esp_err_to_name(r));
            break;
        }
        if (xSemaphoreTake(draw_finish_sem, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "display_draw_filled_rect: wait for draw finish timed out");
        }
    }
    free(line);
}

// ============================================================================
// Touch Controller Initialization
// ============================================================================

esp_err_t init_touch(void)
{
    ESP_LOGI(TAG, "Initializing GT9xx touch controller (GT911/GT9271)...");
    
    // Note: I2C bus is already initialized by JD9365 driver
    // We just need to create the touch panel handle
    
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    
    // Create or get an I2C bus handle (same port/pins as JD9365). The espressif i2c_bus component
    // ensures a singleton per port, so this will return the existing bus if JD9365 already created it.
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_I2C_SDA,
        .scl_io_num = GPIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { .clk_speed = 400000 },
        .clk_flags = 0,
    };

    i2c_bus_handle_t local_i2c_bus = i2c_bus_create(I2C_NUM_1, &i2c_conf);
    if (!local_i2c_bus) {
        ESP_LOGE(TAG, "Failed to create or get I2C bus handle for GT911");
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_bus_handle_t master_i2c_bus = i2c_bus_get_internal_bus_handle(local_i2c_bus);
    if (!master_i2c_bus) {
        ESP_LOGE(TAG, "Failed to get internal I2C master handle from wrapper");
        return ESP_ERR_INVALID_STATE;
    }

    // Attempt to probe address (0x5D or 0x14) by creating a temporary io
    // and reading the PRODUCT ID register. This allows us to pick a correct
    // address for GT9xx devices (GT911/GT9271) that may use one of two
    // possible addresses depending on module wiring.
    uint16_t candidate_addrs[] = { ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP };
    bool found = false;
    char pid[16] = {0};
    for (int i = 0; i < 2 && !found; ++i) {
        tp_io_config.dev_addr = candidate_addrs[i];
        esp_lcd_panel_io_handle_t probe_io = NULL;
        if (esp_lcd_new_panel_io_i2c_v2(master_i2c_bus, &tp_io_config, &probe_io) == ESP_OK && probe_io) {
            // Try reading product ID
            uint8_t buf[8];
            esp_err_t r = esp_lcd_panel_io_rx_param(probe_io, ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG, buf, sizeof(buf));
            if (r == ESP_OK) {
                // Basic check: bytes look like 4-6 ASCII characters or '91'/'9271'
                size_t len = sizeof(buf);
                if (len > sizeof(pid) - 1) len = sizeof(pid) - 1;
                for (size_t k = 0; k < len; ++k) pid[k] = (buf[k] >= 32 && buf[k] < 127) ? buf[k] : '.';
                pid[len] = '\0';
                ESP_LOGI(TAG, "Probe address 0x%02X: product id approx '%s'", (unsigned int)candidate_addrs[i], pid);
                // Use the first successful read
                tp_io_config.dev_addr = candidate_addrs[i];
                found = true;
            }
            // Clean temp io handle
            esp_lcd_panel_io_del(probe_io);
        }
    }
    // If not found, fall back to default (value in tp_io_config set earlier: 0x5D) and log
    if (!found) {
        ESP_LOGW(TAG, "GT9xx probe failed - using default addr 0x%02X", (unsigned int)tp_io_config.dev_addr);
    }
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(master_i2c_bus, &tp_io_config, &tp_io_handle));
    
    esp_lcd_touch_config_t tp_cfg = {
        // The touch controller reports coordinates in the native panel
        // orientation (portrait 800x1280). Use native values to ensure the
        // touch driver maps to the hardware correctly. The calibration
        // layer is responsible for mapping to application landscape coords.
        .x_max = DISPLAY_NATIVE_WIDTH,
        .y_max = DISPLAY_NATIVE_HEIGHT,
        .rst_gpio_num = GPIO_TOUCH_RST,
        .int_gpio_num = -1,  // Polling mode
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            // No raw swap here - the panel is in native portrait orientation.
            // Use mirroring only if required by module wiring.
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    
    // 'gt911_config' is not required here; the touch driver will use the io/probe dev address
    
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "GT9xx touch controller initialized");

    // Try to query the native raw resolution from the GT9xx controller and
    // seed the calibration bounds so the default mapping works correctly
    uint16_t raw_x_max = 0, raw_y_max = 0;
    if (esp_lcd_touch_gt911_get_raw_resolution(touch_handle, &raw_x_max, &raw_y_max) == ESP_OK) {
        // Seed the calibration bounds: min=0, max=raw_max
        cal_set_bounds(0, raw_x_max, 0, raw_y_max);
    }
    
    return ESP_OK;
}

// ============================================================================
// Touch Task - Simple polling with calibration
// ============================================================================

void touch_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Touch task started");
    
    // Initialize calibration system
#if !defined(CAL_DISABLE_CALIBRATION) || (CAL_DISABLE_CALIBRATION == 0)
    cal_init();
#else
    ESP_LOGW(TAG, "CAL_DISABLE_CALIBRATION=1: skipping cal_init()");
#endif
    
    // Optionally, start calibration if no valid calibration exists. By
    // default we avoid automatically entering calibration on boot since it
    // hijacks the UI and blocks other development interaction. To keep the
    // previous behavior, set CAL_AUTO_START_ON_BOOT to 1 in the build.
#if defined(CAL_AUTO_START_ON_BOOT) && (CAL_AUTO_START_ON_BOOT == 1)
    if (!cal_is_valid()) {
        ESP_LOGI(TAG, "No valid calibration - starting calibration sequence (auto-start)");
#if !defined(CAL_DISABLE_CALIBRATION) || (CAL_DISABLE_CALIBRATION == 0)
        cal_start();
#else
        ESP_LOGW(TAG, "CAL_AUTO_START_ON_BOOT prevented by CAL_DISABLE_CALIBRATION=1");
#endif
    }
#else
    if (!cal_is_valid()) {
        ESP_LOGI(TAG, "No valid calibration (auto-start disabled). Press the 'Calibrate' button or invoke cal_start() from UI to begin calibration.");
    }
#endif
    
    uint16_t touch_x[5], touch_y[5];
    uint16_t touch_strength[5];
    uint8_t touch_cnt = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));  // 50Hz polling
        
        if (!touch_handle) continue;
        
        // Read touch data
        esp_lcd_touch_read_data(touch_handle);
        
        bool touched = esp_lcd_touch_get_coordinates(
            touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 5);
        
        if (touched && touch_cnt > 0) {
            uint16_t raw_x = touch_x[0];
            uint16_t raw_y = touch_y[0];

            // Update observed raw bounds used by default mapping in calibration
            cal_update_bounds(raw_x, raw_y);
            
            // Check if we're in calibration mode
            cal_state_t state = cal_get_state();
            if (state >= CAL_STATE_POINT_1 && state <= CAL_STATE_POINT_3) {
                // Feed touch to calibration system
                if (cal_process_touch(raw_x, raw_y)) {
                    // Touch was consumed by calibration
                    continue;
                }
            }
            
            // Apply calibration transform
            uint16_t disp_x, disp_y;
            cal_transform(raw_x, raw_y, &disp_x, &disp_y);
            
            ESP_LOGI(TAG, "Touch: raw(%d,%d) -> display(%d,%d)", 
                     raw_x, raw_y, disp_x, disp_y);
            
            // Debug overlay: draw raw/mapped coordinates so we can visually
            // verify calibration. Draw small squares on screen for quick
            // verification. This is active in debug builds and helps
            // us tune the mapping for GT9271.
            // Raw raw->display mapping using default mapping (no transform)
            uint16_t raw_disp_x = 0, raw_disp_y = 0;
            // Compute raw->display mapping using raw bounds if available
            cal_transform(raw_x, raw_y, &raw_disp_x, &raw_disp_y);
            // Draw raw point (magenta) and computed mapped (green) using calibration helper
            if (cal_get_overlay()) {
                cal_debug_draw_point(raw_disp_x, raw_disp_y, false);
                cal_debug_draw_point(disp_x, disp_y, true);
            }
            // For now, just log it
        }
    }
}

// ============================================================================
// Simple Drawing Helpers
// ============================================================================

static void fill_screen(uint16_t color)
{
    if (!panel_handle || !draw_finish_sem) return;
    display_draw_filled_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void draw_test_pattern(void)
{
    ESP_LOGI(TAG, "Drawing test pattern...");
    
    // Fill with dark blue
    fill_screen(0x0010);
    
    // Draw corner markers
    
    // Top-left: Red (use our app-space rect helper)
    display_draw_filled_rect(0, 0, 50, 50, 0xF800);
    
    // Top-right: Green
    display_draw_filled_rect(DISPLAY_WIDTH - 50, 0, DISPLAY_WIDTH, 50, 0x07E0);
    
    // Bottom-left: Blue
    display_draw_filled_rect(0, DISPLAY_HEIGHT - 50, 50, DISPLAY_HEIGHT, 0x001F);
    
    // Bottom-right: Yellow
    display_draw_filled_rect(DISPLAY_WIDTH - 50, DISPLAY_HEIGHT - 50, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0xFFE0);
    
    // Center: White
    display_draw_filled_rect((DISPLAY_WIDTH - 50) / 2, (DISPLAY_HEIGHT - 50) / 2,
                              (DISPLAY_WIDTH + 50) / 2, (DISPLAY_HEIGHT + 50) / 2, 0xFFFF);
    
    // no temporary block needed
    
    ESP_LOGI(TAG, "Test pattern complete");
    ESP_LOGI(TAG, "  Red=Top-Left, Green=Top-Right");
    ESP_LOGI(TAG, "  Blue=Bottom-Left, Yellow=Bottom-Right");
    ESP_LOGI(TAG, "  White=Center");
}

// ============================================================================
// Main Application
// ============================================================================

/* If BUILD_DISPLAY_V3_03 is defined, the app_main from display_mcu_v3_03.c
 * will be used instead. Keep the init and helper functions in this file and
 * only exclude the second app_main to avoid duplicate entry points.
 */
#ifndef BUILD_DISPLAY_V3_03
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Display MCU v3.02 - Clean Minimal Version");
    ESP_LOGI(TAG, "ESP32-P4 + Waveshare 10.1\" + GT9xx (GT911/GT9271)");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS (needed for calibration storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    
    // Initialize display
    ESP_ERROR_CHECK(init_display());
    
    // Draw initial test pattern
    ESP_LOGI(TAG, "About to draw test pattern");
    draw_test_pattern();
    ESP_LOGI(TAG, "Draw test pattern called");
    
    // Small delay to see the test pattern
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Initialize touch controller
    ESP_ERROR_CHECK(init_touch());
    
    // Start touch task
    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "Touch the screen to calibrate");
    ESP_LOGI(TAG, "========================================");
    
    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif /* BUILD_DISPLAY_V3_03 */
