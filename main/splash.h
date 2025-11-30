// splash.h - Runtime splash loader API
#pragma once
#include "lvgl.h"
#include <stdbool.h>

// LVGL image descriptor for fallback built image
extern const lv_img_dsc_t img_rocket_png;

// Initialize and control backlight/splash
void backlight_init(void);
void set_backlight(uint8_t level);
void backlight_fade(uint8_t from, uint8_t to, int ms);

// Show splash on the given LVGL display (uses runtime override if present)
void show_rocket_splash(lv_display_t *disp);

// Attempt to reload splash from SPIFFS. Returns true if a runtime image was loaded
bool reload_splash_from_storage(void);
