/*
 * IDF-specific helper definitions for display controller
 * Provides placeholder implementations for panel_handle, touch_handle,
 * cut-down display init and touch functions. This file intentionally does
 * NOT use 'display_mcu*' prefix to avoid CMake's duplicate checks.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
// For touch initialization via I2C "panel IO" helper, use the managed i2c_bus component
#include "i2c_bus.h"
#include "esp_idf_version.h"

#include "display_tag.h"

static const char *TAG = DISPLAY_TAG;

// Define the globals expected by the display code
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
SemaphoreHandle_t draw_finish_sem = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;
// Track whether hardware rotation via swap_xy was successfully applied
bool panel_hw_swap_xy = false;

#ifndef DISPLAY_NATIVE_WIDTH
#define DISPLAY_NATIVE_WIDTH 800
#endif
#ifndef DISPLAY_NATIVE_HEIGHT
#define DISPLAY_NATIVE_HEIGHT 1280
#endif

// Placeholder functions used by display_mcu_v3_04.c to avoid link errors during build
esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing JD9365 MIPI-DSI panel (display_helpers_idf::init_display)");
    esp_err_t err = ESP_OK;

    // Create draw semaphore
    if (!draw_finish_sem) {
        draw_finish_sem = xSemaphoreCreateBinary();
    }

    // Power MIPI DSI PHY using LDO (some boards require this)
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    err = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_ldo_acquire_channel failed: %s - continuing; board may provide LDO by hardware", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "MIPI DSI PHY LDO configured (2.5V)");
    }

    // Create DSI bus using Waveshare JD9365 macro-configs where available
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    err = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DSI bus created: %p", (void*)mipi_dsi_bus);

    // Create DBI IO handle bound to the DSI bus. Required for JD9365 10.1" panel driver
    esp_lcd_panel_io_handle_t mipi_dbi = NULL;
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    err = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_dbi failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DBI IO created: %p (mipi_dbi_io=%p)", (void*)mipi_dbi, (void*)mipi_dbi_io);
    mipi_dbi_io = mipi_dbi;

    // Configure panel device; vendor_config supports MIPI interface settings
    // Use the JD9365 DPI config macro (800 x 1280, 60 Hz) and set the dsi bus
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    // Indicate the input buffer format to the DPI driver (full color type including colorspace)
    dpi_config.in_color_format = LCD_COLOR_FMT_RGB565;
    // Be explicit: request a single frame-buffer to minimize PSRAM demand
    dpi_config.num_fbs = 1;
    // Disable DMA2D acceleration if we are tight on memory or do not need it.
    // This reduces additional internal allocations (fbcpy_handle/draw_sem).
    dpi_config.flags.use_dma2d = false;
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
        .flags = { .use_mipi_interface = 1 },
        .backlight_gpio_num = -1,
        .backlight_active_high = 1,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config
    };
    // Create panel handle
    // Use generic name if the specific 10_1 variant is not available in the headers
    // Log PSRAM/internal heap before attempting large allocations so developers can see pressures
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t largest_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // compute expected frame buffer size (w x h x bpp/8)
    uint32_t expected_fb_size = dpi_config.video_timing.h_size * dpi_config.video_timing.v_size * 2; // RGB565 => 2 bytes per pixel
    ESP_LOGI(TAG, "Framebuffer size estimate: %u bytes (num fb: %u), free SPIRAM: %u (largest: %u), free INT: %u (largest: %u)",
            (unsigned)expected_fb_size, (unsigned)dpi_config.num_fbs,
            (unsigned)free_spiram, (unsigned)largest_spiram,
            (unsigned)free_internal, (unsigned)largest_internal);

    // Try a test allocation in SPIRAM to detect if there is sufficient contiguous block (avoid relying on heap_caps_get_free_size)
    void *test_fb = NULL;
    uint32_t alignment = 64; // typical cache alignment
    test_fb = heap_caps_aligned_calloc(alignment, 1, expected_fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_fb) {
        ESP_LOGI(TAG, "Pre-allocation succeeded in SPIRAM: %p; freeing now and continuing", test_fb);
        heap_caps_free(test_fb);
    } else {
        ESP_LOGW(TAG, "Pre-allocation failed in SPIRAM: expected %u bytes; trying internal memory fallback.", (unsigned)expected_fb_size);
        // Try an internal allocation with DMA-capable memory (if possible)
        test_fb = heap_caps_aligned_calloc(alignment, 1, expected_fb_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (test_fb) {
            ESP_LOGW(TAG, "Pre-allocation succeeded in INTERNAL+DMA: %p; freeing now and continuing. This will increase memory pressure!", test_fb);
            heap_caps_free(test_fb);
        } else {
            ESP_LOGE(TAG, "Pre-allocation failed in both SPIRAM and INTERNAL for expected framebuffer size: %u bytes. The DPI panel is likely to fail with no memory.", (unsigned)expected_fb_size);
        }
    }
    err = esp_lcd_new_panel_jd9365(mipi_dbi, &panel_cfg, &panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_jd9365_10_1 failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "JD9365 panel created: %p (reset=%d rgb_order=%d, bpp=%d)", (void*)panel_handle, (int)panel_cfg.reset_gpio_num, (int)panel_cfg.rgb_ele_order, (int)panel_cfg.bits_per_pixel);

    ESP_LOGI(TAG, "JD9365 panel created successfully: %p", (void*)panel_handle);

    // Initialize the panel and show it
    {
        esp_err_t rc;
        rc = esp_lcd_panel_reset(panel_handle);
        ESP_LOGI(TAG, "esp_lcd_panel_reset rc=%s", esp_err_to_name(rc));
        if (rc != ESP_OK) return rc;
        rc = esp_lcd_panel_init(panel_handle);
        ESP_LOGI(TAG, "esp_lcd_panel_init rc=%s", esp_err_to_name(rc));
        if (rc != ESP_OK) return rc;
        rc = esp_lcd_panel_disp_on_off(panel_handle, true);
        ESP_LOGI(TAG, "esp_lcd_panel_disp_on_off rc=%s", esp_err_to_name(rc));
        if (rc != ESP_OK) return rc;
    }

    // Create a small draw semaphore if not already created
    if (!draw_finish_sem) {
        draw_finish_sem = xSemaphoreCreateBinary();
    }

    ESP_LOGI(TAG, "init_display() completed (panel_handle=%p, mipi_dbi_io=%p)", (void*)panel_handle, (void*)mipi_dbi_io);
    /* Simple full-screen write test (white) that draws a few horizontal
     * stripes across the panel. The function uses a small stripe buffer
     * (width x stripe_h) to avoid large contiguous allocations. This is a
     * short-lived diagnostic to confirm that panel data transfers reach
     * the screen early in init. */
#if 1
    {
        const int fill_w = dpi_config.video_timing.h_size;
        const int fill_h = 8; // small stripe height
        const uint16_t white = 0xFFFF; // RGB565 white
        size_t stripe_bytes = (size_t)fill_w * fill_h * sizeof(uint16_t);
        uint16_t *stripe_buf = heap_caps_malloc(stripe_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!stripe_buf) stripe_buf = heap_caps_malloc(stripe_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (stripe_buf) {
            for (int i = 0; i < fill_w * fill_h; ++i) stripe_buf[i] = white;
            /* Draw stripes until the display is filled */
            ESP_LOGI(TAG, "init_display: performing full-screen diagnostic fill (stripe_h=%d)", fill_h);
            for (int y = 0; y < dpi_config.video_timing.v_size; y += fill_h) {
                int yend = y + fill_h;
                if (yend > dpi_config.video_timing.v_size) yend = dpi_config.video_timing.v_size;
                esp_err_t r = esp_lcd_panel_draw_bitmap(panel_handle, 0, y, fill_w, yend, stripe_buf);
                if (r != ESP_OK) {
                    ESP_LOGW(TAG, "init_display: diagnostic draw bitmap failed at y=%d rc=%s", y, esp_err_to_name(r));
                    break;
                }
            }
            heap_caps_free(stripe_buf);
            ESP_LOGI(TAG, "init_display: diagnostic fill complete");
        } else {
            ESP_LOGW(TAG, "init_display: unable to allocate small stripe for full-screen diagnostic");
        }
    }
#endif
    return ESP_OK;
}

esp_err_t init_touch(void)
{
    ESP_LOGI(TAG, "init_touch: attempting GT911 initialization");
    esp_err_t err = ESP_OK;
    // If touch already initialized, return ok
    if (touch_handle) {
        ESP_LOGI(TAG, "init_touch: touch_handle already initialized: %p", (void*)touch_handle);
        return ESP_OK;
    }
    // Try to create a managed I2C bus and register a GT911 device via panel IO
    // - Default I2C pins: SDA=21, SCL=22 (fall back if board defines others)
    i2c_config_t i2c_bus_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
        .master = { .clk_speed = 400000 },
    };
    i2c_bus_handle_t i2c_bus = NULL;
    i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_bus_cfg);
    if (i2c_bus == NULL) {
        ESP_LOGW(TAG, "init_touch: i2c_bus_create failed: leaving touch disabled");
        return ESP_OK;
    }
    // Create a generic panel I2C IO handle for the touch driver
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    // Convert the managed i2c_bus_handle_t to the internal idf handle needed by the esp_lcd API
    i2c_master_bus_handle_t idf_bus_handle = NULL;
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    idf_bus_handle = i2c_bus_get_internal_bus_handle(i2c_bus);
    #endif
    if (idf_bus_handle == NULL) {
        // Fallback: use numeric port id if available via the bus handle
        // Many older environments accept a plain integer 'port' for v1 API
        uint32_t port = (uint32_t)I2C_NUM_0; // default to I2C_NUM_0 if unknown
        err = esp_lcd_new_panel_io_i2c_v1(port, &io_cfg, &io_handle);
    } else {
        err = esp_lcd_new_panel_io_i2c_v2(idf_bus_handle, &io_cfg, &io_handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "init_touch: esp_lcd_new_panel_io_i2c_v2/v1 failed: %s; leaving touch disabled", esp_err_to_name(err));
        if (i2c_bus) i2c_bus_delete(&i2c_bus);
        return ESP_OK;
    }
    // Configure GT911 touch configuration - use native panel resolution
    esp_lcd_touch_io_gt911_config_t tp_gt911_cfg = { .dev_addr = io_cfg.dev_addr };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_NATIVE_WIDTH,
        .y_max = DISPLAY_NATIVE_HEIGHT,
        .rst_gpio_num = (gpio_num_t)-1,
        .int_gpio_num = (gpio_num_t)-1,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .driver_data = &tp_gt911_cfg,
    };
    err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &touch_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "init_touch: esp_lcd_touch_new_i2c_gt911 failed: %s; leaving touch disabled", esp_err_to_name(err));
        // Clean up IO and bus handles before returning; keep touch disabled
        if (io_handle) esp_lcd_panel_io_del(io_handle);
        if (i2c_bus) i2c_bus_delete(&i2c_bus);
        return ESP_OK;
    }
    // If panel hardware rotation (swap_xy) was used, let the touch driver know
    extern bool panel_hw_swap_xy;
    if (panel_hw_swap_xy) {
        // When panel rotates, swap XY on touch as well to match coordinate space
        ESP_LOGI(TAG, "init_touch: setting touch swap_xy to true to match hardware rotation");
        esp_lcd_touch_set_swap_xy(touch_handle, true);
    }
    // Read raw resolution & product ID
    uint16_t raw_x = 0, raw_y = 0;
    if (esp_lcd_touch_gt911_get_raw_resolution(touch_handle, &raw_x, &raw_y) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 raw resolution: %u x %u", raw_x, raw_y);
    }
    char idbuf[64] = {0};
    if (esp_lcd_touch_gt911_get_product_id(touch_handle, idbuf, sizeof(idbuf)) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 product ID: %s", idbuf);
    }
    ESP_LOGI(TAG, "init_touch: GT911 initialized (handle=%p)", (void*)touch_handle);
    return ESP_OK;
}

void draw_test_pattern(void)
{
    if (!panel_handle) {
        ESP_LOGW(TAG, "draw_test_pattern: panel_handle is NULL, skipping test pattern");
        return;
    }
    // Fill a small buffer to draw a color bar to the panel full-size
    #ifndef DISPLAY_NATIVE_WIDTH
    #define DISPLAY_NATIVE_WIDTH 800
    #endif
    #ifndef DISPLAY_NATIVE_HEIGHT
    #define DISPLAY_NATIVE_HEIGHT 1280
    #endif
    const int width = DISPLAY_NATIVE_WIDTH;
    size_t try_heights[] = {40, 32, 24, 16, 8};
    size_t buf_size = 0;
    void *buf = NULL;
    int used_height = 0;
    for (unsigned i = 0; i < sizeof(try_heights)/sizeof(try_heights[0]); ++i) {
        size_t h = try_heights[i];
        size_t s = width * h * sizeof(uint16_t);
        /* Try SPIRAM first, then internal DMA-capable, then small internal */
        buf = heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = heap_caps_malloc(s, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!buf) buf = heap_caps_malloc(s, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (buf) {
            buf_size = s;
            used_height = h;
            ESP_LOGI(TAG, "draw_test_pattern: allocated buffer size %u (h=%u) using %s", (unsigned)buf_size, (unsigned)h, 
                ((heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && (buf >= (void*)0x3FFF8000)) ? "SPIRAM" : "INTERNAL"));
            break;
        }
    }
    if (!buf) {
        ESP_LOGW(TAG, "draw_test_pattern: unable to allocate buffer for test pattern (width=%d) - trying very small fallback", width);
        // Try a very small 8x8 pattern to ensure the panel responds to draw
        used_height = 8;
        buf_size = width * used_height * sizeof(uint16_t);
        buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (buf) {
            ESP_LOGI(TAG, "draw_test_pattern: small fallback succeeded size=%u", (unsigned)buf_size);
        }
    }
    if (!buf) {
        ESP_LOGW(TAG, "draw_test_pattern: unable to allocate buffer for test pattern");
        return;
    }

    // Draw several color bars at the top of the screen
    uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F }; // Red, Green, Blue, Yellow, Purple
    int num_colors = sizeof(colors) / sizeof(colors[0]);
    int stripe_h = used_height ? used_height : 40;
    for (int c = 0; c < num_colors; ++c) {
        for (int i = 0; i < (width * stripe_h); ++i) ((uint16_t*)buf)[i] = colors[c];
        // NOTE: esp_lcd_panel_draw_bitmap expects x_end/y_end to be exclusive
        // coordinates (end index + 1). Use exclusive coordinates here to avoid
        // subtle mismatches with the driver and other consumers.
        esp_err_t rc = esp_lcd_panel_draw_bitmap(panel_handle, 0, c * stripe_h, width, (c + 1) * stripe_h, buf);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "draw_test_pattern: esp_lcd_panel_draw_bitmap failed for stripe %d rc=%s", c, esp_err_to_name(rc));
        }
    }
    // Optional rotation test: draw a small square at the centre that should
    // appear at the center of the screen for both rotated/unrotated modes
#if ENABLE_JD9365_ROTATION_TEST
    int sq_w = 40;
    int sq_h = 40;
    int cx = width / 2;
    int cy = 40 * num_colors + 64; // below the color bar
    uint16_t *sq = heap_caps_malloc(sq_w * sq_h * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (sq) {
        for (int i=0; i < sq_w * sq_h; ++i) sq[i] = 0x07E0; // green
        int x1 = cx - (sq_w/2);
        int y1 = cy - (sq_h/2);
        esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x1 + sq_w, y1 + sq_h, sq);
        heap_caps_free(sq);
    }
#endif
    // free buffer
    if (buf) {
        heap_caps_free(buf);
    }
}

void touch_task(void *pvParameters)
{
    (void) pvParameters;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
