#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_jd9365_10_1.h"

static const char *TAG = "MINIMAL_TEST";

#define MIPI_DSI_LANE_NUM         2
#define MIPI_DSI_LANE_BITRATE_MBPS 800
#define DPI_CLK_MHZ              80

void app_main(void) {
    ESP_LOGI(TAG, "Starting minimal display test");
    
    // Initialize LDO for MIPI DSI PHY power
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_LOGI(TAG, "Acquiring LDO channel for MIPI PHY");
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "Enabling LDO channel");
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
    ESP_LOGI(TAG, "DSI bus created successfully");

    // Create DBI IO
    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    ESP_LOGI(TAG, "DBI IO created successfully");

    // Create DPI panel
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size = 800,
            .v_size = 1280,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = 10,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 30,
        },
        .flags.use_dma2d = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel));
    ESP_LOGI(TAG, "DPI panel created successfully");

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
        ESP_LOGE(TAG, "Failed to create JD9365 panel: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "JD9365 panel created successfully");

    // Initialize the panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_LOGI(TAG, "Panel reset complete");
    
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "Panel initialized");
    
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "Display enabled");

    // Create test buffer
    size_t buffer_size = 800 * 1280 * sizeof(uint16_t);
    uint16_t* buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return;
    }

    // Fill with red color (should be easily visible)
    for (int i = 0; i < 800 * 1280; i++) {
        buffer[i] = 0xF800; // Pure red in RGB565
    }

    esp_cache_msync(buffer, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    
    ESP_LOGI(TAG, "Drawing red screen...");
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 800, 1280, buffer));
    
    ESP_LOGI(TAG, "Red screen drawn. Waiting 10 seconds...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    // Now try landscape configuration
    ESP_LOGI(TAG, "Testing landscape mode 1280x800");
    
    // Cleanup previous
    free(buffer);
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(mipi_dbi_io);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
    
    // Create new configuration for landscape
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    
    // Landscape DPI config
    dpi_config.video_timing.h_size = 1280;
    dpi_config.video_timing.v_size = 800;
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel));
    
    ret = esp_lcd_new_panel_jd9365(mipi_dbi_io, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create landscape panel: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    // Create landscape buffer
    buffer_size = 1280 * 800 * sizeof(uint16_t);
    buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate landscape buffer");
        return;
    }
    
    // Fill with green color
    for (int i = 0; i < 1280 * 800; i++) {
        buffer[i] = 0x07E0; // Pure green in RGB565
    }
    
    esp_cache_msync(buffer, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    
    ESP_LOGI(TAG, "Drawing green landscape screen...");
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 1280, 800, buffer));
    
    ESP_LOGI(TAG, "Landscape test complete. System will restart in 10 seconds.");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    // Cleanup
    free(buffer);
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(mipi_dbi_io);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
    
    // Disable and release LDO
    ESP_LOGI(TAG, "Disabling LDO channel");
    esp_ldo_channel_disable(ldo_mipi_phy);
    esp_ldo_release_channel(ldo_mipi_phy);
    
    esp_restart();
}