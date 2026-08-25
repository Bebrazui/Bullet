#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "ui_theme.h"
#include "system/ui_status_bar.h"
#include "system/ui_quick_settings.h"
#include "system/ui_launcher.h"
#include "system/ui_nav_bar.h"
#include "system/ui_notifications.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_tick(void);

#ifdef __cplusplus
}
#endif

#endif // UI_MAIN_H
