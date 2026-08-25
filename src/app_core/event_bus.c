#include "event_bus.h"
#include <string.h>

#define MAX_SUBSCRIPTIONS 32

typedef struct {
    sys_event_type_t type;
    event_listener_cb_t callback;
    void *user_data;
    bool active;
} event_sub_t;

static event_sub_t s_subscriptions[MAX_SUBSCRIPTIONS];

void event_bus_init(void) {
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
}

bool event_bus_subscribe(sys_event_type_t type, event_listener_cb_t callback, void *user_data) {
    if (!callback) return false;
    
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!s_subscriptions[i].active) {
            s_subscriptions[i].type = type;
            s_subscriptions[i].callback = callback;
            s_subscriptions[i].user_data = user_data;
            s_subscriptions[i].active = true;
            return true;
        }
    }
    return false;
}

void event_bus_publish(const sys_event_t *event) {
    if (!event) return;
    
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active && s_subscriptions[i].type == event->type) {
            s_subscriptions[i].callback(event, s_subscriptions[i].user_data);
        }
    }
}
