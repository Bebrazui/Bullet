#ifndef SYS_STATE_H
#define SYS_STATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Структура глобального состояния системы ESPDroid
typedef struct {
    // Время и дата
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    
    // Питание
    uint8_t battery_percent;
    bool is_charging;
    
    // Сеть
    bool wifi_enabled;
    bool wifi_connected;
    char wifi_ssid[32];
    int8_t wifi_rssi;
    char ip_address[16];
    
    bool ble_enabled;
    bool ble_connected;

    // Дисплей и звук
    uint8_t brightness; // 0-100%
    uint8_t volume;     // 0-100%
    bool dark_mode;
    
    // Аппаратные ресурсы ESP32-S3 N16R8
    uint32_t cpu_freq_mhz;     // 240 MHz
    uint8_t cpu_usage_percent;  // 0-100%
    uint32_t free_heap_bytes;   // Внутренняя память SRAM (~380-512 KB)
    uint32_t total_heap_bytes;
    uint32_t free_psram_bytes;  // Внешняя память 8MB Octal PSRAM
    uint32_t total_psram_bytes; // 8388608 bytes (8MB)
    uint32_t flash_total_bytes; // 16777216 bytes (16MB)
    uint32_t flash_used_bytes;
    float chip_temperature_c;   // Встроенный термосенсор

    // Системные флаги
    bool quick_settings_open;
    bool flashlight_on;
} sys_state_t;

// Глобальный экземпляр состояния
extern sys_state_t g_sys_state;

void sys_state_init(void);
void sys_state_update_tick(void);

#ifdef __cplusplus
}
#endif

#endif // SYS_STATE_H
