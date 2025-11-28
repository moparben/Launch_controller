// Minimal UI manager inspired by Brookesia patterns (status bar, app grid, app switch)
#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"

void ui_manager_init(lv_display_t *display);
void ui_manager_show_home(void);
void ui_manager_launch_app(const char *app_name);

#endif // UI_MANAGER_H
