#ifndef UI_NOTIFICATIONS_H
#define UI_NOTIFICATIONS_H

#include "../ui_theme.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_notifications_init(void *parent);
void ui_notifications_toast(const char *title, const char *message, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif // UI_NOTIFICATIONS_H
