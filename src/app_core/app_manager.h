#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Идентификаторы встроенных приложений
typedef enum {
    APP_ID_LAUNCHER = 0,
    APP_ID_SYSINFO,
    APP_ID_SETTINGS,
    APP_ID_TERMINAL,
    APP_ID_STOPWATCH,
    APP_ID_FILES,
    APP_ID_MAX
} app_id_t;

// Дескриптор приложения
typedef struct {
    app_id_t id;
    const char *name;
    const char *icon_symbol; // Символ иконки (LVGL symbol / unicode)
    uint32_t accent_color;   // HEX цвет темы приложения
    void *(*create_view)(void *parent); // Функция создания GUI
    void (*resume_view)(void);
    void (*pause_view)(void);
    void (*destroy_view)(void);
} app_descriptor_t;

void app_manager_init(void);
void app_manager_register(const app_descriptor_t *app);
bool app_manager_launch(app_id_t id);
bool app_manager_close_current(void);
app_id_t app_manager_get_current_id(void);
const app_descriptor_t* app_manager_get_app(app_id_t id);
int app_manager_get_count(void);

#ifdef __cplusplus
}
#endif

#endif // APP_MANAGER_H
