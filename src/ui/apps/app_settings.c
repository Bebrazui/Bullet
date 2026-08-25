#include "app_settings.h"
#include "../../app_core/app_manager.h"
#include "../../app_core/sys_state.h"
#include "../ui_theme.h"

static void *s_container = NULL;

static void* settings_create_view(void *parent) {
    s_container = parent;
    // LVGL list:
    // - Wi-Fi Switch & Network List
    // - Bluetooth Switch
    // - Display Brightness Slider
    // - Dark Mode Toggle
    // - System Info & Firmware Update (OTA)
    return s_container;
}

static void settings_resume(void) {}
static void settings_pause(void) {}
static void settings_destroy(void) {}

void app_settings_init(void) {
    app_descriptor_t app = {
        .id = APP_ID_SETTINGS,
        .name = "Settings",
        .icon_symbol = "⚙️",
        .accent_color = COLOR_ACCENT_PURPLE,
        .create_view = settings_create_view,
        .resume_view = settings_resume,
        .pause_view = settings_pause,
        .destroy_view = settings_destroy
    };
    app_manager_register(&app);
}
