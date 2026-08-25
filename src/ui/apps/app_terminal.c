#include "app_terminal.h"
#include "../../app_core/app_manager.h"
#include "../../app_core/sys_state.h"
#include "../ui_theme.h"

static void *s_container = NULL;

static void* terminal_create_view(void *parent) {
    s_container = parent;
    // LVGL console log view + command prompt
    return s_container;
}

static void terminal_resume(void) {}
static void terminal_pause(void) {}
static void terminal_destroy(void) {}

void app_terminal_init(void) {
    app_descriptor_t app = {
        .id = APP_ID_TERMINAL,
        .name = "Terminal",
        .icon_symbol = "📟",
        .accent_color = COLOR_SUCCESS_GREEN,
        .create_view = terminal_create_view,
        .resume_view = terminal_resume,
        .pause_view = terminal_pause,
        .destroy_view = terminal_destroy
    };
    app_manager_register(&app);
}
