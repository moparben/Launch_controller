// Minimal theme + styles for our Brookesia-inspired UI
#include "ui_styles.h"
#include <string.h>
static const lv_font_t *font_normal = &lv_font_montserrat_16;

void ui_styles_init(lv_display_t *display)
{
    // Basic LVGL theme initialization with palette & font
    lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), LV_THEME_DEFAULT_DARK, font_normal);
}
