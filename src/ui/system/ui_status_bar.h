#ifndef UI_STATUS_BAR_H
#define UI_STATUS_BAR_H

#include "../ui_theme.h"
#include "../../app_core/sys_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_status_bar_create(void *parent);
void ui_status_bar_update(const sys_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // UI_STATUS_BAR_H
