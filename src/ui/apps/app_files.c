#include "app_files.h"
#include "../../app_core/app_manager.h"
#include "../ui_theme.h"

static void *s_container = NULL;

static void* files_create_view(void *parent) {
    s_container = parent;
    // LVGL file tree list
    return s_container;
}

static void files_resume(void) {}
static void files_pause(void) {}
static void files_destroy(void) {}

void app_files_init(void) {
    app_descriptor_t app = {
        .id = APP_ID_FILES,
        .name = "Files",
        .icon_symbol = "📁",
        .accent_color = 0x00B0FF,
        .create_view = files_create_view,
        .resume_view = files_resume,
        .pause_view = files_pause,
        .destroy_view = files_destroy
    };
    app_manager_register(&app);
}
