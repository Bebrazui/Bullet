/**
 * @file cc1101_driver.h
 * @brief Real Physical CC1101 Sub-GHz SPI Transceiver Driver for ESP32
 */

#ifndef CC1101_DRIVER_H
#define CC1101_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default Hardware Pinout for ESP32 / ESP32-S3
#ifndef CC1101_PIN_CS
#define CC1101_PIN_CS   10  // Chip Select
#endif
#ifndef CC1101_PIN_SCK
#define CC1101_PIN_SCK  12  // SPI Clock
#endif
#ifndef CC1101_PIN_MOSI
#define CC1101_PIN_MOSI 11  // SPI MOSI
#endif
#ifndef CC1101_PIN_MISO
#define CC1101_PIN_MISO 13  // SPI MISO
#endif
#ifndef CC1101_PIN_GDO0
#define CC1101_PIN_GDO0 14  // Raw TX/RX Pulse Data Pin
#endif
#ifndef CC1101_PIN_GDO2
#define CC1101_PIN_GDO2 21  // Carrier Sense / Threshold Pin
#endif

typedef enum {
    CC1101_MOD_OOK = 0x30,
    CC1101_MOD_2FSK = 0x00,
    CC1101_MOD_4FSK = 0x40,
    CC1101_MOD_GFSK = 0x10,
    CC1101_MOD_MSK  = 0x70
} cc1101_modulation_t;

typedef struct {
    float frequency_mhz;
    cc1101_modulation_t modulation;
    int8_t tx_power_dbm;
    bool is_detected;
    uint8_t chip_version;
    uint8_t chip_partnum;
} cc1101_hw_state_t;

#define CC1101_RAW_BUF_SIZE 512

typedef struct {
    uint32_t durations_us[CC1101_RAW_BUF_SIZE];
    uint8_t levels[CC1101_RAW_BUF_SIZE];
    uint16_t count;
    float frequency_mhz;
    char name[32];
} cc1101_raw_signal_t;

/**
 * @brief Initialize SPI bus and check physical presence of CC1101
 * @return true if real CC1101 chip responded with valid version ID
 */
bool cc1101_hw_init(int cs_pin, int sck_pin, int mosi_pin, int miso_pin, int gdo0_pin, int gdo2_pin);

/**
 * @brief Probe CC1101 presence on SPI bus
 */
bool cc1101_hw_probe(void);

/**
 * @brief Configure CC1101 operating frequency (MHz)
 */
bool cc1101_hw_set_frequency(float freq_mhz);

/**
 * @brief Configure CC1101 modulation
 */
bool cc1101_hw_set_modulation(cc1101_modulation_t mod);

/**
 * @brief Start raw asynchronous signal reception (Sniffer / Recorder)
 */
void cc1101_hw_start_rx_raw(void);

/**
 * @brief Stop raw signal reception and get captured pulses
 */
uint16_t cc1101_hw_stop_rx_raw(cc1101_raw_signal_t* out_signal);

/**
 * @brief Transmit/Replay raw signal pulses through GDO0
 */
bool cc1101_hw_tx_raw_signal(const cc1101_raw_signal_t* signal);

/**
 * @brief Read live real-time RSSI (in dBm) from CC1101 AGC
 */
int8_t cc1101_hw_get_rssi_dbm(void);

/**
 * @brief Put CC1101 into low-power IDLE / Sleep
 */
void cc1101_hw_sleep(void);

#ifdef __cplusplus
}
#endif

#endif // CC1101_DRIVER_H
