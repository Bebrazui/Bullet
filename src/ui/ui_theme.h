#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Цветовая палитра ESPDroid Cyber/Glassmorphism
#define COLOR_BG_DARK         0x10141D // Глубокий темно-синий/черный фон
#define COLOR_CARD_BG         0x1A2232 // Цвет карточек и виджетов
#define COLOR_CARD_BORDER     0x2A374F // Граница карточек
#define COLOR_ACCENT_CYAN     0x00E5FF // Неоновый циан (основной акцент)
#define COLOR_ACCENT_PURPLE   0x7C4DFF // Неоновый фиолетовый
#define COLOR_SUCCESS_GREEN   0x00E676 // Индикатор работы / батареи
#define COLOR_WARN_ORANGE     0xFF9100 // Предупреждения
#define COLOR_DANGER_RED      0xFF1744 // Критический уровень / ошибки
#define COLOR_TEXT_PRIMARY    0xFFFFFF // Основной белый текст
#define COLOR_TEXT_MUTED      0x8FA3BF // Вторичный текст

// Размеры экрана по умолчанию (для ESP32-S3 TFT/IPS 320x240 или 480x320)
#define SCREEN_WIDTH          320
#define SCREEN_HEIGHT         240
#define STATUS_BAR_HEIGHT     26
#define NAV_BAR_HEIGHT        26
#define CONTENT_HEIGHT        (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - NAV_BAR_HEIGHT)

typedef struct {
    uint32_t bg_color;
    uint32_t card_bg;
    uint32_t accent_color;
    uint32_t text_primary;
    uint32_t text_muted;
} ui_theme_t;

extern ui_theme_t g_ui_theme;

void ui_theme_init(void);
void ui_theme_toggle_dark_mode(void);

#ifdef __cplusplus
}
#endif

#endif // UI_THEME_H
