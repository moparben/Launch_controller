// Minimal UI manager inspired by Brookesia patterns (status bar, app grid, app switch)
#include "ui_manager.h"
#include "ui_styles.h"
#include <stdlib.h>

static lv_obj_t *screen_home = NULL;
static lv_obj_t *screen_app = NULL;
static lv_display_t *g_disp = NULL;

static void btn_home_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *name = (const char *)lv_obj_get_user_data(btn);
    ui_manager_launch_app(name);
}

void ui_manager_create_home(lv_display_t *display)
{
    g_disp = display;
    if (!display) return;

    // Create a screen and a status bar
    screen_home = lv_obj_create(NULL);
    lv_obj_clear_flag(screen_home, LV_OBJ_FLAG_SCROLLABLE);

    // top status label
    lv_obj_t *status = lv_label_create(screen_home);
    lv_label_set_text(status, "Brookesia-like UI - demo");
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 10);

    // Grid of app buttons (3 columns x ... rows)
    const char *apps[] = {"Settings", "Music", "Camera", "2048", "Calc", "Video"};
    int app_count = sizeof(apps) / sizeof(apps[0]);
    int col = 3;
    int row = (app_count + col - 1) / col;
    int w = 160;
    int h = 80;
    int spacing_x = 20;
    int spacing_y = 20;

    int start_x = (lv_obj_get_width(lv_display_get_screen_active(display)) - (col * w + (col - 1) * spacing_x)) / 2;
    int start_y = 120;

    for (int i = 0; i < app_count; i++) {
        int r = i / col;
        int c = i % col;
        lv_obj_t *btn = lv_btn_create(screen_home);
        lv_obj_set_user_data(btn, (void *)apps[i]);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_pos(btn, start_x + c * (w + spacing_x), start_y + r * (h + spacing_y));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, apps[i]);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, btn_home_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

void ui_manager_init(lv_display_t *display)
{
    ui_styles_init(display);
    ui_manager_create_home(display);
    lv_screen_load(screen_home);
}

void ui_manager_show_home(void)
{
    if (screen_home) lv_screen_load(screen_home);
}

static void app_back_cb(lv_event_t *e)
{
    ui_manager_show_home();
}

void ui_manager_launch_app(const char *app_name)
{
    if (!g_disp) return;
    // Create a simple app screen that shows the app name and a back button
    screen_app = lv_obj_create(NULL);
    lv_obj_t *label = lv_label_create(screen_app);
    lv_label_set_text_fmt(label, "App: %s", app_name);
    lv_obj_center(label);

    lv_obj_t *back = lv_btn_create(screen_app);
    lv_obj_t *lbl = lv_label_create(back);
    lv_label_set_text(lbl, "Back");
    lv_obj_center(lbl);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(back, app_back_cb, LV_EVENT_CLICKED, NULL);

    lv_screen_load(screen_app);
}
