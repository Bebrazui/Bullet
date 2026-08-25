#include "app_manager.h"
#include <string.h>

#define MAX_REGISTERED_APPS 16

static app_descriptor_t s_registered_apps[MAX_REGISTERED_APPS];
static int s_app_count = 0;
static app_id_t s_current_app = APP_ID_LAUNCHER;

void app_manager_init(void) {
    s_app_count = 0;
    s_current_app = APP_ID_LAUNCHER;
    memset(s_registered_apps, 0, sizeof(s_registered_apps));
}

void app_manager_register(const app_descriptor_t *app) {
    if (!app || s_app_count >= MAX_REGISTERED_APPS) return;
    s_registered_apps[s_app_count++] = *app;
}

bool app_manager_launch(app_id_t id) {
    if (s_current_app == id) return true;
    
    // Pause current
    for (int i = 0; i < s_app_count; i++) {
        if (s_registered_apps[i].id == s_current_app && s_registered_apps[i].pause_view) {
            s_registered_apps[i].pause_view();
        }
    }
    
    s_current_app = id;
    
    // Resume new
    for (int i = 0; i < s_app_count; i++) {
        if (s_registered_apps[i].id == s_current_app && s_registered_apps[i].resume_view) {
            s_registered_apps[i].resume_view();
        }
    }
    
    return true;
}

bool app_manager_close_current(void) {
    if (s_current_app == APP_ID_LAUNCHER) return false;
    return app_manager_launch(APP_ID_LAUNCHER);
}

app_id_t app_manager_get_current_id(void) {
    return s_current_app;
}

const app_descriptor_t* app_manager_get_app(app_id_t id) {
    for (int i = 0; i < s_app_count; i++) {
        if (s_registered_apps[i].id == id) {
            return &s_registered_apps[i];
        }
    }
    return NULL;
}

int app_manager_get_count(void) {
    return s_app_count;
}
