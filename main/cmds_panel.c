/* Console commands to control display behavior at runtime */
#include "cmds_panel.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include <stdio.h>

static const char *TAG = "cmds_panel";
static bool _is_swapped = false;

static int cmd_display_toggle_rotation(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern esp_lcd_panel_handle_t panel_handle; // provided by display_helpers_idf.c
    if (!panel_handle) {
        ESP_LOGW(TAG, "toggle_rotation: panel_handle NULL");
        printf("toggle_rotation: panel not initialized\n");
        return -1;
    }
    esp_err_t rc = esp_lcd_panel_swap_xy(panel_handle, !_is_swapped);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_panel_swap_xy failed: %s", esp_err_to_name(rc));
        printf("toggle_rotation: failed (%s)\n", esp_err_to_name(rc));
        return -1;
    }
    _is_swapped = !_is_swapped;
    ESP_LOGI(TAG, "display rotation toggled: swap=%d", _is_swapped);
    printf("toggle_rotation: swap=%d\n", _is_swapped);
    return 0;
}

static int cmd_display_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern esp_lcd_panel_handle_t panel_handle;
    if (!panel_handle) {
        printf("display_status: panel_handle=NULL\n");
        return -1;
    }
    printf("display_status: panel=%p, swap=%d\n", (void*)panel_handle, _is_swapped);
    return 0;
}

static int cmd_display_mem(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    printf("heap internal free=%u, largest_block=%u\n", (unsigned)free_internal, (unsigned)largest_internal);
    printf("heap spiram free=%u\n", (unsigned)free_spiram);
#ifdef LV_MEM_MONITOR
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("lv_mem: free=%u, used=%u, largest_block=%u\n", (unsigned)mon.free_size, (unsigned)mon.used_size, (unsigned)mon.largest_free_block);
#endif
    return 0;
}

void register_panel_console_cmd(void)
{
    const esp_console_cmd_t cmd_toggle = {
        .command = "display_toggle_rot",
        .help = "Toggle display rotation (swap XY)",
        .hint = NULL,
        .func = &cmd_display_toggle_rotation,
    };
    const esp_console_cmd_t cmd_status = {
        .command = "display_status",
        .help = "Show display status and swap state",
        .hint = NULL,
        .func = &cmd_display_status,
    };
    const esp_console_cmd_t cmd_mem = {
        .command = "display_mem",
        .help = "Show heap free sizes and lvgl mem usage",
        .hint = NULL,
        .func = &cmd_display_mem,
    };
#if CONFIG_ESP_CONSOLE_UART || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_toggle));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_status));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_mem));
#else
    (void)cmd_toggle; (void)cmd_status;
#endif
}
