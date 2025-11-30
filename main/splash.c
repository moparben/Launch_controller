#include "splash.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include <sys/lock.h>

#ifndef BACKLIGHT_GPIO
#define BACKLIGHT_GPIO 15
#endif

#define BACKLIGHT_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_TIMER     LEDC_TIMER_0

static const char *TAG = "splash";

// Fallback 4x4 image
static const uint8_t img_rocket_4x4_map[4*4*2] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,
    0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

__attribute__((weak)) const lv_img_dsc_t img_rocket_png = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = 4,
    .header.h = 4,
    .header.stride = 4*2,
    .data_size = sizeof(img_rocket_4x4_map),
    .data = img_rocket_4x4_map,
};

// Only one runtime image at a time
static lv_img_dsc_t *splash_runtime_dsc = NULL;
static uint8_t *splash_runtime_map = NULL;

// RKT1 binary format magic
static const char SPLASH_BIN_MAGIC[4] = {'R','K','T','1'};

// External LVGL lock functions (provided by display main)
extern void lvgl_lock(void);
extern void lvgl_unlock(void);

static lv_img_dsc_t *load_splash_from_spiffs(void)
{
    const char *path = "/spiffs/splash.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        f = fopen("/splash.bin", "rb");
        if (!f) return NULL;
    }
    char magic[4];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return NULL; }
    if (memcmp(magic, SPLASH_BIN_MAGIC, 4) != 0) { fclose(f); return NULL; }
    uint16_t w=0, h=0;
    if (fread(&w, sizeof(uint16_t), 1, f) != 1) { fclose(f); return NULL; }
    if (fread(&h, sizeof(uint16_t), 1, f) != 1) { fclose(f); return NULL; }
    size_t data_size = (size_t)w * (size_t)h * 2;
    uint8_t *map = heap_caps_malloc(data_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!map) { fclose(f); return NULL; }
    if (fread(map, 1, data_size, f) != data_size) { heap_caps_free(map); fclose(f); return NULL; }
    fclose(f);

    lv_img_dsc_t *dsc = heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!dsc) { heap_caps_free(map); return NULL; }
    memset(dsc, 0, sizeof(lv_img_dsc_t));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.flags = 0;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = w * 2;
    dsc->data_size = data_size;
    dsc->data = map;

    splash_runtime_dsc = dsc;
    splash_runtime_map = map;
    ESP_LOGI(TAG, "Loaded splash from SPIFFS: %s (%dx%d)", path, w, h);
    return dsc;
}

bool reload_splash_from_storage(void)
{
    if (splash_runtime_dsc) {
        heap_caps_free((void *)splash_runtime_dsc->data);
        heap_caps_free(splash_runtime_dsc);
        splash_runtime_dsc = NULL;
        splash_runtime_map = NULL;
    }
    lv_img_dsc_t *d = load_splash_from_spiffs();
    return d != NULL;
}

void backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));
    ledc_channel_config_t ch_conf = {
        .gpio_num = BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));
}

void set_backlight(uint8_t level)
{
    uint32_t duty = (level * 1023) / 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}

void backlight_fade(uint8_t from, uint8_t to, int ms)
{
    int steps = 60;
    for (int i = 0; i <= steps; i++) {
        uint8_t level = from + (to - from) * i / steps;
        set_backlight(level);
        vTaskDelay(pdMS_TO_TICKS(ms / steps));
    }
}

void show_rocket_splash(lv_display_t *disp)
{
    if (!disp) return;
    ESP_LOGI(TAG, "Show rocket splash");
    lvgl_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_scr_load(scr);
    lv_obj_t *rocket = lv_img_create(scr);
    const lv_img_dsc_t *imgd = load_splash_from_spiffs();
    if (!imgd) imgd = &img_rocket_png;
    lv_img_set_src(rocket, imgd);
    lv_obj_center(rocket);
    int16_t start_y = lv_obj_get_y(rocket);
    int16_t steps = 60;
    int16_t distance = lv_display_get_vertical_resolution(disp) / 4;
    for (int i = 0; i <= steps; i++) {
        lv_obj_set_y(rocket, start_y - (distance * i / steps));
        lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(15));
        lvgl_lock();
    }
    lv_obj_fade_out(rocket, 800, 0);
    vTaskDelay(pdMS_TO_TICKS(900));
    lv_obj_del(scr);
    lvgl_unlock();
}
