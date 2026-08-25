#ifndef UI_NAV_BAR_H
#define UI_NAV_BAR_H

#include "../ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_nav_bar_create(void *parent);
void ui_nav_bar_on_back_clicked(void);
void ui_nav_bar_on_home_clicked(void);
void ui_nav_bar_on_settings_clicked(void);

#ifdef __cplusplus
}
#endif

#endif // UI_NAV_BAR_H
