/*
 * Serial Configuration Toolkit for JD9365 Display Parameters
 * Implementation file
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "serial_config.h"

static const char *TAG = "serial_config";

// Global variables
display_params_t g_display_params;
esp_lcd_panel_handle_t g_panel_handle = NULL;
bool g_params_changed = false;

// UART configuration
#define UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define RD_BUF_SIZE 256

static void print_help(void) {
    printf("\n=== ESP32-P4 JD9365 Display Configuration Toolkit ===\n");
    printf("Commands:\n");
    printf("  help          - Show this help\n");
    printf("  show          - Show current parameters\n");
    printf("  save          - Save parameters to NVS\n");
    printf("  load          - Load parameters from NVS\n");
    printf("  reset         - Reset to default parameters\n");
    printf("  apply         - Apply current parameters (requires restart)\n");
    printf("\nParameter Commands:\n");
    printf("  hres <value>  - Set horizontal resolution (400-2048)\n");
    printf("  vres <value>  - Set vertical resolution (240-1440)\n");
    printf("  hsync <value> - Set hsync pulse width (1-100)\n");
    printf("  hbp <value>   - Set hsync back porch (1-200)\n");
    printf("  hfp <value>   - Set hsync front porch (1-200)\n");
    printf("  vsync <value> - Set vsync pulse width (1-50)\n");
    printf("  vbp <value>   - Set vsync back porch (1-100)\n");
    printf("  vfp <value>   - Set vsync front porch (1-100)\n");
    printf("  clk <value>   - Set DPI clock MHz (20-200)\n");
    printf("  bitrate <val> - Set lane bitrate Mbps (200-2000)\n");
    printf("  landscape <0|1> - Set landscape mode (0=portrait, 1=landscape)\n");
    printf("  rotation <0|1>  - Set LVGL rotation (0=off, 1=on)\n");
    printf("\nPresets:\n");
    printf("  preset_portrait  - Load known working portrait config\n");
    printf("  preset_landscape - Load landscape test config\n");
    printf("=========================================================\n\n");
}

void display_params_init(display_params_t *params) {
    // Initialize with known working JD9365 parameters (portrait)
    params->h_res = 800;
    params->v_res = 1280;
    params->hsync = 20;
    params->hbp = 20;
    params->hfp = 40;
    params->vsync = 4;
    params->vbp = 10;
    params->vfp = 14;
    params->dpi_clk_mhz = 80;
    params->lane_bitrate_mbps = 800;
    params->landscape_mode = false;
    params->rotation_enabled = false;
}

void display_params_print(display_params_t *params) {
    printf("\n=== Current Display Parameters ===\n");
    printf("Resolution: %dx%d %s\n", params->h_res, params->v_res, 
           params->landscape_mode ? "(Landscape)" : "(Portrait)");
    printf("H-Timing: sync=%d, bp=%d, fp=%d\n", params->hsync, params->hbp, params->hfp);
    printf("V-Timing: sync=%d, bp=%d, fp=%d\n", params->vsync, params->vbp, params->vfp);
    printf("Clock: DPI=%dMHz, Bitrate=%dMbps\n", params->dpi_clk_mhz, params->lane_bitrate_mbps);
    printf("LVGL Rotation: %s\n", params->rotation_enabled ? "Enabled" : "Disabled");
    printf("====================================\n\n");
}

void display_params_save_to_nvs(display_params_t *params) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("display_cfg", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle: %s\n", esp_err_to_name(err));
        return;
    }
    
    err = nvs_set_blob(nvs_handle, "params", params, sizeof(display_params_t));
    if (err != ESP_OK) {
        printf("Error saving parameters: %s\n", esp_err_to_name(err));
    } else {
        printf("Parameters saved to NVS successfully!\n");
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void display_params_load_from_nvs(display_params_t *params) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("display_cfg", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        printf("No saved parameters found, using defaults\n");
        display_params_init(params);
        return;
    }
    
    size_t required_size = sizeof(display_params_t);
    err = nvs_get_blob(nvs_handle, "params", params, &required_size);
    if (err != ESP_OK) {
        printf("Error loading parameters: %s, using defaults\n", esp_err_to_name(err));
        display_params_init(params);
    } else {
        printf("Parameters loaded from NVS successfully!\n");
    }
    
    nvs_close(nvs_handle);
}

static void load_preset_portrait(display_params_t *params) {
    params->h_res = 800;
    params->v_res = 1280;
    params->hsync = 20;
    params->hbp = 20;
    params->hfp = 40;
    params->vsync = 4;
    params->vbp = 10;
    params->vfp = 14;
    params->dpi_clk_mhz = 80;
    params->lane_bitrate_mbps = 800;
    params->landscape_mode = false;
    params->rotation_enabled = false;
    printf("Loaded portrait preset (known working)\n");
}

static void load_preset_landscape(display_params_t *params) {
    params->h_res = 1280;
    params->v_res = 800;
    params->hsync = 4;
    params->hbp = 10;
    params->hfp = 14;
    params->vsync = 20;
    params->vbp = 20;
    params->vfp = 40;
    params->dpi_clk_mhz = 80;
    params->lane_bitrate_mbps = 800;
    params->landscape_mode = true;
    params->rotation_enabled = false;
    printf("Loaded landscape preset (experimental)\n");
}

static void process_command(char *line) {
    char *cmd = strtok(line, " \t\n\r");
    if (!cmd) return;
    
    if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strcmp(cmd, "show") == 0) {
        display_params_print(&g_display_params);
    } else if (strcmp(cmd, "save") == 0) {
        display_params_save_to_nvs(&g_display_params);
    } else if (strcmp(cmd, "load") == 0) {
        display_params_load_from_nvs(&g_display_params);
        display_params_print(&g_display_params);
    } else if (strcmp(cmd, "reset") == 0) {
        display_params_init(&g_display_params);
        printf("Parameters reset to defaults\n");
        display_params_print(&g_display_params);
    } else if (strcmp(cmd, "apply") == 0) {
        g_params_changed = true;
        printf("Parameters marked for application. Please restart the device.\n");
    } else if (strcmp(cmd, "preset_portrait") == 0) {
        load_preset_portrait(&g_display_params);
        display_params_print(&g_display_params);
    } else if (strcmp(cmd, "preset_landscape") == 0) {
        load_preset_landscape(&g_display_params);
        display_params_print(&g_display_params);
    } else {
        // Parameter setting commands
        char *value_str = strtok(NULL, " \t\n\r");
        if (!value_str) {
            printf("Error: Missing value for parameter %s\n", cmd);
            return;
        }
        
        int value = atoi(value_str);
        bool updated = true;
        
        if (strcmp(cmd, "hres") == 0 && value >= 400 && value <= 2048) {
            g_display_params.h_res = value;
        } else if (strcmp(cmd, "vres") == 0 && value >= 240 && value <= 1440) {
            g_display_params.v_res = value;
        } else if (strcmp(cmd, "hsync") == 0 && value >= 1 && value <= 100) {
            g_display_params.hsync = value;
        } else if (strcmp(cmd, "hbp") == 0 && value >= 1 && value <= 200) {
            g_display_params.hbp = value;
        } else if (strcmp(cmd, "hfp") == 0 && value >= 1 && value <= 200) {
            g_display_params.hfp = value;
        } else if (strcmp(cmd, "vsync") == 0 && value >= 1 && value <= 50) {
            g_display_params.vsync = value;
        } else if (strcmp(cmd, "vbp") == 0 && value >= 1 && value <= 100) {
            g_display_params.vbp = value;
        } else if (strcmp(cmd, "vfp") == 0 && value >= 1 && value <= 100) {
            g_display_params.vfp = value;
        } else if (strcmp(cmd, "clk") == 0 && value >= 20 && value <= 200) {
            g_display_params.dpi_clk_mhz = value;
        } else if (strcmp(cmd, "bitrate") == 0 && value >= 200 && value <= 2000) {
            g_display_params.lane_bitrate_mbps = value;
        } else if (strcmp(cmd, "landscape") == 0 && (value == 0 || value == 1)) {
            g_display_params.landscape_mode = (value == 1);
        } else if (strcmp(cmd, "rotation") == 0 && (value == 0 || value == 1)) {
            g_display_params.rotation_enabled = (value == 1);
        } else {
            printf("Error: Unknown command or invalid value: %s %s\n", cmd, value_str);
            updated = false;
        }
        
        if (updated) {
            printf("Updated %s = %d\n", cmd, value);
        }
    }
}

void serial_config_task(void *pvParameters) {
    static char line[RD_BUF_SIZE];
    static int line_pos = 0;
    
    printf("\n=== ESP32-P4 JD9365 Display Config Console Ready ===\n");
    printf("Type 'help' for available commands\n");
    printf(">>> ");
    fflush(stdout);
    
    while (1) {
        int len = uart_read_bytes(UART_NUM, (uint8_t*)line + line_pos, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            char c = line[line_pos];
            
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line[line_pos] = '\0';
                    process_command(line);
                    line_pos = 0;
                }
                printf(">>> ");
                fflush(stdout);
            } else if (c == '\b' || c == 127) { // Backspace
                if (line_pos > 0) {
                    line_pos--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (c >= 32 && c < 127 && line_pos < RD_BUF_SIZE - 1) { // Printable chars
                line[line_pos++] = c;
                printf("%c", c);
                fflush(stdout);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void serial_config_init(void) {
    // Initialize UART (should already be initialized by ESP-IDF)
    // Just create the configuration task
    
    // Initialize parameters with defaults
    display_params_init(&g_display_params);
    
    // Try to load saved parameters
    display_params_load_from_nvs(&g_display_params);
    
    // Create the serial configuration task
    xTaskCreate(serial_config_task, "serial_config", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Serial configuration toolkit initialized");
}