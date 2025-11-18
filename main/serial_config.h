/*
 * Serial Configuration Toolkit for JD9365 Display Parameters
 * Allows real-time adjustment of display timing parameters via serial console
 */

#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Structure to hold adjustable display parameters
typedef struct {
    // Resolution parameters
    uint16_t h_res;
    uint16_t v_res;
    
    // Timing parameters
    uint16_t hsync;
    uint16_t hbp;
    uint16_t hfp;
    uint16_t vsync;
    uint16_t vbp;
    uint16_t vfp;
    
    // Clock parameters
    uint16_t dpi_clk_mhz;
    uint16_t lane_bitrate_mbps;
    
    // Display mode
    bool landscape_mode;
    bool rotation_enabled;
} display_params_t;

// Function prototypes
void serial_config_init(void);
void serial_config_task(void *pvParameters);
void display_params_init(display_params_t *params);
void display_params_apply(display_params_t *params);
void display_params_print(display_params_t *params);
void display_params_save_to_nvs(display_params_t *params);
void display_params_load_from_nvs(display_params_t *params);

// Global display parameters
extern display_params_t g_display_params;
extern esp_lcd_panel_handle_t g_panel_handle;
extern bool g_params_changed;

#ifdef __cplusplus
}
#endif