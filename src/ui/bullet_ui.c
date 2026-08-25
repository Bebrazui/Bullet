/**
 * @file bullet_ui.c
 * @brief Pure C / LVGL bullet Multi-Tool Firmware Implementation
 * @details Compiles directly with PlatformIO / ESP-IDF and renders to display
 */

#include "bullet_ui.h"
#include <stdio.h>
#include <string.h>

#ifndef LV_SYMBOL_CHARGE
#define LV_SYMBOL_CHARGE "\xEF\x83\xA7"
#define LV_SYMBOL_WIFI   "\xEF\x87\xAB"
#define LV_SYMBOL_FILE   "\xEF\x85\x9B"
#define LV_SYMBOL_SETTINGS "\xEF\x80\x93"
#endif

// ============================================================================
// C STATE & DATA
// ============================================================================
typedef struct {
    bullet_view_t current_view;
    int8_t selected_index;
    bullet_theme_t theme;
    uint8_t battery;
    bool is_charging;
    uint8_t cpu;
    uint32_t free_psram;
} bullet_c_state_t;

static bullet_c_state_t g_state = {
    .current_view = bullet_VIEW_MAIN_MENU,
    .selected_index = 0,
    .theme = bullet_THEME_AMBER,
    .battery = 88,
    .is_charging = true,
    .cpu = 16,
    .free_psram = 7549747
};

static const char* menu_items[] = {
    "1. Wi-Fi Attacks & Sniff",
    "2. Bluetooth BLE Spammer",
    "3. Sub-GHz RF Analyzer",
    "4. Infrared Remote & TV-B-Gone",
    "5. BadUSB / DuckScript",
    "6. GPIO & I2C Logic Scope",
    "7. LittleFS / SD Files",
    "8. System Settings"
};
#define MENU_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

// ============================================================================
// PURE C LVGL INITIALIZATION & LOGIC
// ============================================================================
void bullet_ui_init(void) {
    g_state.current_view = bullet_VIEW_MAIN_MENU;
    g_state.selected_index = 0;
    printf("[bullet C/LVGL] UI Initialized with %d menu modules.\n", (int)MENU_COUNT);
}

void bullet_ui_key_handler(uint8_t key) {
    switch (key) {
        case 0: // UP
            if (g_state.current_view == bullet_VIEW_MAIN_MENU) {
                if (g_state.selected_index > 0) g_state.selected_index--;
                else g_state.selected_index = MENU_COUNT - 1;
            }
            break;

        case 1: // DOWN
            if (g_state.current_view == bullet_VIEW_MAIN_MENU) {
                if (g_state.selected_index < (int)MENU_COUNT - 1) g_state.selected_index++;
                else g_state.selected_index = 0;
            }
            break;

        case 4: // ENTER / SELECT
            if (g_state.current_view == bullet_VIEW_MAIN_MENU) {
                g_state.current_view = (bullet_view_t)(g_state.selected_index + 1);
            }
            break;

        case 5: // BACK / ESC
            g_state.current_view = bullet_VIEW_MAIN_MENU;
            break;
    }
}

void bullet_ui_set_theme(bullet_theme_t theme) {
    g_state.theme = theme;
}

void bullet_ui_update_telemetry(uint8_t battery, bool charging, uint8_t cpu, uint32_t free_psram) {
    g_state.battery = battery;
    g_state.is_charging = charging;
    g_state.cpu = cpu;
    g_state.free_psram = free_psram;
}
