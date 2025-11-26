#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
// #include "esp_ldo_regulator.h"  // LDO management seems unavailable
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_jd9365_10_1.h"

static const char *TAG = "DISPLAY_CONFIG_TEST";

#define MIPI_DSI_LANE_NUM         2
#define MIPI_DSI_LANE_BITRATE_MBPS 1000  // Try different bitrate
#define DPI_CLK_MHZ              80

// Different timing configurations to test
typedef struct {
    uint16_t h_size;
    uint16_t v_size;
    uint16_t hsync_back_porch;
    uint16_t hsync_pulse_width;
    uint16_t hsync_front_porch;
    uint16_t vsync_back_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_front_porch;
    const char* name;
} display_config_t;

static const display_config_t test_configs[] = {
    // Original Waveshare recommended (from header)
    {800, 1280, 20, 20, 40, 10, 4, 30, "Waveshare Original"},
    
    // Adjusted timing - more conservative
    {800, 1280, 100, 4, 100, 16, 4, 16, "Conservative Timing"},
    
    // Different resolution attempts
    {1280, 800, 40, 20, 20, 30, 4, 10, "Landscape 1280x800"},
    {1024, 600, 160, 20, 160, 12, 3, 29, "Standard 1024x600"},
    
    // Try exact manufacturer specs if available
    {800, 1280, 88, 44, 148, 36, 4, 16, "Standard MIPI Timing"},
};

// esp_ldo_channel_handle_t ldo_mipi_phy = NULL;

void test_display_config(const display_config_t* config) {
    ESP_LOGI(TAG, "=== Testing %s (%dx%d) ===", config->name, config->h_size, config->v_size);
    
    // Create DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    
    esp_err_t ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DSI bus for %s: %s", config->name, esp_err_to_name(ret));
        return;
    }

    // Create DBI IO
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DBI IO for %s: %s", config->name, esp_err_to_name(ret));
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
        return;
    }

    // Create DPI panel with test config
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size = config->h_size,
            .v_size = config->v_size,
            .hsync_back_porch = config->hsync_back_porch,
            .hsync_pulse_width = config->hsync_pulse_width,
            .hsync_front_porch = config->hsync_front_porch,
            .vsync_back_porch = config->vsync_back_porch,
            .vsync_pulse_width = config->vsync_pulse_width,
            .vsync_front_porch = config->vsync_front_porch,
        },
        .flags.use_dma2d = true,
    };
    
    ret = esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DPI panel for %s: %s", config->name, esp_err_to_name(ret));
        esp_lcd_panel_io_del(mipi_dbi_io);
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
        return;
    }

    // Create JD9365 panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = MIPI_DSI_LANE_NUM,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JD9365 panel for %s: %s", config->name, esp_err_to_name(ret));
        esp_lcd_panel_io_del(mipi_dbi_io);
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
        return;
    }

    // Initialize panel
    ESP_LOGI(TAG, "Initializing panel for %s", config->name);
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Create test pattern buffer
    size_t buffer_size = config->h_size * config->v_size * sizeof(uint16_t);
    uint16_t* buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for %s", config->name);
        esp_lcd_panel_del(panel_handle);
        esp_lcd_panel_io_del(mipi_dbi_io);
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
        return;
    }

    // Create a more detailed test pattern
    ESP_LOGI(TAG, "Creating test pattern for %s", config->name);
    for (int y = 0; y < config->v_size; y++) {
        for (int x = 0; x < config->h_size; x++) {
            uint16_t color;
            
            // Create a grid pattern that's easier to analyze
            if (x < config->h_size / 4) {
                color = 0xF800; // Red quarter
            } else if (x < config->h_size / 2) {
                color = 0x07E0; // Green quarter
            } else if (x < 3 * config->h_size / 4) {
                color = 0x001F; // Blue quarter
            } else {
                color = 0xFFFF; // White quarter
            }
            
            // Add horizontal stripes every 50 pixels to check alignment
            if (y % 50 == 0) {
                color = 0x0000; // Black stripe
            }
            
            buffer[y * config->h_size + x] = color;
        }
    }

    esp_cache_msync(buffer, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    
    ESP_LOGI(TAG, "Drawing test pattern for %s", config->name);
    ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, config->h_size, config->v_size, buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw bitmap for %s: %s", config->name, esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Test pattern drawn for %s. Display for 8 seconds.", config->name);
    ESP_LOGI(TAG, "Expected: Red | Green | Blue | White quarters with black horizontal stripes every 50 pixels");
    vTaskDelay(pdMS_TO_TICKS(8000));

    // Cleanup
    free(buffer);
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(mipi_dbi_io);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
    
    ESP_LOGI(TAG, "=== Completed test for %s ===\n", config->name);
    vTaskDelay(pdMS_TO_TICKS(2000)); // Brief pause between tests
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting systematic display configuration test");
    
    // Note: LDO power management seems to be handled automatically by the system
    ESP_LOGI(TAG, "Relying on system power management for MIPI PHY");
    
    // Test each configuration
    int num_configs = sizeof(test_configs) / sizeof(test_configs[0]);
    ESP_LOGI(TAG, "Will test %d different display configurations", num_configs);
    
    for (int i = 0; i < num_configs; i++) {
        ESP_LOGI(TAG, "--- Starting test %d of %d ---", i + 1, num_configs);
        test_display_config(&test_configs[i]);
        
        if (i < num_configs - 1) {
            ESP_LOGI(TAG, "Preparing next test in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    ESP_LOGI(TAG, "All configuration tests completed!");
    ESP_LOGI(TAG, "Please note which configuration showed the best results.");
    ESP_LOGI(TAG, "System will restart in 10 seconds to repeat the tests.");
    
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}