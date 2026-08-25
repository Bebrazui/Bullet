#include "ui_quick_settings.h"
#include <stdio.h>

static void *s_shade_panel = NULL;
static bool s_is_open = false;

void ui_quick_settings_create(void *parent) {
    s_shade_panel = parent;
    s_is_open = false;
}

void ui_quick_settings_open(void) {
    s_is_open = true;
    g_sys_state.quick_settings_open = true;
}

void ui_quick_settings_close(void) {
    s_is_open = false;
    g_sys_state.quick_settings_open = false;
}

void ui_quick_settings_toggle(void) {
    if (s_is_open) {
        ui_quick_settings_close();
    } else {
        ui_quick_settings_open();
    }
}

bool ui_quick_settings_is_open(void) {
    return s_is_open;
}
