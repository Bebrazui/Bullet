#include "app_stopwatch.h"
#include "../../app_core/app_manager.h"
#include "../ui_theme.h"

static void *s_container = NULL;

static void* stopwatch_create_view(void *parent) {
    s_container = parent;
    return s_container;
}

static void stopwatch_resume(void) {}
static void stopwatch_pause(void) {}
static void stopwatch_destroy(void) {}

void app_stopwatch_init(void) {
    app_descriptor_t app = {
        .id = APP_ID_STOPWATCH,
        .name = "Clock",
        .icon_symbol = "⏱️",
        .accent_color = COLOR_WARN_ORANGE,
        .create_view = stopwatch_create_view,
        .resume_view = stopwatch_resume,
        .pause_view = stopwatch_pause,
        .destroy_view = stopwatch_destroy
    };
    app_manager_register(&app);
}
