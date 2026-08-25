#include "ui_status_bar.h"
#include <stdio.h>

// В реальном LVGL эти указатели соответствуют lv_obj_t*
static void *s_status_bar_obj = NULL;
static void *s_time_label = NULL;
static void *s_battery_label = NULL;
static void *s_battery_bar = NULL;
static void *s_wifi_icon = NULL;
static void *s_psram_badge = NULL;

void ui_status_bar_create(void *parent) {
    // Чистый C/LVGL код создания статус-бара:
    // lv_obj_t *bar = lv_obj_create(parent);
    // lv_obj_set_size(bar, LV_PCT(100), STATUS_BAR_HEIGHT);
    // lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BG_DARK), 0);
    // lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    // lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_status_bar_obj = parent;
}

void ui_status_bar_update(const sys_state_t *state) {
    if (!state) return;
    // Обновление лейблов времени, батареи и WiFi
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", state->hours, state->minutes);
    
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%d%%%s", state->battery_percent, state->is_charging ? " ⚡" : "");
    
    char psram_str[32];
    snprintf(psram_str, sizeof(psram_str), "PSRAM: %dM", (int)(state->free_psram_bytes / (1024 * 1024)));
}
