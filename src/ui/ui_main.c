#include "ui_main.h"
#include "../app_core/app_manager.h"
#include "../app_core/sys_state.h"
#include "../app_core/event_bus.h"

// Регистрация встроенных приложений
extern void app_sysinfo_init(void);
extern void app_settings_init(void);
extern void app_terminal_init(void);
extern void app_stopwatch_init(void);
extern void app_files_init(void);

void ui_init(void) {
    ui_theme_init();
    sys_state_init();
    event_bus_init();
    app_manager_init();

    // Создание корневого экрана
    // ui_status_bar_create(lv_scr_act());
    // ui_quick_settings_create(lv_scr_act());
    // ui_launcher_create(lv_scr_act());
    // ui_nav_bar_create(lv_scr_act());
    // ui_notifications_init(lv_scr_act());

    // Инициализация приложений
    app_sysinfo_init();
    app_settings_init();
    app_terminal_init();
    app_stopwatch_init();
    app_files_init();
}

void ui_tick(void) {
    sys_state_update_tick();
    ui_status_bar_update(&g_sys_state);
}
