/*
 * Simple developer console command to reload the splash image from storage
 * This is intended to be used in development builds where esp_console is
 * configured. The command calls into the already-provided reload_splash_from_storage
 * routine and prints the result to the console via standard logging.
 */

#include "splash.h"
#include "esp_log.h"
#include "esp_console.h"
#include <stdio.h>

static const char *TAG = "cmds_splash";

static int cmd_splash_reload(int argc, char **argv)
{
    (void) argc; (void) argv;
    bool ok = reload_splash_from_storage();
    if (ok) {
        ESP_LOGI(TAG, "splash_reload: success (runtime image reloaded)");
        printf("splash_reload: OK\n");
        return 0;
    }
    ESP_LOGW(TAG, "splash_reload: failed (no image or load error)");
    printf("splash_reload: FAILED\n");
    return -1;
}

void register_splash_console_cmd(void)
{
    const esp_console_cmd_t cmd = {
        .command = "splash_reload",
        .help = "Reload the runtime splash image from SPIFFS (if present)",
        .hint = NULL,
        .func = &cmd_splash_reload,
    };
#if CONFIG_ESP_CONSOLE_UART || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
#else
    (void)cmd;
#endif
}
