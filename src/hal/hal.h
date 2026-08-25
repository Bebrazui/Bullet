#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Интерфейс уровня абстракции от железа
typedef struct {
    void (*init)(void);
    void (*set_backlight)(uint8_t percent);
    void (*get_battery_status)(uint8_t *percent, bool *is_charging);
    void (*get_memory_info)(uint32_t *free_sram, uint32_t *total_sram, uint32_t *free_psram, uint32_t *total_psram);
    float (*get_temperature)(void);
    void (*restart)(void);
} hal_driver_t;

extern const hal_driver_t *g_hal;

void hal_init(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_H
