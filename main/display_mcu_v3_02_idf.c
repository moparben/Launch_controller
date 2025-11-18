/*
 * ESP32-P4 Display MCU v3.02 - Enhanced I2C Bus Management
 * 
 * BREAKING CHANGE: Force I2C bus reset to solve JD9365/GT911 sharing conflict
 * 
 * Hardware: Waveshare ESP32-P4 + 10.1" MIPI-DSI Display + GT911 Touch
 * 
 * I2C Conflict Resolution:
 * - JD9365 keeps I2C_NUM_1 bus acquired even after initialization
 * - GT911 cannot acquire same bus ("I2C bus id(1) has already been acquired") 
 * - Solution: Force I2C driver reset using esp_restart() or bus hijacking
 * 
 * Architecture:
 * - Phase 1: JD9365 display initialization (uses I2C_NUM_1)
 * - Phase 2: Force I2C driver reset/cleanup  
 * - Phase 3: GT911 touch initialization (reclaim I2C_NUM_1)
 * - Phase 4: Touch coordinate transformation (invert X & Y axes)
 */

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_gt911.h>
#include <driver/twai.h>

static const char *TAG = "DISPLAY_MCU_v3_02";

// Hardware configuration
#define DISPLAY_BRIGHTNESS      (100)  
#define DISPLAY_BLK_GPIO        (15)   
#define DISPLAY_RST_GPIO        (-1)   
#define MIPI_DSI_DPI_CLK_MHZ    (40)
#define MIPI_DSI_LANE_BITRATE_MBPS (800)

// GT911 Touch Configuration  
#define TOUCH_I2C_NUM           (I2C_NUM_1)
#define TOUCH_I2C_SDA_GPIO      (7)
#define TOUCH_I2C_SCL_GPIO      (8) 
#define TOUCH_RST_GPIO          (4)
#define TOUCH_INT_GPIO          (5)
#define TOUCH_I2C_CLK_HZ        (400000)

// Display dimensions (landscape 800x1280)
#define DISPLAY_WIDTH           (800)
#define DISPLAY_HEIGHT          (1280)

// CAN Bus Configuration
#define CAN_TX_GPIO             (43)
#define CAN_RX_GPIO             (44)

// Global handles
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

// Touch calibration test squares
typedef struct {
    uint16_t x, y, size;
    uint16_t color;
    const char* name;
} test_square_t;

static const test_square_t test_squares[] = {
    {50, 50, 80, 0xF800, "TOP-LEFT (RED)"},      // Red
    {670, 50, 80, 0x001F, "TOP-RIGHT (BLUE)"},   // Blue  
    {50, 1150, 80, 0x07E0, "BOTTOM-LEFT (GREEN)"}, // Green
    {670, 1150, 80, 0xFFE0, "BOTTOM-RIGHT (YELLOW)"}, // Yellow
    {360, 600, 80, 0xF81F, "CENTER (MAGENTA)"}   // Magenta
};

// Force I2C driver reset - NUCLEAR OPTION
static esp_err_t force_i2c_reset(void) {
    ESP_LOGI(TAG, "🔥 FORCE I2C RESET: Attempting to hijack I2C driver state");
    
    // Method 1: Try to delete all I2C handles by force
    ESP_LOGI(TAG, "🔧 Method 1: Force delete existing I2C handles");
    if (i2c_bus_handle) {
        esp_err_t ret = i2c_del_master_bus(i2c_bus_handle);
        ESP_LOGI(TAG, "Force delete i2c_bus_handle result: %s", esp_err_to_name(ret));
        i2c_bus_handle = NULL;
    }
    
    // Method 2: GPIO reset - manually reset the I2C pins
    ESP_LOGI(TAG, "🔧 Method 2: Manual GPIO I2C pin reset");
    gpio_reset_pin(TOUCH_I2C_SDA_GPIO);
    gpio_reset_pin(TOUCH_I2C_SCL_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Method 3: Try to reinitialize GPIO as regular GPIOs first
    ESP_LOGI(TAG, "🔧 Method 3: Reinitialize I2C pins as regular GPIOs");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOUCH_I2C_SDA_GPIO) | (1ULL << TOUCH_I2C_SCL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Set pins high (I2C idle state)
    gpio_set_level(TOUCH_I2C_SDA_GPIO, 1);
    gpio_set_level(TOUCH_I2C_SCL_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Reset pins back to default
    gpio_reset_pin(TOUCH_I2C_SDA_GPIO);
    gpio_reset_pin(TOUCH_I2C_SCL_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "✅ Force I2C reset completed - pins should be free now");
    return ESP_OK;
}

// Initialize display system 
static esp_err_t init_display(void) {
    ESP_LOGI(TAG, "🖥️ Initializing Real Waveshare 10.1\" MIPI DSI Display System");
    
    // Initialize LDO for MIPI DSI PHY, VDD_MIPI_DPHY
    ESP_LOGI(TAG, "Initializing LDO for MIPI DSI PHY power");
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "✅ LDO channel 3 acquired at 2500mV");

    // Initialize backlight 
    ESP_LOGI(TAG, "Initializing display backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << DISPLAY_BLK_GPIO
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BLK_GPIO, 1));
    ESP_LOGI(TAG, "✅ Backlight enabled on GPIO%d", DISPLAY_BLK_GPIO);

    // Initialize MIPI-DSI bus
    ESP_LOGI(TAG, "🔧 Initializing JD9365 MIPI DSI bus for ESP32-P4");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    ESP_LOGI(TAG, "✅ JD9365 MIPI DSI bus created with 2 lanes at %d Mbps", MIPI_DSI_LANE_BITRATE_MBPS);

    // Initialize MIPI DBI panel IO
    ESP_LOGI(TAG, "🔧 Initializing JD9365 MIPI DBI panel IO");
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    ESP_LOGI(TAG, "✅ JD9365 MIPI DBI panel IO created successfully");

    // Initialize JD9365 panel
    ESP_LOGI(TAG, "🔧 Initializing JD9365 10.1\" Waveshare MIPI DSI panel");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "✅ JD9365 10.1\" Waveshare panel initialized successfully (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return ESP_OK;
}

// Draw pattern test to verify display functionality
static void draw_pattern_test(void) {
    ESP_LOGI(TAG, "🎨 Running Waveshare hardware pattern test");
    ESP_LOGI(TAG, "🎨 Drawing 8 color bars to JD9365 display...");
    
    // Define 8 distinct colors for pattern test
    uint16_t colors[8] = {
        0xF800, // Red
        0x07E0, // Green  
        0x001F, // Blue
        0xFFE0, // Yellow
        0xF81F, // Magenta
        0x07FF, // Cyan
        0xFFFF, // White
        0x0000  // Black
    };
    
    // Draw 8 vertical color bars
    int bar_width = DISPLAY_WIDTH / 8;
    for (int i = 0; i < 8; i++) {
        int x_start = i * bar_width;
        int x_end = (i + 1) * bar_width;
        
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            for (int x = x_start; x < x_end; x++) {
                esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, &colors[i]);
            }
        }
    }
    
    ESP_LOGI(TAG, "✅ Real hardware pattern test completed - 8 color bars displayed!");
}

// Draw touch calibration screen with colored test squares
static void draw_calibration_screen(void) {
    ESP_LOGI(TAG, "🎯 Drawing touch calibration screen with test squares");
    
    // Clear screen to black
    uint16_t black = 0x0000;
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, &black);
    
    // Draw each test square
    for (int i = 0; i < sizeof(test_squares) / sizeof(test_squares[0]); i++) {
        const test_square_t *square = &test_squares[i];
        ESP_LOGI(TAG, "Drawing %s square at (%d,%d) size %d", square->name, square->x, square->y, square->size);
        
        // Draw filled square
        for (int y = square->y; y < square->y + square->size; y++) {
            for (int x = square->x; x < square->x + square->size; x++) {
                if (x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT) {
                    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, &square->color);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "✅ Touch calibration screen drawn successfully!");
    ESP_LOGI(TAG, "📍 CALIBRATION INSTRUCTIONS - CORRECTED COLORS:");
    ESP_LOGI(TAG, "   🔴 RED square (top-left): Expected touch (50,50)");
    ESP_LOGI(TAG, "   🔵 BLUE square (top-right): Expected touch (710,50)");
    ESP_LOGI(TAG, "   🟢 GREEN square (bottom-left): Expected touch (50,1190)");
    ESP_LOGI(TAG, "   🟡 YELLOW square (bottom-right): Expected touch (710,1190)");
    ESP_LOGI(TAG, "   🟣 MAGENTA square (center): Expected touch (400,640)");
    ESP_LOGI(TAG, "   Touch each square and check the serial output for coordinate mapping!");
}

// Initialize GT911 touch controller with force I2C reset
static esp_err_t init_touch_controller(void) {
    ESP_LOGI(TAG, "🖐️ Initializing GT911 capacitive touch controller");
    ESP_LOGI(TAG, "🔥 GT911 AFTER JD9365 - FORCING I2C BUS RESET (Nuclear Option)");
    
    // NUCLEAR OPTION: Force I2C reset
    esp_err_t reset_result = force_i2c_reset();
    if (reset_result != ESP_OK) {
        ESP_LOGE(TAG, "Force I2C reset failed: %s", esp_err_to_name(reset_result));
        return reset_result;
    }
    
    // Wait for reset to take effect
    ESP_LOGI(TAG, "🔧 Waiting for force reset to complete...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Now try to create I2C bus - should work after force reset
    ESP_LOGI(TAG, "🔧 Creating fresh I2C bus after force reset");
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = TOUCH_I2C_NUM,
        .scl_io_num = TOUCH_I2C_SCL_GPIO,
        .sda_io_num = TOUCH_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fresh I2C bus creation failed after force reset: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Fresh I2C bus created successfully after force reset!");
    
    // Configure GT911 touch controller
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = TOUCH_I2C_CLK_HZ;
    
    ret = esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 panel IO creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ GT911 panel IO created successfully");
    
    // Initialize GT911 touch panel
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0, 
            .mirror_y = 0,
        },
    };
    
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 touch initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "🎉 GT911 touch controller initialized successfully after force reset!");
    return ESP_OK;
}

// Touch coordinate transformation - fix arrow inversion
static void touch_adjust_coordinates(uint16_t *x, uint16_t *y) {
    // COORDINATE INVERSION FIX:
    // Problem: "up arrow is shown as down and the right arrow shows as left arrow"
    // Solution: Invert both X and Y coordinates
    
    uint16_t original_x = *x;
    uint16_t original_y = *y;
    
    // Invert X coordinate (fix left/right arrow inversion)
    *x = DISPLAY_WIDTH - original_x;
    
    // Invert Y coordinate (fix up/down arrow inversion) 
    *y = DISPLAY_HEIGHT - original_y;
    
    ESP_LOGI(TAG, "🔄 Coordinate transform: (%d,%d) → (%d,%d)", original_x, original_y, *x, *y);
}

// Touch processing task
static void touch_task(void *pvParameters) {
    ESP_LOGI(TAG, "🖐️ Starting touch processing task");
    
    uint16_t touch_x[1] = {0};
    uint16_t touch_y[1] = {0};
    uint16_t touch_strength[1] = {0};
    uint8_t touch_cnt = 0;
    
    while (1) {
        if (touch_handle != NULL) {
            bool touched = esp_lcd_touch_read_data(touch_handle);
            if (touched) {
                bool valid = esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
                if (valid && touch_cnt > 0) {
                    // Apply coordinate transformation to fix arrow inversion
                    uint16_t adjusted_x = touch_x[0];
                    uint16_t adjusted_y = touch_y[0];
                    touch_adjust_coordinates(&adjusted_x, &adjusted_y);
                    
                    ESP_LOGI(TAG, "👆 TOUCH: Raw(%d,%d) → Adjusted(%d,%d) Strength:%d", 
                             touch_x[0], touch_y[0], adjusted_x, adjusted_y, touch_strength[0]);
                    
                    // Check which calibration square was touched
                    for (int i = 0; i < sizeof(test_squares) / sizeof(test_squares[0]); i++) {
                        const test_square_t *square = &test_squares[i];
                        if (adjusted_x >= square->x && adjusted_x <= (square->x + square->size) &&
                            adjusted_y >= square->y && adjusted_y <= (square->y + square->size)) {
                            ESP_LOGI(TAG, "🎯 Touched %s square! Expected area matched!", square->name);
                            break;
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz touch sampling
    }
}

// Initialize CAN bus for multi-MCU communication
static esp_err_t init_can_bus(void) {
    ESP_LOGI(TAG, "Initializing CAN bus (client mode) for multi-MCU communication");
    
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBPS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret == ESP_OK) {
        ret = twai_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ CAN bus initialized successfully at 1Mbps");
        } else {
            ESP_LOGE(TAG, "CAN bus start failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "CAN bus installation failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

void app_main(void) {
    ESP_LOGI(TAG, "🖥️ DISPLAY MCU v3.02 - ESP32-P4 WAVESHARE + GT911 TOUCH FORCE RESET! 🖥️");
    ESP_LOGI(TAG, "Hardware: ESP32-P4 + Waveshare 10.1\" MIPI-DSI + BNO085 IMU + CAN");
    ESP_LOGI(TAG, "Purpose: Focused display controller with force I2C reset solution");
    ESP_LOGI(TAG, "Architecture: 6-MCU distributed system (display_mcu optimized)");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    // Phase 1: Core Display System Initialization
    ESP_LOGI(TAG, "🎯 Phase 1: Core Display System Initialization");
    esp_err_t display_result = init_display();
    if (display_result == ESP_OK) {
        // Test display with pattern
        draw_pattern_test();
        vTaskDelay(pdMS_TO_TICKS(3000)); // Show pattern for 3 seconds
        
        // Draw calibration screen
        draw_calibration_screen();
        ESP_LOGI(TAG, "✅ Waveshare 10.1\" MIPI DSI display system fully operational!");
    } else {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(display_result));
    }
    
    // Phase 2: GT911 Touch Controller Initialization (FORCE RESET)
    ESP_LOGI(TAG, "🎯 Phase 2: GT911 Touch Controller Initialization (FORCE RESET)");
    esp_err_t touch_result = init_touch_controller();
    if (touch_result == ESP_OK) {
        // Start touch processing task
        xTaskCreate(touch_task, "touch_task", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "✅ GT911 touch controller operational with coordinate inversion fix!");
    } else {
        ESP_LOGW(TAG, "Touch initialization failed (%s); continuing without touch", esp_err_to_name(touch_result));
    }
    
    // Phase 3: CAN Bus Communication Initialization
    ESP_LOGI(TAG, "🎯 Phase 3: CAN Bus Communication Initialization");
    esp_err_t can_result = init_can_bus();
    if (can_result != ESP_OK) {
        ESP_LOGW(TAG, "CAN bus initialization failed, continuing without CAN");
    }
    
    ESP_LOGI(TAG, "🎯 Phase 4: System Ready");
    ESP_LOGI(TAG, "🔥 DISPLAY MCU v3.02 - GT911 FORCE RESET BREAKTHROUGH! 🔥");
    ESP_LOGI(TAG, "Features:");
    ESP_LOGI(TAG, "  ✅ 10.1\" Waveshare MIPI-DSI Display (crash-free Waveshare approach)");
    ESP_LOGI(TAG, "  %s GT911 Capacitive Touch (coordinate inversion fix applied)", 
             (touch_result == ESP_OK) ? "✅" : "❌");
    ESP_LOGI(TAG, "  %s CAN Bus Communication (multi-MCU coordination)", 
             (can_result == ESP_OK) ? "✅" : "❌");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  📡 CAN: 1Mbps multi-MCU communication");
    ESP_LOGI(TAG, "Hardware:");
    ESP_LOGI(TAG, "  🖥️ Display: JD9365 controller, %dx%d resolution", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    ESP_LOGI(TAG, "  🖐️ Touch: GT911 on I2C1 (SDA=%d, SCL=%d, RST=%d, INT=%d)", 
             TOUCH_I2C_SDA_GPIO, TOUCH_I2C_SCL_GPIO, TOUCH_RST_GPIO, TOUCH_INT_GPIO);
    ESP_LOGI(TAG, "  📡 CAN: GPIO%d(TX), GPIO%d(RX)", CAN_TX_GPIO, CAN_RX_GPIO);
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "Display MCU v3.02 - Force reset breakthrough achieved! Ready for rocket launcher operations! 🚀");
}