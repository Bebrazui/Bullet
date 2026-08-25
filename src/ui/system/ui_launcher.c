#include "ui_launcher.h"
#include <stdio.h>

static void *s_launcher_container = NULL;

void* ui_launcher_create(void *parent) {
    s_launcher_container = parent;
    // Чистый C/LVGL код создания сетки приложений:
    // lv_obj_t *grid = lv_obj_create(parent);
    // lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    // lv_obj_set_style_pad_all(grid, 10, 0);
    // lv_obj_set_style_pad_gap(grid, 12, 0);
    return s_launcher_container;
}

void ui_launcher_show(void) {
    // lv_obj_clear_flag(s_launcher_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_launcher_hide(void) {
    // lv_obj_add_flag(s_launcher_container, LV_OBJ_FLAG_HIDDEN);
}
