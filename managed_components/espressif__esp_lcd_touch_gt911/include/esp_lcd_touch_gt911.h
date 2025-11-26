/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LCD touch: GT911
 */

#pragma once

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new GT911 touch driver
 *
 * @note The I2C communication should be initialized before use this function.
 *
 * @param io LCD/Touch panel IO handle
 * @param config: Touch configuration
 * @param out_touch: Touch instance handle
 * @return
 *      - ESP_OK                    on success
 *      - ESP_ERR_NO_MEM            if there is no memory for allocating main structure
 */
esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch);

/**
 * @brief Query raw resolution from a GT9xx controller via config registers
 *
 * If the device supports it, populates raw_x_max and raw_y_max with the
 * controller's native coordinate resolution. Returns ESP_OK when values are
 * valid. If not present/invalid, returns ESP_ERR_NOT_SUPPORTED/ESP_ERR_NOT_FOUND.
 */
esp_err_t esp_lcd_touch_gt911_get_raw_resolution(esp_lcd_touch_handle_t tp, uint16_t *raw_x_max, uint16_t *raw_y_max);

/**
 * @brief Read product ID string from GT9xx device
 *
 * Reads up to `buf_len - 1` bytes and NUL-terminates the result. If the
 * data read contains ASCII text (likely product ID like "911" or "9271"),
 * the caller can inspect it.
 */
esp_err_t esp_lcd_touch_gt911_get_product_id(esp_lcd_touch_handle_t tp, char *buf, size_t buf_len);

/**
 * @brief I2C address of the GT911 controller
 *
 * @note When power-on detects low level of the interrupt gpio, address is 0x5D.
 * @note Interrupt gpio is high level, address is 0x14.
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS          (0x5D)
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP   (0x14)

/**
 * @brief GT911 Configuration Type
 *
 */
typedef struct {
    uint8_t dev_addr;  /*!< I2C device address */
} esp_lcd_touch_io_gt911_config_t;

/**
 * @brief Touch IO configuration structure
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()           \
    {                                       \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, \
        .control_phase_bytes = 1,           \
        .dc_bit_offset = 0,                 \
        .lcd_cmd_bits = 16,                 \
        .flags =                            \
        {                                   \
            .disable_control_phase = 1,     \
        }                                   \
    }

#ifdef __cplusplus
}
#endif
