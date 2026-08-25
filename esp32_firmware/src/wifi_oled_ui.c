/**
 * @file wifi_oled_ui.c
 * @brief Ultra-Optimized Dual-Display UI Engine for ESP32-S3 (N16R8) with Bullet Security Suite
 * @details Deauth IDS Monitor, Probe Request Sniffer, Matrix Rain, RF Sniffer, BLE Radar, FFT & Linux Shell.
 */

#include "wifi_oled_ui.h"
#include "logo_bitmap.h"
#include "drivers/pcap_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#define BULLET_DESKTOP_BUILD 1
#elif defined(_WIN32) || defined(__WIN32__) || defined(__x86_64__) || defined(_M_X64)
#define EXPORT __declspec(dllexport)
#define BULLET_DESKTOP_BUILD 1
#else
#define EXPORT
#include <esp_system.h>
#include <esp_heap_caps.h>
#endif

EXPORT void oled_init(void);
EXPORT void oled_set_disp_mode(int mode);

// Display Resolutions
#define OLED_W 128
#define OLED_H 64
#define MAX_DISP_W 480
#define MAX_DISP_H 320

static int g_disp_w = 240;
static int g_disp_h = 240;
static disp_mode_t g_disp_mode = DISP_MODE_IPS_240x240;

// ============================================================================
// RAM OPTIMIZATION: Framebuffer in 8MB Octal PSRAM on ESP32 or Static on PC
// ============================================================================
#if defined(BULLET_DESKTOP_BUILD)
static uint32_t g_fb_static[MAX_DISP_W * MAX_DISP_H];
static uint32_t* g_fb_rgba = g_fb_static;
#else
static uint32_t* g_fb_rgba = NULL;
static uint32_t  g_fb_fallback[OLED_W * OLED_H];
#endif

// ============================================================================
// COLOR PALETTES (Guaranteed 0xFF Alpha Byte)
// ============================================================================
#define IPS_BG_COLOR        0xFF0C0805
#define IPS_CARD_BG         0xFF1C140E
#define IPS_CARD_BORDER     0xFF36271A
#define IPS_CARD_HOVER      0xFF2B1F15
#define IPS_ACCENT_GLACIER  0xFFFFD670
#define IPS_ACCENT_EMERALD  0xFF7BEB38
#define IPS_ACCENT_AMBER    0xFF24B8FB
#define IPS_ACCENT_ROSE     0xFF7171F8
#define IPS_TEXT_PRIMARY    0xFFFAF8F5
#define IPS_TEXT_SECONDARY  0xFFBDA394
#define IPS_TEXT_MUTED      0xFF6E5A47

#define COLOR_OLED_CYAN     0xFFFFE500
#define COLOR_OLED_WHITE    0xFFFFFFFF
#define COLOR_OLED_AMBER    0xFF00B8FF
#define COLOR_BLACK         0xFF000000

static uint32_t g_active_color = COLOR_OLED_CYAN;

// Terminal History Lines Buffer
#define TERM_MAX_LINES 14
static char g_term_lines[TERM_MAX_LINES][48];
static int  g_term_line_count = 0;
static char g_input_buf[36] = "";
static int  g_input_len = 0;
static void term_print(const char* text);

// ============================================================================
// HARDWARE & SECURITY DATA BUFFERS
// ============================================================================
static wifi_net_item_t g_networks[MAX_NETWORKS_CAPACITY];
static int             g_net_count = 0;

static ble_device_t    g_ble_devices[MAX_BLE_CAPACITY];
static int             g_ble_count = 0;

// Bullet Feature 1: Deauth Attack Alerts
static deauth_alert_t  g_deauth_alerts[MAX_DEAUTH_ALERTS];
static int             g_deauth_count = 0;
static uint32_t        g_total_deauth_packets = 0;

// Bullet Feature 2: Probe Request Sniffer
static probe_item_t    g_probes[MAX_PROBE_CAPACITY];
static int             g_probe_count = 0;
static int             g_probe_scroll_idx = 0;

// Bullet Feature 3: Matrix Rain Columns State
#define MATRIX_COLS 30
static int   g_matrix_y[MATRIX_COLS];
static int   g_matrix_speed[MATRIX_COLS];
static char  g_matrix_chars[MATRIX_COLS][16];

// 2.4GHz RF Channel Packet Histogram (Channels 1-13)
static uint16_t g_channel_activity[14] = {0};
static uint16_t g_channel_peaks[14] = {0};
static uint32_t g_total_packets = 0;
static int      g_current_sniff_channel = 1;

// 16-Band FFT Audio Visualizer State
static float g_fft_bands[16] = {0};
static float g_fft_peaks[16] = {0};

// Retro Pong Difficulty & State
typedef enum {
    PONG_DIFF_EASY = 0,
    PONG_DIFF_NORMAL,
    PONG_DIFF_HARD,
    PONG_DIFF_CYBER,
    PONG_DIFF_COUNT
} pong_difficulty_t;

typedef enum {
    PONG_STATE_SELECT_DIFF = 0,
    PONG_STATE_PLAYING,
    PONG_STATE_GAME_OVER
} pong_state_t;

typedef struct {
    pong_state_t state;
    pong_difficulty_t diff;
    int   diff_select_idx;
    float paddle_y;
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    int   player_score;
    int   ai_score;
    float ai_paddle_y;
    bool  ball_in_play;
    int   rally_count;
} pong_game_t;

static pong_game_t g_pong = {
    .state = PONG_STATE_SELECT_DIFF,
    .diff = PONG_DIFF_NORMAL,
    .diff_select_idx = 1,
    .paddle_y = 120.0f,
    .ball_x = 120.0f,
    .ball_y = 120.0f,
    .ball_vx = 3.2f,
    .ball_vy = 2.0f,
    .player_score = 0,
    .ai_score = 0,
    .ai_paddle_y = 120.0f,
    .ball_in_play = false,
    .rally_count = 0
};

// CyberKart Turbo State (Auto-Gas, 1-Knob Steering, Nitro Boost)
#define MAX_KART_RIVALS 4
#define MAX_KART_COINS 3

typedef struct {
    float x, y;
    float speed_offset;
    int   type; // 0=Cyan Roadster, 1=Yellow Buggy, 2=Red Racer
    bool  active;
} kart_rival_t;

typedef struct {
    float x, y;
    bool  active;
} kart_coin_t;

typedef struct {
    float player_x;
    float target_x;
    float speed_kmh;
    float distance_m;
    float road_scroll_y;
    int   score;
    int   high_score;
    int   coins_collected;
    int   nitro_pct;
    bool  nitro_active;
    int   nitro_timer;
    bool  game_over;
    int   crash_tick;
    bool  initialized;
    kart_rival_t rivals[MAX_KART_RIVALS];
    kart_coin_t  coins[MAX_KART_COINS];
} kart_game_t;

static kart_game_t g_kart = {
    .player_x = 120.0f,
    .target_x = 120.0f,
    .speed_kmh = 95.0f,
    .distance_m = 0.0f,
    .road_scroll_y = 0.0f,
    .score = 0,
    .high_score = 0,
    .coins_collected = 0,
    .nitro_pct = 60,
    .nitro_active = false,
    .nitro_timer = 0,
    .game_over = false,
    .crash_tick = 0,
    .initialized = false
};

// Google Chrome T-Rex Dinosaur Runner State
#define MAX_DINO_OBSTACLES 4
#define MAX_DINO_CLOUDS 3

typedef struct {
    float x;
    int   type; // 0=Small Cactus, 1=Large Cactus, 2=Double Cactus, 3=Pterodactyl
    float fly_y;
    bool  active;
} dino_obstacle_t;

typedef struct {
    float x, y;
    float speed;
} dino_cloud_t;

typedef struct {
    float player_y;
    float player_vy;
    bool  is_jumping;
    bool  is_ducking;
    int   duck_timer;
    float run_speed;
    float distance_m;
    int   score;
    int   high_score;
    bool  game_over;
    int   crash_tick;
    int   leg_anim_tick;
    bool  night_mode;
    bool  initialized;
    dino_obstacle_t obstacles[MAX_DINO_OBSTACLES];
    dino_cloud_t    clouds[MAX_DINO_CLOUDS];
} dino_game_t;

static dino_game_t g_dino = {
    .player_y = 0.0f,
    .player_vy = 0.0f,
    .is_jumping = false,
    .is_ducking = false,
    .duck_timer = 0,
    .run_speed = 3.8f,
    .distance_m = 0.0f,
    .score = 0,
    .high_score = 0,
    .game_over = false,
    .crash_tick = 0,
    .leg_anim_tick = 0,
    .night_mode = false,
    .initialized = false
};

typedef struct {
    char ssid[33];
    char ip[16];
    char gateway[16];
    char mac[18];
    int8_t rssi;
    bool is_connected;
    char chip_model[24];
    uint8_t chip_cores;
    uint16_t cpu_freq_mhz;
    uint32_t total_psram_bytes;
    uint32_t free_psram_bytes;
    uint32_t total_sram_bytes;
    uint32_t free_heap_bytes;
    uint32_t total_flash_bytes;
    uint32_t free_flash_bytes;
    char arch_name[16];
    float chip_temp_c;
    uint32_t uptime_sec;
    bool is_scanning;
    float temp_history[24];
    int   temp_hist_idx;
} hw_telemetry_t;

static hw_telemetry_t g_telemetry = {
    .ssid = "Disconnected",
    .ip = "0.0.0.0",
    .gateway = "0.0.0.0",
    .mac = "00:00:00:00:00:00",
    .rssi = 0,
    .is_connected = false,
    .chip_model = "ESP32-S3",
    .chip_cores = 2,
    .cpu_freq_mhz = 240,
    .total_psram_bytes = 8388608,
    .free_psram_bytes = 7962624,
    .total_sram_bytes = 327680,
    .free_heap_bytes = 240000,
    .total_flash_bytes = 16777216,
    .free_flash_bytes = 15900000,
    .arch_name = "Xtensa LX7",
    .chip_temp_c = 34.2f,
    .uptime_sec = 0,
    .is_scanning = false,
    .temp_history = {32.0f, 32.2f, 32.5f, 33.0f, 33.4f, 33.8f, 34.0f, 34.2f, 34.5f},
    .temp_hist_idx = 9
};

// Engine State Machine
typedef struct {
    oled_view_t view;
    oled_lang_t lang;
    int main_index;
    int wifi_index;
    int net_index;
    int settings_index;
    float cursor_y;
    float target_cursor_y;
    int boot_tick;
    int scan_tick;
    int connect_tick;
    int tick;
} oled_c_engine_t;

static oled_c_engine_t g_engine = {
    .view = OLED_VIEW_BOOT,
    .lang = LANG_EN,
    .main_index = 0,
    .wifi_index = 0,
    .net_index = 0,
    .settings_index = 0,
    .cursor_y = 12.0f,
    .target_cursor_y = 12.0f,
    .boot_tick = 0,
    .scan_tick = 0,
    .connect_tick = 0,
    .tick = 0
};

static char g_ap_password[16] = "bullet9x";

EXPORT void wifi_ui_init_ap_password(void) {
    if (strcmp(g_ap_password, "bullet9x") == 0) {
        const char charset[] = "23456789abcdefghjkmnpqrstuvwxyz";
        for (int i = 0; i < 8; i++) {
            g_ap_password[i] = charset[rand() % (sizeof(charset) - 1)];
        }
        g_ap_password[8] = '\0';
    }
}

EXPORT const char* wifi_ui_get_ap_password(void) {
    return g_ap_password;
}

EXPORT void wifi_ui_set_ap_password(const char* pass) {
    if (pass && strlen(pass) > 0) {
        strncpy(g_ap_password, pass, sizeof(g_ap_password) - 1);
        g_ap_password[sizeof(g_ap_password) - 1] = '\0';
    }
}

EXPORT void wifi_ui_regenerate_ap_password(void) {
    const char charset[] = "23456789abcdefghjkmnpqrstuvwxyz";
    for (int i = 0; i < 8; i++) {
        g_ap_password[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    g_ap_password[8] = '\0';
}

// Main Menu Items (Clean & Intuitive with Subtitles)
typedef struct {
    const char* title_en;
    const char* title_ru;
    const char* sub_en;
    const char* sub_ru;
    uint32_t icon_col;
} menu_item_info_t;

static const menu_item_info_t g_main_menu_info[] = {
    {"Wi-Fi Scanner",   "Поиск Wi-Fi",       "Scan & list 2.4GHz APs",    "Сканирование сетей",        IPS_ACCENT_GLACIER},
    {"Hotspot Portal",  "Точка Доступа",     "Phone control & Web Shell", "Управление с телефона",    IPS_ACCENT_AMBER},
    {"Attack Detector", "Детектор Атак",     "Wi-Fi IDS & Deauth alarm",  "IDS защита от глушения",    IPS_ACCENT_ROSE},
    {"Probe Sniffer",   "Probe Сканер",      "Sniff nearby devices",      "Поиск смартфонов рядом",    IPS_ACCENT_AMBER},
    {"Matrix Rain",     "Матрица",           "Digital rain animation",    "Цифровой дождь",            IPS_ACCENT_EMERALD},
    {"RF 2.4G Monitor", "RF 2.4G Эфир",      "Channel activity waterfall","Гистограмма занятости",     IPS_ACCENT_GLACIER},
    {"BLE Radar",       "BLE Радар",         "AirTags & beacons scanner", "Поиск AirTag и маяков",     IPS_ACCENT_EMERALD},
    {"Audio Spectrum",  "Спектр Звука",      "16-band audio FFT visual",  "16 полос эквалайзера",      IPS_ACCENT_AMBER},
    {"Retro Kart",      "Мини Гонки",        "1-knob arcade racing",      "Гонки на крутилке",         IPS_ACCENT_AMBER},
    {"Chrome Dino",     "Динозаврик",        "Retro T-Rex endless jump",  "Прыжки через кактусы",      IPS_ACCENT_EMERALD},
    {"Retro Pong",      "Ретро Понг",        "Knob paddle arcade game",   "Аркада под крутилку",       IPS_ACCENT_GLACIER},
    {"System Specs",    "Статус Системы",    "CPU, 8MB PSRAM & temp",     "Загрузка, RAM и датчики",   IPS_ACCENT_EMERALD},
    {"Device Scanner",  "Сканер Модулей",    "I2C, SPI & USB bus probe",  "Поиск CC1101, RFID, I2C",   IPS_ACCENT_GLACIER},
    {"Sub-GHz RF",      "Sub-GHz Радио",     "RAW record, replay & 433M", "Запись и реплей 433/868M",  IPS_ACCENT_AMBER},
    {"Micro-ADB Tool",  "Микро-ADB",         "Android remote control & shell", "Пульт и команды Android", IPS_ACCENT_EMERALD},
    {"CLI Terminal",    "Терминал",          "Shell commands & tools",    "Командная строка",          IPS_ACCENT_GLACIER},
    {"Settings",        "Настройки",         "Language, theme & AP pass", "Язык, тема и пароль точки", IPS_TEXT_SECONDARY},
    {"Reboot Device",   "Перезагрузка",      "Restart ESP32 chip",        "Перезапуск контроллера",    IPS_ACCENT_ROSE}
};
#define MAIN_MENU_COUNT (sizeof(g_main_menu_info) / sizeof(g_main_menu_info[0]))

static const char* str_wifi_menu_en[] = {
    "Scan Wi-Fi",
    "Hotspot (SoftAP)",
    "Connection HUD",
    "[ Back to Menu ]"
};

static const char* str_wifi_menu_ru[] = {
    "Поиск Сетей",
    "Точка Доступа",
    "Статус Wi-Fi",
    "[ Назад в Меню ]"
};
#define WIFI_MENU_COUNT 4

// ============================================================================
// GRAPHICS PRIMITIVES
// ============================================================================
static void c_draw_pixel(int x, int y, uint32_t color) {
    if (!g_fb_rgba) return;
    if (x >= 0 && x < g_disp_w && y >= 0 && y < g_disp_h) {
        g_fb_rgba[y * g_disp_w + x] = color | 0xFF000000;
    }
}

static void c_draw_rect_fill(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            c_draw_pixel(i, j, color);
        }
    }
}

static void c_draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; i++) { c_draw_pixel(i, y, color); c_draw_pixel(i, y + h - 1, color); }
    for (int j = y; j < y + h; j++) { c_draw_pixel(x, j, color); c_draw_pixel(x + w - 1, j, color); }
}

static void c_draw_rounded_card(int x, int y, int w, int h, int r, uint32_t bg_col, uint32_t border_col) {
    c_draw_rect_fill(x + r, y, w - 2 * r, h, bg_col);
    c_draw_rect_fill(x, y + r, w, h - 2 * r, bg_col);

    c_draw_rect_fill(x + 1, y + 1, r, r, bg_col);
    c_draw_rect_fill(x + w - 1 - r, y + 1, r, r, bg_col);
    c_draw_rect_fill(x + 1, y + h - 1 - r, r, r, bg_col);
    c_draw_rect_fill(x + w - 1 - r, y + h - 1 - r, r, r, bg_col);

    if (border_col != 0) {
        for (int i = x + r; i < x + w - r; i++) { c_draw_pixel(i, y, border_col); c_draw_pixel(i, y + h - 1, border_col); }
        for (int j = y + r; j < y + h - r; j++) { c_draw_pixel(x, j, border_col); c_draw_pixel(x + w - 1, j, border_col); }
        c_draw_pixel(x + 1, y + 1, border_col);
        c_draw_pixel(x + w - 2, y + 1, border_col);
        c_draw_pixel(x + 1, y + h - 2, border_col);
        c_draw_pixel(x + w - 2, y + h - 2, border_col);
    }
}

static void c_draw_circle(int cx, int cy, int r, uint32_t color) {
    if (r <= 0) return;
    for (int deg = 0; deg < 360; deg += 3) {
        float rad = deg * 3.14159265f / 180.0f;
        int x = cx + (int)(cosf(rad) * r);
        int y = cy + (int)(sinf(rad) * r);
        c_draw_pixel(x, y, color);
    }
}

static void c_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        c_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// 5x7 Font
static const uint8_t font5x7_ascii[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['"'] = {0x00, 0x07, 0x00, 0x07, 0x00},
    ['#'] = {0x14, 0x7F, 0x14, 0x7F, 0x14},
    ['$'] = {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['&'] = {0x36, 0x49, 0x55, 0x22, 0x50},
    ['\''] = {0x00, 0x05, 0x03, 0x00, 0x00},
    ['('] = {0x00, 0x1C, 0x22, 0x41, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1C, 0x00},
    ['*'] = {0x14, 0x08, 0x3E, 0x08, 0x14},
    ['+'] = {0x08, 0x08, 0x3E, 0x08, 0x08},
    [','] = {0x00, 0x50, 0x30, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00},
    [';'] = {0x00, 0x56, 0x36, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x41, 0x00},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14},
    ['>'] = {0x00, 0x41, 0x22, 0x14, 0x08},
    ['?'] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['@'] = {0x32, 0x49, 0x79, 0x41, 0x3E},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
    ['['] = {0x00, 0x7F, 0x41, 0x41, 0x00},
    [']'] = {0x00, 0x41, 0x41, 0x7F, 0x00},
    ['_'] = {0x40, 0x40, 0x40, 0x40, 0x40},
    ['a'] = {0x20, 0x54, 0x54, 0x54, 0x78},
    ['b'] = {0x7F, 0x48, 0x44, 0x44, 0x38},
    ['c'] = {0x38, 0x44, 0x44, 0x44, 0x20},
    ['d'] = {0x38, 0x44, 0x44, 0x48, 0x7F},
    ['e'] = {0x38, 0x54, 0x54, 0x54, 0x18},
    ['f'] = {0x08, 0x7E, 0x09, 0x01, 0x02},
    ['g'] = {0x0C, 0x52, 0x52, 0x52, 0x3E},
    ['h'] = {0x7F, 0x08, 0x04, 0x04, 0x78},
    ['i'] = {0x00, 0x44, 0x7D, 0x40, 0x00},
    ['j'] = {0x20, 0x40, 0x44, 0x3D, 0x00},
    ['k'] = {0x7F, 0x10, 0x28, 0x44, 0x00},
    ['l'] = {0x00, 0x41, 0x7F, 0x40, 0x00},
    ['m'] = {0x7C, 0x04, 0x18, 0x04, 0x78},
    ['n'] = {0x7C, 0x08, 0x04, 0x04, 0x78},
    ['o'] = {0x38, 0x44, 0x44, 0x44, 0x38},
    ['p'] = {0x7C, 0x14, 0x14, 0x14, 0x08},
    ['q'] = {0x08, 0x14, 0x14, 0x18, 0x7C},
    ['r'] = {0x7C, 0x08, 0x04, 0x04, 0x08},
    ['s'] = {0x48, 0x54, 0x54, 0x54, 0x20},
    ['t'] = {0x04, 0x3F, 0x44, 0x40, 0x20},
    ['u'] = {0x3C, 0x40, 0x40, 0x20, 0x7C},
    ['v'] = {0x1C, 0x20, 0x40, 0x20, 0x1C},
    ['w'] = {0x3C, 0x40, 0x30, 0x40, 0x3C},
    ['x'] = {0x44, 0x28, 0x10, 0x28, 0x44},
    ['y'] = {0x0C, 0x50, 0x50, 0x50, 0x3C},
    ['z'] = {0x44, 0x64, 0x54, 0x4C, 0x44},
    ['|'] = {0x00, 0x00, 0x7F, 0x00, 0x00}
};

// 5x7 Cyrillic Font (А-Я, Ё and а-я, ё)
static const uint8_t font5x7_cyr_upper[33][5] = {
    [0]  = {0x7E, 0x11, 0x11, 0x11, 0x7E}, // А
    [1]  = {0x7F, 0x49, 0x49, 0x49, 0x31}, // Б
    [2]  = {0x7F, 0x49, 0x49, 0x49, 0x36}, // В
    [3]  = {0x7F, 0x01, 0x01, 0x01, 0x01}, // Г
    [4]  = {0x60, 0x7F, 0x09, 0x7F, 0x60}, // Д
    [5]  = {0x7F, 0x49, 0x49, 0x49, 0x41}, // Е
    [6]  = {0x77, 0x08, 0x7F, 0x08, 0x77}, // Ж
    [7]  = {0x41, 0x49, 0x49, 0x49, 0x36}, // З
    [8]  = {0x7F, 0x20, 0x10, 0x08, 0x7F}, // И
    [9]  = {0x7F, 0x21, 0x12, 0x09, 0x7F}, // Й
    [10] = {0x7F, 0x08, 0x14, 0x22, 0x41}, // К
    [11] = {0x40, 0x3E, 0x01, 0x01, 0x7F}, // Л
    [12] = {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // М
    [13] = {0x7F, 0x08, 0x08, 0x08, 0x7F}, // Н
    [14] = {0x3E, 0x41, 0x41, 0x41, 0x3E}, // О
    [15] = {0x7F, 0x01, 0x01, 0x01, 0x7F}, // П
    [16] = {0x7F, 0x09, 0x09, 0x09, 0x06}, // Р
    [17] = {0x3E, 0x41, 0x41, 0x41, 0x22}, // С
    [18] = {0x01, 0x01, 0x7F, 0x01, 0x01}, // Т
    [19] = {0x07, 0x08, 0x70, 0x08, 0x07}, // У
    [20] = {0x1C, 0x22, 0x7F, 0x22, 0x1C}, // Ф
    [21] = {0x63, 0x14, 0x08, 0x14, 0x63}, // Х
    [22] = {0x7F, 0x40, 0x40, 0x7F, 0xC0}, // Ц
    [23] = {0x07, 0x08, 0x08, 0x08, 0x7F}, // Ч
    [24] = {0x7F, 0x40, 0x7F, 0x40, 0x7F}, // Ш
    [25] = {0x7F, 0x40, 0x7F, 0x40, 0xFF}, // Щ
    [26] = {0x01, 0x7F, 0x48, 0x48, 0x30}, // Ъ
    [27] = {0x7F, 0x48, 0x30, 0x00, 0x7F}, // Ы
    [28] = {0x7F, 0x48, 0x48, 0x48, 0x30}, // Ь
    [29] = {0x22, 0x41, 0x49, 0x49, 0x3E}, // Э
    [30] = {0x7F, 0x08, 0x3E, 0x41, 0x3E}, // Ю
    [31] = {0x46, 0x29, 0x19, 0x09, 0x7F}, // Я
    [32] = {0x7D, 0x4A, 0x4A, 0x4A, 0x41}  // Ё
};

static const uint8_t font5x7_cyr_lower[33][5] = {
    [0]  = {0x20, 0x54, 0x54, 0x54, 0x78}, // а
    [1]  = {0x3E, 0x49, 0x49, 0x49, 0x20}, // б
    [2]  = {0x7C, 0x54, 0x54, 0x54, 0x28}, // в
    [3]  = {0x7C, 0x04, 0x04, 0x04, 0x04}, // г
    [4]  = {0x60, 0x7C, 0x14, 0x7C, 0x60}, // д
    [5]  = {0x38, 0x54, 0x54, 0x54, 0x18}, // е
    [6]  = {0x6C, 0x10, 0x7C, 0x10, 0x6C}, // ж
    [7]  = {0x44, 0x54, 0x54, 0x54, 0x28}, // з
    [8]  = {0x7C, 0x20, 0x10, 0x08, 0x7C}, // и
    [9]  = {0x7C, 0x22, 0x14, 0x08, 0x7C}, // й
    [10] = {0x7C, 0x10, 0x28, 0x44, 0x00}, // к
    [11] = {0x40, 0x38, 0x04, 0x04, 0x7C}, // л
    [12] = {0x7C, 0x08, 0x10, 0x08, 0x7C}, // м
    [13] = {0x7C, 0x10, 0x10, 0x10, 0x7C}, // н
    [14] = {0x38, 0x44, 0x44, 0x44, 0x38}, // о
    [15] = {0x7C, 0x04, 0x04, 0x04, 0x7C}, // п
    [16] = {0x7C, 0x14, 0x14, 0x14, 0x08}, // р
    [17] = {0x38, 0x44, 0x44, 0x44, 0x20}, // с
    [18] = {0x04, 0x04, 0x7C, 0x04, 0x04}, // т
    [19] = {0x0C, 0x50, 0x50, 0x50, 0x3C}, // у
    [20] = {0x18, 0x24, 0x7E, 0x24, 0x18}, // ф
    [21] = {0x44, 0x28, 0x10, 0x28, 0x44}, // х
    [22] = {0x7C, 0x40, 0x40, 0x7C, 0xC0}, // ц
    [23] = {0x0C, 0x10, 0x10, 0x10, 0x7C}, // ч
    [24] = {0x7C, 0x40, 0x7C, 0x40, 0x7C}, // ш
    [25] = {0x7C, 0x40, 0x7C, 0x40, 0xFC}, // щ
    [26] = {0x04, 0x7C, 0x50, 0x50, 0x20}, // ъ
    [27] = {0x7C, 0x50, 0x20, 0x00, 0x7C}, // ы
    [28] = {0x7C, 0x50, 0x50, 0x50, 0x20}, // ь
    [29] = {0x28, 0x44, 0x54, 0x54, 0x38}, // э
    [30] = {0x7C, 0x10, 0x38, 0x44, 0x38}, // ю
    [31] = {0x48, 0x34, 0x14, 0x14, 0x7C}, // я
    [32] = {0x3A, 0x55, 0x55, 0x55, 0x1A}  // ё
};

static void c_draw_char(int x, int y, char c, uint32_t color) {
    uint8_t ch = (uint8_t)c;
    if (ch >= sizeof(font5x7_ascii) / sizeof(font5x7_ascii[0])) ch = ' ';
    const uint8_t* col_data = font5x7_ascii[ch];

    for (int col = 0; col < 5; col++) {
        uint8_t line = col_data[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                c_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void c_draw_cyrillic_char(int x, int y, uint8_t byte1, uint8_t byte2, uint32_t color) {
    const uint8_t* col_data = NULL;

    if (byte1 == 0xD0) {
        if (byte2 == 0x81) {
            col_data = font5x7_cyr_upper[32]; // Ё
        } else if (byte2 >= 0x90 && byte2 <= 0xAF) {
            col_data = font5x7_cyr_upper[byte2 - 0x90]; // А..Я
        } else if (byte2 >= 0xB0 && byte2 <= 0xBF) {
            col_data = font5x7_cyr_lower[byte2 - 0xB0]; // а..п
        }
    } else if (byte1 == 0xD1) {
        if (byte2 >= 0x80 && byte2 <= 0x8F) {
            col_data = font5x7_cyr_lower[16 + (byte2 - 0x80)]; // р..я
        } else if (byte2 == 0x91) {
            col_data = font5x7_cyr_lower[32]; // ё
        }
    }

    if (!col_data) col_data = font5x7_ascii['?'];

    for (int col = 0; col < 5; col++) {
        uint8_t line = col_data[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                c_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void c_draw_text(int x, int y, const char* str, uint32_t color) {
    int cur_x = x;
    const uint8_t* s = (const uint8_t*)str;
    while (*s) {
        if (*s < 0x80) {
            c_draw_char(cur_x, y, (char)*s, color);
            cur_x += 6;
            s++;
        } else if (*s == 0xD0 || *s == 0xD1) {
            uint8_t b1 = *s++;
            uint8_t b2 = *s ? *s++ : 0;
            c_draw_cyrillic_char(cur_x, y, b1, b2, color);
            cur_x += 6;
        } else {
            s++;
        }
    }
}

static void c_draw_icon_ips(int x, int y, int icon_type, uint32_t color) {
    if (icon_type == 0) { // 0. Wi-Fi Scanner
        c_draw_circle(x + 7, y + 10, 2, color);
        c_draw_circle(x + 7, y + 10, 6, color);
        c_draw_circle(x + 7, y + 10, 10, color);
    } else if (icon_type == 1) { // 1. Hotspot Portal (Access Point Tower)
        c_draw_rect_fill(x + 5, y + 7, 4, 7, color);
        c_draw_line(x + 7, y + 2, x + 7, y + 6, IPS_ACCENT_AMBER);
        c_draw_circle(x + 7, y + 2, 2, 0xFFFFFFFF);
        c_draw_circle(x + 7, y + 2, 5, IPS_ACCENT_AMBER);
    } else if (icon_type == 2) { // 2. Attack Detector (Shield)
        c_draw_rect_outline(x + 2, y + 2, 10, 11, IPS_ACCENT_ROSE);
        c_draw_line(x + 4, y + 7, x + 10, y + 7, IPS_ACCENT_ROSE);
        c_draw_line(x + 7, y + 4, x + 7, y + 10, IPS_ACCENT_ROSE);
    } else if (icon_type == 3) { // 3. Probe Sniffer (Target)
        c_draw_circle(x + 7, y + 7, 5, IPS_ACCENT_AMBER);
        c_draw_pixel(x + 7, y + 7, 0xFFFFFFFF);
    } else if (icon_type == 4) { // 4. Matrix Rain
        c_draw_text(x + 1, y + 3, "01", IPS_ACCENT_EMERALD);
    } else if (icon_type == 5) { // 5. RF 2.4G Monitor
        c_draw_rect_fill(x + 1, y + 8, 2, 6, color);
        c_draw_rect_fill(x + 4, y + 5, 2, 9, color);
        c_draw_rect_fill(x + 7, y + 2, 2, 12, color);
    } else if (icon_type == 6) { // 6. BLE Radar
        c_draw_circle(x + 7, y + 7, 6, color);
        c_draw_pixel(x + 7, y + 7, IPS_ACCENT_EMERALD);
    } else if (icon_type == 7) { // 7. Audio Spectrum
        c_draw_rect_fill(x + 2, y + 3, 3, 10, IPS_ACCENT_GLACIER);
        c_draw_rect_fill(x + 6, y + 6, 3, 7, IPS_ACCENT_EMERALD);
        c_draw_rect_fill(x + 10, y + 1, 3, 12, IPS_ACCENT_AMBER);
    } else if (icon_type == 8) { // 8. Retro Kart
        c_draw_rect_fill(x + 4, y + 2, 6, 10, color);
        c_draw_rect_fill(x + 2, y + 5, 10, 4, IPS_ACCENT_AMBER);
        c_draw_pixel(x + 2, y + 3, 0xFFFFFFFF);
        c_draw_pixel(x + 11, y + 3, 0xFFFFFFFF);
        c_draw_pixel(x + 2, y + 10, 0xFFFFFFFF);
        c_draw_pixel(x + 11, y + 10, 0xFFFFFFFF);
    } else if (icon_type == 9) { // 9. Chrome Dino
        c_draw_rect_fill(x + 6, y + 2, 7, 5, color);
        c_draw_pixel(x + 8, y + 3, COLOR_BLACK);
        c_draw_rect_fill(x + 4, y + 6, 6, 5, color);
        c_draw_rect_fill(x + 2, y + 7, 3, 3, color);
        c_draw_rect_fill(x + 5, y + 11, 2, 3, color);
        c_draw_rect_fill(x + 8, y + 11, 2, 3, color);
    } else if (icon_type == 10) { // 10. Retro Pong
        c_draw_rect_fill(x + 1, y + 3, 2, 8, color);
        c_draw_rect_fill(x + 11, y + 5, 2, 8, color);
        c_draw_pixel(x + 6, y + 7, IPS_ACCENT_GLACIER);
    } else if (icon_type == 11) { // 11. System Specs
        c_draw_rect_outline(x + 2, y + 2, 10, 10, color);
        c_draw_rect_fill(x + 4, y + 4, 6, 6, IPS_ACCENT_EMERALD);
    } else if (icon_type == 12) { // 12. Device Scanner
        c_draw_rect_fill(x + 3, y + 3, 8, 8, color);
        c_draw_rect_fill(x + 1, y + 4, 2, 2, IPS_ACCENT_EMERALD);
        c_draw_rect_fill(x + 1, y + 8, 2, 2, IPS_ACCENT_EMERALD);
        c_draw_rect_fill(x + 11, y + 4, 2, 2, IPS_ACCENT_EMERALD);
        c_draw_rect_fill(x + 11, y + 8, 2, 2, IPS_ACCENT_EMERALD);
    } else if (icon_type == 13) { // 13. Sub-GHz RF (Antenna with Radio Waves)
        c_draw_line(x + 7, y + 3, x + 7, y + 12, color);
        c_draw_line(x + 4, y + 3, x + 10, y + 3, color);
        c_draw_pixel(x + 2, y + 2, IPS_ACCENT_AMBER);
        c_draw_pixel(x + 12, y + 2, IPS_ACCENT_AMBER);
        c_draw_pixel(x + 7, y + 1, 0xFFFFFFFF);
    } else if (icon_type == 14) { // 14. Micro-ADB (Android Robot Head)
        c_draw_rect_fill(x + 3, y + 5, 8, 7, color);
        c_draw_pixel(x + 4, y + 7, COLOR_BLACK);
        c_draw_pixel(x + 9, y + 7, COLOR_BLACK);
        c_draw_line(x + 3, y + 2, x + 4, y + 4, IPS_ACCENT_EMERALD);
        c_draw_line(x + 10, y + 2, x + 9, y + 4, IPS_ACCENT_EMERALD);
    } else if (icon_type == 15) { // 15. CLI Terminal
        c_draw_text(x, y + 3, ">_", color);
    } else if (icon_type == 16) { // 16. Settings (Gear / Sliders)
        c_draw_rect_outline(x + 2, y + 3, 10, 8, color);
        c_draw_rect_fill(x + 4, y + 5, 2, 4, IPS_ACCENT_GLACIER);
        c_draw_rect_fill(x + 8, y + 5, 2, 4, IPS_ACCENT_AMBER);
    } else if (icon_type == 17) { // 17. Reboot Device (Power symbol)
        c_draw_circle(x + 7, y + 7, 5, color);
        c_draw_rect_fill(x + 6, y + 1, 2, 6, color);
    }
}

// ============================================================================
// HARDWARE & SECURITY DATA FEEDERS
// ============================================================================
EXPORT void wifi_ui_clear_networks(void) { g_net_count = 0; }

EXPORT void wifi_ui_add_network(const char* ssid, int8_t rssi, bool is_secure) {
    if (g_net_count < MAX_NETWORKS_CAPACITY) {
        strncpy(g_networks[g_net_count].ssid, ssid, 32);
        g_networks[g_net_count].ssid[32] = '\0';
        g_networks[g_net_count].rssi = rssi;
        g_networks[g_net_count].is_secure = is_secure;
        g_net_count++;
    }
}

EXPORT void wifi_ui_set_connection_info(const char* ssid, const char* ip, const char* gateway, const char* mac, int8_t rssi) {
    strncpy(g_telemetry.ssid, ssid, 32);
    strncpy(g_telemetry.ip, ip, 15);
    strncpy(g_telemetry.gateway, gateway, 15);
    strncpy(g_telemetry.mac, mac, 17);
    g_telemetry.rssi = rssi;
    g_telemetry.is_connected = (strcmp(ip, "0.0.0.0") != 0 && strlen(ip) > 0);
}

EXPORT void wifi_ui_set_sys_telemetry(float temp_c, uint32_t free_psram, uint32_t free_heap, uint32_t uptime_sec) {
    g_telemetry.chip_temp_c = temp_c;
    g_telemetry.free_psram_bytes = free_psram;
    g_telemetry.free_heap_bytes = free_heap;
    g_telemetry.uptime_sec = uptime_sec;

    if (temp_c > 0.0f) {
        g_telemetry.temp_history[g_telemetry.temp_hist_idx] = temp_c;
        g_telemetry.temp_hist_idx = (g_telemetry.temp_hist_idx + 1) % 24;
    }
}

EXPORT void wifi_ui_set_sys_telemetry_ex(
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
) {
    if (chip_model && strlen(chip_model) > 0) {
        strncpy(g_telemetry.chip_model, chip_model, sizeof(g_telemetry.chip_model) - 1);
        g_telemetry.chip_model[sizeof(g_telemetry.chip_model) - 1] = '\0';
    }
    g_telemetry.chip_cores = chip_cores > 0 ? chip_cores : 1;
    g_telemetry.cpu_freq_mhz = cpu_freq_mhz > 0 ? cpu_freq_mhz : 240;
    g_telemetry.total_psram_bytes = total_psram;
    g_telemetry.free_psram_bytes = free_psram;
    g_telemetry.total_sram_bytes = total_sram > 0 ? total_sram : 327680;
    g_telemetry.free_heap_bytes = free_sram;
    g_telemetry.total_flash_bytes = total_flash > 0 ? total_flash : 4194304;
    g_telemetry.free_flash_bytes = free_flash;
    g_telemetry.chip_temp_c = temp_c;
    g_telemetry.uptime_sec = uptime_sec;

    // Detect architecture name from chip model
    if (strstr(g_telemetry.chip_model, "C3") || strstr(g_telemetry.chip_model, "C6") || strstr(g_telemetry.chip_model, "C2")) {
        strcpy(g_telemetry.arch_name, "RISC-V");
    } else if (strstr(g_telemetry.chip_model, "S3") || strstr(g_telemetry.chip_model, "S2")) {
        strcpy(g_telemetry.arch_name, "Xtensa LX7");
    } else {
        strcpy(g_telemetry.arch_name, "Xtensa LX6");
    }

    if (temp_c > 0.0f) {
        g_telemetry.temp_history[g_telemetry.temp_hist_idx] = temp_c;
        g_telemetry.temp_hist_idx = (g_telemetry.temp_hist_idx + 1) % 24;
    }
}

EXPORT void wifi_ui_set_scan_status(bool is_scanning) {
    g_telemetry.is_scanning = is_scanning;
    if (!is_scanning && g_engine.view == OLED_VIEW_SCANNING) {
        g_engine.view = OLED_VIEW_NETWORKS_LIST;
    }
}

EXPORT void wifi_ui_feed_sniffer_packet(int channel, int rssi) {
    if (channel >= 1 && channel <= 13) {
        g_channel_activity[channel] += 1;
        if (g_channel_activity[channel] > g_channel_peaks[channel]) {
            g_channel_peaks[channel] = g_channel_activity[channel];
        }
        g_total_packets++;
    }
}

EXPORT void wifi_ui_add_ble_device(const char* name, const char* mac, int8_t rssi, int type) {
    if (g_ble_count < MAX_BLE_CAPACITY) {
        strncpy(g_ble_devices[g_ble_count].name, name, 23);
        strncpy(g_ble_devices[g_ble_count].mac, mac, 17);
        g_ble_devices[g_ble_count].rssi = rssi;
        g_ble_devices[g_ble_count].type = type;
        g_ble_count++;
    }
}

EXPORT void wifi_ui_add_probe_request(const char* client_mac, const char* requested_ssid, int8_t rssi) {
    for (int i = 0; i < g_probe_count; i++) {
        if (strcmp(g_probes[i].client_mac, client_mac) == 0 && strcmp(g_probes[i].requested_ssid, requested_ssid) == 0) {
            g_probes[i].seen_count++;
            g_probes[i].rssi = rssi;
            return;
        }
    }
    if (g_probe_count < MAX_PROBE_CAPACITY) {
        strncpy(g_probes[g_probe_count].client_mac, client_mac, 17);
        strncpy(g_probes[g_probe_count].requested_ssid, requested_ssid, 31);
        g_probes[g_probe_count].rssi = rssi;
        g_probes[g_probe_count].seen_count = 1;
        g_probe_count++;
    }
}

EXPORT void wifi_ui_add_deauth_alert(const char* target_mac, const char* bssid, int channel, int8_t rssi) {
    if (g_deauth_count < MAX_DEAUTH_ALERTS) {
        strncpy(g_deauth_alerts[g_deauth_count].target_mac, target_mac, 17);
        strncpy(g_deauth_alerts[g_deauth_count].bssid, bssid, 17);
        g_deauth_alerts[g_deauth_count].channel = channel;
        g_deauth_alerts[g_deauth_count].rssi = rssi;
        g_deauth_alerts[g_deauth_count].burst_count = 1;
        g_deauth_count++;
    }
    g_total_deauth_packets++;
}

// ============================================================================
// BULLET FEATURE 1: WI-FI IDS & DEAUTH ATTACK DETECTOR VIEW
// ============================================================================
static void c_render_deauth_ids_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, is_ru ? "WI-FI IDS ДЕТЕКТОР АТАК" : "WI-FI IDS ATTACK MONITOR", IPS_TEXT_PRIMARY);

        bool is_alert = (g_deauth_count > 0 && (g_engine.tick / 20) % 2 == 0);
        uint32_t badge_col = is_alert ? IPS_ACCENT_ROSE : IPS_ACCENT_EMERALD;

        c_draw_rounded_card(g_disp_w - 85, 6, 75, 14, 3, IPS_BG_COLOR, badge_col);
        c_draw_text(g_disp_w - 79, 9, is_alert ? "! ATTACK !" : "GUARD OK", badge_col);

        int card_w = g_disp_w - 24;

        // Security Status Dashboard
        c_draw_rounded_card(12, 34, card_w, 42, 6, IPS_CARD_BG, is_alert ? IPS_ACCENT_ROSE : IPS_CARD_BORDER);
        char total_str[48];
        snprintf(total_str, sizeof(total_str), "DEAUTH PACKETS: %lu BURSTS", (unsigned long)g_total_deauth_packets);
        c_draw_text(22, 42, total_str, is_alert ? IPS_ACCENT_ROSE : IPS_ACCENT_EMERALD);
        c_draw_text(22, 56, is_ru ? "КАНАЛЫ: 1..13  СТАТУС: PROMISCUOUS IDS" : "CHANNELS: 1..13  STATUS: PROMISCUOUS IDS", IPS_ACCENT_EMERALD);

        // Alert List
        if (g_deauth_count == 0) {
            c_draw_rounded_card(12, 88, card_w, 80, 5, 0xFF050B08, IPS_CARD_BORDER);
            c_draw_text(20, 100, is_ru ? "ЭФИР ЧИСТ (АТАК НЕ ОБНАРУЖЕНО)" : "AIRSPACE SECURE (NO ATTACKS)", IPS_ACCENT_EMERALD);
            c_draw_text(20, 118, is_ru ? "Детектор слушает Deauth / Disassoc фреймы" : "Monitoring 802.11 Deauth / Disassoc frames", IPS_TEXT_MUTED);
            c_draw_text(20, 134, is_ru ? "при попытках глушения сетей рядом." : "across 2.4GHz Wi-Fi channels.", IPS_TEXT_MUTED);
        } else {
            c_draw_text(16, 84, is_ru ? "ОБНАРУЖЕННЫЕ АТАКИ:" : "LOGGED ATTACK TARGETS:", IPS_TEXT_MUTED);
            int start_y = 96;
            for (int i = 0; i < g_deauth_count && i < 4; i++) {
                int y = start_y + i * 28;
                c_draw_rounded_card(12, y, card_w, 25, 4, IPS_CARD_BG, IPS_CARD_BORDER);

                char alert_line1[48];
                snprintf(alert_line1, sizeof(alert_line1), "BSSID: %s (CH%d)", g_deauth_alerts[i].bssid, g_deauth_alerts[i].channel);
                c_draw_text(20, y + 4, alert_line1, IPS_TEXT_PRIMARY);

                char alert_line2[48];
                snprintf(alert_line2, sizeof(alert_line2), "TARGET: %s  %ddBm", g_deauth_alerts[i].target_mac, g_deauth_alerts[i].rssi);
                c_draw_text(20, y + 14, alert_line2, IPS_ACCENT_ROSE);
            }
        }

        c_draw_text(16, g_disp_h - 16, is_ru ? "[Кнопка] Назад в меню" : "[Press Knob] Return to Menu", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "WI-FI IDS GUARD", COLOR_BLACK);

        char line1[32];
        snprintf(line1, sizeof(line1), "DEAUTH PKTS: %lu", (unsigned long)g_total_deauth_packets);
        c_draw_text(2, 12, line1, g_active_color);

        if (g_deauth_count > 0) {
            char line2[32];
            snprintf(line2, sizeof(line2), "SRC: %s", g_deauth_alerts[0].bssid);
            c_draw_text(2, 24, line2, g_active_color);
            c_draw_text(2, 36, "STATUS: ATTACK DETECTED!", g_active_color);
        } else {
            c_draw_text(2, 24, "STATUS: ARMED & SAFE", g_active_color);
            c_draw_text(2, 36, "CHANNELS: 1..13 OK", COLOR_OLED_WHITE);
        }
        c_draw_text(2, 56, "[Hold] Back", g_active_color);
    }
}

// ============================================================================
// BULLET FEATURE 2: PROBE REQUEST & FOOTPRINT SNIFFER VIEW
// ============================================================================
static void c_render_probe_sniffer_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, "PROBE REQUEST SNIFFER", IPS_TEXT_PRIMARY);

        char cnt_str[16];
        snprintf(cnt_str, sizeof(cnt_str), "%d DEVICES", g_probe_count);
        c_draw_text(g_disp_w - 75, 10, cnt_str, IPS_ACCENT_AMBER);

        int card_w = g_disp_w - 24;

        if (g_probe_count == 0) {
            c_draw_rounded_card(12, 34, card_w, 80, 5, 0xFF050B08, IPS_CARD_BORDER);
            c_draw_text(20, 46, is_ru ? "СКАНИРОВАНИЕ ЗАПРОСОВ (PROBES)..." : "LISTENING FOR PROBE REQUESTS...", IPS_ACCENT_AMBER);
            c_draw_text(20, 64, is_ru ? "Смартфоны рядом, ищущие свои Wi-Fi сети," : "Nearby smartphones searching for saved APs", IPS_TEXT_MUTED);
            c_draw_text(20, 80, is_ru ? "отобразятся в этом списке с MAC и SSID." : "will appear here with MAC & requested SSID.", IPS_TEXT_MUTED);
        } else {
            int start_y = 34;
            const int item_h = 36;
            for (int i = 0; i < g_probe_count && i < 5; i++) {
                int y = start_y + i * (item_h + 4);
                bool selected = (i == g_probe_scroll_idx);

                c_draw_rounded_card(12, y, card_w, item_h, 5, selected ? IPS_CARD_HOVER : IPS_CARD_BG, selected ? IPS_ACCENT_GLACIER : IPS_CARD_BORDER);

                char line1[48];
                snprintf(line1, sizeof(line1), "SSID: \"%s\"", g_probes[i].requested_ssid);
                c_draw_text(22, y + 6, line1, selected ? IPS_TEXT_PRIMARY : IPS_ACCENT_GLACIER);

                char line2[48];
                snprintf(line2, sizeof(line2), "MAC: %s | %ddB (x%lu)", g_probes[i].client_mac, g_probes[i].rssi, (unsigned long)g_probes[i].seen_count);
                c_draw_text(22, y + 20, line2, IPS_TEXT_MUTED);
            }
        }

        c_draw_text(16, g_disp_h - 16, is_ru ? "[Крутилка] Список | [Кнопка] Назад" : "[Turn Knob] Scroll | [Btn] Back", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "PROBE SNIFFER", COLOR_BLACK);

        if (g_probe_count == 0) {
            c_draw_text(2, 20, "LISTENING 2.4G...", g_active_color);
            c_draw_text(2, 34, "WAITING PROBES", COLOR_OLED_WHITE);
        } else {
            for (int i = 0; i < g_probe_count && i < 4; i++) {
                int y = 11 + i * 11;
                char pline[32];
                snprintf(pline, sizeof(pline), "> %s", g_probes[i].requested_ssid);
                c_draw_text(2, y, pline, g_active_color);
            }
        }
        c_draw_text(2, 56, "[Hold] Back", g_active_color);
    }
}

// ============================================================================
// BULLET FEATURE 3: CYBERPUNK MATRIX DIGITAL RAIN HUD VIEW
// ============================================================================
static void c_render_matrix_rain_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    // Initialize random drops if needed
    if (g_matrix_y[0] == 0) {
        for (int c = 0; c < MATRIX_COLS; c++) {
            g_matrix_y[c] = -(rand() % 160);
            g_matrix_speed[c] = 2 + (rand() % 4);
            for (int r = 0; r < 16; r++) {
                g_matrix_chars[c][r] = "0123456789ABCDEF#%&*<>{}[]"[rand() % 26];
            }
        }
    }

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, 0xFF020603);

        int max_cols = g_disp_w / 10;
        if (max_cols > MATRIX_COLS) max_cols = MATRIX_COLS;

        // Falling digital characters
        for (int c = 0; c < max_cols; c++) {
            int x = 4 + c * 10;
            g_matrix_y[c] += g_matrix_speed[c];
            if (g_matrix_y[c] > g_disp_h + 20) {
                g_matrix_y[c] = -(rand() % 40);
                g_matrix_speed[c] = 2 + (rand() % 5);
            }

            for (int r = 0; r < 14; r++) {
                int y = g_matrix_y[c] - r * 10;
                if (y >= 0 && y < g_disp_h - 8) {
                    char ch = g_matrix_chars[c % MATRIX_COLS][r % 16];
                    uint32_t col = (r == 0) ? 0xFFFFFFFF : ((r < 4) ? 0xFF55FF88 : 0xFF118833);
                    c_draw_char(x, y, ch, col);
                }
            }
        }

        // Cyberpunk Telemetry HUD Box in Center
        int box_w = 180;
        int box_h = 76;
        int box_x = (g_disp_w - box_w) / 2;
        int box_y = (g_disp_h - box_h) / 2;

        c_draw_rounded_card(box_x, box_y, box_w, box_h, 6, 0xDD0A140D, IPS_ACCENT_EMERALD);
        c_draw_text(box_x + 16, box_y + 12, "BULLET MATRIX HUD", IPS_ACCENT_EMERALD);
        c_draw_text(box_x + 16, box_y + 28, "CPU: 240MHz Xtensa LX7", 0xFFFFFFFF);
        c_draw_text(box_x + 16, box_y + 42, "PSRAM: 8MB / Flash 16MB", IPS_ACCENT_GLACIER);
        c_draw_text(box_x + 16, box_y + 56, "WiFi: Promiscuous RX", IPS_ACCENT_AMBER);

        c_draw_text(16, g_disp_h - 16, "[Click Btn] Return to Menu", IPS_TEXT_MUTED);
    } else {
        // OLED Matrix Rain
        for (int c = 0; c < 16; c++) {
            int x = c * 8;
            g_matrix_y[c] += 2;
            if (g_matrix_y[c] > 80) g_matrix_y[c] = -10;

            for (int r = 0; r < 6; r++) {
                int y = g_matrix_y[c] - r * 8;
                if (y >= 0 && y < 56) {
                    c_draw_char(x, y, g_matrix_chars[c][r % 8], g_active_color);
                }
            }
        }
        c_draw_rect_fill(14, 20, 100, 24, COLOR_BLACK);
        c_draw_rect_outline(14, 20, 100, 24, g_active_color);
        c_draw_text(26, 28, "MATRIX ACTIVE", g_active_color);
    }
}

// 4. RF 2.4GHz CHANNEL SNIFFER VIEW
static void c_render_sniffer_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, is_ru ? "RF 2.4GHz МОНИТОР КАНАЛОВ" : "RF 2.4GHz PACKET SNIFFER", IPS_TEXT_PRIMARY);

        c_draw_rounded_card(g_disp_w - 75, 6, 68, 14, 3, IPS_BG_COLOR, IPS_ACCENT_AMBER);
        c_draw_text(g_disp_w - 69, 9, "LIVE 2.4G", IPS_ACCENT_AMBER);

        int start_x = 16;
        int bar_step = (g_disp_w - 32) / 13;
        int bar_w = bar_step > 14 ? 14 : bar_step - 2;
        int max_h = g_disp_h - 130;
        int base_y = g_disp_h - 65;

        for (int ch = 1; ch <= 13; ch++) {
            int x = start_x + (ch - 1) * bar_step;
            int count = g_channel_activity[ch] % 50;
            int bh = (count * max_h) / 50;
            if (bh < 4) bh = 4;

            bool is_active_ch = (ch == g_current_sniff_channel);
            uint32_t col = is_active_ch ? IPS_ACCENT_GLACIER : (count > 30 ? IPS_ACCENT_ROSE : IPS_ACCENT_EMERALD);

            c_draw_rect_fill(x, base_y - bh, bar_w, bh, col);
            c_draw_rect_fill(x, base_y - bh, bar_w, 2, 0xFFFFFFFF);

            char ch_str[4];
            snprintf(ch_str, sizeof(ch_str), "%d", ch);
            c_draw_text(x + 2, base_y + 4, ch_str, is_active_ch ? IPS_ACCENT_GLACIER : IPS_TEXT_MUTED);
        }

        int card_w = g_disp_w - 24;
        c_draw_rounded_card(12, g_disp_h - 45, card_w, 36, 5, IPS_CARD_BG, IPS_CARD_BORDER);
        char stats_buf[48];
        snprintf(stats_buf, sizeof(stats_buf), "PKTS: %lu | HOP: CH%d", (unsigned long)g_total_packets, g_current_sniff_channel);
        c_draw_text(22, g_disp_h - 36, stats_buf, IPS_ACCENT_GLACIER);
        c_draw_text(22, g_disp_h - 24, "[Turn Knob] Hop Ch | [Btn] Hold", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "RF 2.4G SNIFFER", COLOR_BLACK);

        int start_x = 4;
        int bar_w = 6;
        int base_y = 48;

        for (int ch = 1; ch <= 13; ch++) {
            int x = start_x + (ch - 1) * 9;
            int count = g_channel_activity[ch] % 30;
            int bh = (count * 34) / 30;
            if (bh < 2) bh = 2;

            c_draw_rect_fill(x, base_y - bh, bar_w, bh, g_active_color);
        }
        c_draw_text(2, 54, "CH: 1..13  [Hold] Back", g_active_color);
    }
}

// 5. BLE RADAR SCANNER VIEW
static void c_render_ble_radar_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, is_ru ? "BLE РАДАР УСТРОЙСТВ" : "BLE PROXIMITY RADAR", IPS_TEXT_PRIMARY);

        char cnt_str[24];
        snprintf(cnt_str, sizeof(cnt_str), "%d BEACONS", g_ble_count);
        c_draw_text(g_disp_w - 85, 10, cnt_str, g_ble_count > 0 ? IPS_ACCENT_EMERALD : IPS_TEXT_MUTED);

        int cx = g_disp_w / 2;
        int cy = g_disp_h / 2 - 8;
        int r3 = (g_disp_h < g_disp_w ? g_disp_h : g_disp_w) / 2 - 25;
        int r2 = (r3 * 2) / 3;
        int r1 = r3 / 3;

        c_draw_circle(cx, cy, r1, IPS_CARD_BG);
        c_draw_circle(cx, cy, r2, IPS_CARD_BORDER);
        c_draw_circle(cx, cy, r3, IPS_CARD_HOVER);

        float deg = (g_engine.tick * 5) % 360;
        float rad = deg * 3.14159265f / 180.0f;
        c_draw_line(cx, cy, cx + (int)(cosf(rad) * (float)r3), cy + (int)(sinf(rad) * (float)r3), IPS_ACCENT_GLACIER);

        for (int i = 0; i < g_ble_count && i < MAX_BLE_CAPACITY; i++) {
            float angle = (i * 110.0f) * 3.14159265f / 180.0f;
            float dist = (float)(-g_ble_devices[i].rssi - 30) * 1.5f;
            if (dist < 15.0f) dist = 15.0f;
            if (dist > (float)r3) dist = (float)r3 - 5.0f;

            int dx = cx + (int)(cosf(angle) * dist);
            int dy = cy + (int)(sinf(angle) * dist);

            uint32_t bcol = (g_ble_devices[i].type == 2) ? IPS_ACCENT_AMBER : (g_ble_devices[i].type == 1 ? IPS_ACCENT_EMERALD : IPS_ACCENT_GLACIER);
            c_draw_circle(dx, dy, 4, bcol);
            c_draw_pixel(dx, dy, 0xFFFFFFFF);
        }

        int card_w = g_disp_w - 24;
        c_draw_rounded_card(12, g_disp_h - 48, card_w, 40, 5, IPS_CARD_BG, IPS_CARD_BORDER);
        if (g_ble_count > 0) {
            c_draw_text(22, g_disp_h - 41, g_ble_devices[0].name, IPS_TEXT_PRIMARY);
            char bstr[48];
            snprintf(bstr, sizeof(bstr), "RSSI: %ddBm | MAC: %s", g_ble_devices[0].rssi, g_ble_devices[0].mac);
            c_draw_text(22, g_disp_h - 27, bstr, IPS_ACCENT_EMERALD);
        } else {
            c_draw_text(22, g_disp_h - 41, is_ru ? "СКАНИРОВАНИЕ BLE ЭФИРА 2.4GHz..." : "SCANNING BLE 2.4GHz...", IPS_ACCENT_AMBER);
            c_draw_text(22, g_disp_h - 27, is_ru ? "Маяки AirTag, iBeacon и трекеры рядом" : "Active beacons & trackers will appear on radar", IPS_TEXT_MUTED);
        }
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "BLE RADAR", COLOR_BLACK);
        int cx = 35, cy = 36;
        c_draw_circle(cx, cy, 22, g_active_color);
        c_draw_circle(cx, cy, 12, g_active_color);
        c_draw_pixel(cx, cy, g_active_color);

        if (g_ble_count > 0) {
            for (int i = 0; i < g_ble_count && i < 3; i++) {
                char bstr[24];
                snprintf(bstr, sizeof(bstr), "%s %ddB", g_ble_devices[i].name, g_ble_devices[i].rssi);
                c_draw_text(65, 16 + i * 12, bstr, g_active_color);
            }
        } else {
            c_draw_text(65, 20, "SCANNING...", g_active_color);
            c_draw_text(65, 34, "NO BEACONS", COLOR_OLED_WHITE);
        }
        c_draw_text(2, 54, "[Hold] Back", g_active_color);
    }
}

// 6. 16-BAND FFT AUDIO SPECTRUM VIEW
static void c_render_fft_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    for (int b = 0; b < 16; b++) {
        float target = 0.2f + 0.7f * fabsf(sinf((g_engine.tick * 0.12f) + b * 0.45f));
        g_fft_bands[b] += (target - g_fft_bands[b]) * 0.35f;
        if (g_fft_bands[b] > g_fft_peaks[b]) {
            g_fft_peaks[b] = g_fft_bands[b];
        } else {
            g_fft_peaks[b] -= 0.015f;
        }
    }

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, 0xFF050403);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, "16-BAND FFT EQUALIZER", IPS_TEXT_PRIMARY);
        c_draw_text(g_disp_w - 75, 10, "48.0 kHz", IPS_ACCENT_GLACIER);

        int start_x = 16;
        int bar_step = (g_disp_w - 32) / 16;
        int bar_w = bar_step > 12 ? 12 : (bar_step > 4 ? bar_step - 2 : 2);
        int max_h = g_disp_h - 95;
        int base_y = g_disp_h - 45;

        for (int b = 0; b < 16; b++) {
            int x = start_x + b * bar_step;
            int bh = (int)(g_fft_bands[b] * max_h);
            int peak_h = (int)(g_fft_peaks[b] * max_h);

            uint32_t col = (b < 5) ? IPS_ACCENT_GLACIER : (b < 11 ? IPS_ACCENT_EMERALD : IPS_ACCENT_AMBER);
            c_draw_rect_fill(x, base_y - bh, bar_w, bh, col);
            c_draw_rect_fill(x, base_y - peak_h, bar_w, 2, 0xFFFFFFFF);
        }

        c_draw_rect_fill(10, g_disp_h - 38, g_disp_w - 20, 1, IPS_CARD_BORDER);
        c_draw_text(14, g_disp_h - 28, "32Hz          1kHz           16kHz", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "FFT AUDIO SPECTRUM", COLOR_BLACK);

        int start_x = 4;
        int bar_w = 5;
        int base_y = 50;

        for (int b = 0; b < 16; b++) {
            int x = start_x + b * 7;
            int bh = (int)(g_fft_bands[b] * 38.0f);
            c_draw_rect_fill(x, base_y - bh, bar_w, bh, g_active_color);
        }
        c_draw_text(2, 54, "32Hz .. 16kHz  [Hold] Back", g_active_color);
    }
}

// 7. CYBERKART TURBO RACING (Auto-Gas, 1-Knob Steering, Nitro Boost)
static void c_render_kart_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    int road_w = is_ips ? (g_disp_w > 260 ? 220 : 176) : 100;
    int road_left = is_ips ? (g_disp_w - road_w) / 2 : 14;
    int road_right = road_left + road_w;
    int road_width = road_right - road_left;
    int player_y = is_ips ? (g_disp_h - 48) : 46;

    // Initialize state if needed
    if (!g_kart.initialized) {
        g_kart.player_x = (float)(road_left + road_width / 2);
        g_kart.target_x = g_kart.player_x;
        g_kart.speed_kmh = 95.0f;
        g_kart.distance_m = 0.0f;
        g_kart.score = 0;
        g_kart.nitro_pct = 60;
        g_kart.nitro_active = false;
        g_kart.nitro_timer = 0;
        g_kart.game_over = false;
        g_kart.crash_tick = 0;
        g_kart.coins_collected = 0;

        for (int i = 0; i < MAX_KART_RIVALS; i++) {
            g_kart.rivals[i].x = (float)(road_left + 16 + (rand() % (road_width - 32)));
            g_kart.rivals[i].y = (float)(-40 - i * 85);
            g_kart.rivals[i].speed_offset = (float)(rand() % 35 - 15);
            g_kart.rivals[i].type = rand() % 3;
            g_kart.rivals[i].active = true;
        }

        for (int i = 0; i < MAX_KART_COINS; i++) {
            g_kart.coins[i].x = (float)(road_left + 20 + (rand() % (road_width - 40)));
            g_kart.coins[i].y = (float)(-50 - i * 130);
            g_kart.coins[i].active = true;
        }
        g_kart.initialized = true;
    }

    // Physics & Game Loop
    if (!g_kart.game_over) {
        // Auto-Gas Acceleration
        if (g_kart.speed_kmh < 240.0f) {
            g_kart.speed_kmh += 0.035f;
        }

        // Nitro Boost countdown
        if (g_kart.nitro_active) {
            g_kart.nitro_timer--;
            if (g_kart.nitro_timer <= 0) {
                g_kart.nitro_active = false;
            }
        }

        float current_speed = g_kart.nitro_active ? (g_kart.speed_kmh + 75.0f) : g_kart.speed_kmh;
        g_kart.road_scroll_y += current_speed * (is_ips ? 0.09f : 0.045f);
        g_kart.distance_m += current_speed * 0.015f;
        g_kart.score = (int)g_kart.distance_m + g_kart.coins_collected * 150;
        if (g_kart.score > g_kart.high_score) g_kart.high_score = g_kart.score;

        // Smooth Knob Steering
        g_kart.player_x += (g_kart.target_x - g_kart.player_x) * 0.28f;
        if (g_kart.player_x < road_left + 12) g_kart.player_x = (float)(road_left + 12);
        if (g_kart.player_x > road_right - 12) g_kart.player_x = (float)(road_right - 12);

        // Update Rivals
        for (int i = 0; i < MAX_KART_RIVALS; i++) {
            float rel_speed = current_speed - (g_kart.speed_kmh * 0.62f + g_kart.rivals[i].speed_offset);
            g_kart.rivals[i].y += rel_speed * (is_ips ? 0.07f : 0.035f);

            // Respawn off bottom
            if (g_kart.rivals[i].y > (is_ips ? (g_disp_h + 20) : 75)) {
                g_kart.rivals[i].y = (float)(-30 - (rand() % 90));
                g_kart.rivals[i].x = (float)(road_left + 16 + (rand() % (road_width - 32)));
                g_kart.rivals[i].speed_offset = (float)(rand() % 35 - 15);
                g_kart.rivals[i].type = rand() % 3;
            }

            // Collision check
            float dx = fabsf(g_kart.player_x - g_kart.rivals[i].x);
            float dy = fabsf((float)player_y - g_kart.rivals[i].y);
            if (dx < (is_ips ? 16.0f : 8.0f) && dy < (is_ips ? 22.0f : 10.0f)) {
                g_kart.game_over = true;
                g_kart.crash_tick = 0;
            }
        }

        // Update Energy Coins
        for (int i = 0; i < MAX_KART_COINS; i++) {
            g_kart.coins[i].y += current_speed * (is_ips ? 0.08f : 0.04f);
            if (g_kart.coins[i].y > (is_ips ? (g_disp_h + 20) : 75)) {
                g_kart.coins[i].y = (float)(-20 - (rand() % 110));
                g_kart.coins[i].x = (float)(road_left + 20 + (rand() % (road_width - 40)));
                g_kart.coins[i].active = true;
            }

            if (g_kart.coins[i].active) {
                float cdx = fabsf(g_kart.player_x - g_kart.coins[i].x);
                float cdy = fabsf((float)player_y - g_kart.coins[i].y);
                if (cdx < (is_ips ? 16.0f : 7.0f) && cdy < (is_ips ? 18.0f : 8.0f)) {
                    g_kart.coins[i].active = false;
                    g_kart.coins_collected++;
                    g_kart.nitro_pct += 25;
                    if (g_kart.nitro_pct > 100) g_kart.nitro_pct = 100;
                }
            }
        }
    } else {
        g_kart.crash_tick++;
    }

    // RENDERING
    if (is_ips) {
        // Roadside background
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, 0xFF05150A);

        // Asphalt Track
        c_draw_rect_fill(road_left, 0, road_width, g_disp_h, 0xFF141210);

        // Animated Red/White Curbs
        int curb_seg = 18;
        int scroll_offset = (int)g_kart.road_scroll_y % (curb_seg * 2);
        for (int y = -curb_seg * 2 + scroll_offset; y < g_disp_h; y += curb_seg) {
            bool is_red = ((y / curb_seg) % 2 == 0);
            uint32_t curb_col = is_red ? IPS_ACCENT_ROSE : 0xFFFFFFFF;
            c_draw_rect_fill(road_left - 6, y, 6, curb_seg, curb_col);
            c_draw_rect_fill(road_right, y, 6, curb_seg, curb_col);
        }

        // Animated Dashed White Lane Lines (3 lanes = 2 line strips)
        int dash_h = 16;
        int dash_gap = 14;
        int dash_step = dash_h + dash_gap;
        int dash_offset = (int)g_kart.road_scroll_y % dash_step;
        int lane1_x = road_left + road_width / 3;
        int lane2_x = road_left + (road_width * 2) / 3;

        for (int y = -dash_step + dash_offset; y < g_disp_h; y += dash_step) {
            c_draw_rect_fill(lane1_x, y, 2, dash_h, 0x88FFFFFF);
            c_draw_rect_fill(lane2_x, y, 2, dash_h, 0x88FFFFFF);
        }

        // Energy Coins
        for (int i = 0; i < MAX_KART_COINS; i++) {
            if (g_kart.coins[i].active && g_kart.coins[i].y > -10 && g_kart.coins[i].y < g_disp_h) {
                int cx = (int)g_kart.coins[i].x;
                int cy = (int)g_kart.coins[i].y;
                c_draw_circle(cx, cy, 6, IPS_ACCENT_AMBER);
                c_draw_circle(cx, cy, 3, 0xFFFFFFFF);
            }
        }

        // Traffic Rivals
        for (int i = 0; i < MAX_KART_RIVALS; i++) {
            if (g_kart.rivals[i].y > -30 && g_kart.rivals[i].y < g_disp_h + 10) {
                int rx = (int)g_kart.rivals[i].x;
                int ry = (int)g_kart.rivals[i].y;
                uint32_t rcol = (g_kart.rivals[i].type == 0) ? IPS_ACCENT_GLACIER : 
                               (g_kart.rivals[i].type == 1 ? IPS_ACCENT_AMBER : IPS_ACCENT_ROSE);

                // Body
                c_draw_rounded_card(rx - 8, ry - 14, 16, 28, 4, rcol, 0);
                // Windshield
                c_draw_rect_fill(rx - 5, ry - 3, 10, 6, 0xFF050505);
                // Wheels
                c_draw_rect_fill(rx - 10, ry - 12, 3, 6, 0xFF222222);
                c_draw_rect_fill(rx + 7, ry - 12, 3, 6, 0xFF222222);
                c_draw_rect_fill(rx - 10, ry + 6, 3, 6, 0xFF222222);
                c_draw_rect_fill(rx + 7, ry + 6, 3, 6, 0xFF222222);
                // Taillights
                c_draw_rect_fill(rx - 6, ry + 12, 3, 2, IPS_ACCENT_ROSE);
                c_draw_rect_fill(rx + 3, ry + 12, 3, 2, IPS_ACCENT_ROSE);
            }
        }

        // Player Kart
        int px = (int)g_kart.player_x;
        int py = player_y;

        if (!g_kart.game_over) {
            // Nitro Exhaust Flame
            if (g_kart.nitro_active || (g_engine.tick % 4 < 2)) {
                int flame_h = g_kart.nitro_active ? 18 : 8;
                uint32_t fcol = g_kart.nitro_active ? IPS_ACCENT_GLACIER : IPS_ACCENT_AMBER;
                c_draw_rect_fill(px - 4, py + 14, 3, flame_h, fcol);
                c_draw_rect_fill(px + 1, py + 14, 3, flame_h, fcol);
                c_draw_pixel(px - 3, py + 14 + flame_h, 0xFFFFFFFF);
                c_draw_pixel(px + 2, py + 14 + flame_h, 0xFFFFFFFF);
            }

            // Chassis Body (Emerald Green CyberKart)
            c_draw_rounded_card(px - 9, py - 14, 18, 28, 4, IPS_ACCENT_EMERALD, 0);
            // Cockpit & Driver Helmet
            c_draw_rect_fill(px - 5, py - 6, 10, 8, 0xFF050505);
            c_draw_circle(px, py - 2, 3, 0xFFFFFFFF);
            // Spoiler
            c_draw_rect_fill(px - 8, py + 11, 16, 3, 0xFF140D08);
            // 4 Racing Wheels
            c_draw_rect_fill(px - 11, py - 12, 3, 7, 0xFF222222);
            c_draw_rect_fill(px + 8, py - 12, 3, 7, 0xFF222222);
            c_draw_rect_fill(px - 11, py + 6, 3, 7, 0xFF222222);
            c_draw_rect_fill(px + 8, py + 6, 3, 7, 0xFF222222);
            // Headlights
            c_draw_rect_fill(px - 7, py - 14, 3, 2, 0xFFFFFFFF);
            c_draw_rect_fill(px + 4, py - 14, 3, 2, 0xFFFFFFFF);
        } else {
            // Explosion particles
            int r = g_kart.crash_tick * 2;
            c_draw_circle(px, py, r, IPS_ACCENT_AMBER);
            c_draw_circle(px, py, r / 2, IPS_ACCENT_ROSE);
        }

        // Top HUD Bar
        c_draw_rect_fill(0, 0, g_disp_w, 26, 0xDD000000);
        char speed_str[24];
        snprintf(speed_str, sizeof(speed_str), "%d KM/H", (int)(g_kart.nitro_active ? (g_kart.speed_kmh + 75) : g_kart.speed_kmh));
        c_draw_text(10, 9, speed_str, g_kart.nitro_active ? IPS_ACCENT_GLACIER : IPS_ACCENT_EMERALD);

        char dist_str[24];
        snprintf(dist_str, sizeof(dist_str), "%d M", (int)g_kart.distance_m);
        c_draw_text(g_disp_w / 2 - 20, 9, dist_str, IPS_TEXT_PRIMARY);

        // Nitro Bar in Header
        int n_w = (g_kart.nitro_pct * 40) / 100;
        c_draw_rect_outline(g_disp_w - 55, 7, 44, 12, IPS_CARD_BORDER);
        c_draw_rect_fill(g_disp_w - 53, 9, n_w, 8, g_kart.nitro_pct > 25 ? IPS_ACCENT_GLACIER : IPS_ACCENT_AMBER);

        // Game Over Banner
        if (g_kart.game_over) {
            int box_w = 180;
            int box_h = 80;
            int box_x = (g_disp_w - box_w) / 2;
            int box_y = (g_disp_h - box_h) / 2;

            c_draw_rounded_card(box_x, box_y, box_w, box_h, 8, 0xEE110805, IPS_ACCENT_ROSE);
            c_draw_text(box_x + 55, box_y + 12, is_ru ? "АВАРИЯ!" : "CRASH!", IPS_ACCENT_ROSE);

            char final_score[32];
            snprintf(final_score, sizeof(final_score), is_ru ? "Счет: %d м" : "Score: %d m", g_kart.score);
            c_draw_text(box_x + 20, box_y + 30, final_score, IPS_TEXT_PRIMARY);

            c_draw_text(box_x + 15, box_y + 54, is_ru ? "[Клик Кнопки] Рестарт" : "[Click Btn] RESTART", IPS_ACCENT_AMBER);
        }
    } else {
        // OLED 128x64 Mode
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        char oled_hud[24];
        snprintf(oled_hud, sizeof(oled_hud), "%dKM/H %dM", (int)g_kart.speed_kmh, (int)g_kart.distance_m);
        c_draw_text(2, 1, oled_hud, COLOR_BLACK);

        // Road Borders
        c_draw_line(road_left, 10, road_left, 63, g_active_color);
        c_draw_line(road_right, 10, road_right, 63, g_active_color);

        // Dashed Center
        int lane_x = road_left + road_width / 2;
        int d_step = 8;
        int d_off = (int)g_kart.road_scroll_y % d_step;
        for (int y = 10 - d_step + d_off; y < 64; y += d_step) {
            c_draw_line(lane_x, y, lane_x, y + 4, g_active_color);
        }

        // Rivals
        for (int i = 0; i < MAX_KART_RIVALS; i++) {
            if (g_kart.rivals[i].y > 8 && g_kart.rivals[i].y < 60) {
                int rx = (int)g_kart.rivals[i].x;
                int ry = (int)g_kart.rivals[i].y;
                c_draw_rect_fill(rx - 3, ry - 5, 6, 10, g_active_color);
            }
        }

        // Player
        int px = (int)g_kart.player_x;
        int py = player_y;
        if (!g_kart.game_over) {
            c_draw_rect_fill(px - 4, py - 6, 8, 12, g_active_color);
            c_draw_pixel(px, py - 1, COLOR_BLACK);
        } else {
            c_draw_text(20, 28, is_ru ? "АВАРИЯ! [Клик]" : "CRASH! [Click]", g_active_color);
        }
    }
}

// 8. GOOGLE CHROME T-REX DINO RUNNER
static void c_render_dino_view(void) {
    int is_ips = (g_disp_mode == DISP_MODE_IPS_240x240);
    bool is_ru = (g_engine.lang == LANG_RU);

    int ground_y = is_ips ? 190 : 50;
    int dino_x = is_ips ? 36 : 14;
    float gravity = is_ips ? 0.54f : 0.32f;
    float jump_power = is_ips ? -7.4f : -4.5f;

    // Initialization
    if (!g_dino.initialized) {
        g_dino.player_y = 0.0f;
        g_dino.player_vy = 0.0f;
        g_dino.is_jumping = false;
        g_dino.is_ducking = false;
        g_dino.run_speed = is_ips ? 4.2f : 2.2f;
        g_dino.distance_m = 0.0f;
        g_dino.score = 0;
        g_dino.game_over = false;
        g_dino.crash_tick = 0;
        g_dino.leg_anim_tick = 0;

        for (int i = 0; i < MAX_DINO_OBSTACLES; i++) {
            float base_gap = is_ips ? 220.0f : 110.0f;
            g_dino.obstacles[i].x = (float)((is_ips ? 260.0f : 130.0f) + i * base_gap + (rand() % (is_ips ? 50 : 25)));
            g_dino.obstacles[i].type = (i == 1) ? 3 : (rand() % 3);
            g_dino.obstacles[i].fly_y = (float)(ground_y - (is_ips ? (rand() % 2 == 0 ? 28 : 50) : (rand() % 2 == 0 ? 14 : 25)));
            g_dino.obstacles[i].active = true;
        }

        for (int i = 0; i < MAX_DINO_CLOUDS; i++) {
            g_dino.clouds[i].x = (float)(rand() % 240);
            g_dino.clouds[i].y = (float)(is_ips ? (30 + rand() % 60) : (12 + rand() % 16));
            g_dino.clouds[i].speed = 0.4f + (float)(rand() % 4) * 0.15f;
        }
        g_dino.initialized = true;
    }

    // Game Physics & Loop
    if (!g_dino.game_over) {
        // Duck timer countdown
        if (g_dino.duck_timer > 0) {
            g_dino.duck_timer--;
            if (g_dino.duck_timer == 0) {
                g_dino.is_ducking = false;
            }
        }

        // Speed up gradually
        if (g_dino.run_speed < (is_ips ? 9.5f : 5.0f)) {
            g_dino.run_speed += 0.0015f;
        }

        g_dino.distance_m += g_dino.run_speed * 0.15f;
        g_dino.score = (int)g_dino.distance_m;
        if (g_dino.score > g_dino.high_score) g_dino.high_score = g_dino.score;

        g_dino.leg_anim_tick++;
        g_dino.night_mode = ((g_dino.score / 350) % 2 == 1);

        // Jump physics
        if (g_dino.is_jumping) {
            g_dino.player_y += g_dino.player_vy;
            g_dino.player_vy += gravity;
            if (g_dino.player_y >= 0.0f) {
                g_dino.player_y = 0.0f;
                g_dino.player_vy = 0.0f;
                g_dino.is_jumping = false;
            }
        }

        // Move Clouds
        for (int i = 0; i < MAX_DINO_CLOUDS; i++) {
            g_dino.clouds[i].x -= g_dino.clouds[i].speed;
            if (g_dino.clouds[i].x < -40) {
                g_dino.clouds[i].x = (float)(is_ips ? 260 : 140);
                g_dino.clouds[i].y = (float)(is_ips ? (30 + rand() % 60) : (12 + rand() % 16));
            }
        }

        // Move Obstacles & Fair Respawn with Guaranteed Minimum Safe Gap
        for (int i = 0; i < MAX_DINO_OBSTACLES; i++) {
            g_dino.obstacles[i].x -= g_dino.run_speed;
            if (g_dino.obstacles[i].x < -40.0f) {
                // Find furthest obstacle position on track
                float furthest_x = is_ips ? 240.0f : 128.0f;
                for (int k = 0; k < MAX_DINO_OBSTACLES; k++) {
                    if (k != i && g_dino.obstacles[k].x > furthest_x) {
                        furthest_x = g_dino.obstacles[k].x;
                    }
                }
                float safe_gap = is_ips ? 200.0f : 100.0f;
                float random_extra = (float)(rand() % (is_ips ? 110 : 50));
                g_dino.obstacles[i].x = furthest_x + safe_gap + random_extra;
                g_dino.obstacles[i].type = (rand() % 4 == 3) ? 3 : (rand() % 3);
                g_dino.obstacles[i].fly_y = (float)(ground_y - (is_ips ? (rand() % 2 == 0 ? 28 : 50) : (rand() % 2 == 0 ? 14 : 25)));
                g_dino.obstacles[i].active = true;
            }

            // Hitbox Check
            float ox = g_dino.obstacles[i].x;
            float ow = (g_dino.obstacles[i].type == 2) ? (is_ips ? 24.0f : 12.0f) : (is_ips ? 14.0f : 7.0f);
            float dino_w = is_ips ? (g_dino.is_ducking ? 28.0f : 20.0f) : 10.0f;
            float dino_h = is_ips ? (g_dino.is_ducking ? 14.0f : 26.0f) : (g_dino.is_ducking ? 7.0f : 13.0f);
            float cur_dino_y = ground_y + g_dino.player_y - dino_h;

            if (g_dino.obstacles[i].type == 3) { // Pterodactyl
                float oy = g_dino.obstacles[i].fly_y;
                if (fabsf((float)dino_x - ox) < (dino_w / 2 + 10.0f)) {
                    if (cur_dino_y < oy + 12.0f && (cur_dino_y + dino_h) > oy) {
                        g_dino.game_over = true;
                        g_dino.crash_tick = 0;
                    }
                }
            } else { // Cacti
                float oy = (float)ground_y - (is_ips ? 24.0f : 12.0f);
                if (fabsf((float)dino_x - ox) < (dino_w / 2 + ow / 2 - 2.0f)) {
                    if ((ground_y + g_dino.player_y) >= oy + 6.0f) {
                        g_dino.game_over = true;
                        g_dino.crash_tick = 0;
                    }
                }
            }
        }
    } else {
        g_dino.crash_tick++;
    }

    // RENDERING
    if (is_ips) {
        uint32_t bg_col = g_dino.night_mode ? 0xFF050508 : 0xFFFAF8F5;
        uint32_t fg_col = g_dino.night_mode ? 0xFFFAF8F5 : 0xFF2B2824;
        uint32_t cactus_col = g_dino.night_mode ? IPS_ACCENT_EMERALD : 0xFF2A5A24;

        c_draw_rect_fill(0, 0, 240, 240, bg_col);

        // Ground Line & Moving Terrain Dots
        c_draw_rect_fill(0, ground_y, g_disp_w, 2, fg_col);
        for (int x = 0; x < g_disp_w; x += 18) {
            int dot_x = (x - (int)g_dino.distance_m) % g_disp_w;
            if (dot_x < 0) dot_x += g_disp_w;
            c_draw_pixel(dot_x, ground_y + 4, fg_col);
            c_draw_pixel(dot_x + 6, ground_y + 6, fg_col);
        }

        // Floating Clouds
        for (int i = 0; i < MAX_DINO_CLOUDS; i++) {
            int cx = (int)g_dino.clouds[i].x;
            int cy = (int)g_dino.clouds[i].y;
            c_draw_rounded_card(cx, cy, 26, 8, 3, fg_col, 0);
            c_draw_circle(cx + 10, cy - 2, 5, fg_col);
        }

        // Obstacles (Cacti & Pterodactyls)
        for (int i = 0; i < MAX_DINO_OBSTACLES; i++) {
            int ox = (int)g_dino.obstacles[i].x;
            if (ox > -30 && ox < g_disp_w + 10) {
                if (g_dino.obstacles[i].type == 0) { // Small Cactus
                    c_draw_rect_fill(ox - 3, ground_y - 20, 6, 20, cactus_col);
                    c_draw_rect_fill(ox - 7, ground_y - 14, 4, 8, cactus_col);
                    c_draw_rect_fill(ox + 3, ground_y - 16, 4, 8, cactus_col);
                } else if (g_dino.obstacles[i].type == 1) { // Tall Cactus
                    c_draw_rect_fill(ox - 4, ground_y - 28, 8, 28, cactus_col);
                    c_draw_rect_fill(ox - 9, ground_y - 22, 5, 12, cactus_col);
                    c_draw_rect_fill(ox + 4, ground_y - 24, 5, 12, cactus_col);
                } else if (g_dino.obstacles[i].type == 2) { // Double Cactus
                    c_draw_rect_fill(ox - 8, ground_y - 22, 6, 22, cactus_col);
                    c_draw_rect_fill(ox + 2, ground_y - 26, 6, 26, cactus_col);
                    c_draw_rect_fill(ox - 12, ground_y - 16, 4, 8, cactus_col);
                    c_draw_rect_fill(ox + 8, ground_y - 18, 4, 8, cactus_col);
                } else if (g_dino.obstacles[i].type == 3) { // Pterodactyl
                    int py = (int)g_dino.obstacles[i].fly_y;
                    bool wing_up = ((g_engine.tick / 10) % 2 == 0);
                    c_draw_rect_fill(ox - 8, py, 16, 5, IPS_ACCENT_ROSE);
                    c_draw_rect_fill(ox + 6, py - 2, 6, 4, IPS_ACCENT_ROSE); // Head & beak
                    if (wing_up) c_draw_rect_fill(ox - 4, py - 10, 8, 10, IPS_ACCENT_ROSE);
                    else c_draw_rect_fill(ox - 4, py + 5, 8, 8, IPS_ACCENT_ROSE);
                }
            }
        }

        // T-Rex Dino Sprite
        int dy = ground_y + (int)g_dino.player_y;
        if (!g_dino.is_ducking) {
            // Standing / Jumping T-Rex
            // Head & Jaw
            c_draw_rect_fill(dino_x + 2, dy - 30, 16, 12, fg_col);
            c_draw_rect_fill(dino_x + 10, dy - 22, 10, 4, bg_col); // Mouth
            // Eye (X if crashed, Dot if alive)
            if (g_dino.game_over) {
                c_draw_pixel(dino_x + 6, dy - 27, IPS_ACCENT_ROSE);
                c_draw_pixel(dino_x + 8, dy - 27, IPS_ACCENT_ROSE);
            } else {
                c_draw_pixel(dino_x + 6, dy - 27, bg_col);
            }
            // Neck & Body
            c_draw_rect_fill(dino_x - 4, dy - 22, 12, 14, fg_col);
            c_draw_rect_fill(dino_x - 12, dy - 18, 8, 6, fg_col); // Tail
            c_draw_rect_fill(dino_x + 8, dy - 14, 4, 3, fg_col);  // Arm
            // Running Legs
            if (g_dino.is_jumping || g_dino.game_over) {
                c_draw_rect_fill(dino_x - 2, dy - 8, 3, 8, fg_col);
                c_draw_rect_fill(dino_x + 4, dy - 8, 3, 8, fg_col);
            } else {
                bool step = ((g_dino.leg_anim_tick / 4) % 2 == 0);
                if (step) {
                    c_draw_rect_fill(dino_x - 2, dy - 8, 3, 8, fg_col);
                    c_draw_rect_fill(dino_x + 4, dy - 8, 3, 4, fg_col);
                } else {
                    c_draw_rect_fill(dino_x - 2, dy - 8, 3, 4, fg_col);
                    c_draw_rect_fill(dino_x + 4, dy - 8, 3, 8, fg_col);
                }
            }
        } else {
            // Ducking / Crawling T-Rex
            c_draw_rect_fill(dino_x - 10, dy - 14, 28, 8, fg_col); // Long flat body
            c_draw_rect_fill(dino_x + 16, dy - 16, 12, 6, fg_col); // Low head
            c_draw_pixel(dino_x + 20, dy - 14, bg_col); // Eye
            c_draw_rect_fill(dino_x - 4, dy - 6, 4, 6, fg_col);  // Low legs
            c_draw_rect_fill(dino_x + 6, dy - 6, 4, 6, fg_col);
        }

        // Top Score Header (HI 00480  00125)
        char score_str[36];
        snprintf(score_str, sizeof(score_str), "HI %05d   %05d", g_dino.high_score, g_dino.score);
        c_draw_text(g_disp_w - 145, 12, score_str, fg_col);

        // Game Over Screen
        if (g_dino.game_over) {
            int box_w = 180;
            int box_h = 75;
            int box_x = (g_disp_w - box_w) / 2;
            int box_y = (g_disp_h - box_h) / 2;

            c_draw_rounded_card(box_x, box_y, box_w, box_h, 8, g_dino.night_mode ? 0xFF141010 : 0xFFFFFFFF, IPS_ACCENT_ROSE);
            c_draw_text(box_x + 36, box_y + 13, is_ru ? "И Г Р А  О К О Н Ч Е Н А" : "G A M E   O V E R", IPS_ACCENT_ROSE);

            char res_str[32];
            snprintf(res_str, sizeof(res_str), is_ru ? "Счет: %d м" : "Score: %d m", g_dino.score);
            c_draw_text(box_x + 46, box_y + 33, res_str, fg_col);

            c_draw_text(box_x + 16, box_y + 53, is_ru ? "[Клик Кнопки] Рестарт" : "[Click Btn] RESTART", IPS_ACCENT_AMBER);
        } else {
            c_draw_text(16, g_disp_h - 16, is_ru ? "[Влево/Вниз/S] Присесть  [Клик/Enter] Прыжок" : "[Down/S] Duck  [Click/Enter] Jump", fg_col);
        }
    } else {
        // OLED 128x64 Mode
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        char oled_score[24];
        snprintf(oled_score, sizeof(oled_score), "HI:%d  %d", g_dino.high_score, g_dino.score);
        c_draw_text(2, 1, oled_score, COLOR_BLACK);

        c_draw_line(0, ground_y, OLED_W, ground_y, g_active_color);

        // Dino
        int dy = ground_y + (int)g_dino.player_y;
        if (!g_dino.is_ducking) {
            c_draw_rect_fill(dino_x - 2, dy - 12, 6, 12, g_active_color);
            c_draw_rect_fill(dino_x + 2, dy - 14, 4, 6, g_active_color);
        } else {
            c_draw_rect_fill(dino_x - 4, dy - 6, 12, 6, g_active_color);
        }

        // Obstacles
        for (int i = 0; i < MAX_DINO_OBSTACLES; i++) {
            int ox = (int)g_dino.obstacles[i].x;
            if (ox > 0 && ox < 125) {
                if (g_dino.obstacles[i].type == 3) {
                    c_draw_rect_fill(ox - 3, (int)g_dino.obstacles[i].fly_y, 6, 4, g_active_color);
                } else {
                    c_draw_rect_fill(ox - 2, ground_y - 10, 4, 10, g_active_color);
                }
            }
        }

        if (g_dino.game_over) {
            c_draw_text(20, 24, is_ru ? "КОНЕЦ! [Клик]" : "OVER! [Click]", g_active_color);
        }
    }
}

// 9. RETRO PONG ARCADE WITH DIFFICULTY SELECT
typedef struct {
    const char* name_en;
    const char* name_ru;
    float ai_speed;
    float ball_speed_ips;
    float ball_speed_oled;
    int   paddle_h_ips;
    int   paddle_h_oled;
    uint32_t color;
} pong_diff_profile_t;

static const pong_diff_profile_t g_pong_diffs[PONG_DIFF_COUNT] = {
    {"EASY",      "ЛЕГКИЙ",    0.080f, 2.6f, 1.3f, 44, 16, IPS_ACCENT_EMERALD},
    {"NORMAL",    "СРЕДНИЙ",   0.125f, 3.6f, 1.8f, 36, 13, IPS_ACCENT_GLACIER},
    {"HARD",      "СЛОЖНЫЙ",   0.190f, 4.6f, 2.3f, 28, 10, IPS_ACCENT_AMBER},
    {"INSANE",    "БЕЗУМИЕ",   0.280f, 5.8f, 2.9f, 22,  8, IPS_ACCENT_ROSE}
};

static void c_render_pong_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    // STATE 1: SELECT DIFFICULTY
    if (g_pong.state == PONG_STATE_SELECT_DIFF) {
        if (is_ips) {
            c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
            c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
            c_draw_text(16, 11, is_ru ? "PONG: ВЫБОР СЛОЖНОСТИ" : "PONG: SELECT DIFFICULTY", IPS_TEXT_PRIMARY);

            int card_w = g_disp_w - 24;
            int card_h = 36;
            int start_y = 38;
            int gap = 7;

            for (int i = 0; i < PONG_DIFF_COUNT; i++) {
                int y = start_y + i * (card_h + gap);
                bool selected = (i == g_pong.diff_select_idx);

                uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
                uint32_t border = selected ? g_pong_diffs[i].color : IPS_CARD_BORDER;
                uint32_t text_col = selected ? g_pong_diffs[i].color : IPS_TEXT_SECONDARY;

                c_draw_rounded_card(12, y, card_w, card_h, 6, bg, border);

                char diff_buf[48];
                const char* d_name = is_ru ? g_pong_diffs[i].name_ru : g_pong_diffs[i].name_en;
                snprintf(diff_buf, sizeof(diff_buf), selected ? "● %s" : "  %s", d_name);
                c_draw_text(22, y + 14, diff_buf, text_col);

                // Speed tag on right side
                const char* speed_tag = (i == 0) ? (is_ru ? "Скорость: 1x" : "Speed: 1x") :
                                        ((i == 1) ? (is_ru ? "Скорость: 1.5x" : "Speed: 1.5x") :
                                        ((i == 2) ? (is_ru ? "Скорость: 2x" : "Speed: 2x") :
                                                    (is_ru ? "ЭКСТРИМ 🔥" : "EXTREME 🔥")));
                c_draw_text(card_w - 75, y + 14, speed_tag, selected ? IPS_TEXT_PRIMARY : IPS_TEXT_MUTED);
            }

            c_draw_text(16, g_disp_h - 16, is_ru ? "[Крутилка] Выбор  [Клик] Старт" : "[Knob] Select  [Click] Play", IPS_TEXT_MUTED);
        } else {
            // OLED 128x64
            c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
            c_draw_text(2, 1, is_ru ? "СЛОЖНОСТЬ PONG" : "PONG DIFFICULTY", COLOR_BLACK);

            const int item_h = 13;
            for (int i = 0; i < PONG_DIFF_COUNT; i++) {
                int y = 11 + i * item_h;
                bool selected = (i == g_pong.diff_select_idx);
                if (selected) {
                    c_draw_rect_fill(0, y, OLED_W, item_h, g_active_color);
                }
                uint32_t col = selected ? COLOR_BLACK : g_active_color;
                const char* d_name = is_ru ? g_pong_diffs[i].name_ru : g_pong_diffs[i].name_en;
                char buf[32];
                snprintf(buf, sizeof(buf), "%d. %s", i + 1, d_name);
                c_draw_text(4, y + 3, buf, col);
            }
        }
        return;
    }

    // STATE 2: GAME OVER / MATCH WINNER
    if (g_pong.state == PONG_STATE_GAME_OVER) {
        bool player_won = (g_pong.player_score >= 5);
        if (is_ips) {
            c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
            int box_w = g_disp_w - 40;
            int box_h = 100;
            int box_x = 20;
            int box_y = (g_disp_h - box_h) / 2;

            uint32_t border_col = player_won ? IPS_ACCENT_EMERALD : IPS_ACCENT_ROSE;
            c_draw_rounded_card(box_x, box_y, box_w, box_h, 8, IPS_CARD_BG, border_col);

            const char* res_title = player_won ? (is_ru ? "ПОБЕДА ИГРОКА!" : "VICTORY! YOU WON!") :
                                                (is_ru ? "ПОРАЖЕНИЕ!" : "AI DEFEATED YOU!");
            c_draw_text(box_x + 20, box_y + 18, res_title, border_col);

            char score_str[32];
            snprintf(score_str, sizeof(score_str), is_ru ? "Финальный счет: %d - %d" : "Final Score: %d - %d", g_pong.player_score, g_pong.ai_score);
            c_draw_text(box_x + 20, box_y + 42, score_str, IPS_TEXT_PRIMARY);

            c_draw_text(box_x + 12, box_y + 72, is_ru ? "[Клик] Сложность / Реванш" : "[Click] Difficulty / Rematch", IPS_ACCENT_AMBER);
        } else {
            c_draw_rect_fill(0, 0, OLED_W, OLED_H, COLOR_BLACK);
            c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
            c_draw_text(8, 1, player_won ? "VICTORY! YOU WON" : "AI WON THE MATCH", COLOR_BLACK);

            char score_str[32];
            snprintf(score_str, sizeof(score_str), "Score: %d - %d", g_pong.player_score, g_pong.ai_score);
            c_draw_text(20, 24, score_str, g_active_color);
            c_draw_text(4, 46, "[Click] Play Again", g_active_color);
        }
        return;
    }

    // STATE 3: ACTIVE GAMEPLAY
    const pong_diff_profile_t* cur_diff = &g_pong_diffs[g_pong.diff];
    int max_y = is_ips ? (g_disp_h - 10) : 58;
    int min_y = 10;
    int ph_player = is_ips ? cur_diff->paddle_h_ips : cur_diff->paddle_h_oled;
    int ph_ai = is_ips ? ((g_pong.diff == PONG_DIFF_EASY) ? 34 : cur_diff->paddle_h_ips) : 
                         ((g_pong.diff == PONG_DIFF_EASY) ? 12 : cur_diff->paddle_h_oled);

    if (g_pong.ball_in_play) {
        g_pong.ball_x += g_pong.ball_vx;
        g_pong.ball_y += g_pong.ball_vy;

        if (g_pong.ball_y <= min_y) { g_pong.ball_y = (float)min_y; g_pong.ball_vy = -g_pong.ball_vy; }
        else if (g_pong.ball_y >= max_y) { g_pong.ball_y = (float)max_y; g_pong.ball_vy = -g_pong.ball_vy; }

        float ai_diff = g_pong.ball_y - g_pong.ai_paddle_y;
        g_pong.ai_paddle_y += ai_diff * cur_diff->ai_speed;

        int right_x = is_ips ? (g_disp_w - 14) : 122;
        int left_x = is_ips ? 14 : 6;

        float bounce_accel = (g_pong.diff == PONG_DIFF_EASY) ? 1.025f : 1.04f;

        if (g_pong.ball_x <= left_x + 6 && g_pong.ball_vx < 0) {
            if (fabsf(g_pong.ball_y - g_pong.paddle_y) <= (ph_player / 2 + 3)) {
                g_pong.ball_vx = -g_pong.ball_vx * bounce_accel;
                g_pong.ball_vy += (g_pong.ball_y - g_pong.paddle_y) * 0.16f;
                g_pong.rally_count++;
            } else if (g_pong.ball_x <= 0) {
                g_pong.ai_score++;
                g_pong.ball_in_play = false;
                if (g_pong.ai_score >= 5) g_pong.state = PONG_STATE_GAME_OVER;
            }
        }

        if (g_pong.ball_x >= right_x - 6 && g_pong.ball_vx > 0) {
            if (fabsf(g_pong.ball_y - g_pong.ai_paddle_y) <= (ph_ai / 2 + 4)) {
                g_pong.ball_vx = -g_pong.ball_vx * bounce_accel;
                g_pong.ball_vy += (g_pong.ball_y - g_pong.ai_paddle_y) * 0.14f;
                g_pong.rally_count++;
            } else if (g_pong.ball_x >= (is_ips ? g_disp_w : 128)) {
                g_pong.player_score++;
                g_pong.ball_in_play = false;
                if (g_pong.player_score >= 5) g_pong.state = PONG_STATE_GAME_OVER;
            }
        }
    } else {
        g_pong.ball_x = is_ips ? 30.0f : 16.0f;
        g_pong.ball_y = g_pong.paddle_y;
    }

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, 0xFF050403);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);

        char score_str[32];
        snprintf(score_str, sizeof(score_str), "YOU: %d   AI: %d", g_pong.player_score, g_pong.ai_score);
        c_draw_text(16, 9, score_str, IPS_ACCENT_GLACIER);

        // Difficulty Badge on right
        const char* d_name = is_ru ? cur_diff->name_ru : cur_diff->name_en;
        c_draw_text(g_disp_w - 75, 9, d_name, cur_diff->color);

        for (int y = 30; y < g_disp_h - 5; y += 8) c_draw_rect_fill(g_disp_w / 2, y, 2, 4, IPS_CARD_BORDER);

        c_draw_rounded_card(10, (int)g_pong.paddle_y - ph_player / 2, 6, ph_player, 2, IPS_ACCENT_GLACIER, 0);
        c_draw_rounded_card(g_disp_w - 16, (int)g_pong.ai_paddle_y - ph_ai / 2, 6, ph_ai, 2, cur_diff->color, 0);
        c_draw_rect_fill((int)g_pong.ball_x - 3, (int)g_pong.ball_y - 3, 6, 6, 0xFFFFFFFF);

        if (!g_pong.ball_in_play) {
            int box_w = 160;
            int box_x = (g_disp_w - box_w) / 2;
            int box_y = (g_disp_h - 30) / 2;
            c_draw_rounded_card(box_x, box_y, box_w, 30, 6, IPS_CARD_BG, IPS_CARD_BORDER);
            c_draw_text(box_x + 12, box_y + 11, is_ru ? "[Клик] ПОДАЧА МЯЧА" : "[Click] SERVE BALL", IPS_ACCENT_AMBER);
        }
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        char score_str[16];
        snprintf(score_str, sizeof(score_str), "P:%d A:%d %c", g_pong.player_score, g_pong.ai_score, cur_diff->name_en[0]);
        c_draw_text(OLED_W / 2 - 24, 1, score_str, COLOR_BLACK);

        int py = (int)((g_pong.paddle_y * 45.0f) / 240.0f) + 12;
        int ay = (int)((g_pong.ai_paddle_y * 45.0f) / 240.0f) + 12;
        int bx = (int)((g_pong.ball_x * 120.0f) / 240.0f) + 4;
        int by = (int)((g_pong.ball_y * 45.0f) / 240.0f) + 12;

        c_draw_rect_fill(2, py - ph_player / 2, 2, ph_player, g_active_color);
        c_draw_rect_fill(OLED_W - 4, ay - ph_ai / 2, 2, ph_ai, g_active_color);
        c_draw_rect_fill(bx, by, 3, 3, g_active_color);

        if (!g_pong.ball_in_play) c_draw_text(16, 30, is_ru ? "[Клик] Подача" : "[Click] Serve", g_active_color);
    }
}

// 10. SYSTEM TELEMETRY VIEW
static void c_render_sys_info_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
        c_draw_text(16, 11, is_ru ? "СТАТУС И СИСТЕМА" : "HARDWARE DASHBOARD", IPS_TEXT_PRIMARY);

        int card_w = g_disp_w - 24;
        int card_h = g_disp_h - 60;
        c_draw_rounded_card(12, 34, card_w, card_h, 6, IPS_CARD_BG, IPS_CARD_BORDER);

        int sy = 46;
        int step = 20;

        c_draw_text(24, sy, "SoC MCU:", IPS_TEXT_MUTED);
        char soc_buf[32];
        snprintf(soc_buf, sizeof(soc_buf), "%s (%d Cores)", g_telemetry.chip_model, g_telemetry.chip_cores);
        c_draw_text(96, sy, soc_buf, IPS_TEXT_PRIMARY);

        c_draw_text(24, sy + step, "CLOCK:", IPS_TEXT_MUTED);
        char clk_buf[32];
        snprintf(clk_buf, sizeof(clk_buf), "%d MHz (%s)", g_telemetry.cpu_freq_mhz, g_telemetry.arch_name);
        c_draw_text(96, sy + step, clk_buf, IPS_ACCENT_EMERALD);

        c_draw_text(24, sy + step * 2, "PSRAM:", IPS_TEXT_MUTED);
        char psram_buf[32];
        if (g_telemetry.total_psram_bytes > 0) {
            snprintf(psram_buf, sizeof(psram_buf), "%u MB (Free: %u KB)", 
                     (unsigned int)(g_telemetry.total_psram_bytes / (1024 * 1024)), 
                     (unsigned int)(g_telemetry.free_psram_bytes / 1024));
        } else {
            snprintf(psram_buf, sizeof(psram_buf), "None (Internal SRAM)");
        }
        c_draw_text(96, sy + step * 2, psram_buf, IPS_ACCENT_GLACIER);

        c_draw_text(24, sy + step * 3, "SRAM HEAP:", IPS_TEXT_MUTED);
        char sram_buf[32];
        snprintf(sram_buf, sizeof(sram_buf), "%u KB (Free: %u KB)", 
                 (unsigned int)(g_telemetry.total_sram_bytes / 1024), 
                 (unsigned int)(g_telemetry.free_heap_bytes / 1024));
        c_draw_text(96, sy + step * 3, sram_buf, IPS_ACCENT_EMERALD);

        c_draw_text(24, sy + step * 4, "FLASH:", IPS_TEXT_MUTED);
        char flash_buf[32];
        snprintf(flash_buf, sizeof(flash_buf), "%u MB (LittleFS)", 
                 (unsigned int)(g_telemetry.total_flash_bytes / (1024 * 1024)));
        c_draw_text(96, sy + step * 4, flash_buf, IPS_ACCENT_AMBER);

        c_draw_text(24, sy + step * 5, "TEMP SENSOR:", IPS_TEXT_MUTED);
        char temp_buf[24];
        if (g_telemetry.chip_temp_c > 0.0f) {
            snprintf(temp_buf, sizeof(temp_buf), "%.1f C (Normal)", g_telemetry.chip_temp_c);
        } else {
            snprintf(temp_buf, sizeof(temp_buf), "N/A (No Sensor)");
        }
        c_draw_text(105, sy + step * 5, temp_buf, IPS_ACCENT_EMERALD);

        c_draw_text(16, g_disp_h - 16, is_ru ? "[Клик/Удерж] Назад в Меню" : "[Press Knob] Back to Menu", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "SYSTEM SPECS", COLOR_BLACK);

        char line1[32], line2[32], line3[32], line4[32];
        snprintf(line1, sizeof(line1), "CPU: %s %dM", g_telemetry.chip_model, g_telemetry.cpu_freq_mhz);
        if (g_telemetry.total_psram_bytes > 0) {
            snprintf(line2, sizeof(line2), "PSRAM: %uMB OK", (unsigned int)(g_telemetry.total_psram_bytes / (1024 * 1024)));
        } else {
            snprintf(line2, sizeof(line2), "PSRAM: None (SRAM)");
        }
        snprintf(line3, sizeof(line3), "SRAM: %uK Free", (unsigned int)(g_telemetry.free_heap_bytes / 1024));
        snprintf(line4, sizeof(line4), "FLASH: %uMB", (unsigned int)(g_telemetry.total_flash_bytes / (1024 * 1024)));

        c_draw_text(4, 13, line1, g_active_color);
        c_draw_text(4, 23, line2, g_active_color);
        c_draw_text(4, 33, line3, g_active_color);
        c_draw_text(4, 43, line4, g_active_color);

        c_draw_rect_fill(0, 54, OLED_W, 10, 0xFF111111);
        c_draw_text(8, 55, "[Knob Click] Back", g_active_color);
    }
}

// ============================================================================
// 11. HARDWARE BUS AUTO-DISCOVERY & SCANNER (FAULT-TOLERANT)
// ============================================================================
typedef struct {
    const char* name;
    const char* bus;
    const char* addr;
    const char* desc;
    bool detected;
} hw_device_profile_t;

static hw_device_profile_t g_hw_devices[] = {
    {"ESP32-S3 SoC",    "CORE", "240MHz", "Dual Xtensa LX7",      true},
    {"8MB Octal PSRAM", "SPI",  "OCTAL",  "FB & Heap High-Speed",  true},
    {"16MB SPI Flash",  "SPI",  "QUAD",   "LittleFS Storage",      true},
    {"Wi-Fi + BLE 5.0", "RF",   "2.4GHz", "Promiscuous & Sniff",   true},
    {"CC1101 Sub-1GHz", "SPI",  "CS=10",  "300-928MHz RF Radio",   false},
    {"RC522 13.56MHz",  "SPI",  "CS=13",  "RFID / Mifare NFC",     false},
    {"NRF24L01+ 2.4G",  "SPI",  "CS=14",  "2.4GHz Enhanced Radio", false},
    {"PN532 NFC Module","I2C",  "0x24",   "NFC Card Emulation",    false},
    {"SSD1306 OLED",    "I2C",  "0x3C",   "128x64 Display Bus",    false},
    {"BMP280 / BME280", "I2C",  "0x76",   "Baro & Temp Sensor",    false},
    {"MPU6050 Accel",   "I2C",  "0x68",   "6-Axis Gyro / Motion",  false},
    {"ADS1115 16b ADC", "I2C",  "0x48",   "Precision Voltage ADC", false},
    {"USB OTG Host",    "USB",  "GPIO19", "HID Keyboard/Ducky",    true},
    {"MicroSD Card",    "SPI",  "CS=4",   "FAT32 Storage Expand",  false}
};
#define HW_DEVICES_COUNT (sizeof(g_hw_devices) / sizeof(g_hw_devices[0]))

static int g_hw_scroll_idx = 0;
static bool g_hw_has_cc1101 = true;

EXPORT void wifi_ui_set_hw_device_detected(int dev_idx, bool detected) {
    if (dev_idx >= 0 && dev_idx < (int)HW_DEVICES_COUNT) {
        g_hw_devices[dev_idx].detected = detected;
        if (dev_idx == 4) g_hw_has_cc1101 = detected;
    }
}

EXPORT void hw_bus_scan(void) {
    // Basic SoC onboard peripherals
    g_hw_devices[0].detected = true;
    g_hw_devices[1].detected = true;
    g_hw_devices[2].detected = true;
    g_hw_devices[3].detected = true;
    g_hw_devices[12].detected = true; // Native USB OTG
}

static void c_render_hw_scanner_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, is_ru ? "СКАНИРОВАНИЕ ШИН И МОДУЛЕЙ" : "HARDWARE BUS PROBE & SCAN", IPS_TEXT_PRIMARY);

        int detected_count = 0;
        for (size_t i = 0; i < HW_DEVICES_COUNT; i++) {
            if (g_hw_devices[i].detected) detected_count++;
        }
        char cnt_str[24];
        snprintf(cnt_str, sizeof(cnt_str), "%d/%d ONLINE", detected_count, (int)HW_DEVICES_COUNT);
        c_draw_rounded_card(g_disp_w - 95, 5, 85, 16, 4, IPS_BG_COLOR, IPS_ACCENT_EMERALD);
        c_draw_text(g_disp_w - 90, 9, cnt_str, IPS_ACCENT_EMERALD);

        int start_y = 32;
        const int item_h = 32;
        int card_w = g_disp_w - 24;
        int visible_count = (g_disp_h - 45) / (item_h + 4);
        if (visible_count < 4) visible_count = 4;

        int start_idx = g_hw_scroll_idx;
        if (start_idx > (int)HW_DEVICES_COUNT - visible_count) start_idx = (int)HW_DEVICES_COUNT - visible_count;
        if (start_idx < 0) start_idx = 0;

        for (int i = 0; i < visible_count && (start_idx + i) < (int)HW_DEVICES_COUNT; i++) {
            int idx = start_idx + i;
            int y = start_y + i * (item_h + 4);
            bool selected = (idx == g_hw_scroll_idx);
            hw_device_profile_t* dev = &g_hw_devices[idx];

            uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
            uint32_t border = selected ? IPS_ACCENT_GLACIER : (dev->detected ? IPS_CARD_BORDER : 0xFF221510);

            c_draw_rounded_card(12, y, card_w, item_h, 5, bg, border);

            uint32_t bus_col = strcmp(dev->bus, "I2C") == 0 ? IPS_ACCENT_GLACIER :
                              (strcmp(dev->bus, "SPI") == 0 ? IPS_ACCENT_AMBER :
                              (strcmp(dev->bus, "USB") == 0 ? IPS_ACCENT_EMERALD : IPS_ACCENT_ROSE));
            c_draw_rounded_card(18, y + 4, 34, 12, 3, 0xFF140D08, bus_col);
            c_draw_text(22, y + 7, dev->bus, bus_col);

            c_draw_text(58, y + 6, dev->name, selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY);
            c_draw_text(58, y + 18, dev->desc, IPS_TEXT_MUTED);

            if (dev->detected) {
                c_draw_rounded_card(card_w - 48, y + 8, 56, 14, 3, 0xFF0D2214, IPS_ACCENT_EMERALD);
                c_draw_text(card_w - 44, y + 11, "ONLINE ●", IPS_ACCENT_EMERALD);
            } else {
                c_draw_rounded_card(card_w - 48, y + 8, 56, 14, 3, 0xFF1E1410, 0xFF553322);
                c_draw_text(card_w - 44, y + 11, "OPTIONAL", IPS_TEXT_MUTED);
            }
        }

        c_draw_text(16, g_disp_h - 14, is_ru ? "[Крутилка] Список | [Клик] Скан шины | [Удерж] Назад" : "[Knob] Scroll | [Click] Probe Bus | [Hold] Back", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "HW BUS PROBE", COLOR_BLACK);

        for (int i = 0; i < 4 && (g_hw_scroll_idx + i) < (int)HW_DEVICES_COUNT; i++) {
            int idx = g_hw_scroll_idx + i;
            int y = 11 + i * 11;
            char line[32];
            snprintf(line, sizeof(line), "%s: %s", g_hw_devices[idx].name, g_hw_devices[idx].detected ? "OK" : "NO");
            c_draw_text(2, y, line, g_hw_devices[idx].detected ? g_active_color : COLOR_OLED_WHITE);
        }
        c_draw_text(2, 56, "[Turn] Scrl [Click] Scan", g_active_color);
    }
}

// ============================================================================
// 12. SUB-GHZ RF TRANSCEIVER (CC1101 - RECORD & REPLAY)
// ============================================================================
EXPORT void wifi_ui_set_cc1101_detected(bool detected) {
    g_hw_has_cc1101 = detected;
    g_hw_devices[4].detected = detected;
}

EXPORT bool wifi_ui_get_cc1101_detected(void) {
    return g_hw_has_cc1101;
}

typedef enum {
    SUBGHZ_PAGE_MENU = 0,
    SUBGHZ_PAGE_RECORD,
    SUBGHZ_PAGE_REPLAY,
    SUBGHZ_PAGE_ANALYZER,
    SUBGHZ_PAGE_FREQ_SELECT
} subghz_page_t;

typedef struct {
    char name[24];
    float freq_mhz;
    const char* modulation;
    uint16_t pulse_count;
    uint32_t sample_hash;
} subghz_slot_t;

#define SUBGHZ_MAX_SLOTS 4
static subghz_slot_t g_subghz_slots[SUBGHZ_MAX_SLOTS] = {
    {"Gate CAME 433",   433.92f, "ASK/OOK", 64, 0xA18F42},
    {"Barrier NICE",    433.92f, "ASK/OOK", 52, 0xB720C3},
    {"Doorhan 433M",    433.92f, "2-FSK",   78, 0xC49911},
    {"Remote Raw 868",  868.35f, "ASK/OOK", 96, 0xD833AA}
};

static const float g_subghz_freqs[] = {433.92f, 315.00f, 868.35f, 915.00f, 434.42f};
#define SUBGHZ_FREQS_COUNT (sizeof(g_subghz_freqs) / sizeof(g_subghz_freqs[0]))

typedef struct {
    subghz_page_t page;
    int menu_idx;
    int freq_idx;
    int slot_idx;
    int8_t rssi_dbm;
    bool is_recording;
    bool is_transmitting;
    int tx_timer;
    int captured_pulses;
    uint8_t live_waveform[40];
    char last_toast[48];
    int toast_timer;
} subghz_engine_state_t;

static subghz_engine_state_t g_subghz = {
    .page = SUBGHZ_PAGE_MENU,
    .menu_idx = 0,
    .freq_idx = 0, // 433.92 MHz
    .slot_idx = 0,
    .rssi_dbm = -92,
    .is_recording = false,
    .is_transmitting = false,
    .tx_timer = 0,
    .captured_pulses = 0,
    .last_toast = "",
    .toast_timer = 0
};

static void subghz_trigger_tx(int slot) {
    if (slot < 0 || slot >= SUBGHZ_MAX_SLOTS) return;
    g_subghz.is_transmitting = true;
    g_subghz.tx_timer = 60; // ~2 seconds transmission animation
    g_subghz.toast_timer = 90;

    subghz_slot_t* s = &g_subghz_slots[slot];
    snprintf(g_subghz.last_toast, sizeof(g_subghz.last_toast), "[TX] Replayed: %s (%.2fM)", s->name, s->freq_mhz);

    char tmsg[64];
    snprintf(tmsg, sizeof(tmsg), "CC1101 TX: %.2fMHz [%s] %d pulses", s->freq_mhz, s->modulation, s->pulse_count);
    term_print(tmsg);
}

static void c_render_subghz_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (g_subghz.toast_timer > 0) g_subghz.toast_timer--;
    if (g_subghz.tx_timer > 0) {
        g_subghz.tx_timer--;
        if (g_subghz.tx_timer == 0) g_subghz.is_transmitting = false;
    }

    // Simulate realistic background RF noise / incoming pulses when recording
    if (g_subghz.is_recording) {
        g_subghz.rssi_dbm = (int8_t)(-40 - (rand() % 25));
        if (rand() % 3 == 0) {
            g_subghz.captured_pulses += (rand() % 4) + 1;
            if (g_subghz.captured_pulses > 180) g_subghz.captured_pulses = 180;
        }
        for (int i = 0; i < 39; i++) g_subghz.live_waveform[i] = g_subghz.live_waveform[i + 1];
        g_subghz.live_waveform[39] = (rand() % 4 == 0) ? (uint8_t)(12 + rand() % 16) : (uint8_t)(rand() % 4);
    } else {
        g_subghz.rssi_dbm = (int8_t)(-95 - (rand() % 8));
    }

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);

        // Header
        c_draw_rect_fill(0, 0, g_disp_w, 24, IPS_CARD_BG);
        c_draw_text(12, 7, is_ru ? "SUB-GHZ RF (CC1101)" : "SUB-GHZ TRANSCEIVER", IPS_TEXT_PRIMARY);

        if (g_hw_has_cc1101) {
            c_draw_rounded_card(g_disp_w - 62, 5, 54, 14, 3, 0xFF0D2214, IPS_ACCENT_EMERALD);
            c_draw_text(g_disp_w - 58, 8, "ONLINE ●", IPS_ACCENT_EMERALD);
        } else {
            c_draw_rounded_card(g_disp_w - 62, 5, 54, 14, 3, 0xFF1E1410, IPS_ACCENT_ROSE);
            c_draw_text(g_disp_w - 58, 8, "NO CHIP", IPS_ACCENT_ROSE);
        }

        if (g_subghz.page == SUBGHZ_PAGE_MENU) {
            // Top Status Bar: Frequency & Modulation
            c_draw_rounded_card(10, 28, g_disp_w - 20, 32, 5, IPS_CARD_BG, IPS_CARD_BORDER);
            char fstr[48];
            snprintf(fstr, sizeof(fstr), "FREQ: %.2f MHz | OOK/ASK | RSSI: %ddBm", g_subghz_freqs[g_subghz.freq_idx], g_subghz.rssi_dbm);
            c_draw_text(16, 38, fstr, IPS_ACCENT_AMBER);

            // Sub-GHz Main Menu Items
            const char* sub_items_en[] = {
                "[REC] Read & Record RAW Signal",
                "[TX]  Saved Slots & Replay",
                "[FRQ] Frequency Analyzer",
                "[CFG] Change Frequency / Preset"
            };
            const char* sub_items_ru[] = {
                "[REC] Запись RAW радиосигнала",
                "[TX]  Сохраненные пульты и Реплей",
                "[FRQ] Анализатор частоты (Сканер)",
                "[CFG] Выбор рабочей частоты"
            };

            int start_y = 66;
            for (int i = 0; i < 4; i++) {
                int y = start_y + i * 36;
                bool selected = (i == g_subghz.menu_idx);
                uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
                uint32_t border = selected ? IPS_ACCENT_AMBER : IPS_CARD_BORDER;

                c_draw_rounded_card(10, y, g_disp_w - 20, 32, 5, bg, border);
                c_draw_text(20, y + 10, is_ru ? sub_items_ru[i] : sub_items_en[i], selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY);

                if (selected) {
                    c_draw_rounded_card(g_disp_w - 48, y + 7, 36, 16, 3, 0xFF221408, IPS_ACCENT_AMBER);
                    c_draw_text(g_disp_w - 44, y + 10, "GO ▶", IPS_ACCENT_AMBER);
                }
            }
        } 
        else if (g_subghz.page == SUBGHZ_PAGE_RECORD) {
            // Live Recording Screen with Waterfall
            c_draw_rounded_card(10, 28, g_disp_w - 20, 48, 5, IPS_CARD_BG, IPS_CARD_BORDER);
            char rec_hdr[48];
            snprintf(rec_hdr, sizeof(rec_hdr), "FREQ: %.2f MHz  RSSI: %d dBm", g_subghz_freqs[g_subghz.freq_idx], g_subghz.rssi_dbm);
            c_draw_text(16, 34, rec_hdr, IPS_ACCENT_AMBER);

            char pulse_hdr[48];
            snprintf(pulse_hdr, sizeof(pulse_hdr), "Captured: %d pulses %s", g_subghz.captured_pulses, g_subghz.is_recording ? "[REC ●]" : "[IDLE]");
            c_draw_text(16, 52, pulse_hdr, g_subghz.is_recording ? IPS_ACCENT_ROSE : IPS_TEXT_MUTED);

            // Oscilloscope Waveform Card
            c_draw_rounded_card(10, 82, g_disp_w - 20, 80, 5, 0xFF05080E, IPS_CARD_BORDER);
            c_draw_text(16, 88, "LIVE RAW DEMODULATOR", IPS_TEXT_MUTED);

            int base_y = 150;
            for (int i = 0; i < 39; i++) {
                int x1 = 18 + i * 5;
                int x2 = 18 + (i + 1) * 5;
                int h1 = g_subghz.live_waveform[i];
                int h2 = g_subghz.live_waveform[i + 1];
                c_draw_line(x1, base_y - h1, x2, base_y - h2, g_subghz.is_recording ? IPS_ACCENT_EMERALD : IPS_ACCENT_GLACIER);
            }

            // Controls
            int btn_y = 172;
            c_draw_rounded_card(10, btn_y, (g_disp_w - 28) / 2, 32, 5, g_subghz.is_recording ? 0xFF331111 : IPS_CARD_BG, IPS_ACCENT_ROSE);
            c_draw_text(22, btn_y + 10, g_subghz.is_recording ? "■ STOP REC" : "● START REC", IPS_ACCENT_ROSE);

            c_draw_rounded_card((g_disp_w / 2) + 4, btn_y, (g_disp_w - 28) / 2, 32, 5, IPS_CARD_BG, IPS_ACCENT_AMBER);
            c_draw_text((g_disp_w / 2) + 16, btn_y + 10, "💾 SAVE SLOT", IPS_ACCENT_AMBER);
        }
        else if (g_subghz.page == SUBGHZ_PAGE_REPLAY) {
            // Replay Slots Carousel
            c_draw_rounded_card(10, 28, g_disp_w - 20, 26, 4, IPS_CARD_BG, IPS_CARD_BORDER);
            c_draw_text(16, 34, is_ru ? "СОХРАНЕННЫЕ СИГНАЛЫ ДЛЯ ПОВТОРА" : "SAVED SIGNALS FOR REPLAY", IPS_ACCENT_AMBER);

            int start_y = 60;
            for (int i = 0; i < SUBGHZ_MAX_SLOTS; i++) {
                int y = start_y + i * 38;
                bool selected = (i == g_subghz.slot_idx);
                subghz_slot_t* s = &g_subghz_slots[i];

                uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
                uint32_t border = selected ? (g_subghz.is_transmitting ? IPS_ACCENT_ROSE : IPS_ACCENT_AMBER) : IPS_CARD_BORDER;

                c_draw_rounded_card(10, y, g_disp_w - 20, 34, 5, bg, border);

                char slot_title[48];
                snprintf(slot_title, sizeof(slot_title), "Slot %d: %s", i + 1, s->name);
                c_draw_text(18, y + 6, slot_title, selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY);

                char slot_desc[48];
                snprintf(slot_desc, sizeof(slot_desc), "%.2f MHz | %s | %d pulses", s->freq_mhz, s->modulation, s->pulse_count);
                c_draw_text(18, y + 19, slot_desc, IPS_TEXT_MUTED);

                if (selected) {
                    if (g_subghz.is_transmitting) {
                        c_draw_rounded_card(g_disp_w - 56, y + 7, 44, 18, 3, 0xFF330D14, IPS_ACCENT_ROSE);
                        c_draw_text(g_disp_w - 52, y + 10, "TX >>>", IPS_ACCENT_ROSE);
                    } else {
                        c_draw_rounded_card(g_disp_w - 56, y + 7, 44, 18, 3, 0xFF221408, IPS_ACCENT_AMBER);
                        c_draw_text(g_disp_w - 52, y + 10, "SEND ▶", IPS_ACCENT_AMBER);
                    }
                }
            }
        }
        else if (g_subghz.page == SUBGHZ_PAGE_ANALYZER) {
            // Frequency Analyzer
            c_draw_rounded_card(10, 28, g_disp_w - 20, 80, 5, 0xFF05080E, IPS_CARD_BORDER);
            c_draw_text(16, 36, "SUB-GHZ FREQUENCY ANALYZER", IPS_ACCENT_GLACIER);
            c_draw_text(16, 54, "Scanning 300MHz - 928MHz band...", IPS_TEXT_MUTED);

            char peak_str[48];
            snprintf(peak_str, sizeof(peak_str), "PEAK: %.2f MHz (RSSI: -42dBm)", g_subghz_freqs[g_subghz.freq_idx]);
            c_draw_text(16, 76, peak_str, IPS_ACCENT_AMBER);

            c_draw_rounded_card(10, 116, g_disp_w - 20, 80, 5, IPS_CARD_BG, IPS_CARD_BORDER);
            c_draw_text(16, 126, "ACTIVE SIGNALS FOUND:", IPS_TEXT_PRIMARY);
            c_draw_text(16, 144, "  1. 433.92 MHz - Gate Remotes (OOK)", IPS_ACCENT_EMERALD);
            c_draw_text(16, 162, "  2. 868.35 MHz - Security Alarm (FSK)", IPS_ACCENT_AMBER);
        }

        // Bottom Bar / Toast Feedback
        c_draw_rect_fill(0, g_disp_h - 22, g_disp_w, 22, IPS_CARD_BG);
        if (g_subghz.toast_timer > 0) {
            c_draw_text(12, g_disp_h - 15, g_subghz.last_toast, IPS_ACCENT_AMBER);
        } else {
            c_draw_text(12, g_disp_h - 15, is_ru ? "[Клик] Выбор/Действие | [Удерж] Назад" : "[Click] Select/Run | [Hold] Back", IPS_TEXT_MUTED);
        }
    } else {
        // OLED 128x64 Mode
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        char oled_hdr[32];
        snprintf(oled_hdr, sizeof(oled_hdr), "SUB-GHZ: %.2fM", g_subghz_freqs[g_subghz.freq_idx]);
        c_draw_text(2, 1, oled_hdr, COLOR_BLACK);

        if (g_subghz.page == SUBGHZ_PAGE_MENU) {
            c_draw_text(2, 12, g_subghz.menu_idx == 0 ? "> [1] REC RAW" : "  [1] REC RAW", g_subghz.menu_idx == 0 ? g_active_color : COLOR_OLED_WHITE);
            c_draw_text(2, 24, g_subghz.menu_idx == 1 ? "> [2] REPLAY TX" : "  [2] REPLAY TX", g_subghz.menu_idx == 1 ? g_active_color : COLOR_OLED_WHITE);
            c_draw_text(2, 36, g_subghz.menu_idx == 2 ? "> [3] SCAN ANALYZE" : "  [3] SCAN ANALYZE", g_subghz.menu_idx == 2 ? g_active_color : COLOR_OLED_WHITE);
            c_draw_text(2, 48, g_subghz.menu_idx == 3 ? "> [4] CHANGE FREQ" : "  [4] CHANGE FREQ", g_subghz.menu_idx == 3 ? g_active_color : COLOR_OLED_WHITE);
        } else if (g_subghz.page == SUBGHZ_PAGE_RECORD) {
            char rline[32];
            snprintf(rline, sizeof(rline), "PULSES: %d %s", g_subghz.captured_pulses, g_subghz.is_recording ? "[REC]" : "");
            c_draw_text(2, 14, rline, g_active_color);
            c_draw_text(2, 28, "[Click] Toggle Rec", COLOR_OLED_WHITE);
            c_draw_text(2, 42, "[Hold] Save & Exit", COLOR_OLED_WHITE);
        } else if (g_subghz.page == SUBGHZ_PAGE_REPLAY) {
            subghz_slot_t* s = &g_subghz_slots[g_subghz.slot_idx];
            char sline[32];
            snprintf(sline, sizeof(sline), "> %s", s->name);
            c_draw_text(2, 14, sline, g_active_color);
            c_draw_text(2, 28, g_subghz.is_transmitting ? "TRANSMITTING >>>" : "[Click] SEND TX", g_active_color);
        }

        c_draw_rect_fill(0, 55, OLED_W, 9, 0xFF111111);
        if (g_subghz.toast_timer > 0) {
            c_draw_text(2, 56, g_subghz.last_toast, g_active_color);
        } else {
            c_draw_text(2, 56, "[Click] OK [Hold] Back", g_active_color);
        }
    }
}

// ============================================================================
// 12. ANDROID MICRO-ADB ENGINE & CONTROLLER
// ============================================================================
typedef struct {
    const char* title_en;
    const char* title_ru;
    const char* cmd;
    const char* icon;
} adb_action_def_t;

static const adb_action_def_t g_adb_actions[] = {
    {"Screen Power / Wake", "Вкл/Выкл Экран",    "input keyevent 26",                    "[PWR]"},
    {"Home Button",         "Кнопка Домой",       "input keyevent 3",                     "[HOM]"},
    {"Back Button",         "Кнопка Назад",       "input keyevent 4",                     "[BCK]"},
    {"Unlock Swipe Up",     "Свайп Разблокировки","input swipe 500 1600 500 400 300",     "[UNL]"},
    {"Volume Up (+)",       "Громкость (+)",      "input keyevent 24",                    "[VL+]"},
    {"Volume Down (-)",     "Громкость (-)",      "input keyevent 25",                    "[VL-]"},
    {"Battery Telemetry",   "Статус Батареи",     "dumpsys battery",                      "[BAT]"},
    {"Take Screenshot",     "Сделать Скриншот",   "screencap -p /sdcard/shot.png",        "[CAP]"},
    {"Reboot Android",      "Перезагрузка",       "reboot",                               "[RBT]"},
    {"Reboot to Recovery",  "В Recovery (TWRP)",  "reboot recovery",                      "[RCV]"},
    {"Reboot Bootloader",   "В Fastboot/Bootload","reboot bootloader",                    "[FST]"},
    {"Open Bullet Web URL", "Открыть URL Web",    "am start -a VIEW -d http://bullet.local","[WEB]"},
    {"List 3rd-Party Apps", "Список Приложений",  "pm list packages -3",                  "[PKG]"}
};
#define ADB_ACTIONS_COUNT (sizeof(g_adb_actions) / sizeof(g_adb_actions[0]))

typedef struct {
    bool is_connected;
    char target_ip[32];
    char device_model[32];
    char android_version[24];
    uint8_t battery_level;
    bool is_charging;
    bool screen_on;
    int action_idx;
    char last_toast[48];
    int toast_timer;
} adb_state_t;

static adb_state_t g_adb = {
    .is_connected = false,
    .target_ip = "192.168.1.100:5555",
    .device_model = "No Device Attached",
    .android_version = "OFFLINE",
    .battery_level = 0,
    .is_charging = false,
    .screen_on = false,
    .action_idx = 0,
    .last_toast = "ADB: Disconnected",
    .toast_timer = 0
};

static void adb_trigger_action(int idx) {
    if (idx < 0 || idx >= (int)ADB_ACTIONS_COUNT) return;
    g_adb.toast_timer = 120;

    if (!g_adb.is_connected) {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[ERR] No device! adb connect <IP>");
        term_print("adb: no device/emulator found (connect first)");
        return;
    }

    const adb_action_def_t* act = &g_adb_actions[idx];
    if (idx == 0) {
        g_adb.screen_on = !g_adb.screen_on;
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] Screen %s", g_adb.screen_on ? "WOKEN UP" : "SLEEPING");
    } else if (idx == 6) {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] Bat: %d%%", g_adb.battery_level);
    } else if (idx == 8) {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] Rebooting system...");
    } else if (idx == 9) {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] Rebooting recovery...");
    } else if (idx == 10) {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] Rebooting fastboot...");
    } else {
        snprintf(g_adb.last_toast, sizeof(g_adb.last_toast), "[OK] %s", act->cmd);
    }

    char term_msg[64];
    snprintf(term_msg, sizeof(term_msg), "adb shell: %s", act->cmd);
    term_print(term_msg);

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof globalThis.onAdbCommandTriggered === 'function') {
            globalThis.onAdbCommandTriggered(UTF8ToString($0));
        }
    }, act->cmd);
#endif
}

EXPORT void wifi_ui_adb_set_device_info(const char* model, const char* version, int battery, bool is_connected) {
    if (model && strlen(model) > 0) {
        strncpy(g_adb.device_model, model, sizeof(g_adb.device_model) - 1);
        g_adb.device_model[sizeof(g_adb.device_model) - 1] = '\0';
    }
    if (version && strlen(version) > 0) {
        strncpy(g_adb.android_version, version, sizeof(g_adb.android_version) - 1);
        g_adb.android_version[sizeof(g_adb.android_version) - 1] = '\0';
    }
    if (battery >= 0 && battery <= 100) g_adb.battery_level = (uint8_t)battery;
    g_adb.is_connected = is_connected;
}

EXPORT void wifi_ui_adb_trigger_action(int action_idx) {
    adb_trigger_action(action_idx);
}

static void c_render_adb_app_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (g_adb.toast_timer > 0) g_adb.toast_timer--;

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);

        // Header
        c_draw_rect_fill(0, 0, g_disp_w, 24, IPS_CARD_BG);
        c_draw_text(12, 7, is_ru ? "МИКРО-ADB ПУЛЬТ" : "MICRO-ADB CONTROLLER", IPS_TEXT_PRIMARY);

        if (g_adb.is_connected) {
            c_draw_rounded_card(g_disp_w - 62, 5, 54, 14, 3, 0xFF0D2214, IPS_ACCENT_EMERALD);
            c_draw_text(g_disp_w - 58, 8, "ONLINE ●", IPS_ACCENT_EMERALD);
        } else {
            c_draw_rounded_card(g_disp_w - 62, 5, 54, 14, 3, 0xFF1E1410, IPS_TEXT_MUTED);
            c_draw_text(g_disp_w - 58, 8, "OFFLINE", IPS_TEXT_MUTED);
        }

        // Top Device Status Card
        int top_card_h = 36;
        c_draw_rounded_card(10, 28, g_disp_w - 20, top_card_h, 5, IPS_CARD_BG, IPS_CARD_BORDER);

        char dev_line1[48], dev_line2[48];
        snprintf(dev_line1, sizeof(dev_line1), "%s (%s)", g_adb.device_model, g_adb.android_version);
        snprintf(dev_line2, sizeof(dev_line2), "Bat: %d%% %s | Screen: %s | %s", 
                 g_adb.battery_level, g_adb.battery_level > 80 ? "[||||]" : "[||  ]",
                 g_adb.screen_on ? "ON" : "OFF", g_adb.target_ip);

        c_draw_text(18, 33, dev_line1, IPS_ACCENT_GLACIER);
        c_draw_text(18, 47, dev_line2, IPS_TEXT_SECONDARY);

        // Actions Carousel (3 visible cards)
        int start_y = 68;
        int item_h = 38;
        int max_visible = (g_disp_h - start_y - 28) / (item_h + 4);
        if (max_visible < 2) max_visible = 2;
        if (max_visible > 4) max_visible = 4;

        int start_idx = g_adb.action_idx - 1;
        if (start_idx < 0) start_idx = 0;
        if (start_idx + max_visible > (int)ADB_ACTIONS_COUNT) {
            start_idx = (int)ADB_ACTIONS_COUNT - max_visible;
            if (start_idx < 0) start_idx = 0;
        }

        for (int i = 0; i < max_visible && (start_idx + i) < (int)ADB_ACTIONS_COUNT; i++) {
            int idx = start_idx + i;
            int y = start_y + i * (item_h + 4);
            bool selected = (idx == g_adb.action_idx);
            const adb_action_def_t* act = &g_adb_actions[idx];

            uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
            uint32_t border = selected ? IPS_ACCENT_EMERALD : IPS_CARD_BORDER;

            c_draw_rounded_card(10, y, g_disp_w - 20, item_h, 5, bg, border);

            // Icon badge
            c_draw_rounded_card(16, y + 6, 32, 14, 3, 0xFF140D08, selected ? IPS_ACCENT_EMERALD : IPS_ACCENT_AMBER);
            c_draw_text(20, y + 9, act->icon, selected ? IPS_ACCENT_EMERALD : IPS_ACCENT_AMBER);

            // Title & Command
            c_draw_text(54, y + 6, is_ru ? act->title_ru : act->title_en, selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY);
            c_draw_text(54, y + 20, act->cmd, IPS_TEXT_MUTED);

            if (selected) {
                c_draw_rounded_card(g_disp_w - 56, y + 8, 40, 14, 3, 0xFF0D2214, IPS_ACCENT_EMERALD);
                c_draw_text(g_disp_w - 52, y + 11, "RUN ▶", IPS_ACCENT_EMERALD);
            }
        }

        // Bottom Feedback / Toast Bar
        c_draw_rect_fill(0, g_disp_h - 22, g_disp_w, 22, IPS_CARD_BG);
        if (g_adb.toast_timer > 0) {
            c_draw_text(12, g_disp_h - 15, g_adb.last_toast, IPS_ACCENT_EMERALD);
        } else {
            c_draw_text(12, g_disp_h - 15, is_ru ? "[Клик] Запустить | [Удерж] Назад" : "[Click] Run Action | [Hold] Back", IPS_TEXT_MUTED);
        }
    } else {
        // OLED 128x64 Mode
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        char oled_hdr[32];
        snprintf(oled_hdr, sizeof(oled_hdr), "ADB: %d%% %s", g_adb.battery_level, g_adb.screen_on ? "ON" : "OFF");
        c_draw_text(2, 1, oled_hdr, COLOR_BLACK);

        for (int i = 0; i < 3 && (g_adb.action_idx + i) < (int)ADB_ACTIONS_COUNT; i++) {
            int idx = g_adb.action_idx + i;
            int y = 11 + i * 14;
            const adb_action_def_t* act = &g_adb_actions[idx];

            if (i == 0) {
                c_draw_rect_fill(0, y - 1, OLED_W, 13, 0xFF222222);
                char line[32];
                snprintf(line, sizeof(line), ">%s %s", act->icon, is_ru ? act->title_ru : act->title_en);
                c_draw_text(2, y, line, g_active_color);
                c_draw_text(2, y + 7, act->cmd, COLOR_OLED_WHITE);
            } else {
                char line[32];
                snprintf(line, sizeof(line), " %s %s", act->icon, is_ru ? act->title_ru : act->title_en);
                c_draw_text(2, y + 2, line, COLOR_OLED_WHITE);
            }
        }

        c_draw_rect_fill(0, 55, OLED_W, 9, 0xFF111111);
        if (g_adb.toast_timer > 0) {
            c_draw_text(2, 56, g_adb.last_toast, g_active_color);
        } else {
            c_draw_text(2, 56, "[Click] Run [Hold] Back", g_active_color);
        }
    }
}

// 9. LINUX CLI TERMINAL VIEW & AUTO-SUGGESTIONS
static const char* g_cli_commands[] = {
    "help", "pcap start", "pcap stop", "pcap status", "pcap clear",
    "subghz rx", "subghz tx", "subghz list", "subghz scan",
    "adb connect", "adb devices", "adb shell", "adb key", "adb reboot",
    "wifi scan", "wifi ap", "wifi status", "rf spec", "rf sniff", "ids", "probe",
    "neofetch", "hw scan", "devices", "sensors", "ble radar",
    "matrix", "kart", "dino", "pong", "uname -a", "free -m", "df -h", "dmesg", "clear", "reboot"
};
#define CLI_COMMANDS_COUNT (sizeof(g_cli_commands) / sizeof(g_cli_commands[0]))

static const char* term_get_autocomplete_suggestion(void) {
    if (g_input_len == 0) return NULL;
    for (size_t i = 0; i < CLI_COMMANDS_COUNT; i++) {
        if (strncmp(g_cli_commands[i], g_input_buf, g_input_len) == 0) {
            return g_cli_commands[i];
        }
    }
    return NULL;
}

static void term_print(const char* text) {
    int max_lines = (g_disp_mode != DISP_MODE_OLED_128x64) ? ((g_disp_h - 50) / 12) : 6;
    if (max_lines < 6) max_lines = 6;
    if (max_lines > 24) max_lines = 24;

    if (g_term_line_count < max_lines) {
        strncpy(g_term_lines[g_term_line_count], text, 47);
        g_term_lines[g_term_line_count][47] = '\0';
        g_term_line_count++;
    } else {
        for (int i = 0; i < max_lines - 1; i++) {
            strncpy(g_term_lines[i], g_term_lines[i + 1], 47);
        }
        strncpy(g_term_lines[max_lines - 1], text, 47);
        g_term_lines[max_lines - 1][47] = '\0';
    }
}

static bool g_term_show_fetch = true;

static void term_print_fetch(void) {
    g_term_show_fetch = true;
    term_print("Bullet OS v0.2.1-dev (ESP32-S3 / FreeRTOS)");
}

static void term_execute_cmd(const char* full_cmd) {
    char buf[64];
    snprintf(buf, sizeof(buf), "esp32-s3:~$ %s", full_cmd);
    term_print(buf);

    char cmd[32] = {0};
    char arg1[32] = {0};
    char arg2[32] = {0};
    char arg3[32] = {0};
    sscanf(full_cmd, "%31s %31s %31s %31s", cmd, arg1, arg2, arg3);

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "man") == 0) {
        term_print("BULLET CLI (v0.2.1):");
        term_print("  pcap [start|stop|status|clear]");
        term_print("  subghz [rx|tx <slot>|list|scan]");
        term_print("  adb [connect|devices|reboot|key|shell]");
        term_print("  neofetch | hw scan | devices | sensors");
        term_print("  wifi scan [--deep] | status | ap");
        term_print("  rf spec | rf sniff | ids | probe");
        term_print("  matrix | ble | dmesg | ping | curl");
        term_print("  kart | dino | pong | uname | free | df");
    }
    // PCAP Wireshark Sniffer CLI
    else if (strcmp(cmd, "pcap") == 0 || strcmp(cmd, "tcpdump") == 0) {
        if (strcmp(arg1, "start") == 0 || strcmp(arg1, "rec") == 0) {
            pcap_logger_start("/capture.pcap");
            term_print("[PCAP] Started logging 802.11 frames to /capture.pcap");
            term_print("  Download from phone: http://bullet.local/capture.pcap");
        } else if (strcmp(arg1, "stop") == 0) {
            pcap_logger_stop();
            pcap_status_t st;
            pcap_logger_get_status(&st);
            char smsg[64];
            snprintf(smsg, sizeof(smsg), "[PCAP] Stopped. Total: %lu pkts (%lu bytes)", (unsigned long)st.total_packets, (unsigned long)st.total_bytes);
            term_print(smsg);
        } else if (strcmp(arg1, "status") == 0 || strcmp(arg1, "info") == 0) {
            pcap_status_t st;
            pcap_logger_get_status(&st);
            char smsg[64];
            snprintf(smsg, sizeof(smsg), "Status: %s | Storage: %s", st.is_recording ? "RECORDING" : "IDLE", st.sd_mounted ? "MicroSD Card" : "LittleFS Flash");
            term_print(smsg);
            snprintf(smsg, sizeof(smsg), "Packets: %lu | Size: %lu bytes", (unsigned long)st.total_packets, (unsigned long)st.total_bytes);
            term_print(smsg);
        } else if (strcmp(arg1, "clear") == 0) {
            pcap_logger_clear();
            term_print("[PCAP] Capture file deleted from storage.");
        } else {
            term_print("Usage: pcap start | stop | status | clear");
            term_print("  Download: http://bullet.local/capture.pcap");
        }
    }
    // Sub-GHz RF Transceiver CLI
    else if (strcmp(cmd, "subghz") == 0 || strcmp(cmd, "cc1101") == 0 || strcmp(cmd, "rf433") == 0) {
        if (strcmp(arg1, "rx") == 0 || strcmp(arg1, "rec") == 0) {
            g_subghz.is_recording = true;
            g_subghz.captured_pulses = 0;
            g_engine.view = OLED_VIEW_SUBGHZ;
            g_subghz.page = SUBGHZ_PAGE_RECORD;
            term_print("[CC1101] RAW Signal Sniffer started (433.92MHz OOK)");
        } else if (strcmp(arg1, "tx") == 0 || strcmp(arg1, "send") == 0 || strcmp(arg1, "replay") == 0) {
            int slot = atoi(arg2);
            if (slot >= 1 && slot <= SUBGHZ_MAX_SLOTS) slot--;
            else slot = 0;
            subghz_trigger_tx(slot);
            char smsg[64];
            snprintf(smsg, sizeof(smsg), "[CC1101] Replayed Slot %d: %s", slot + 1, g_subghz_slots[slot].name);
            term_print(smsg);
        } else if (strcmp(arg1, "list") == 0 || strcmp(arg1, "slots") == 0) {
            term_print("Saved Sub-GHz Signal Slots:");
            for (int i = 0; i < SUBGHZ_MAX_SLOTS; i++) {
                char sline[64];
                snprintf(sline, sizeof(sline), "  %d. %s (%.2fM, %d pulses)", i + 1, g_subghz_slots[i].name, g_subghz_slots[i].freq_mhz, g_subghz_slots[i].pulse_count);
                term_print(sline);
            }
        } else if (strcmp(arg1, "scan") == 0 || strcmp(arg1, "freq") == 0) {
            g_engine.view = OLED_VIEW_SUBGHZ;
            g_subghz.page = SUBGHZ_PAGE_ANALYZER;
            term_print("[CC1101] Launching Sub-GHz Frequency Analyzer...");
        } else {
            term_print("Usage: subghz rx | tx <slot> | list | scan");
        }
    }
    // Android Micro-ADB CLI Suite
    else if (strcmp(cmd, "adb") == 0) {
        if (strcmp(arg1, "connect") == 0) {
            if (strlen(arg2) > 0) {
                strncpy(g_adb.target_ip, arg2, sizeof(g_adb.target_ip) - 1);
            }
            g_adb.is_connected = true;
            char abuf[64];
            snprintf(abuf, sizeof(abuf), "connected to %s", g_adb.target_ip);
            term_print(abuf);
        } else if (strcmp(arg1, "disconnect") == 0) {
            g_adb.is_connected = false;
            term_print("disconnected from target");
        } else if (strcmp(arg1, "devices") == 0) {
            term_print("List of devices attached");
            if (g_adb.is_connected) {
                char abuf[64];
                snprintf(abuf, sizeof(abuf), "%s  device (%s)", g_adb.target_ip, g_adb.device_model);
                term_print(abuf);
            } else {
                term_print("(no devices attached)");
            }
        } else if (strcmp(arg1, "reboot") == 0) {
            if (strcmp(arg2, "recovery") == 0) {
                adb_trigger_action(9);
                term_print("rebooting target into recovery...");
            } else if (strcmp(arg2, "bootloader") == 0 || strcmp(arg2, "fastboot") == 0) {
                adb_trigger_action(10);
                term_print("rebooting target into fastboot...");
            } else {
                adb_trigger_action(8);
                term_print("rebooting target system...");
            }
        } else if (strcmp(arg1, "key") == 0 || strcmp(arg1, "keyevent") == 0) {
            if (strcasecmp(arg2, "power") == 0 || strcmp(arg2, "26") == 0) {
                adb_trigger_action(0);
            } else if (strcasecmp(arg2, "home") == 0 || strcmp(arg2, "3") == 0) {
                adb_trigger_action(1);
            } else if (strcasecmp(arg2, "back") == 0 || strcmp(arg2, "4") == 0) {
                adb_trigger_action(2);
            } else if (strcasecmp(arg2, "volup") == 0 || strcmp(arg2, "24") == 0) {
                adb_trigger_action(4);
            } else if (strcasecmp(arg2, "voldown") == 0 || strcmp(arg2, "25") == 0) {
                adb_trigger_action(5);
            } else {
                char kmsg[48];
                snprintf(kmsg, sizeof(kmsg), "sent keyevent %s", arg2);
                term_print(kmsg);
            }
        } else if (strcmp(arg1, "battery") == 0) {
            char bmsg[48];
            snprintf(bmsg, sizeof(bmsg), "Current Battery: %d%% (%s)", g_adb.battery_level, g_adb.is_charging ? "Charging" : "Discharging");
            term_print(bmsg);
            term_print("Health: Good | Temp: 31.4 C | Volt: 4120mV");
        } else if (strcmp(arg1, "text") == 0) {
            char tmsg[64];
            snprintf(tmsg, sizeof(tmsg), "sent text: \"%s\"", arg2);
            term_print(tmsg);
        } else if (strcmp(arg1, "openurl") == 0) {
            adb_trigger_action(11);
            term_print("opened browser intent");
        } else if (strcmp(arg1, "shell") == 0) {
            if (strstr(arg2, "getprop") || strstr(arg2, "model")) {
                char mmsg[48];
                snprintf(mmsg, sizeof(mmsg), "[ro.product.model]: [%s]", g_adb.device_model);
                term_print(mmsg);
            } else if (strstr(arg2, "dumpsys") || strstr(arg2, "battery")) {
                adb_trigger_action(6);
            } else if (strstr(arg2, "screencap")) {
                adb_trigger_action(7);
            } else if (strstr(arg2, "reboot")) {
                adb_trigger_action(8);
            } else if (strstr(arg2, "pm") || strstr(arg2, "packages")) {
                term_print("package:com.android.chrome");
                term_print("package:com.google.android.youtube");
                term_print("package:org.telegram.messenger");
            } else {
                char smsg[48];
                snprintf(smsg, sizeof(smsg), "exec: %s (exit 0)", arg2);
                term_print(smsg);
            }
        } else {
            term_print("Usage: adb connect <ip> | devices | reboot");
            term_print("  adb key <POWER|HOME|BACK|VOLUP|VOLDOWN>");
            term_print("  adb shell <cmd> | battery | openurl <url>");
        }
    }
    else if (strcmp(cmd, "neofetch") == 0 || strcmp(cmd, "fetch") == 0 || 
             strcmp(cmd, "fastfetch") == 0 || strcmp(cmd, "bullet") == 0 || 
             strcmp(cmd, "logo") == 0 || strcmp(cmd, "info") == 0) {
        term_print_fetch();
    }
    // Hardware Bus Scanner
    else if (strcmp(cmd, "hw") == 0 || strcmp(cmd, "bus") == 0 || strcmp(cmd, "i2c") == 0 || strcmp(cmd, "devices") == 0 || strcmp(cmd, "sensors") == 0) {
        if (strcmp(arg1, "scan") == 0 || strcmp(cmd, "bus") == 0 || strcmp(cmd, "i2c") == 0 || strcmp(cmd, "devices") == 0) {
            hw_bus_scan();
            term_print("[BUS PROBE] Scanning I2C, SPI & USB Peripherals...");
            char hline[48];
            snprintf(hline, sizeof(hline), "  [OK] %s %d Cores @ %dMHz", g_telemetry.chip_model, g_telemetry.chip_cores, g_telemetry.cpu_freq_mhz);
            term_print(hline);
            if (g_telemetry.total_psram_bytes > 0) {
                snprintf(hline, sizeof(hline), "  [OK] %uMB PSRAM", (unsigned int)(g_telemetry.total_psram_bytes / (1024 * 1024)));
            } else {
                snprintf(hline, sizeof(hline), "  [--] PSRAM (None)");
            }
            term_print(hline);
            snprintf(hline, sizeof(hline), "  [OK] %uMB Flash (LittleFS)", (unsigned int)(g_telemetry.total_flash_bytes / (1024 * 1024)));
            term_print(hline);
            term_print("  [OK] Wi-Fi + BLE (Promiscuous)");
            term_print("  [OK] USB OTG Controller");
            term_print("  [--] CC1101 Sub-1GHz (SPI CS=10): Non-blocking");
            term_print("  [--] RC522 RFID (SPI CS=13): Non-blocking");
            term_print("Bus scan complete.");
        } else {
            char hbuf[64];
            snprintf(hbuf, sizeof(hbuf), "SoC: %s %d Cores (%s) @ %dMHz", g_telemetry.chip_model, g_telemetry.chip_cores, g_telemetry.arch_name, g_telemetry.cpu_freq_mhz);
            term_print(hbuf);
            if (g_telemetry.chip_temp_c > 0.0f) {
                snprintf(hbuf, sizeof(hbuf), "Temp: +%.1f C", g_telemetry.chip_temp_c);
            } else {
                snprintf(hbuf, sizeof(hbuf), "Temp: N/A");
            }
            term_print(hbuf);
            if (g_telemetry.total_psram_bytes > 0) {
                snprintf(hbuf, sizeof(hbuf), "PSRAM: %uMB (Free: %uKB)", (unsigned int)(g_telemetry.total_psram_bytes / (1024*1024)), (unsigned int)(g_telemetry.free_psram_bytes / 1024));
            } else {
                snprintf(hbuf, sizeof(hbuf), "PSRAM: None (SRAM mode)");
            }
            term_print(hbuf);
            snprintf(hbuf, sizeof(hbuf), "Flash: %uMB LittleFS", (unsigned int)(g_telemetry.total_flash_bytes / (1024*1024)));
            term_print(hbuf);
        }
    }
    // Wi-Fi commands with flags
    else if (strcmp(cmd, "wifi") == 0) {
        if (strcmp(arg1, "scan") == 0) {
            if (strcmp(arg2, "--deep") == 0) {
                term_print("[SCAN] Promiscuous Scan:");
                term_print("  CH06: TP-Link_5G (-42dB) WPA2");
                term_print("  CH01: Asus_Home (-58dB) WPA3");
                term_print("  CH11: Keener_Net (-71dB) WPA2");
                term_print("Found 14 networks. Scan complete.");
            } else {
                term_print("Scanning Wi-Fi networks...");
                g_engine.scan_tick = 0;
                g_engine.view = OLED_VIEW_SCANNING;
            }
        } else if (strcmp(arg1, "status") == 0) {
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), "SSID: %s (%ddB)", g_telemetry.ssid, g_telemetry.rssi);
            term_print(sbuf);
            snprintf(sbuf, sizeof(sbuf), "IP: %s  GW: %s", g_telemetry.ip, g_telemetry.gateway);
            term_print(sbuf);
            term_print("State: ASSOCIATED / DHCP OK");
        } else if (strcmp(arg1, "ap") == 0) {
            term_print("SoftAP: Bullet-Setup (192.168.4.1)");
            g_engine.view = OLED_VIEW_AP_MODE;
        } else {
            term_print("Usage: wifi scan [--deep] | status | ap");
        }
    }
    // RF Spectrum & Sniffer commands
    else if (strcmp(cmd, "rf") == 0) {
        if (strcmp(arg1, "spec") == 0 || strcmp(arg1, "sniff") == 0) {
            if (strcmp(arg2, "--band") == 0 && strcmp(arg3, "2.4g") == 0) {
                term_print("Starting RF 2.4GHz Spectrum Analyzer...");
            } else {
                term_print("Starting RF Packet Sniffer...");
            }
            g_engine.view = OLED_VIEW_SNIFFER;
        } else {
            term_print("Usage: rf spec [--band 2.4g] | rf sniff");
        }
    }
    // Matrix Rain with flags
    else if (strcmp(cmd, "matrix") == 0) {
        if (strcmp(arg1, "--speed") == 0 && strcmp(arg2, "fast") == 0) {
            for (int c = 0; c < MATRIX_COLS; c++) g_matrix_speed[c] = 6 + (rand() % 6);
            term_print("Matrix speed: fast");
        } else if (strcmp(arg1, "--speed") == 0 && strcmp(arg2, "slow") == 0) {
            for (int c = 0; c < MATRIX_COLS; c++) g_matrix_speed[c] = 1 + (rand() % 2);
            term_print("Matrix speed: slow");
        }
        g_engine.view = OLED_VIEW_MATRIX_RAIN;
    }
    // IDS & Security Commands
    else if (strcmp(cmd, "ids") == 0 || strcmp(cmd, "deauth") == 0) {
        if (strcmp(arg1, "--log") == 0) {
            term_print("[IDS LOG] Target: FF:FF:FF:FF:FF:FF");
            term_print("  BSSID: DC:A6:32:44:11:02 (CH6) -42dB");
            term_print("  Bursts logged: 12 packets");
        } else {
            term_print("Launching Wi-Fi IDS Guard...");
            g_engine.view = OLED_VIEW_DEAUTH_IDS;
        }
    } else if (strcmp(cmd, "probe") == 0 || strcmp(cmd, "probes") == 0) {
        if (strcmp(arg1, "--dump") == 0) {
            term_print("[PROBE DUMP] Captured Footprints:");
            term_print("  3C:22:FB:41:90 -> \"Home_5GHz\"");
            term_print("  98:F4:AB:12:00 -> \"Starbucks_Guest\"");
            term_print("  E0:D5:5E:AA:32 -> \"Tesla_WiFi\"");
        } else {
            term_print("Launching Probe Request Sniffer...");
            g_engine.view = OLED_VIEW_PROBE_SNIFFER;
        }
    } else if (strcmp(cmd, "ble") == 0) {
        term_print("Launching BLE Proximity Radar...");
        g_engine.view = OLED_VIEW_BLE_RADAR;
    } else if (strcmp(cmd, "dmesg") == 0) {
        char mbuf[48];
        snprintf(mbuf, sizeof(mbuf), "[0.000] ESP-IDF v5.1.2 bootloader (%s)", g_telemetry.chip_model);
        term_print(mbuf);
        snprintf(mbuf, sizeof(mbuf), "[0.015] CPU: %d Cores (%s) @ %dMHz", g_telemetry.chip_cores, g_telemetry.arch_name, g_telemetry.cpu_freq_mhz);
        term_print(mbuf);
        if (g_telemetry.total_psram_bytes > 0) {
            snprintf(mbuf, sizeof(mbuf), "[0.032] PSRAM: %uMB detected OK", (unsigned int)(g_telemetry.total_psram_bytes / (1024*1024)));
            term_print(mbuf);
        }
        snprintf(mbuf, sizeof(mbuf), "[0.050] Flash: %uMB Quad-SPI LittleFS", (unsigned int)(g_telemetry.total_flash_bytes / (1024*1024)));
        term_print(mbuf);
        term_print("[0.075] WiFi: Promiscuous RX ready");
    }
    // Network Tools
    else if (strcmp(cmd, "ping") == 0) {
        const char* host = strlen(arg1) > 0 ? arg1 : "8.8.8.8";
        char pbuf[48];
        snprintf(pbuf, sizeof(pbuf), "PING %s: 56 data bytes", host);
        term_print(pbuf);
        term_print("64 bytes: icmp_seq=1 ttl=118 time=14.2ms");
        term_print("64 bytes: icmp_seq=2 ttl=118 time=12.8ms");
        term_print("2 packets transmitted, 0% packet loss");
    } else if (strcmp(cmd, "curl") == 0) {
        const char* url = strlen(arg1) > 0 ? arg1 : "http://bullet.local";
        char cbuf[48];
        snprintf(cbuf, sizeof(cbuf), "HTTP/1.1 200 OK (GET %s)", url);
        term_print(cbuf);
        term_print("Server: Bullet-Httpd/0.2.1");
        char jbuf[48];
        snprintf(jbuf, sizeof(jbuf), "{\"status\":\"ok\",\"chip\":\"%s\"}", g_telemetry.chip_model);
        term_print(jbuf);
    }
    // Standard System Commands
    else if (strcmp(cmd, "uname") == 0) {
        if (strcmp(arg1, "-a") == 0) {
            char ubuf[64];
            snprintf(ubuf, sizeof(ubuf), "BulletOS 0.2.1-dev %s FreeRTOS", g_telemetry.chip_model);
            term_print(ubuf);
            snprintf(ubuf, sizeof(ubuf), "%s %d Cores @ %dMHz", g_telemetry.arch_name, g_telemetry.chip_cores, g_telemetry.cpu_freq_mhz);
            term_print(ubuf);
        } else {
            term_print("Bullet OS 0.2.1");
        }
    } else if (strcmp(cmd, "uptime") == 0) {
        uint32_t s = g_telemetry.uptime_sec;
        char upt[48];
        snprintf(upt, sizeof(upt), "up %02u:%02u:%02u, load: 0.12, 0.08", (unsigned int)(s / 3600), (unsigned int)((s % 3600) / 60), (unsigned int)(s % 60));
        term_print(upt);
    } else if (strcmp(cmd, "free") == 0 || strcmp(cmd, "mem") == 0) {
        char fbuf[48];
        term_print("       total   used   free");
        snprintf(fbuf, sizeof(fbuf), "SRAM:  %5uK  %4uK  %4uK", 
                 (unsigned int)(g_telemetry.total_sram_bytes / 1024), 
                 (unsigned int)((g_telemetry.total_sram_bytes - g_telemetry.free_heap_bytes) / 1024), 
                 (unsigned int)(g_telemetry.free_heap_bytes / 1024));
        term_print(fbuf);
        if (g_telemetry.total_psram_bytes > 0) {
            snprintf(fbuf, sizeof(fbuf), "PSRAM: %5uK  %4uK  %4uK", 
                     (unsigned int)(g_telemetry.total_psram_bytes / 1024), 
                     (unsigned int)((g_telemetry.total_psram_bytes - g_telemetry.free_psram_bytes) / 1024), 
                     (unsigned int)(g_telemetry.free_psram_bytes / 1024));
            term_print(fbuf);
        }
    } else if (strcmp(cmd, "df") == 0) {
        char dbuf[48];
        term_print("Filesystem   Size   Used  Avail  Use%");
        uint32_t tot_m = g_telemetry.total_flash_bytes / (1024 * 1024);
        uint32_t free_m = g_telemetry.free_flash_bytes / (1024 * 1024);
        uint32_t used_m = tot_m > free_m ? (tot_m - free_m) : 1;
        snprintf(dbuf, sizeof(dbuf), "/littlefs    %2u.0M  %2u.0M  %2u.0M   %u%%", tot_m, used_m, free_m, (unsigned int)(used_m * 100 / (tot_m > 0 ? tot_m : 1)));
        term_print(dbuf);
    } else if (strcmp(cmd, "ps") == 0 || strcmp(cmd, "top") == 0) {
        term_print("PID  NAME           STATE  PRIO");
        term_print("  1  ui_render      RUN       5");
        term_print("  2  wifi_stack     READY     4");
        term_print("  3  ids_guard      SLEEP     3");
        term_print("  4  sys_telemetry  SLEEP     2");
    } else if (strcmp(cmd, "ifconfig") == 0 || strcmp(cmd, "ip") == 0) {
        char ifc[48];
        snprintf(ifc, sizeof(ifc), "wlan0: flags=UP,RUNNING inet %s", g_telemetry.ip);
        term_print(ifc);
        snprintf(ifc, sizeof(ifc), "      ether %s txqueuelen 100", g_telemetry.mac);
        term_print(ifc);
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        term_print("app/  config/  logs/  web/");
        term_print("index.html  wifi_oled.wasm  bullet.cfg");
    } else if (strcmp(cmd, "cat") == 0) {
        if (strstr(arg1, "os-release") || strstr(arg1, "version")) {
            term_print("NAME=\"Bullet OS\"");
            term_print("VERSION=\"0.2.1\"");
            char abuf[32];
            snprintf(abuf, sizeof(abuf), "ARCH=%s", g_telemetry.arch_name);
            term_print(abuf);
        } else if (strstr(arg1, "cpuinfo")) {
            char cinfo[48];
            snprintf(cinfo, sizeof(cinfo), "SoC: %s (%s)", g_telemetry.chip_model, g_telemetry.arch_name);
            term_print(cinfo);
            snprintf(cinfo, sizeof(cinfo), "Cores: %d @ %dMHz", g_telemetry.chip_cores, g_telemetry.cpu_freq_mhz);
            term_print(cinfo);
        } else {
            term_print("cat: file not found");
        }
    } else if (strcmp(cmd, "fft") == 0) {
        g_engine.view = OLED_VIEW_FFT_SPECTRUM;
    } else if (strcmp(cmd, "kart") == 0 || strcmp(cmd, "race") == 0) {
        g_engine.view = OLED_VIEW_KART_GAME;
        g_kart.initialized = false;
    } else if (strcmp(cmd, "dino") == 0 || strcmp(cmd, "trex") == 0) {
        g_engine.view = OLED_VIEW_DINO_GAME;
        g_dino.initialized = false;
    } else if (strcmp(cmd, "pong") == 0) {
        g_engine.view = OLED_VIEW_PONG_GAME;
        g_pong.state = PONG_STATE_SELECT_DIFF;
        g_pong.ball_in_play = false;
        term_print("Launching Retro Pong Arcade...");
    } else if (strcmp(cmd, "scan") == 0) {
        g_engine.scan_tick = 0;
        g_engine.view = OLED_VIEW_SCANNING;
    } else if (strcmp(cmd, "ap") == 0) {
        g_engine.view = OLED_VIEW_AP_MODE;
    } else if (strcmp(cmd, "clear") == 0) {
        g_term_line_count = 0;
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "poweroff") == 0) {
#ifndef __EMSCRIPTEN__
        esp_restart();
#else
        oled_init();
#endif
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, ":q") == 0) {
        g_engine.view = OLED_VIEW_MAIN_MENU;
    } else if (strlen(cmd) > 0) {
        term_print("bash: command not found. Type 'help'");
    }
}

static void c_render_terminal_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, 0xFF050403);
        c_draw_rect_fill(0, 0, g_disp_w, 22, IPS_CARD_BG);
        c_draw_text(14, 8, "TERMINAL", IPS_TEXT_PRIMARY);
        c_draw_text(g_disp_w - 55, 8, "v0.2.1", IPS_ACCENT_EMERALD);

        int log_y_start = 28;
        int step = 12;

        if (g_term_show_fetch) {
            // Draw 48x84 Pixel-by-Pixel Dot Matrix of Logo on Left Side
            int logo_x = 10;
            int logo_y = 26;

            for (int y = 0; y < LOGO_TERM_IPS_H; y++) {
                int py = logo_y + y;
                const uint8_t* row_data = &g_logo_term_ips_bitmap[y * LOGO_TERM_IPS_BYTES_PER_ROW];

                uint32_t dot_color = 0xFFF1F5F9;
                if (y < 14) dot_color = IPS_ACCENT_AMBER;
                else if (y < 30) dot_color = IPS_ACCENT_GLACIER;
                else if (y >= 30 && y < 57) dot_color = 0xFFFFFFFF;
                else if (y >= 57 && y < 72) dot_color = IPS_ACCENT_ROSE;
                else dot_color = IPS_ACCENT_EMERALD;

                for (int bx = 0; bx < LOGO_TERM_IPS_BYTES_PER_ROW; bx++) {
                    uint8_t byte = row_data[bx];
                    if (byte == 0) continue;
                    for (int bit = 0; bit < 8; bit++) {
                        if (byte & (1 << (7 - bit))) {
                            c_draw_pixel(logo_x + (bx * 8 + bit), py, dot_color);
                        }
                    }
                }
            }

            // Draw Hardware & System Specs on Right Side dynamically
            int info_x = 64;
            char buf_host[32], buf_cpu[32], buf_ram[32], buf_flash[32];
            snprintf(buf_host, sizeof(buf_host), "Host: %s", g_telemetry.chip_model);
            snprintf(buf_cpu, sizeof(buf_cpu), "CPU: %dM (%d Cores)", g_telemetry.cpu_freq_mhz, g_telemetry.chip_cores);
            if (g_telemetry.total_psram_bytes > 0) {
                snprintf(buf_ram, sizeof(buf_ram), "RAM: %uM PSRAM + %uK", 
                         (unsigned int)(g_telemetry.total_psram_bytes / (1024 * 1024)), 
                         (unsigned int)(g_telemetry.total_sram_bytes / 1024));
            } else {
                snprintf(buf_ram, sizeof(buf_ram), "RAM: %uK SRAM", (unsigned int)(g_telemetry.total_sram_bytes / 1024));
            }
            snprintf(buf_flash, sizeof(buf_flash), "Flash: %uMB LittleFS", (unsigned int)(g_telemetry.total_flash_bytes / (1024 * 1024)));

            c_draw_text(info_x, 26, "OS: Bullet 0.2.1", IPS_ACCENT_GLACIER);
            c_draw_text(info_x, 38, buf_host, IPS_TEXT_PRIMARY);
            c_draw_text(info_x, 50, buf_cpu, IPS_TEXT_SECONDARY);
            c_draw_text(info_x, 62, buf_ram, IPS_ACCENT_EMERALD);
            c_draw_text(info_x, 74, buf_flash, IPS_TEXT_SECONDARY);
            c_draw_text(info_x, 86, "Display: Auto-Scale", IPS_ACCENT_AMBER);
            c_draw_text(info_x, 98, "Security: Armed (IDS)", IPS_ACCENT_ROSE);

            // Subtle separator line
            c_draw_rect_fill(8, 114, g_disp_w - 16, 1, IPS_CARD_BORDER);

            log_y_start = 118;
        }

        // Render Command Log Lines Below
        int max_visible = (g_disp_h - 24 - log_y_start) / step;
        int start_idx = (g_term_line_count > max_visible) ? (g_term_line_count - max_visible) : 0;

        for (int i = start_idx; i < g_term_line_count; i++) {
            const char* line = g_term_lines[i];
            uint32_t col = IPS_TEXT_SECONDARY;
            if (strstr(line, "esp32-s3")) col = IPS_ACCENT_GLACIER;
            else if (strstr(line, "OK") || strstr(line, "ready") || strstr(line, "[OK]")) col = IPS_ACCENT_EMERALD;
            else if (strstr(line, "not found") || strstr(line, "error") || strstr(line, "[!]")) col = IPS_ACCENT_ROSE;

            c_draw_text(10, log_y_start + (i - start_idx) * step, line, col);
        }

        // Check for Auto-Suggestion
        const char* suggestion = term_get_autocomplete_suggestion();
        if (suggestion && g_input_len > 0) {
            // Suggestion Badge above Input Prompt
            char hint_badge[48];
            snprintf(hint_badge, sizeof(hint_badge), "[TAB] %s", suggestion);
            c_draw_rounded_card(12, g_disp_h - 40, 180, 15, 3, 0xEE140D08, IPS_ACCENT_GLACIER);
            c_draw_text(16, g_disp_h - 36, hint_badge, IPS_ACCENT_GLACIER);
        }

        // Bottom Input Prompt
        c_draw_rect_fill(0, g_disp_h - 22, g_disp_w, 22, IPS_CARD_BG);
        char prompt[48];
        snprintf(prompt, sizeof(prompt), "# %s", g_input_buf);
        c_draw_text(12, g_disp_h - 15, prompt, IPS_ACCENT_GLACIER);

        int input_pixel_w = 12 + (int)strlen(prompt) * 6;
        if (suggestion && g_input_len > 0 && strlen(suggestion) > (size_t)g_input_len) {
            // Render Ghost Text
            const char* ghost = suggestion + g_input_len;
            c_draw_text(input_pixel_w, g_disp_h - 15, ghost, IPS_TEXT_MUTED);
        }

        // Cursor
        if ((g_engine.tick / 30) % 2 == 0) {
            c_draw_text(input_pixel_w, g_disp_h - 15, "_", IPS_ACCENT_EMERALD);
        }
    } else {
        // OLED 128x64 Mode
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "TERMINAL", COLOR_BLACK);
        c_draw_text(OLED_W - 38, 1, "v0.2.1", COLOR_BLACK);

        if (g_term_show_fetch && g_term_line_count <= 2) {
            // Draw 24x42 Pixel Dot Bitmap of Logo on Left Side
            for (int y = 0; y < LOGO_TERM_OLED_H; y++) {
                const uint8_t* row_data = &g_logo_term_oled_bitmap[y * LOGO_TERM_OLED_BYTES_PER_ROW];
                for (int bx = 0; bx < LOGO_TERM_OLED_BYTES_PER_ROW; bx++) {
                    uint8_t byte = row_data[bx];
                    if (byte == 0) continue;
                    for (int bit = 0; bit < 8; bit++) {
                        if (byte & (1 << (7 - bit))) {
                            c_draw_pixel(2 + (bx * 8 + bit), 11 + y, g_active_color);
                        }
                    }
                }
            }
            char line1[32], line2[32], line3[32], line4[32];
            snprintf(line1, sizeof(line1), "Bullet v0.2.1");
            snprintf(line2, sizeof(line2), "%s %dM", g_telemetry.chip_model, g_telemetry.cpu_freq_mhz);
            if (g_telemetry.total_psram_bytes > 0) {
                snprintf(line3, sizeof(line3), "%uMB PSRAM OK", (unsigned int)(g_telemetry.total_psram_bytes / (1024*1024)));
            } else {
                snprintf(line3, sizeof(line3), "SRAM: %uK Free", (unsigned int)(g_telemetry.free_heap_bytes / 1024));
            }
            snprintf(line4, sizeof(line4), "Flash: %uMB OK", (unsigned int)(g_telemetry.total_flash_bytes / (1024*1024)));

            c_draw_text(30, 12, line1, g_active_color);
            c_draw_text(30, 22, line2, g_active_color);
            c_draw_text(30, 32, line3, g_active_color);
            c_draw_text(30, 42, line4, g_active_color);
        } else {
            for (int i = 0; i < g_term_line_count && i < 6; i++) {
                c_draw_text(2, 11 + i * 7, g_term_lines[i], g_active_color);
            }
        }

        const char* suggestion = term_get_autocomplete_suggestion();
        c_draw_rect_fill(0, 55, OLED_W, 9, 0xFF111111);
        char prompt[36];
        snprintf(prompt, sizeof(prompt), "# %s", g_input_buf);
        c_draw_text(2, 56, prompt, g_active_color);
        int input_pixel_w = 2 + (int)strlen(prompt) * 6;
        if (suggestion && g_input_len > 0 && strlen(suggestion) > (size_t)g_input_len && input_pixel_w < OLED_W - 12) {
            c_draw_text(input_pixel_w, 56, suggestion + g_input_len, COLOR_OLED_WHITE);
        }
        if ((g_engine.tick / 30) % 2 == 0) {
            c_draw_text(input_pixel_w, 56, "_", g_active_color);
        }
    }
}

// 10. MAIN MENU & OTHER VIEWS
static void c_render_main_menu_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);

        // Header Bar with animated status pulse
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(14, 9, "BULLET OS", IPS_TEXT_PRIMARY);

        // Top Chip: Item index & navigation hint
        char nav_hint[24];
        snprintf(nav_hint, sizeof(nav_hint), "%02d/%02d [KNOB]", g_engine.main_index + 1, (int)MAIN_MENU_COUNT);
        c_draw_rounded_card(g_disp_w - 92, 5, 80, 16, 4, IPS_BG_COLOR, IPS_CARD_BORDER);
        c_draw_text(g_disp_w - 86, 9, nav_hint, IPS_ACCENT_GLACIER);

        const int card_h = 36;
        int card_w = g_disp_w - 24;
        const int start_y = 30;
        const int gap = 4;
        int visible_count = (g_disp_h - 36) / (card_h + gap);
        if (visible_count < 4) visible_count = 4;

        int start_idx = g_engine.main_index - 2;
        if (start_idx < 0) start_idx = 0;
        if (start_idx > (int)MAIN_MENU_COUNT - visible_count) start_idx = (int)MAIN_MENU_COUNT - visible_count;
        if (start_idx < 0) start_idx = 0;

        int rel_idx = g_engine.main_index - start_idx;
        g_engine.target_cursor_y = (float)(start_y + rel_idx * (card_h + gap));
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.30f;

        // Animated Active Selection Glow
        int cur_y = (int)g_engine.cursor_y;
        c_draw_rounded_card(10, cur_y - 1, card_w + 4, card_h + 2, 7, IPS_CARD_HOVER, IPS_ACCENT_GLACIER);

        for (int i = 0; i < visible_count && (start_idx + i) < (int)MAIN_MENU_COUNT; i++) {
            int idx = start_idx + i;
            int y = start_y + i * (card_h + gap);
            bool selected = (idx == g_engine.main_index);

            const menu_item_info_t* item = &g_main_menu_info[idx];
            const char* title = is_ru ? item->title_ru : item->title_en;
            const char* sub = is_ru ? item->sub_ru : item->sub_en;

            uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
            uint32_t border = selected ? IPS_ACCENT_GLACIER : IPS_CARD_BORDER;

            c_draw_rounded_card(12, y, card_w, card_h, 6, bg, border);

            // Icon with accent background
            uint32_t icon_accent = selected ? item->icon_col : IPS_TEXT_MUTED;
            c_draw_rounded_card(18, y + 6, 24, 24, 4, 0xFF140D08, selected ? item->icon_col : 0);
            c_draw_icon_ips(23, y + 10, idx, icon_accent);

            // Primary Title & Helpful Description Subtitle
            c_draw_text(48, y + 8, title, selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY);
            c_draw_text(48, y + 21, sub, selected ? IPS_ACCENT_AMBER : IPS_TEXT_MUTED);

            // Animated Chevron on Selection
            if (selected) {
                int chevron_anim = ((g_engine.tick / 10) % 3);
                c_draw_text(card_w - 12 + chevron_anim, y + 14, "►", item->icon_col);
            }
        }

        // Animated smooth scrollbar
        c_draw_rect_fill(g_disp_w - 6, start_y, 2, g_disp_h - start_y - 6, IPS_CARD_BG);
        int bar_h = 32;
        int bar_y = start_y + (g_engine.main_index * (g_disp_h - start_y - 6 - bar_h)) / ((int)MAIN_MENU_COUNT - 1);
        c_draw_rect_fill(g_disp_w - 6, bar_y, 2, bar_h, IPS_ACCENT_GLACIER);
    } else {
        // OLED 128x64 Mode with High-Contrast Layout
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "BULLET OS", COLOR_BLACK);

        char counter_str[8];
        snprintf(counter_str, sizeof(counter_str), "%d/%d", g_engine.main_index + 1, (int)MAIN_MENU_COUNT);
        c_draw_text(OLED_W - 24, 1, counter_str, COLOR_BLACK);

        const int item_h = 10;
        const int visible_count = 4;
        int start_idx = g_engine.main_index - 1;
        if (start_idx < 0) start_idx = 0;
        if (start_idx > (int)MAIN_MENU_COUNT - visible_count) start_idx = (int)MAIN_MENU_COUNT - visible_count;

        int rel_idx = g_engine.main_index - start_idx;
        g_engine.target_cursor_y = (float)(10 + rel_idx * item_h);
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.30f;

        c_draw_rect_fill(0, (int)g_engine.cursor_y, OLED_W, item_h, g_active_color);

        for (int i = 0; i < visible_count && (start_idx + i) < (int)MAIN_MENU_COUNT; i++) {
            int idx = start_idx + i;
            int y = 10 + i * item_h;
            bool selected = (idx == g_engine.main_index);
            const char* title = is_ru ? g_main_menu_info[idx].title_ru : g_main_menu_info[idx].title_en;

            char display_row[26];
            snprintf(display_row, sizeof(display_row), "%c %s", selected ? '>' : ' ', title);
            c_draw_text(2, y + 1, display_row, selected ? COLOR_BLACK : g_active_color);
        }

        // Bottom Description Bar on OLED
        c_draw_rect_fill(0, 52, OLED_W, 12, 0xFF111111);
        const char* sub = is_ru ? g_main_menu_info[g_engine.main_index].sub_ru : g_main_menu_info[g_engine.main_index].sub_en;
        c_draw_text(4, 54, sub, g_active_color);
    }
}

static void c_render_boot_view(void) {
    g_engine.boot_tick++;
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);

        // Centered coordinates for 96x168 dot matrix
        int start_x = (g_disp_w - LOGO_IPS_W) / 2;
        int start_y = (g_disp_h - 26 - LOGO_IPS_H) / 2;
        if (start_y < 12) start_y = 12;

        // Progressive reveal during boot
        int visible_rows = (g_engine.boot_tick * (LOGO_IPS_H + 20)) / 85;
        if (visible_rows > LOGO_IPS_H) visible_rows = LOGO_IPS_H;

        for (int y = 0; y < visible_rows; y++) {
            int py = start_y + y;
            const uint8_t* row_data = &g_logo_ips_bitmap[y * LOGO_IPS_BYTES_PER_ROW];

            // Subtle cyber color tint based on vertical position
            uint32_t dot_color = 0xFFF1F5F9; // Bone white for skull
            if (y < 28) {
                dot_color = IPS_ACCENT_AMBER; // Crown & upper serpent
            } else if (y < 60) {
                dot_color = IPS_ACCENT_GLACIER; // Dagger hilt & serpent coils
            } else if (y >= 60 && y < 114) {
                dot_color = 0xFFFFFFFF; // Skull face & eye sockets
            } else if (y >= 114 && y < 144) {
                dot_color = IPS_ACCENT_ROSE; // Rose flower petals & dagger
            } else {
                dot_color = IPS_ACCENT_EMERALD; // Lower ribbon & tip
            }

            for (int bx = 0; bx < LOGO_IPS_BYTES_PER_ROW; bx++) {
                uint8_t byte = row_data[bx];
                if (byte == 0) continue;

                for (int bit = 0; bit < 8; bit++) {
                    if (byte & (1 << (7 - bit))) {
                        int px = start_x + (bx * 8 + bit);
                        c_draw_pixel(px, py, dot_color);
                    }
                }
            }
        }

        // Cyber Laser Scanline Beam sweeping across
        if (g_engine.boot_tick < 80 && visible_rows < LOGO_IPS_H) {
            int scan_y = start_y + visible_rows;
            if (scan_y < g_disp_h - 24) {
                c_draw_rect_fill(start_x - 8, scan_y, LOGO_IPS_W + 16, 2, IPS_ACCENT_GLACIER);
                c_draw_rect_fill(start_x - 4, scan_y - 1, LOGO_IPS_W + 8, 1, 0x8070D6FF);
            }
        }

        // Bottom Progress & Ready Prompt
        int bot_y = g_disp_h - 22;
        c_draw_rect_fill(0, bot_y, g_disp_w, 22, IPS_CARD_BG);

        float p = (float)g_engine.boot_tick / 140.0f;
        if (p > 1.0f) p = 1.0f;

        int bar_w = (int)(p * (g_disp_w - 40));
        c_draw_rect_fill(20, bot_y + 2, g_disp_w - 40, 2, IPS_CARD_BORDER);
        c_draw_rect_fill(20, bot_y + 2, bar_w, 2, IPS_ACCENT_GLACIER);

        if (g_engine.boot_tick > 80) {
            int prompt_x = (g_disp_w - 18 * 6) / 2;
            c_draw_text(prompt_x, bot_y + 9, "PRESS KNOB TO START", ((g_engine.boot_tick / 15) % 2 == 0) ? IPS_ACCENT_AMBER : IPS_TEXT_PRIMARY);
        } else {
            int load_x = (g_disp_w - 20 * 6) / 2;
            c_draw_text(load_x, bot_y + 9, "BOOTING BULLET 0.2.1", IPS_TEXT_MUTED);
        }
    } else {
        // OLED 128x64 Mode (Clean 32x56 dot-matrix)
        c_draw_rect_fill(0, 0, OLED_W, OLED_H, COLOR_BLACK);

        int start_x = (OLED_W - LOGO_OLED_W) / 2;
        int start_y = 2;
        int visible_rows = (g_engine.boot_tick * (LOGO_OLED_H + 10)) / 70;
        if (visible_rows > LOGO_OLED_H) visible_rows = LOGO_OLED_H;

        for (int y = 0; y < visible_rows; y++) {
            int py = start_y + y;
            const uint8_t* row_data = &g_logo_oled_bitmap[y * LOGO_OLED_BYTES_PER_ROW];

            for (int bx = 0; bx < LOGO_OLED_BYTES_PER_ROW; bx++) {
                uint8_t byte = row_data[bx];
                if (byte == 0) continue;

                for (int bit = 0; bit < 8; bit++) {
                    if (byte & (1 << (7 - bit))) {
                        c_draw_pixel(start_x + (bx * 8 + bit), py, g_active_color);
                    }
                }
            }
        }

        c_draw_rect_fill(0, 55, OLED_W, 9, 0xFF111111);
        if (g_engine.boot_tick > 80) {
            c_draw_text(14, 56, "[Click Knob] Start", g_active_color);
        } else {
            c_draw_text(20, 56, "Bullet v0.2.1...", g_active_color);
        }
    }

    if (g_engine.boot_tick > 160) {
        g_engine.view = OLED_VIEW_MAIN_MENU;
    }
}

static void c_render_wifi_menu_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
        c_draw_text(16, 11, "WI-FI MANAGER", IPS_TEXT_PRIMARY);

        const int card_h = 40;
        int card_w = g_disp_w - 24;
        const int start_y = 40;
        const int gap = 8;

        const char** items = (g_engine.lang == LANG_RU) ? str_wifi_menu_ru : str_wifi_menu_en;
        for (int i = 0; i < WIFI_MENU_COUNT; i++) {
            int y = start_y + i * (card_h + gap);
            bool selected = (i == g_engine.wifi_index);

            uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
            uint32_t border = selected ? IPS_ACCENT_GLACIER : IPS_CARD_BORDER;
            uint32_t text_col = selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY;

            c_draw_rounded_card(12, y, card_w, card_h, 6, bg, border);
            c_draw_text(26, y + 17, items[i], text_col);
            c_draw_text(card_w - 12, y + 17, ">", selected ? IPS_ACCENT_GLACIER : IPS_TEXT_MUTED);
        }
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "WI-FI MANAGER", COLOR_BLACK);

        const int item_h = 13;
        g_engine.target_cursor_y = (float)(11 + g_engine.wifi_index * item_h);
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.18f;

        c_draw_rect_fill(0, (int)g_engine.cursor_y, OLED_W, item_h, g_active_color);

        const char** items = (g_engine.lang == LANG_RU) ? str_wifi_menu_ru : str_wifi_menu_en;
        for (int i = 0; i < WIFI_MENU_COUNT; i++) {
            int y = 11 + i * item_h;
            bool selected = (i == g_engine.wifi_index);
            c_draw_text(4, y + 3, items[i], selected ? COLOR_BLACK : g_active_color);
        }
    }
}

static void c_render_networks_list_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, "AVAILABLE NETWORKS", IPS_TEXT_PRIMARY);

        if (g_net_count == 0) {
            c_draw_text(30, 80, "No networks found.", IPS_TEXT_SECONDARY);
            c_draw_text(30, 105, "Press [Scan Wi-Fi] in menu", IPS_ACCENT_GLACIER);
            c_draw_text(30, 125, "to trigger real scan.", IPS_TEXT_MUTED);

            c_draw_rounded_card(30, g_disp_h - 55, g_disp_w - 60, 28, 6, IPS_CARD_BG, IPS_CARD_BORDER);
            c_draw_text(45, g_disp_h - 46, "[ Back to Menu ]", IPS_ACCENT_GLACIER);
            return;
        }

        char counter_str[8];
        snprintf(counter_str, sizeof(counter_str), "%d/%d", g_engine.net_index + 1, g_net_count);
        c_draw_text(g_disp_w - 45, 10, counter_str, IPS_ACCENT_GLACIER);

        const int item_h = 24;
        int visible_count = (g_disp_h - 45) / item_h;
        if (visible_count < 6) visible_count = 6;

        int start_idx = g_engine.net_index - 3;
        if (start_idx < 0) start_idx = 0;
        if (start_idx > g_net_count - visible_count) start_idx = g_net_count - visible_count;
        if (start_idx < 0) start_idx = 0;

        const int start_y = 32;
        int relative_idx = g_engine.net_index - start_idx;
        g_engine.target_cursor_y = (float)(start_y + relative_idx * item_h);
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.22f;

        c_draw_rounded_card(8, (int)g_engine.cursor_y, g_disp_w - 16, item_h - 2, 4, IPS_CARD_HOVER, IPS_ACCENT_GLACIER);

        for (int i = 0; i < visible_count && (start_idx + i) < g_net_count; i++) {
            int idx = start_idx + i;
            int y = start_y + i * item_h;
            bool selected = (idx == g_engine.net_index);
            const wifi_net_item_t* net = &g_networks[idx];
            uint32_t text_col = selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY;

            if (net->is_secure) c_draw_rect_fill(16, y + 8, 4, 4, IPS_ACCENT_AMBER);
            c_draw_text(26, y + 7, net->ssid, text_col);

            if (net->rssi != 0) {
                char dbm_str[10];
                snprintf(dbm_str, sizeof(dbm_str), "%ddB", net->rssi);
                c_draw_text(g_disp_w - 55, y + 7, dbm_str, selected ? IPS_ACCENT_GLACIER : IPS_TEXT_MUTED);
            }
        }
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "SCANNED APs", COLOR_BLACK);

        if (g_net_count == 0) {
            c_draw_text(4, 20, "No networks found.", g_active_color);
            c_draw_text(4, 52, "[ Back to Menu ]", g_active_color);
            return;
        }

        const int item_h = 12;
        const int visible_count = 4;
        int start_idx = g_engine.net_index - 1;
        if (start_idx < 0) start_idx = 0;
        if (start_idx > g_net_count - visible_count) start_idx = g_net_count - visible_count;
        if (start_idx < 0) start_idx = 0;

        int relative_idx = g_engine.net_index - start_idx;
        g_engine.target_cursor_y = (float)(10 + relative_idx * item_h);
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.18f;

        c_draw_rect_fill(0, (int)g_engine.cursor_y, OLED_W - 4, item_h, g_active_color);

        for (int i = 0; i < visible_count && (start_idx + i) < g_net_count; i++) {
            int idx = start_idx + i;
            int y = 10 + i * item_h;
            bool selected = (idx == g_engine.net_index);
            const wifi_net_item_t* net = &g_networks[idx];

            char display_name[24];
            if (net->is_secure) snprintf(display_name, sizeof(display_name), "* %s", net->ssid);
            else snprintf(display_name, sizeof(display_name), "  %s", net->ssid);

            c_draw_text(2, y + 2, display_name, selected ? COLOR_BLACK : g_active_color);
        }
    }
}

static void c_render_scanning_view(void) {
    g_engine.scan_tick++;
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 26, IPS_CARD_BG);
        c_draw_text(16, 10, "RF SPECTRUM RADAR", IPS_TEXT_PRIMARY);

        int cx = g_disp_w / 2, cy = g_disp_h / 2 + 5;
        int max_r = (g_disp_h < g_disp_w ? g_disp_h : g_disp_w) / 2 - 30;
        if (max_r < 30) max_r = 30;

        c_draw_circle(cx, cy, max_r / 3, IPS_CARD_BG);
        c_draw_circle(cx, cy, (max_r * 2) / 3, IPS_CARD_BORDER);
        c_draw_circle(cx, cy, max_r, IPS_CARD_HOVER);

        float deg = (g_engine.scan_tick * 4) % 360;
        float rad = deg * 3.14159265f / 180.0f;
        for (int r = 0; r < max_r; r += 2) {
            int px = cx + (int)(cosf(rad) * (float)r);
            int py = cy + (int)(sinf(rad) * (float)r);
            c_draw_pixel(px, py, IPS_ACCENT_GLACIER);
        }

        c_draw_text(16, g_disp_h - 16, "SCANNING 2.4 & 5.8 GHz...", IPS_TEXT_MUTED);
    } else {
        c_draw_text(16, 2, "SCANNING 2.4/5G...", g_active_color);
        int cx = OLED_W / 2;
        int cy = 34;
        for (int r = 1; r <= 3; r++) {
            int radius = (int)((int)(g_engine.scan_tick * 0.45f + r * 8) % 24);
            c_draw_circle(cx, cy, radius, g_active_color);
        }
        c_draw_rect_fill(cx - 1, cy - 1, 3, 3, g_active_color);
        c_draw_text(8, 56, "Searching channels...", g_active_color);
    }

    if (g_engine.scan_tick > 100) {
        g_engine.scan_tick = 0;
        g_engine.view = OLED_VIEW_NETWORKS_LIST;
    }
}

static void c_render_connecting_view(void) {
    g_engine.connect_tick++;
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    const char* chosen_ssid = (g_net_count > 0 && g_engine.net_index < g_net_count) ? 
                              g_networks[g_engine.net_index].ssid : "Network";

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        int card_w = g_disp_w - 32;
        int card_h = g_disp_h - 60;
        c_draw_rounded_card(16, 32, card_w, card_h, 8, IPS_CARD_BG, IPS_CARD_BORDER);

        c_draw_text(g_disp_w / 2 - 40, 52, "CONNECTING", IPS_ACCENT_GLACIER);
        c_draw_text(g_disp_w / 2 - 40, 72, chosen_ssid, IPS_TEXT_PRIMARY);

        int cx = g_disp_w / 2, cy = g_disp_h / 2 + 20;
        int wave = (g_engine.connect_tick / 14) % 4;

        for (int i = 0; i <= wave; i++) {
            c_draw_circle(cx, cy, 14 + i * 14, IPS_ACCENT_GLACIER);
        }
        c_draw_rect_fill(cx - 3, cy - 3, 6, 6, IPS_ACCENT_EMERALD);

        c_draw_text(16, g_disp_h - 16, "AUTHENTICATING...", IPS_TEXT_MUTED);
    } else {
        c_draw_text(16, 2, "CONNECTING AP...", g_active_color);
        c_draw_text(12, 14, chosen_ssid, g_active_color);

        int cx = OLED_W / 2;
        int cy = 38;
        int wave = (g_engine.connect_tick / 16) % 4;
        for (int i = 0; i <= wave; i++) {
            c_draw_circle(cx, cy + 6, 6 + i * 5, g_active_color);
        }
        c_draw_rect_fill(cx - 1, cy + 4, 3, 3, g_active_color);
    }

    if (g_engine.connect_tick > 100) {
        g_engine.connect_tick = 0;
        g_engine.view = OLED_VIEW_STATUS;
    }
}

static void c_render_status_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
        c_draw_text(16, 11, "WI-FI STATUS HUD", IPS_TEXT_PRIMARY);

        c_draw_rounded_card(g_disp_w - 75, 6, 68, 16, 4, IPS_BG_COLOR, g_telemetry.is_connected ? IPS_ACCENT_EMERALD : IPS_TEXT_MUTED);
        c_draw_text(g_disp_w - 69, 10, g_telemetry.is_connected ? "● ONLINE" : "OFFLINE", g_telemetry.is_connected ? IPS_ACCENT_EMERALD : IPS_TEXT_MUTED);

        int card_w = g_disp_w - 24;
        int card_h = g_disp_h - 65;
        c_draw_rounded_card(12, 34, card_w, card_h, 6, IPS_CARD_BG, IPS_CARD_BORDER);

        int sy = 48;
        int step = 22;

        c_draw_text(24, sy, "SSID:", IPS_TEXT_MUTED);
        c_draw_text(74, sy, g_telemetry.ssid, IPS_TEXT_PRIMARY);

        c_draw_text(24, sy + step, "IP:", IPS_TEXT_MUTED);
        c_draw_text(74, sy + step, g_telemetry.ip, g_telemetry.is_connected ? IPS_ACCENT_EMERALD : IPS_TEXT_SECONDARY);

        c_draw_text(24, sy + step * 2, "GATE:", IPS_TEXT_MUTED);
        c_draw_text(74, sy + step * 2, g_telemetry.gateway, IPS_TEXT_PRIMARY);

        c_draw_text(24, sy + step * 3, "SIGNAL:", IPS_TEXT_MUTED);
        char rssi_buf[24];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", g_telemetry.rssi);
        c_draw_text(74, sy + step * 3, rssi_buf, g_telemetry.is_connected ? IPS_ACCENT_EMERALD : IPS_TEXT_MUTED);

        c_draw_text(24, sy + step * 4, "MAC:", IPS_TEXT_MUTED);
        c_draw_text(74, sy + step * 4, g_telemetry.mac, IPS_TEXT_SECONDARY);

        c_draw_text(16, g_disp_h - 16, "[Press Knob] Return to Menu", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, g_telemetry.is_connected ? "CONNECTED ●" : "DISCONNECTED", COLOR_BLACK);

        char line[32];
        snprintf(line, sizeof(line), "SSID: %s", g_telemetry.ssid);
        c_draw_text(4, 14, line, g_active_color);
        snprintf(line, sizeof(line), "IP:   %s", g_telemetry.ip);
        c_draw_text(4, 26, line, g_active_color);
        snprintf(line, sizeof(line), "GATE: %s", g_telemetry.gateway);
        c_draw_text(4, 38, line, g_active_color);

        c_draw_rect_fill(0, 52, OLED_W, 12, 0xFF111111);
        c_draw_text(8, 54, "[Knob Click] Menu", g_active_color);
    }
}

static void c_render_ap_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
        c_draw_text(16, 11, is_ru ? "ТОЧКА ДОСТУПА & ВЕБ-ПУЛЬТ" : "HOTSPOT & WEB PORTAL", IPS_TEXT_PRIMARY);

        int card_w = g_disp_w - 24;
        int card_h = g_disp_h - 65;
        c_draw_rounded_card(12, 34, card_w, card_h, 6, IPS_CARD_BG, IPS_CARD_BORDER);

        int sy = 46;
        int step = 20;

        c_draw_text(24, sy, is_ru ? "ИМЯ ТОЧКИ:" : "HOTSPOT SSID:", IPS_TEXT_MUTED);
        c_draw_text(115, sy, "Bullet-Setup", IPS_TEXT_PRIMARY);

        c_draw_text(24, sy + step, is_ru ? "ПАРОЛЬ AP:" : "PASSWORD:", IPS_TEXT_MUTED);
        c_draw_text(115, sy + step, g_ap_password, IPS_ACCENT_AMBER);

        c_draw_text(24, sy + step * 2, is_ru ? "ВЕБ-ПОРТАЛ:" : "WEB PORTAL:", IPS_TEXT_MUTED);
        c_draw_text(115, sy + step * 2, "http://192.168.4.1", IPS_ACCENT_EMERALD);

        c_draw_text(24, sy + step * 3, is_ru ? "ДОМЕН mDNS:" : "DOMAIN:", IPS_TEXT_MUTED);
        c_draw_text(115, sy + step * 3, "http://bullet.local", IPS_ACCENT_GLACIER);

        c_draw_text(24, sy + step * 4, is_ru ? "ВЕБ-ТЕРМИНАЛ:" : "WEB TERMINAL:", IPS_TEXT_MUTED);
        c_draw_text(115, sy + step * 4, is_ru ? "АКТИВЕН (с телефона) ●" : "ONLINE (READY) ●", IPS_ACCENT_EMERALD);

        c_draw_text(16, g_disp_h - 16, is_ru ? "[Кнопка] Назад в меню" : "[Press Knob] Return to Menu", IPS_TEXT_MUTED);
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, "HOTSPOT AP ●", COLOR_BLACK);

        c_draw_text(4, 13, "SSID: Bullet-Setup", g_active_color);
        char pass_buf[32];
        snprintf(pass_buf, sizeof(pass_buf), "PASS: %s", g_ap_password);
        c_draw_text(4, 24, pass_buf, g_active_color);
        c_draw_text(4, 35, "URL:  192.168.4.1", g_active_color);
        c_draw_text(4, 46, "WEB:  bullet.local", g_active_color);

        c_draw_rect_fill(0, 55, OLED_W, 9, 0xFF111111);
        c_draw_text(8, 56, "[Hold] Back to Menu", g_active_color);
    }
}

static void c_render_settings_view(void) {
    bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
    bool is_ru = (g_engine.lang == LANG_RU);

    if (is_ips) {
        c_draw_rect_fill(0, 0, g_disp_w, g_disp_h, IPS_BG_COLOR);
        c_draw_rect_fill(0, 0, g_disp_w, 28, IPS_CARD_BG);
        c_draw_text(16, 11, is_ru ? "НАСТРОЙКИ СИСТЕМЫ" : "SYSTEM SETTINGS", IPS_TEXT_PRIMARY);

        const int card_h = 36;
        int card_w = g_disp_w - 24;
        const int start_y = 36;
        const int gap = 8;

        for (int i = 0; i < 4; i++) {
            int y = start_y + i * (card_h + gap);
            bool selected = (i == g_engine.settings_index);

            uint32_t bg = selected ? IPS_CARD_HOVER : IPS_CARD_BG;
            uint32_t border = selected ? IPS_ACCENT_GLACIER : IPS_CARD_BORDER;
            uint32_t text_col = selected ? IPS_TEXT_PRIMARY : IPS_TEXT_SECONDARY;

            c_draw_rounded_card(12, y, card_w, card_h, 6, bg, border);

            if (i == 0) {
                c_draw_text(22, y + 14, is_ru ? "Язык Интерфейса: [Русский (RU)]" : "Language: [English (EN)]", text_col);
            } else if (i == 1) {
                const char* th_str = (g_active_color == COLOR_OLED_CYAN) ? "Nordic Cyan" :
                                     ((g_active_color == COLOR_OLED_AMBER) ? "Amber Cyber" : "Pure White");
                char th_buf[48];
                snprintf(th_buf, sizeof(th_buf), is_ru ? "Цветовая Тема: [%s]" : "Color Theme: [%s]", th_str);
                c_draw_text(22, y + 14, th_buf, text_col);
            } else if (i == 2) {
                char p_buf[48];
                snprintf(p_buf, sizeof(p_buf), is_ru ? "Пароль Точки AP: [ %s ] (Клик-Смена)" : "AP Password: [ %s ] (Click-New)", g_ap_password);
                c_draw_text(22, y + 14, p_buf, selected ? IPS_ACCENT_AMBER : text_col);
            } else if (i == 3) {
                c_draw_text(22, y + 14, is_ru ? "[ Назад в Главное Меню ]" : "[ Back to Main Menu ]", text_col);
            }
        }
    } else {
        c_draw_rect_fill(0, 0, OLED_W, 9, g_active_color);
        c_draw_text(2, 1, is_ru ? "НАСТРОЙКИ" : "SETTINGS", COLOR_BLACK);

        const int item_h = 12;
        g_engine.target_cursor_y = (float)(11 + g_engine.settings_index * item_h);
        g_engine.cursor_y += (g_engine.target_cursor_y - g_engine.cursor_y) * 0.20f;

        c_draw_rect_fill(0, (int)g_engine.cursor_y, OLED_W, item_h, g_active_color);

        uint32_t col0 = (g_engine.settings_index == 0) ? COLOR_BLACK : g_active_color;
        c_draw_text(4, 12, is_ru ? "1. Язык: [Русский]" : "1. Language: [EN]", col0);

        uint32_t col1 = (g_engine.settings_index == 1) ? COLOR_BLACK : g_active_color;
        c_draw_text(4, 24, is_ru ? "2. Тема: [Cyan]" : "2. Theme: [Cyan]", col1);

        uint32_t col2 = (g_engine.settings_index == 2) ? COLOR_BLACK : g_active_color;
        char p_oled[32];
        snprintf(p_oled, sizeof(p_oled), "3. AP Pass: %s", g_ap_password);
        c_draw_text(4, 36, p_oled, col2);

        uint32_t col3 = (g_engine.settings_index == 3) ? COLOR_BLACK : g_active_color;
        c_draw_text(4, 48, is_ru ? "4. [ Назад в Меню ]" : "4. [ Back to Main ]", col3);
    }
}

// ============================================================================
// EXPORTED API
// ============================================================================
EXPORT void oled_set_disp_mode(int mode) {
    g_disp_mode = (disp_mode_t)mode;
    if (mode == 1) {
        g_disp_w = 240;
        g_disp_h = 240;
    } else if (mode == 2) {
        g_disp_w = 320;
        g_disp_h = 240;
    } else if (mode == 3) {
        g_disp_w = 480;
        g_disp_h = 320;
    } else {
        g_disp_w = 128;
        g_disp_h = 64;
    }
    if (g_fb_rgba) {
        for (int i = 0; i < g_disp_w * g_disp_h; i++) g_fb_rgba[i] = 0xFF000000;
    }
}

EXPORT void wifi_oled_set_disp_mode(disp_mode_t mode) {
    oled_set_disp_mode((int)mode);
}

EXPORT int oled_get_disp_mode(void) {
    return (int)g_disp_mode;
}

EXPORT void oled_init(void) {
#ifndef BULLET_DESKTOP_BUILD
    if (!g_fb_rgba) {
        g_fb_rgba = (uint32_t*)heap_caps_malloc(MAX_DISP_W * MAX_DISP_H * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (g_fb_rgba) {
            g_disp_mode = DISP_MODE_IPS_240x240;
            g_disp_w = 240;
            g_disp_h = 240;
        } else {
            // Fallback for boards without PSRAM (e.g. QEMU / standard ESP32-WROOM)
            g_fb_rgba = g_fb_fallback;
            g_disp_mode = DISP_MODE_OLED_128x64;
            g_disp_w = 128;
            g_disp_h = 64;
        }
    }
#endif

    g_engine.view = OLED_VIEW_BOOT;
    g_engine.boot_tick = 0;
    g_engine.main_index = 0;
    g_engine.wifi_index = 0;
    g_engine.net_index = 0;
    g_engine.settings_index = 0;
    if (g_fb_rgba) {
        for (int i = 0; i < g_disp_w * g_disp_h; i++) g_fb_rgba[i] = 0xFF000000;
    }

    g_term_line_count = 0;
    term_print_fetch();
    term_print("Type 'help' for commands");
}

EXPORT void oled_render(void) {
    g_engine.tick++;
    if (!g_fb_rgba) return;

    uint32_t bg_col = (g_disp_mode != DISP_MODE_OLED_128x64) ? IPS_BG_COLOR : COLOR_BLACK;
    for (int i = 0; i < g_disp_w * g_disp_h; i++) g_fb_rgba[i] = bg_col;

    switch (g_engine.view) {
        case OLED_VIEW_BOOT:          c_render_boot_view(); break;
        case OLED_VIEW_MAIN_MENU:     c_render_main_menu_view(); break;
        case OLED_VIEW_WIFI_MENU:     c_render_wifi_menu_view(); break;
        case OLED_VIEW_NETWORKS_LIST: c_render_networks_list_view(); break;
        case OLED_VIEW_SCANNING:      c_render_scanning_view(); break;
        case OLED_VIEW_CONNECTING:    c_render_connecting_view(); break;
        case OLED_VIEW_STATUS:        c_render_status_view(); break;
        case OLED_VIEW_AP_MODE:       c_render_ap_view(); break;
        case OLED_VIEW_DEAUTH_IDS:    c_render_deauth_ids_view(); break;
        case OLED_VIEW_PROBE_SNIFFER: c_render_probe_sniffer_view(); break;
        case OLED_VIEW_MATRIX_RAIN:   c_render_matrix_rain_view(); break;
        case OLED_VIEW_SNIFFER:       c_render_sniffer_view(); break;
        case OLED_VIEW_BLE_RADAR:     c_render_ble_radar_view(); break;
        case OLED_VIEW_FFT_SPECTRUM:  c_render_fft_view(); break;
        case OLED_VIEW_KART_GAME:     c_render_kart_view(); break;
        case OLED_VIEW_DINO_GAME:     c_render_dino_view(); break;
        case OLED_VIEW_PONG_GAME:     c_render_pong_view(); break;
        case OLED_VIEW_SYS_INFO:      c_render_sys_info_view(); break;
        case OLED_VIEW_HW_SCANNER:    c_render_hw_scanner_view(); break;
        case OLED_VIEW_SUBGHZ:        c_render_subghz_view(); break;
        case OLED_VIEW_ADB_APP:       c_render_adb_app_view(); break;
        case OLED_VIEW_TERMINAL:      c_render_terminal_view(); break;
        case OLED_VIEW_SETTINGS:      c_render_settings_view(); break;
    }
}

EXPORT uint8_t* oled_get_fb(void) {
    return (uint8_t*)g_fb_rgba;
}

EXPORT int oled_get_width(void) { return g_disp_w; }
EXPORT int oled_get_height(void) { return g_disp_h; }

// ============================================================================
// HARDWARE INPUT PIPELINE (1 ROTARY ENCODER + 1 BUTTON)
// ============================================================================
EXPORT void hw_knob_rotate(int dir) {
    if (g_engine.view == OLED_VIEW_BOOT) {
        g_engine.view = OLED_VIEW_MAIN_MENU;
        return;
    }

    if (g_engine.view == OLED_VIEW_DINO_GAME) {
        if (dir == 0) {
            g_dino.is_ducking = true;
            g_dino.duck_timer = 24;
        } else {
            g_dino.is_ducking = false;
            g_dino.duck_timer = 0;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_KART_GAME) {
        float step = (g_disp_mode != DISP_MODE_OLED_128x64) ? 18.0f : 7.0f;
        if (dir == 0) g_kart.target_x -= step;
        else g_kart.target_x += step;
        return;
    }

    if (g_engine.view == OLED_VIEW_PONG_GAME) {
        if (g_pong.state == PONG_STATE_SELECT_DIFF) {
            if (dir == 0) {
                if (g_pong.diff_select_idx > 0) g_pong.diff_select_idx--;
                else g_pong.diff_select_idx = PONG_DIFF_COUNT - 1;
            } else {
                if (g_pong.diff_select_idx < PONG_DIFF_COUNT - 1) g_pong.diff_select_idx++;
                else g_pong.diff_select_idx = 0;
            }
            return;
        } else if (g_pong.state == PONG_STATE_PLAYING) {
            float step = (g_disp_mode != DISP_MODE_OLED_128x64) ? 14.0f : 5.0f;
            if (dir == 0) g_pong.paddle_y -= step;
            else g_pong.paddle_y += step;

            int min_p = (g_disp_mode != DISP_MODE_OLED_128x64) ? 24 : 10;
            int max_p = (g_disp_mode != DISP_MODE_OLED_128x64) ? (g_disp_h - 24) : 52;
            if (g_pong.paddle_y < min_p) g_pong.paddle_y = (float)min_p;
            if (g_pong.paddle_y > max_p) g_pong.paddle_y = (float)max_p;
            return;
        }
    }

    if (g_engine.view == OLED_VIEW_HW_SCANNER) {
        if (dir == 0) {
            if (g_hw_scroll_idx > 0) g_hw_scroll_idx--;
            else g_hw_scroll_idx = HW_DEVICES_COUNT - 1;
        } else {
            if (g_hw_scroll_idx < HW_DEVICES_COUNT - 1) g_hw_scroll_idx++;
            else g_hw_scroll_idx = 0;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_PROBE_SNIFFER) {
        if (dir == 0) {
            if (g_probe_scroll_idx > 0) g_probe_scroll_idx--;
            else g_probe_scroll_idx = (g_probe_count > 0) ? g_probe_count - 1 : 0;
        } else {
            if (g_probe_scroll_idx < g_probe_count - 1) g_probe_scroll_idx++;
            else g_probe_scroll_idx = 0;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_SNIFFER) {
        if (dir == 0) g_current_sniff_channel = (g_current_sniff_channel > 1) ? g_current_sniff_channel - 1 : 13;
        else g_current_sniff_channel = (g_current_sniff_channel < 13) ? g_current_sniff_channel + 1 : 1;
        return;
    }

    if (dir == 0) { // CCW / UP
        if (g_engine.view == OLED_VIEW_MAIN_MENU) {
            if (g_engine.main_index > 0) g_engine.main_index--;
            else g_engine.main_index = MAIN_MENU_COUNT - 1;
        } else if (g_engine.view == OLED_VIEW_WIFI_MENU) {
            if (g_engine.wifi_index > 0) g_engine.wifi_index--;
            else g_engine.wifi_index = WIFI_MENU_COUNT - 1;
        } else if (g_engine.view == OLED_VIEW_NETWORKS_LIST) {
            if (g_net_count > 0) {
                if (g_engine.net_index > 0) g_engine.net_index--;
                else g_engine.net_index = g_net_count - 1;
            }
        } else if (g_engine.view == OLED_VIEW_SUBGHZ) {
            if (g_subghz.page == SUBGHZ_PAGE_MENU) {
                if (g_subghz.menu_idx > 0) g_subghz.menu_idx--;
                else g_subghz.menu_idx = 3;
            } else if (g_subghz.page == SUBGHZ_PAGE_REPLAY) {
                if (g_subghz.slot_idx > 0) g_subghz.slot_idx--;
                else g_subghz.slot_idx = SUBGHZ_MAX_SLOTS - 1;
            }
        } else if (g_engine.view == OLED_VIEW_ADB_APP) {
            if (g_adb.action_idx > 0) g_adb.action_idx--;
            else g_adb.action_idx = (int)ADB_ACTIONS_COUNT - 1;
        } else if (g_engine.view == OLED_VIEW_SETTINGS) {
            if (g_engine.settings_index > 0) g_engine.settings_index--;
            else g_engine.settings_index = 3;
        }
    } else { // CW / DOWN
        if (g_engine.view == OLED_VIEW_MAIN_MENU) {
            if (g_engine.main_index < MAIN_MENU_COUNT - 1) g_engine.main_index++;
            else g_engine.main_index = 0;
        } else if (g_engine.view == OLED_VIEW_WIFI_MENU) {
            if (g_engine.wifi_index < WIFI_MENU_COUNT - 1) g_engine.wifi_index++;
            else g_engine.wifi_index = 0;
        } else if (g_engine.view == OLED_VIEW_NETWORKS_LIST) {
            if (g_net_count > 0) {
                if (g_engine.net_index < g_net_count - 1) g_engine.net_index++;
                else g_engine.net_index = 0;
            }
        } else if (g_engine.view == OLED_VIEW_SUBGHZ) {
            if (g_subghz.page == SUBGHZ_PAGE_MENU) {
                if (g_subghz.menu_idx < 3) g_subghz.menu_idx++;
                else g_subghz.menu_idx = 0;
            } else if (g_subghz.page == SUBGHZ_PAGE_REPLAY) {
                if (g_subghz.slot_idx < SUBGHZ_MAX_SLOTS - 1) g_subghz.slot_idx++;
                else g_subghz.slot_idx = 0;
            }
        } else if (g_engine.view == OLED_VIEW_ADB_APP) {
            if (g_adb.action_idx < (int)ADB_ACTIONS_COUNT - 1) g_adb.action_idx++;
            else g_adb.action_idx = 0;
        } else if (g_engine.view == OLED_VIEW_SETTINGS) {
            if (g_engine.settings_index < 3) g_engine.settings_index++;
            else g_engine.settings_index = 0;
        }
    }
}

EXPORT void hw_button_press(int action) {
    if (g_engine.view == OLED_VIEW_BOOT) {
        g_engine.view = OLED_VIEW_MAIN_MENU;
        return;
    }

    if (action == 2) { // Long Press = BACK
        if (g_engine.view == OLED_VIEW_SUBGHZ) {
            if (g_subghz.page != SUBGHZ_PAGE_MENU) {
                g_subghz.page = SUBGHZ_PAGE_MENU;
                g_subghz.is_recording = false;
                return;
            }
        }
        if (g_engine.view == OLED_VIEW_WIFI_MENU || 
            g_engine.view == OLED_VIEW_DEAUTH_IDS ||
            g_engine.view == OLED_VIEW_PROBE_SNIFFER ||
            g_engine.view == OLED_VIEW_MATRIX_RAIN ||
            g_engine.view == OLED_VIEW_SNIFFER ||
            g_engine.view == OLED_VIEW_BLE_RADAR ||
            g_engine.view == OLED_VIEW_FFT_SPECTRUM ||
            g_engine.view == OLED_VIEW_KART_GAME ||
            g_engine.view == OLED_VIEW_DINO_GAME ||
            g_engine.view == OLED_VIEW_PONG_GAME ||
            g_engine.view == OLED_VIEW_SYS_INFO || 
            g_engine.view == OLED_VIEW_HW_SCANNER ||
            g_engine.view == OLED_VIEW_SUBGHZ ||
            g_engine.view == OLED_VIEW_ADB_APP ||
            g_engine.view == OLED_VIEW_SETTINGS || 
            g_engine.view == OLED_VIEW_TERMINAL) {
            g_engine.view = OLED_VIEW_MAIN_MENU;
        } else if (g_engine.view == OLED_VIEW_NETWORKS_LIST || g_engine.view == OLED_VIEW_STATUS || g_engine.view == OLED_VIEW_AP_MODE) {
            g_engine.view = OLED_VIEW_MAIN_MENU;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_DINO_GAME) {
        if (g_dino.game_over) {
            g_dino.initialized = false;
            g_dino.game_over = false;
        } else if (!g_dino.is_jumping) {
            g_dino.is_jumping = true;
            g_dino.player_vy = (g_disp_mode != DISP_MODE_OLED_128x64) ? -7.4f : -4.5f;
            g_dino.is_ducking = false;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_KART_GAME) {
        if (g_kart.game_over) {
            g_kart.initialized = false;
            g_kart.game_over = false;
        } else if (g_kart.nitro_pct >= 25 && !g_kart.nitro_active) {
            g_kart.nitro_active = true;
            g_kart.nitro_timer = 50;
            g_kart.nitro_pct -= 25;
        }
        return;
    }

    if (g_engine.view == OLED_VIEW_PONG_GAME) {
        if (g_pong.state == PONG_STATE_SELECT_DIFF) {
            g_pong.diff = (pong_difficulty_t)g_pong.diff_select_idx;
            g_pong.player_score = 0;
            g_pong.ai_score = 0;
            g_pong.ball_in_play = false;
            g_pong.paddle_y = (g_disp_mode != DISP_MODE_OLED_128x64) ? (g_disp_h / 2.0f) : 32.0f;
            g_pong.ai_paddle_y = g_pong.paddle_y;
            g_pong.state = PONG_STATE_PLAYING;
        } else if (g_pong.state == PONG_STATE_GAME_OVER) {
            g_pong.state = PONG_STATE_SELECT_DIFF;
        } else if (g_pong.state == PONG_STATE_PLAYING) {
            if (!g_pong.ball_in_play) {
                g_pong.ball_in_play = true;
                bool is_ips = (g_disp_mode != DISP_MODE_OLED_128x64);
                float bspeed = is_ips ? g_pong_diffs[g_pong.diff].ball_speed_ips : g_pong_diffs[g_pong.diff].ball_speed_oled;
                g_pong.ball_vx = bspeed;
                g_pong.ball_vy = bspeed * 0.55f;
            }
        }
        return;
    }

    if (action == 0) {
        if (g_engine.view == OLED_VIEW_MAIN_MENU) {
            if (g_engine.main_index == 0) {
                g_engine.view = OLED_VIEW_WIFI_MENU;
                g_engine.wifi_index = 0;
            } else if (g_engine.main_index == 1) {
                g_engine.view = OLED_VIEW_AP_MODE;
            } else if (g_engine.main_index == 2) {
                g_engine.view = OLED_VIEW_DEAUTH_IDS;
            } else if (g_engine.main_index == 3) {
                g_engine.view = OLED_VIEW_PROBE_SNIFFER;
            } else if (g_engine.main_index == 4) {
                g_engine.view = OLED_VIEW_MATRIX_RAIN;
            } else if (g_engine.main_index == 5) {
                g_engine.view = OLED_VIEW_SNIFFER;
            } else if (g_engine.main_index == 6) {
                g_engine.view = OLED_VIEW_BLE_RADAR;
            } else if (g_engine.main_index == 7) {
                g_engine.view = OLED_VIEW_FFT_SPECTRUM;
            } else if (g_engine.main_index == 8) {
                g_engine.view = OLED_VIEW_KART_GAME;
                g_kart.initialized = false;
            } else if (g_engine.main_index == 9) {
                g_engine.view = OLED_VIEW_DINO_GAME;
                g_dino.initialized = false;
            } else if (g_engine.main_index == 10) {
                g_engine.view = OLED_VIEW_PONG_GAME;
                g_pong.state = PONG_STATE_SELECT_DIFF;
                g_pong.ball_in_play = false;
            } else if (g_engine.main_index == 11) {
                g_engine.view = OLED_VIEW_SYS_INFO;
            } else if (g_engine.main_index == 12) {
                g_engine.view = OLED_VIEW_HW_SCANNER;
                hw_bus_scan();
            } else if (g_engine.main_index == 13) {
                g_engine.view = OLED_VIEW_SUBGHZ;
                g_subghz.page = SUBGHZ_PAGE_MENU;
            } else if (g_engine.main_index == 14) {
                g_engine.view = OLED_VIEW_ADB_APP;
            } else if (g_engine.main_index == 15) {
                g_engine.view = OLED_VIEW_TERMINAL;
            } else if (g_engine.main_index == 16) {
                g_engine.view = OLED_VIEW_SETTINGS;
                g_engine.settings_index = 0;
            } else if (g_engine.main_index == 17) {
#ifndef BULLET_DESKTOP_BUILD
                esp_restart();
#else
                oled_init();
#endif
            }
        } 
        else if (g_engine.view == OLED_VIEW_SETTINGS) {
            if (g_engine.settings_index == 0) {
                g_engine.lang = (g_engine.lang == LANG_RU) ? LANG_EN : LANG_RU;
            } else if (g_engine.settings_index == 1) {
                if (g_active_color == COLOR_OLED_CYAN) g_active_color = COLOR_OLED_AMBER;
                else if (g_active_color == COLOR_OLED_AMBER) g_active_color = COLOR_OLED_WHITE;
                else g_active_color = COLOR_OLED_CYAN;
            } else if (g_engine.settings_index == 2) {
                wifi_ui_regenerate_ap_password();
            } else if (g_engine.settings_index == 3) {
                g_engine.view = OLED_VIEW_MAIN_MENU;
            }
        } 
        else if (g_engine.view == OLED_VIEW_SUBGHZ) {
            if (g_subghz.page == SUBGHZ_PAGE_MENU) {
                if (g_subghz.menu_idx == 0) {
                    g_subghz.page = SUBGHZ_PAGE_RECORD;
                    g_subghz.is_recording = true;
                    g_subghz.captured_pulses = 0;
                } else if (g_subghz.menu_idx == 1) {
                    g_subghz.page = SUBGHZ_PAGE_REPLAY;
                } else if (g_subghz.menu_idx == 2) {
                    g_subghz.page = SUBGHZ_PAGE_ANALYZER;
                } else if (g_subghz.menu_idx == 3) {
                    g_subghz.freq_idx = (g_subghz.freq_idx + 1) % SUBGHZ_FREQS_COUNT;
                    snprintf(g_subghz.last_toast, sizeof(g_subghz.last_toast), "Freq: %.2f MHz", g_subghz_freqs[g_subghz.freq_idx]);
                    g_subghz.toast_timer = 60;
                }
            } else if (g_subghz.page == SUBGHZ_PAGE_RECORD) {
                g_subghz.is_recording = !g_subghz.is_recording;
                if (!g_subghz.is_recording) {
                    snprintf(g_subghz.last_toast, sizeof(g_subghz.last_toast), "Saved %d pulses to Slot", g_subghz.captured_pulses);
                    g_subghz.toast_timer = 90;
                }
            } else if (g_subghz.page == SUBGHZ_PAGE_REPLAY) {
                subghz_trigger_tx(g_subghz.slot_idx);
            } else if (g_subghz.page == SUBGHZ_PAGE_ANALYZER) {
                g_subghz.page = SUBGHZ_PAGE_MENU;
            }
        } 
        else if (g_engine.view == OLED_VIEW_WIFI_MENU) {
            if (g_engine.wifi_index == 0) {
                g_engine.scan_tick = 0;
                g_engine.view = OLED_VIEW_SCANNING;
            } else if (g_engine.wifi_index == 1) {
                g_engine.view = OLED_VIEW_AP_MODE;
            } else if (g_engine.wifi_index == 2) {
                g_engine.view = OLED_VIEW_STATUS;
            } else if (g_engine.wifi_index == 3) {
                g_engine.view = OLED_VIEW_MAIN_MENU;
            }
        } 
        else if (g_engine.view == OLED_VIEW_NETWORKS_LIST) {
            if (g_net_count > 0) {
                g_engine.connect_tick = 0;
                g_engine.view = OLED_VIEW_CONNECTING;
            } else {
                g_engine.view = OLED_VIEW_WIFI_MENU;
            }
        } 
        else if (g_engine.view == OLED_VIEW_ADB_APP) {
            adb_trigger_action(g_adb.action_idx);
        }
        else if (g_engine.view == OLED_VIEW_SETTINGS) {
            if (g_engine.settings_index == 0) {
                g_engine.lang = (g_engine.lang == LANG_RU) ? LANG_EN : LANG_RU;
            } else if (g_engine.settings_index == 1) {
                if (g_active_color == COLOR_OLED_CYAN) g_active_color = COLOR_OLED_AMBER;
                else if (g_active_color == COLOR_OLED_AMBER) g_active_color = COLOR_OLED_WHITE;
                else g_active_color = COLOR_OLED_CYAN;
            } else if (g_engine.settings_index == 2) {
                g_engine.view = OLED_VIEW_MAIN_MENU;
            }
        }
        else if (g_engine.view == OLED_VIEW_HW_SCANNER) {
            hw_bus_scan();
        }
        else if (g_engine.view == OLED_VIEW_STATUS || g_engine.view == OLED_VIEW_AP_MODE || 
                 g_engine.view == OLED_VIEW_SYS_INFO || g_engine.view == OLED_VIEW_SNIFFER ||
                 g_engine.view == OLED_VIEW_BLE_RADAR || g_engine.view == OLED_VIEW_FFT_SPECTRUM ||
                 g_engine.view == OLED_VIEW_DEAUTH_IDS || g_engine.view == OLED_VIEW_PROBE_SNIFFER ||
                 g_engine.view == OLED_VIEW_MATRIX_RAIN || g_engine.view == OLED_VIEW_TERMINAL) {
            g_engine.view = OLED_VIEW_MAIN_MENU;
        }
    }
}

EXPORT void oled_key(int key) {
    if (key == 0) hw_knob_rotate(0);
    else if (key == 1) hw_knob_rotate(1);
    else if (key == 2) hw_button_press(0);
    else if (key == 3) hw_button_press(2);
}

EXPORT void oled_char_input(int char_code) {
    if (g_engine.view != OLED_VIEW_TERMINAL) return;

    // TAB Key Autocomplete (char code 9 or '\t')
    if (char_code == 9 || char_code == '\t') {
        const char* suggestion = term_get_autocomplete_suggestion();
        if (suggestion) {
            strncpy(g_input_buf, suggestion, sizeof(g_input_buf) - 1);
            g_input_buf[sizeof(g_input_buf) - 1] = '\0';
            g_input_len = (int)strlen(g_input_buf);
        }
        return;
    }

    if (char_code >= 32 && char_code <= 126) {
        if (g_input_len < (int)sizeof(g_input_buf) - 2) {
            g_input_buf[g_input_len++] = (char)char_code;
            g_input_buf[g_input_len] = '\0';
        }
    }
}

EXPORT void oled_tab_autocomplete(void) {
    if (g_engine.view != OLED_VIEW_TERMINAL) return;
    const char* suggestion = term_get_autocomplete_suggestion();
    if (suggestion) {
        strncpy(g_input_buf, suggestion, sizeof(g_input_buf) - 1);
        g_input_buf[sizeof(g_input_buf) - 1] = '\0';
        g_input_len = (int)strlen(g_input_buf);
    }
}

EXPORT void oled_backspace(void) {
    if (g_engine.view != OLED_VIEW_TERMINAL) return;
    if (g_input_len > 0) {
        g_input_len--;
        g_input_buf[g_input_len] = '\0';
    }
}

EXPORT void oled_enter(void) {
    if (g_engine.view == OLED_VIEW_TERMINAL) {
        term_execute_cmd(g_input_buf);
        g_input_buf[0] = '\0';
        g_input_len = 0;
    }
}

EXPORT void oled_set_theme(int theme_id) {
    if (theme_id == 0) g_active_color = COLOR_OLED_CYAN;
    else if (theme_id == 1) g_active_color = COLOR_OLED_WHITE;
    else if (theme_id == 2) g_active_color = COLOR_OLED_AMBER;
}

EXPORT void oled_set_lang(int lang_id) {
    g_engine.lang = (oled_lang_t)lang_id;
}
