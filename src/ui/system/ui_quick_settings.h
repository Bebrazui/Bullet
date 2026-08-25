#ifndef UI_QUICK_SETTINGS_H
#define UI_QUICK_SETTINGS_H

#include "../ui_theme.h"
#include "../../app_core/sys_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_quick_settings_create(void *parent);
void ui_quick_settings_open(void);
void ui_quick_settings_close(void);
void ui_quick_settings_toggle(void);
bool ui_quick_settings_is_open(void);

#ifdef __cplusplus
}
#endif

#endif // UI_QUICK_SETTINGS_H
