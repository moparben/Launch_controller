#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>
#include <esp_cache.h>
#include <esp_system.h>
#include <lvgl.h>
#include <esp_lcd_jd9365_10_1.h>

static const char *TAG = "DISPLAY_TEST";

#define EXAMPLE_MIPI_DSI_DPI_CLK_MHZ  80
#define EXAMPLE_MIPI_DSI_LANE_NUM     2
#define EXAMPLE_MIPI_DSI_LANE_BITRATE_MBPS  800

// Test different resolutions
typedef struct {
    uint16_t width;
    uint16_t height;
    const char* name;
} display_config_t;

static const display_config_t test_configs[] = {
    {800, 1280, "Portrait 800x1280"},    // Original working
    {1280, 800, "Landscape 1280x800"},   // Target landscape
    {1024, 600, "Landscape 1024x600"},   // Alternative landscape
    {720, 1280, "Portrait 720x1280"},    // Reduced width
};

void test_display_config(const display_config_t* config) {
    ESP_LOGI(TAG, "Testing %s (%dx%d)", config->name, config->width, config->height);
    
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_ERROR_CHECK(esp_ldo_channel_enable(ldo_mipi_phy));

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = EXAMPLE_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = EXAMPLE_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = EXAMPLE_MIPI_DSI_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size = config->width,
            .v_size = config->height,
            .hsync_back_porch = 100,
            .hsync_pulse_width = 4,
            .hsync_front_porch = 100,
            .vsync_back_porch = 16,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 16,
        },
        .flags.use_dma2d = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &mipi_dpi_panel));

    esp_lcd_panel_handle_t panel_handle = NULL;
    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = EXAMPLE_MIPI_DSI_LANE_NUM,
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
        ESP_LOGE(TAG, "Failed to create panel for %s: %s", config->name, esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Create a simple test pattern
    size_t buffer_size = config->width * config->height * sizeof(uint16_t);
    uint16_t* buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for %s", config->name);
        return;
    }

    // Fill with test pattern - vertical color bars
    for (int y = 0; y < config->height; y++) {
        for (int x = 0; x < config->width; x++) {
            uint16_t color;
            int section = x / (config->width / 4);
            switch (section) {
                case 0: color = 0xF800; break; // Red
                case 1: color = 0x07E0; break; // Green  
                case 2: color = 0x001F; break; // Blue
                default: color = 0xFFFF; break; // White
            }
            buffer[y * config->width + x] = color;
        }
    }

    esp_cache_msync(buffer, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    
    ESP_LOGI(TAG, "Drawing test pattern for %s", config->name);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, config->width, config->height, buffer));

    ESP_LOGI(TAG, "Test complete for %s. Waiting 5 seconds...", config->name);
    vTaskDelay(pdMS_TO_TICKS(5000));

    free(buffer);
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(mipi_dbi_io);
    esp_lcd_del_dsi_bus(mipi_dsi_bus);
    esp_ldo_channel_disable(ldo_mipi_phy);
    esp_ldo_release_channel(ldo_mipi_phy);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting display configuration tests");
    
    // Test each configuration
    for (int i = 0; i < sizeof(test_configs) / sizeof(test_configs[0]); i++) {
        test_display_config(&test_configs[i]);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause between tests
    }

    ESP_LOGI(TAG, "All tests completed. System will restart in 10 seconds.");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}