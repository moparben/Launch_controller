#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"  
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_jd9365_10_1.h"

static const char *TAG = "TIMING_TEST";

#define MIPI_DSI_DPI_CLK_MHZ           80
#define MIPI_DSI_LANE_NUM             2
#define MIPI_DSI_LANE_BITRATE_MBPS    800

// Test different timing configurations for JD9365 10.1" display
typedef struct {
    const char* name;
    uint16_t h_size;
    uint16_t v_size;
    uint16_t hsync_back_porch;
    uint16_t hsync_pulse_width;
    uint16_t hsync_front_porch;
    uint16_t vsync_back_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_front_porch;
    uint16_t dpi_clk_mhz;
} timing_config_t;

static const timing_config_t timing_tests[] = {
    // Original working configuration (shows green dots)
    {
        .name = "Original (Green Dots)",
        .h_size = 800, .v_size = 1280,
        .hsync_back_porch = 100, .hsync_pulse_width = 4, .hsync_front_porch = 100,
        .vsync_back_porch = 16, .vsync_pulse_width = 4, .vsync_front_porch = 16,
        .dpi_clk_mhz = 80
    },
    // Waveshare recommended timings
    {
        .name = "Waveshare Recommended",
        .h_size = 800, .v_size = 1280,
        .hsync_back_porch = 20, .hsync_pulse_width = 20, .hsync_front_porch = 40,
        .vsync_back_porch = 10, .vsync_pulse_width = 4, .vsync_front_porch = 30,
        .dpi_clk_mhz = 80
    },
    // Tighter timing attempt 1
    {
        .name = "Tighter Timing v1",
        .h_size = 800, .v_size = 1280,
        .hsync_back_porch = 10, .hsync_pulse_width = 10, .hsync_front_porch = 20,
        .vsync_back_porch = 5, .vsync_pulse_width = 2, .vsync_front_porch = 15,
        .dpi_clk_mhz = 80
    },
    // Lower clock frequency
    {
        .name = "Lower Clock (60MHz)",
        .h_size = 800, .v_size = 1280,
        .hsync_back_porch = 20, .hsync_pulse_width = 20, .hsync_front_porch = 40,
        .vsync_back_porch = 10, .vsync_pulse_width = 4, .vsync_front_porch = 30,
        .dpi_clk_mhz = 60
    },
    // Higher clock frequency
    {
        .name = "Higher Clock (100MHz)",
        .h_size = 800, .v_size = 1280,
        .hsync_back_porch = 20, .hsync_pulse_width = 20, .hsync_front_porch = 40,
        .vsync_back_porch = 10, .vsync_pulse_width = 4, .vsync_front_porch = 30,
        .dpi_clk_mhz = 100
    },
    // Try landscape mode
    {
        .name = "Landscape Mode",
        .h_size = 1280, .v_size = 800,
        .hsync_back_porch = 20, .hsync_pulse_width = 20, .hsync_front_porch = 40,
        .vsync_back_porch = 10, .vsync_pulse_width = 4, .vsync_front_porch = 30,
        .dpi_clk_mhz = 80
    }
};

void test_timing_configuration(const timing_config_t* config) {
    ESP_LOGI(TAG, "Testing: %s (%dx%d, CLK:%dMHz)", 
             config->name, config->h_size, config->v_size, config->dpi_clk_mhz);
    
    // Initialize LDO for MIPI DSI PHY
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_ERROR_CHECK(esp_ldo_channel_enable(ldo_mipi_phy));

    // Create DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    // Create DBI IO
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    // Create DPI panel with test timing
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = config->dpi_clk_mhz,
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
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel));

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

    esp_err_t ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Create test pattern - solid color blocks
    size_t buffer_size = config->h_size * config->v_size * sizeof(uint16_t);
    uint16_t* buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        goto cleanup_panel;
    }

    // Fill with different colored quadrants for easy identification
    for (int y = 0; y < config->v_size; y++) {
        for (int x = 0; x < config->h_size; x++) {
            uint16_t color;
            if (x < config->h_size/2 && y < config->v_size/2) {
                color = 0xF800; // Red - top left
            } else if (x >= config->h_size/2 && y < config->v_size/2) {
                color = 0x07E0; // Green - top right
            } else if (x < config->h_size/2 && y >= config->v_size/2) {
                color = 0x001F; // Blue - bottom left
            } else {
                color = 0xFFE0; // Yellow - bottom right
            }
            buffer[y * config->h_size + x] = color;
        }
    }

    esp_cache_msync(buffer, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    
    ESP_LOGI(TAG, "Drawing test pattern...");
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, config->h_size, config->v_size, buffer));
    
    ESP_LOGI(TAG, "Test complete for %s. Displaying for 8 seconds...", config->name);
    vTaskDelay(pdMS_TO_TICKS(8000));

    free(buffer);
cleanup_panel:
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(mipi_dbi_io);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
cleanup:
    esp_ldo_channel_disable(ldo_mipi_phy);
    esp_ldo_release_channel(ldo_mipi_phy);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting display timing configuration tests");
    
    const int num_tests = sizeof(timing_tests) / sizeof(timing_tests[0]);
    
    for (int i = 0; i < num_tests; i++) {
        ESP_LOGI(TAG, "=== Test %d/%d ===", i+1, num_tests);
        test_timing_configuration(&timing_tests[i]);
        
        // Brief pause between tests
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGI(TAG, "All timing tests completed. System will restart in 10 seconds.");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}