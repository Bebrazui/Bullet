#include "ui_theme.h"

ui_theme_t g_ui_theme;

void ui_theme_init(void) {
    g_ui_theme.bg_color = COLOR_BG_DARK;
    g_ui_theme.card_bg = COLOR_CARD_BG;
    g_ui_theme.accent_color = COLOR_ACCENT_CYAN;
    g_ui_theme.text_primary = COLOR_TEXT_PRIMARY;
    g_ui_theme.text_muted = COLOR_TEXT_MUTED;
}

void ui_theme_toggle_dark_mode(void) {
    if (g_ui_theme.bg_color == COLOR_BG_DARK) {
        g_ui_theme.bg_color = 0xF0F4F8;
        g_ui_theme.card_bg = 0xFFFFFF;
        g_ui_theme.accent_color = 0x007AFF;
        g_ui_theme.text_primary = 0x1A202C;
        g_ui_theme.text_muted = 0x718096;
    } else {
        ui_theme_init();
    }
}
