#include "app_sysinfo.h"
#include "../../app_core/app_manager.h"
#include "../../app_core/sys_state.h"
#include "../ui_theme.h"

static void *s_container = NULL;

static void* sysinfo_create_view(void *parent) {
    s_container = parent;
    // LVGL widgets:
    // Gauge / arc for CPU (240 MHz)
    // Meter for 8MB PSRAM (Used vs Free)
    // Meter for 16MB Flash (Used vs Free)
    // Info cards for Chip Model: ESP32-S3-WROOM-1 (N16R8)
    return s_container;
}

static void sysinfo_resume(void) {}
static void sysinfo_pause(void) {}
static void sysinfo_destroy(void) {}

void app_sysinfo_init(void) {
    app_descriptor_t app = {
        .id = APP_ID_SYSINFO,
        .name = "SysInfo",
        .icon_symbol = "📊",
        .accent_color = COLOR_ACCENT_CYAN,
        .create_view = sysinfo_create_view,
        .resume_view = sysinfo_resume,
        .pause_view = sysinfo_pause,
        .destroy_view = sysinfo_destroy
    };
    app_manager_register(&app);
}
