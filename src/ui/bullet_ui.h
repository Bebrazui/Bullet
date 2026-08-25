/**
 * @file bullet_ui.h
 * @brief Pure C / LVGL bullet Multi-Tool Firmware for ESP32-S3
 */

#ifndef bullet_UI_H
#define bullet_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "lvgl.h"
#endif

typedef enum {
    bullet_THEME_AMBER = 0,
    bullet_THEME_MATRIX,
    bullet_THEME_CYAN,
    bullet_THEME_STEALTH
} bullet_theme_t;

typedef enum {
    bullet_VIEW_MAIN_MENU = 0,
    bullet_VIEW_WIFI,
    bullet_VIEW_BLE,
    bullet_VIEW_SUBGHZ,
    bullet_VIEW_INFRARED,
    bullet_VIEW_BADUSB,
    bullet_VIEW_GPIO,
    bullet_VIEW_FILES,
    bullet_VIEW_SETTINGS
} bullet_view_t;

/**
 * @brief Initialize all LVGL objects, styles, lists, and screens in C
 */
void bullet_ui_init(void);

/**
 * @brief Hardware input key handler
 * @param key 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT, 4=ENTER, 5=BACK
 */
void bullet_ui_key_handler(uint8_t key);

/**
 * @brief Set LVGL UI theme
 */
void bullet_ui_set_theme(bullet_theme_t theme);

/**
 * @brief Update telemetry data in LVGL status bar
 */
void bullet_ui_update_telemetry(uint8_t battery, bool charging, uint8_t cpu, uint32_t free_psram);

#ifdef __cplusplus
}
#endif

#endif // bullet_UI_H
