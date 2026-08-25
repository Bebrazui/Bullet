/**
 * @file wifi_oled_ui.h
 * @brief Ultra-Optimized Dual-Display Engine for ESP32-S3 (N16R8) & WASM
 * @details Security IDS, Probe Request Sniffer, Matrix Rain, RF Sniffer, BLE Radar, FFT & Linux CLI.
 */

#ifndef WIFI_OLED_UI_H
#define WIFI_OLED_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DISP_MODE_OLED_128x64 = 0,
    DISP_MODE_IPS_240x240 = 1,
    DISP_MODE_IPS_320x240 = 2, // Landscape QVGA (CYD / ILI9341 / ST7789)
    DISP_MODE_IPS_480x320 = 3  // HVGA Terminal (ST7796 / ILI9488)
} disp_mode_t;

typedef enum {
    LANG_EN = 0,
    LANG_RU = 1
} oled_lang_t;

typedef enum {
    OLED_VIEW_BOOT = 0,
    OLED_VIEW_MAIN_MENU,
    OLED_VIEW_WIFI_MENU,
    OLED_VIEW_SCANNING,
    OLED_VIEW_NETWORKS_LIST,
    OLED_VIEW_CONNECTING,
    OLED_VIEW_STATUS,
    OLED_VIEW_AP_MODE,
    OLED_VIEW_DEAUTH_IDS,    // Security Suite: Wi-Fi IDS & Deauth Attack Detector
    OLED_VIEW_PROBE_SNIFFER, // Security Suite: Probe Request & Footprint Sniffer
    OLED_VIEW_MATRIX_RAIN,   // Cyberpunk Matrix Digital Rain HUD
    OLED_VIEW_SNIFFER,       // 2.4GHz RF Channel Utilization
    OLED_VIEW_BLE_RADAR,     // Bluetooth LE Radar & Proximity
    OLED_VIEW_FFT_SPECTRUM,  // 16-Band Cyberpunk Audio Spectrum
    OLED_VIEW_KART_GAME,     // CyberKart Turbo Racing (Auto-Gas, 1-Knob Steer)
    OLED_VIEW_DINO_GAME,     // Chrome T-Rex Dino Endless Runner
    OLED_VIEW_PONG_GAME,     // Retro Paddle Arcade
    OLED_VIEW_SYS_INFO,      // Hardware Dashboard
    OLED_VIEW_HW_SCANNER,    // I2C/SPI/USB Bus Hardware Scanner
    OLED_VIEW_SUBGHZ,        // Sub-GHz RF Transceiver (CC1101 Record & Replay)
    OLED_VIEW_ADB_APP,       // Android Micro-ADB Controller & Shell
    OLED_VIEW_TERMINAL,      // Linux CLI Console
    OLED_VIEW_SETTINGS       // Display & Settings
} oled_view_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool is_secure;
} wifi_net_item_t;

typedef struct {
    char name[24];
    char mac[18];
    int8_t rssi;
    int type; // 0=Phone, 1=Beacon/AirTag, 2=Flipper/Tool, 3=Audio
} ble_device_t;

typedef struct {
    char client_mac[18];
    char requested_ssid[32];
    int8_t rssi;
    uint32_t seen_count;
} probe_item_t;

typedef struct {
    char target_mac[18];
    char bssid[18];
    int channel;
    uint32_t burst_count;
    int8_t rssi;
} deauth_alert_t;

#define MAX_NETWORKS_CAPACITY 32
#define MAX_BLE_CAPACITY 16
#define MAX_PROBE_CAPACITY 16
#define MAX_DEAUTH_ALERTS 8

// Core Engine lifecycle
void oled_init(void);
void oled_render(void);
void oled_set_disp_mode(int mode);
void wifi_oled_set_disp_mode(disp_mode_t mode);
int  oled_get_disp_mode(void);
uint8_t* oled_get_fb(void);
int  oled_get_width(void);
int  oled_get_height(void);
void oled_set_lang(int lang_id);
void oled_set_theme(int theme_id);

// Hardware Inputs
void hw_knob_rotate(int dir); // 0=CCW (Up/Left), 1=CW (Down/Right)
void hw_button_press(int action); // 0=Short Click, 1=Double Click, 2=Long Press (Back)
void oled_key(int key);

// Terminal & Text Input
void oled_char_input(int char_code);
void oled_backspace(void);
void oled_enter(void);

// Real Hardware Data Feeders
void wifi_ui_clear_networks(void);
void wifi_ui_add_network(const char* ssid, int8_t rssi, bool is_secure);
void wifi_ui_set_connection_info(const char* ssid, const char* ip, const char* gateway, const char* mac, int8_t rssi);
void wifi_ui_set_sys_telemetry(float temp_c, uint32_t free_psram, uint32_t free_heap, uint32_t uptime_sec);
void wifi_ui_set_sys_telemetry_ex(
    const char* chip_model,
    uint8_t chip_cores,
    uint16_t cpu_freq_mhz,
    uint32_t total_psram,
    uint32_t free_psram,
    uint32_t total_sram,
    uint32_t free_sram,
    uint32_t total_flash,
    uint32_t free_flash,
    float temp_c,
    uint32_t uptime_sec
);
void wifi_ui_set_scan_status(bool is_scanning);
void wifi_ui_feed_sniffer_packet(int channel, int rssi);
void wifi_ui_add_ble_device(const char* name, const char* mac, int8_t rssi, int type);
void wifi_ui_add_probe_request(const char* client_mac, const char* requested_ssid, int8_t rssi);
void wifi_ui_add_deauth_alert(const char* target_mac, const char* bssid, int channel, int8_t rssi);
void wifi_ui_adb_set_device_info(const char* model, const char* version, int battery, bool is_connected);
void wifi_ui_adb_trigger_action(int action_idx);
void wifi_ui_set_cc1101_detected(bool detected);
bool wifi_ui_get_cc1101_detected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_OLED_UI_H
