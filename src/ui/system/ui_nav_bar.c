#include "ui_nav_bar.h"
#include "../../app_core/app_manager.h"
#include "ui_quick_settings.h"

void ui_nav_bar_create(void *parent) {
    // В LVGL: панель внизу с 3 кнопками (Назад, Домой, Шторка настроек)
}

void ui_nav_bar_on_back_clicked(void) {
    if (ui_quick_settings_is_open()) {
        ui_quick_settings_close();
    } else {
        app_manager_close_current();
    }
}

void ui_nav_bar_on_home_clicked(void) {
    if (ui_quick_settings_is_open()) {
        ui_quick_settings_close();
    }
    app_manager_launch(APP_ID_LAUNCHER);
}

void ui_nav_bar_on_settings_clicked(void) {
    ui_quick_settings_toggle();
}
