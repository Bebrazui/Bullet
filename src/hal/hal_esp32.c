#include "hal.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#define BACKLIGHT_GPIO  38
#define BACKLIGHT_PWM_CH LEDC_CHANNEL_0

static void esp32_hal_init(void) {
    // Инициализация ШИМ подсветки (LEDC)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = BACKLIGHT_PWM_CH,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BACKLIGHT_GPIO,
        .duty           = 255,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

static void esp32_set_backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_PWM_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_PWM_CH);
}

static void esp32_get_battery_status(uint8_t *percent, bool *is_charging) {
    if (percent) *percent = 92; // Чтение через ADC (например, GPIO 1 или MAX17048)
    if (is_charging) *is_charging = false;
}

static void esp32_get_memory_info(uint32_t *free_sram, uint32_t *total_sram, uint32_t *free_psram, uint32_t *total_psram) {
    if (free_sram) *free_sram = esp_get_free_internal_heap_size();
    if (total_sram) *total_sram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    if (free_psram) *free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (total_psram) *total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

static float esp32_get_temperature(void) {
    return 38.2f;
}

static void esp32_restart(void) {
    esp_restart();
}

static const hal_driver_t s_esp32_driver = {
    .init = esp32_hal_init,
    .set_backlight = esp32_set_backlight,
    .get_battery_status = esp32_get_battery_status,
    .get_memory_info = esp32_get_memory_info,
    .get_temperature = esp32_get_temperature,
    .restart = esp32_restart
};

const hal_driver_t *g_hal = &s_esp32_driver;

void hal_init(void) {
    g_hal->init();
}

#endif
