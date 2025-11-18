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
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_jd9365_10_1.h"

static const char *TAG = "TIMING_TEST";

#define MIPI_DSI_DPI_CLK_MHZ           80
#define MIPI_DSI_LANE_NUM             2
#define MIPI_DSI_LANE_BITRATE_MBPS    800

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXAMPLE_MIPI_DSI_IMAGE_HSYNC           40
#define EXAMPLE_MIPI_DSI_IMAGE_HBP             140
#define EXAMPLE_MIPI_DSI_IMAGE_HFP             40
#define EXAMPLE_MIPI_DSI_IMAGE_VSYNC           4
#define EXAMPLE_MIPI_DSI_IMAGE_VBP             16
#define EXAMPLE_MIPI_DSI_IMAGE_VFP             16

#define EXAMPLE_MIPI_DSI_IMAGE_HSIZE           800
#define EXAMPLE_MIPI_DSI_IMAGE_VSIZE           1280

// Timing configurations to test
typedef struct {
    const char* name;
    int h_size;
    int v_size;
    int hsync;
    int hbp; 
    int hfp;
    int vsync;
    int vbp;
    int vfp;
    int dpi_clk_mhz;
} timing_config_t;

static const timing_config_t timing_configs[] = {
    // Original configuration (produces green dots)
    {
        .name = "Original (Green Dots)",
        .h_size = 800, .v_size = 1280,
        .hsync = 40, .hbp = 140, .hfp = 40,
        .vsync = 4, .vbp = 16, .vfp = 16,
        .dpi_clk_mhz = 80
    },
    // Waveshare recommended timing
    {
        .name = "Waveshare Recommended",
        .h_size = 800, .v_size = 1280,
        .hsync = 20, .hbp = 20, .hfp = 40,
        .vsync = 10, .vbp = 4, .vfp = 30,
        .dpi_clk_mhz = 80
    },
    // Tighter timing
    {
        .name = "Tighter Timing",
        .h_size = 800, .v_size = 1280,
        .hsync = 10, .hbp = 10, .hfp = 20,
        .vsync = 5, .vbp = 2, .vfp = 15,
        .dpi_clk_mhz = 80
    },
    // Lower clock frequency
    {
        .name = "Lower Clock 60MHz",
        .h_size = 800, .v_size = 1280,
        .hsync = 40, .hbp = 140, .hfp = 40,
        .vsync = 4, .vbp = 16, .vfp = 16,
        .dpi_clk_mhz = 60
    },
    // Higher clock frequency
    {
        .name = "Higher Clock 100MHz",
        .h_size = 800, .v_size = 1280,
        .hsync = 40, .hbp = 140, .hfp = 40,
        .vsync = 4, .vbp = 16, .vfp = 16,
        .dpi_clk_mhz = 100
    },
    // Landscape mode attempt
    {
        .name = "Landscape Mode",
        .h_size = 1280, .v_size = 800,
        .hsync = 40, .hbp = 140, .hfp = 40,
        .vsync = 4, .vbp = 16, .vfp = 16,
        .dpi_clk_mhz = 80
    }
};

static const int num_configs = sizeof(timing_configs) / sizeof(timing_configs[0]);

static esp_err_t test_timing_configuration(const timing_config_t *config) {
    ESP_LOGI(TAG, "Testing configuration: %s (%dx%d, clk=%dMHz)", 
             config->name, config->h_size, config->v_size, config->dpi_clk_mhz);

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;

    // MIPI DSI bus configuration
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    esp_lcd_dsi_panel_config_t dsi_config = {
        .virtual_channel = 0,
        .dpi_clk_freq_mhz = config->dpi_clk_mhz,
        .dpi_config = {
            .video_timing = {
                .h_size = config->h_size,
                .v_size = config->v_size,
                .hsync_back_porch = config->hbp,
                .hsync_pulse_width = config->hsync,
                .hsync_front_porch = config->hfp,
                .vsync_back_porch = config->vbp,
                .vsync_pulse_width = config->vsync,
                .vsync_front_porch = config->vfp,
            },
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365_10_1(mipi_dbi_io, &panel_config, &dsi_config, &panel_handle));

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
    
    // Draw the test pattern
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, config->h_size, config->v_size, buffer));
    
    // Wait 8 seconds to observe the pattern
    vTaskDelay(pdMS_TO_TICKS(8000));
    
    free(buffer);

cleanup_panel:
    esp_lcd_panel_del(panel_handle);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting MIPI DSI timing configuration test");
    
    // Test each configuration
    for (int i = 0; i < num_configs; i++) {
        ESP_LOGI(TAG, "\n=== Test %d/%d ===", i+1, num_configs);
        test_timing_configuration(&timing_configs[i]);
        
        // Brief pause between tests
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    ESP_LOGI(TAG, "All timing tests completed. Restarting cycle...");
    
    // Restart the cycle
    esp_restart();
}