/**
 * @file micro_adb.h
 * @brief Embedded Micro-ADB (Android Debug Bridge) Client Protocol Engine
 * @details Standalone, lightweight C implementation of the ADB v1.0 wire protocol
 *          over TCP (Wi-Fi) and USB OTG Host for ESP32 and native platforms.
 */

#ifndef MICRO_ADB_H
#define MICRO_ADB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MICRO_ADB_OK = 0,
    MICRO_ADB_ERR_CONNECT = -1,
    MICRO_ADB_ERR_AUTH = -2,
    MICRO_ADB_ERR_TIMEOUT = -3,
    MICRO_ADB_ERR_PROTOCOL = -4,
    MICRO_ADB_ERR_DISCONNECTED = -5,
    MICRO_ADB_ERR_PAYLOAD = -6
} micro_adb_status_t;

typedef struct {
    bool is_connected;
    char target_ip[32];
    uint16_t port;
    char product_model[32];
    char android_version[24];
    char product_name[32];
    uint8_t battery_level;
    bool is_charging;
    bool screen_on;
    char banner[128];
} micro_adb_device_info_t;

/**
 * @brief Initialize the embedded Micro-ADB engine
 */
void micro_adb_init(void);

/**
 * @brief Connect to an Android device over ADB TCP/IP (WiFi ADB)
 * @param ip Target Android device IP address (e.g. "192.168.1.100")
 * @param port Target ADB port (default 5555)
 * @return MICRO_ADB_OK on success or error code
 */
micro_adb_status_t micro_adb_connect_tcp(const char* ip, uint16_t port);

/**
 * @brief Disconnect active ADB session
 */
void micro_adb_disconnect(void);

/**
 * @brief Check if ADB session is active
 */
bool micro_adb_is_connected(void);

/**
 * @brief Get currently connected device information & live telemetry
 */
bool micro_adb_get_device_info(micro_adb_device_info_t* out_info);

/**
 * @brief Execute a raw shell command over active ADB session
 * @param cmd Shell command string (e.g. "input keyevent 26", "dumpsys battery")
 * @param out_buf Output buffer to receive command stdout (can be NULL if not needed)
 * @param out_max_len Max capacity of out_buf
 * @return MICRO_ADB_OK on success
 */
micro_adb_status_t micro_adb_exec_shell(const char* cmd, char* out_buf, size_t out_max_len);

/**
 * @brief Send keyevent code to remote Android device
 * @param keycode Android KeyCode (e.g. 26=Power, 3=Home, 4=Back, 24=Vol+, 25=Vol-)
 */
micro_adb_status_t micro_adb_send_keyevent(int keycode);

/**
 * @brief Send swipe gesture to remote Android device
 */
micro_adb_status_t micro_adb_swipe(int x1, int y1, int x2, int y2, int duration_ms);

/**
 * @brief Reboot target Android device (normal, recovery, or bootloader/fastboot)
 * @param target_mode Mode string: "" (normal), "recovery", or "bootloader"
 */
micro_adb_status_t micro_adb_reboot(const char* target_mode);

/**
 * @brief Open a URL in Android default browser via intent
 */
micro_adb_status_t micro_adb_open_url(const char* url);

/**
 * @brief Query live telemetry (battery level, screen status, model) from Android device
 */
micro_adb_status_t micro_adb_poll_telemetry(void);

#ifdef __cplusplus
}
#endif

#endif // MICRO_ADB_H
