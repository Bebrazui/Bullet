/**
 * @file micro_adb.c
 * @brief Embedded Micro-ADB Client Implementation for Bullet OS
 */

#include "micro_adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    #include <lwip/sockets.h>
    #include <lwip/netdb.h>
    #include <unistd.h>
    #define SOCKET_TYPE int
    #define INVALID_SOCKET_VAL (-1)
    #define CLOSE_SOCKET(s) close(s)
#elif defined(_WIN32) || defined(__WIN32__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define SOCKET_TYPE SOCKET
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SOCKET_TYPE int
    #define INVALID_SOCKET_VAL (-1)
    #define CLOSE_SOCKET(s) close(s)
#endif

// ADB Protocol Constants
#define A_SYNC 0x434e5953
#define A_CNXN 0x4e584e43
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257
#define A_AUTH 0x48545541

#define A_VERSION 0x01000000
#define MAX_ADB_PAYLOAD 4096

#pragma pack(push, 1)
typedef struct {
    uint32_t command;     // command identifier constant
    uint32_t arg0;        // first argument
    uint32_t arg1;        // second argument
    uint32_t data_length; // length of following payload in bytes
    uint32_t data_crc32;  // checksum of data payload
    uint32_t magic;       // command ^ 0xFFFFFFFF
} adb_header_t;
#pragma pack(pop)

static SOCKET_TYPE g_adb_sock = INVALID_SOCKET_VAL;
static uint32_t g_local_stream_id = 100;
static micro_adb_device_info_t g_dev_info = {
    .is_connected = false,
    .target_ip = "192.168.1.100",
    .port = 5555,
    .product_model = "No Device Attached",
    .android_version = "OFFLINE",
    .product_name = "None",
    .battery_level = 0,
    .is_charging = false,
    .screen_on = false,
    .banner = ""
};

static uint32_t calc_adb_checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    if (!data) return 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

static bool adb_send_msg(SOCKET_TYPE sock, uint32_t cmd, uint32_t arg0, uint32_t arg1, const void* data, uint32_t len) {
    if (sock == INVALID_SOCKET_VAL) return false;

    adb_header_t hdr;
    hdr.command = cmd;
    hdr.arg0 = arg0;
    hdr.arg1 = arg1;
    hdr.data_length = len;
    hdr.data_crc32 = calc_adb_checksum((const uint8_t*)data, len);
    hdr.magic = cmd ^ 0xFFFFFFFF;

    int sent = send(sock, (const char*)&hdr, sizeof(hdr), 0);
    if (sent != (int)sizeof(hdr)) return false;

    if (data && len > 0) {
        sent = send(sock, (const char*)data, len, 0);
        if (sent != (int)len) return false;
    }
    return true;
}

static bool adb_recv_exact(SOCKET_TYPE sock, void* buf, size_t len) {
    size_t total = 0;
    char* p = (char*)buf;
    while (total < len) {
        int r = recv(sock, p + total, (int)(len - total), 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

static bool adb_recv_msg(SOCKET_TYPE sock, adb_header_t* out_hdr, void* data_buf, size_t max_data_len) {
    if (sock == INVALID_SOCKET_VAL || !out_hdr) return false;

    if (!adb_recv_exact(sock, out_hdr, sizeof(adb_header_t))) return false;

    // Validate magic
    if ((out_hdr->command ^ 0xFFFFFFFF) != out_hdr->magic) return false;

    if (out_hdr->data_length > 0) {
        if (out_hdr->data_length > max_data_len) {
            // Drain excess
            size_t to_read = max_data_len;
            if (!adb_recv_exact(sock, data_buf, to_read)) return false;
            size_t remaining = out_hdr->data_length - to_read;
            char dummy[128];
            while (remaining > 0) {
                size_t chunk = remaining > sizeof(dummy) ? sizeof(dummy) : remaining;
                if (!adb_recv_exact(sock, dummy, chunk)) return false;
                remaining -= chunk;
            }
        } else {
            if (!adb_recv_exact(sock, data_buf, out_hdr->data_length)) return false;
        }
    }
    return true;
}

void micro_adb_init(void) {
#if defined(_WIN32) || defined(__WIN32__)
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_inited = true;
    }
#endif
    g_dev_info.is_connected = false;
}

static void parse_cnxn_banner(const char* banner) {
    if (!banner) return;
    strncpy(g_dev_info.banner, banner, sizeof(g_dev_info.banner) - 1);

    // format: "device::ro.product.name=...;ro.product.model=...;ro.product.device=...;features=..."
    const char* model_tag = "ro.product.model=";
    char* p_model = strstr(banner, model_tag);
    if (p_model) {
        p_model += strlen(model_tag);
        char* end = strchr(p_model, ';');
        size_t len = end ? (size_t)(end - p_model) : strlen(p_model);
        if (len >= sizeof(g_dev_info.product_model)) len = sizeof(g_dev_info.product_model) - 1;
        strncpy(g_dev_info.product_model, p_model, len);
        g_dev_info.product_model[len] = '\0';
    } else {
        strncpy(g_dev_info.product_model, "Android Device", sizeof(g_dev_info.product_model) - 1);
    }

    const char* name_tag = "ro.product.name=";
    char* p_name = strstr(banner, name_tag);
    if (p_name) {
        p_name += strlen(name_tag);
        char* end = strchr(p_name, ';');
        size_t len = end ? (size_t)(end - p_name) : strlen(p_name);
        if (len >= sizeof(g_dev_info.product_name)) len = sizeof(g_dev_info.product_name) - 1;
        strncpy(g_dev_info.product_name, p_name, len);
        g_dev_info.product_name[len] = '\0';
    }
}

micro_adb_status_t micro_adb_connect_tcp(const char* ip, uint16_t port) {
    micro_adb_init();

    if (g_adb_sock != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(g_adb_sock);
        g_adb_sock = INVALID_SOCKET_VAL;
    }

    if (!ip || strlen(ip) == 0) ip = "127.0.0.1";
    if (port == 0) port = 5555;

    strncpy(g_dev_info.target_ip, ip, sizeof(g_dev_info.target_ip) - 1);
    g_dev_info.port = port;

    SOCKET_TYPE s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET_VAL) return MICRO_ADB_ERR_CONNECT;

    // Timeout (3 seconds)
#if defined(_WIN32)
    DWORD tv = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(s);
        g_dev_info.is_connected = false;
        return MICRO_ADB_ERR_CONNECT;
    }

    // Send CNXN handshake packet
    const char* host_banner = "host::BulletOS-MicroADB\0";
    uint32_t banner_len = (uint32_t)strlen(host_banner) + 1;

    if (!adb_send_msg(s, A_CNXN, A_VERSION, MAX_ADB_PAYLOAD, host_banner, banner_len)) {
        CLOSE_SOCKET(s);
        return MICRO_ADB_ERR_PROTOCOL;
    }

    // Read device response
    adb_header_t resp_hdr;
    char payload_buf[512] = {0};
    if (!adb_recv_msg(s, &resp_hdr, payload_buf, sizeof(payload_buf) - 1)) {
        CLOSE_SOCKET(s);
        return MICRO_ADB_ERR_TIMEOUT;
    }

    if (resp_hdr.command == A_AUTH) {
        // Authentication required (RSA key exchange)
        CLOSE_SOCKET(s);
        g_dev_info.is_connected = false;
        return MICRO_ADB_ERR_AUTH;
    }

    if (resp_hdr.command != A_CNXN) {
        CLOSE_SOCKET(s);
        g_dev_info.is_connected = false;
        return MICRO_ADB_ERR_PROTOCOL;
    }

    g_adb_sock = s;
    g_dev_info.is_connected = true;
    parse_cnxn_banner(payload_buf);

    // Live query initial telemetry
    micro_adb_poll_telemetry();

    return MICRO_ADB_OK;
}

void micro_adb_disconnect(void) {
    if (g_adb_sock != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(g_adb_sock);
        g_adb_sock = INVALID_SOCKET_VAL;
    }
    g_dev_info.is_connected = false;
    strncpy(g_dev_info.product_model, "No Device Attached", sizeof(g_dev_info.product_model) - 1);
    strncpy(g_dev_info.android_version, "OFFLINE", sizeof(g_dev_info.android_version) - 1);
    g_dev_info.battery_level = 0;
}

bool micro_adb_is_connected(void) {
    return g_dev_info.is_connected && (g_adb_sock != INVALID_SOCKET_VAL);
}

bool micro_adb_get_device_info(micro_adb_device_info_t* out_info) {
    if (!out_info) return false;
    *out_info = g_dev_info;
    return true;
}

micro_adb_status_t micro_adb_exec_shell(const char* cmd, char* out_buf, size_t out_max_len) {
    if (!micro_adb_is_connected()) return MICRO_ADB_ERR_DISCONNECTED;

    uint32_t local_id = ++g_local_stream_id;
    char open_dest[256];
    snprintf(open_dest, sizeof(open_dest), "shell:%s", cmd ? cmd : "");
    uint32_t dest_len = (uint32_t)strlen(open_dest) + 1;

    // Send A_OPEN
    if (!adb_send_msg(g_adb_sock, A_OPEN, local_id, 0, open_dest, dest_len)) {
        micro_adb_disconnect();
        return MICRO_ADB_ERR_PROTOCOL;
    }

    // Wait for A_OKAY (or A_CLSE if rejected)
    adb_header_t hdr;
    char chunk[512];
    if (!adb_recv_msg(g_adb_sock, &hdr, chunk, sizeof(chunk) - 1)) {
        micro_adb_disconnect();
        return MICRO_ADB_ERR_TIMEOUT;
    }

    if (hdr.command != A_OKAY) {
        return MICRO_ADB_ERR_PROTOCOL;
    }

    uint32_t remote_id = hdr.arg0;
    size_t out_written = 0;
    if (out_buf && out_max_len > 0) out_buf[0] = '\0';

    // Stream reading loop until A_CLSE
    while (true) {
        memset(chunk, 0, sizeof(chunk));
        if (!adb_recv_msg(g_adb_sock, &hdr, chunk, sizeof(chunk) - 1)) break;

        if (hdr.command == A_WRTE) {
            // Append to output
            if (out_buf && out_written < out_max_len - 1) {
                size_t to_copy = hdr.data_length;
                if (out_written + to_copy >= out_max_len) to_copy = out_max_len - 1 - out_written;
                memcpy(out_buf + out_written, chunk, to_copy);
                out_written += to_copy;
                out_buf[out_written] = '\0';
            }
            // Acknowledge WRTE
            adb_send_msg(g_adb_sock, A_OKAY, local_id, remote_id, NULL, 0);
        } else if (hdr.command == A_CLSE) {
            // Remote closed stream
            adb_send_msg(g_adb_sock, A_CLSE, local_id, remote_id, NULL, 0);
            break;
        }
    }

    return MICRO_ADB_OK;
}

micro_adb_status_t micro_adb_send_keyevent(int keycode) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "input keyevent %d", keycode);
    return micro_adb_exec_shell(cmd, NULL, 0);
}

micro_adb_status_t micro_adb_swipe(int x1, int y1, int x2, int y2, int duration_ms) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "input swipe %d %d %d %d %d", x1, y1, x2, y2, duration_ms);
    return micro_adb_exec_shell(cmd, NULL, 0);
}

micro_adb_status_t micro_adb_reboot(const char* target_mode) {
    char cmd[48];
    if (target_mode && strlen(target_mode) > 0) {
        snprintf(cmd, sizeof(cmd), "reboot %s", target_mode);
    } else {
        snprintf(cmd, sizeof(cmd), "reboot");
    }
    return micro_adb_exec_shell(cmd, NULL, 0);
}

micro_adb_status_t micro_adb_open_url(const char* url) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "am start -a android.intent.action.VIEW -d %s", url ? url : "http://bullet.local");
    return micro_adb_exec_shell(cmd, NULL, 0);
}

micro_adb_status_t micro_adb_poll_telemetry(void) {
    if (!micro_adb_is_connected()) return MICRO_ADB_ERR_DISCONNECTED;

    // 1. Android Release Version
    char ver_buf[64] = {0};
    if (micro_adb_exec_shell("getprop ro.build.version.release", ver_buf, sizeof(ver_buf)) == MICRO_ADB_OK) {
        char* end = strpbrk(ver_buf, "\r\n");
        if (end) *end = '\0';
        if (strlen(ver_buf) > 0) {
            snprintf(g_dev_info.android_version, sizeof(g_dev_info.android_version), "Android %s", ver_buf);
        }
    }

    // 2. Battery Telemetry
    char bat_buf[512] = {0};
    if (micro_adb_exec_shell("dumpsys battery", bat_buf, sizeof(bat_buf)) == MICRO_ADB_OK) {
        char* p_level = strstr(bat_buf, "level:");
        if (p_level) {
            int lvl = atoi(p_level + 6);
            if (lvl >= 0 && lvl <= 100) g_dev_info.battery_level = (uint8_t)lvl;
        }
        char* p_status = strstr(bat_buf, "status:");
        if (p_status) {
            int st = atoi(p_status + 7);
            g_dev_info.is_charging = (st == 2); // 2 = BATTERY_STATUS_CHARGING
        }
    }

    // 3. Screen State
    char pwr_buf[256] = {0};
    if (micro_adb_exec_shell("dumpsys power", pwr_buf, sizeof(pwr_buf)) == MICRO_ADB_OK) {
        if (strstr(pwr_buf, "mHoldingDisplaySuspendBlocker=true") || strstr(pwr_buf, "Display Power: state=ON")) {
            g_dev_info.screen_on = true;
        } else if (strstr(pwr_buf, "mHoldingDisplaySuspendBlocker=false") || strstr(pwr_buf, "Display Power: state=OFF")) {
            g_dev_info.screen_on = false;
        }
    }

    return MICRO_ADB_OK;
}
