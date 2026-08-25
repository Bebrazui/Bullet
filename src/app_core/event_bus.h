#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_EVENT_NONE = 0,
    SYS_EVENT_TICK_1S,
    SYS_EVENT_BATTERY_CHANGED,
    SYS_EVENT_WIFI_STATUS_CHANGED,
    SYS_EVENT_BRIGHTNESS_CHANGED,
    SYS_EVENT_NOTIFICATION_POSTED,
    SYS_EVENT_GESTURE_SWIPE_DOWN,
    SYS_EVENT_GESTURE_SWIPE_UP,
    SYS_EVENT_BUTTON_HOME,
    SYS_EVENT_BUTTON_BOOT,
    SYS_EVENT_APP_OPENED,
    SYS_EVENT_APP_CLOSED
} sys_event_type_t;

typedef struct {
    sys_event_type_t type;
    union {
        int32_t int_val;
        float float_val;
        const char *str_val;
        void *ptr_val;
    } data;
} sys_event_t;

typedef void (*event_listener_cb_t)(const sys_event_t *event, void *user_data);

void event_bus_init(void);
bool event_bus_subscribe(sys_event_type_t type, event_listener_cb_t callback, void *user_data);
void event_bus_publish(const sys_event_t *event);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H
