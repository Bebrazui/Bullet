#ifndef UI_LAUNCHER_H
#define UI_LAUNCHER_H

#include "../ui_theme.h"
#include "../../app_core/app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void* ui_launcher_create(void *parent);
void ui_launcher_show(void);
void ui_launcher_hide(void);

#ifdef __cplusplus
}
#endif

#endif // UI_LAUNCHER_H
