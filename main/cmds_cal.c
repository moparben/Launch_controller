/*
 * Console commands to control touch calibration at runtime (dev only).
 * Allows disabling calibration overlay / cancelling calibration without
 * reflashing.
 */

#include "touch_calibration.h"
#include "esp_log.h"
#include "esp_console.h"
#include <stdio.h>

static const char *TAG = "cmds_cal";

static int cmd_cal_disable(int argc, char **argv)
{
    (void) argc; (void) argv;
    /* Cancel any running calibration and ensure the overlay is disabled */
    cal_cancel();
    cal_set_overlay(false);
    ESP_LOGI(TAG, "cal_disable: cal_cancel() & overlay disabled");
    printf("cal_disable: OK\n");
    return 0;
}

void register_cal_console_cmd(void)
{
    const esp_console_cmd_t cmd = {
        .command = "cal_disable",
        .help = "Cancel calibration and disable overlay",
        .hint = NULL,
        .func = &cmd_cal_disable,
    };
#if CONFIG_ESP_CONSOLE_UART || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
#else
    (void) cmd;
#endif
}
