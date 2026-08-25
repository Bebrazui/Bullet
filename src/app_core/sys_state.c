#include "sys_state.h"
#include <string.h>

sys_state_t g_sys_state;

void sys_state_init(void) {
    memset(&g_sys_state, 0, sizeof(sys_state_t));
    
    g_sys_state.hours = 12;
    g_sys_state.minutes = 45;
    g_sys_state.seconds = 0;
    
    g_sys_state.battery_percent = 88;
    g_sys_state.is_charging = false;
    
    g_sys_state.wifi_enabled = true;
    g_sys_state.wifi_connected = true;
    strncpy(g_sys_state.wifi_ssid, "ESP_Cyber_Net", sizeof(g_sys_state.wifi_ssid));
    g_sys_state.wifi_rssi = -55;
    strncpy(g_sys_state.ip_address, "192.168.1.137", sizeof(g_sys_state.ip_address));
    
    g_sys_state.ble_enabled = true;
    g_sys_state.ble_connected = false;
    
    g_sys_state.brightness = 80;
    g_sys_state.volume = 65;
    g_sys_state.dark_mode = true;
    
    // ESP32-S3 N16R8 Defaults
    g_sys_state.cpu_freq_mhz = 240;
    g_sys_state.cpu_usage_percent = 18;
    g_sys_state.total_heap_bytes = 384 * 1024;      // 384 KB SRAM
    g_sys_state.free_heap_bytes = 260 * 1024;
    g_sys_state.total_psram_bytes = 8 * 1024 * 1024; // 8 MB Octal PSRAM
    g_sys_state.free_psram_bytes = 7240 * 1024;
    g_sys_state.flash_total_bytes = 16 * 1024 * 1024; // 16 MB Flash
    g_sys_state.flash_used_bytes = 3450 * 1024;
    g_sys_state.chip_temperature_c = 36.5f;

    g_sys_state.quick_settings_open = false;
    g_sys_state.flashlight_on = false;
}

void sys_state_update_tick(void) {
    g_sys_state.seconds++;
    if (g_sys_state.seconds >= 60) {
        g_sys_state.seconds = 0;
        g_sys_state.minutes++;
        if (g_sys_state.minutes >= 60) {
            g_sys_state.minutes = 0;
            g_sys_state.hours = (g_sys_state.hours + 1) % 24;
        }
    }
}
