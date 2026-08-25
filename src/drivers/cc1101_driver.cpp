/**
 * @file cc1101_driver.cpp
 * @brief Real Physical CC1101 SPI Transceiver Driver for ESP32 & FreeRTOS
 */

#include "cc1101_driver.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Arduino.h>
#include <SPI.h>

// CC1101 Register Map
#define CC1101_IOCFG2    0x00
#define CC1101_IOCFG1    0x01
#define CC1101_IOCFG0    0x02
#define CC1101_FIFOTHR   0x03
#define CC1101_SYNC1     0x04
#define CC1101_SYNC0     0x05
#define CC1101_PKTLEN    0x06
#define CC1101_PKTCTRL1  0x07
#define CC1101_PKTCTRL0  0x08
#define CC1101_ADDR      0x09
#define CC1101_CHANNR    0x0A
#define CC1101_FSCTRL1   0x0B
#define CC1101_FSCTRL0   0x0C
#define CC1101_FREQ2     0x0D
#define CC1101_FREQ1     0x0E
#define CC1101_FREQ0     0x0F
#define CC1101_MDMCFG4   0x10
#define CC1101_MDMCFG3   0x11
#define CC1101_MDMCFG2   0x12
#define CC1101_MDMCFG1   0x13
#define CC1101_MDMCFG0   0x14
#define CC1101_DEVIATN   0x15
#define CC1101_MCSM2     0x16
#define CC1101_MCSM1     0x17
#define CC1101_MCSM0     0x18
#define CC1101_FOCCFG    0x19
#define CC1101_BSCFG     0x1A
#define CC1101_AGCCTRL2  0x1B
#define CC1101_AGCCTRL1  0x1C
#define CC1101_AGCCTRL0  0x1D
#define CC1101_FREND1    0x21
#define CC1101_FREND0    0x22
#define CC1101_FSCAL3    0x23
#define CC1101_FSCAL2    0x24
#define CC1101_FSCAL1    0x25
#define CC1101_FSCAL0    0x26
#define CC1101_TEST2     0x2C
#define CC1101_TEST1     0x2D
#define CC1101_TEST0     0x2E

// Command Strobes
#define CC1101_SRES      0x30
#define CC1101_SFSTXON   0x31
#define CC1101_SXOFF     0x32
#define CC1101_SCAL      0x33
#define CC1101_SRX       0x34
#define CC1101_STX       0x35
#define CC1101_SIDLE     0x36
#define CC1101_SPWD      0x39
#define CC1101_SFRX      0x3A
#define CC1101_SFTX      0x3B

// Status Registers
#define CC1101_PARTNUM   (0x30 | 0xC0)
#define CC1101_VERSION   (0x31 | 0xC0)
#define CC1101_RSSI      (0x34 | 0xC0)
#define CC1101_MARCSTATE (0x35 | 0xC0)

static int g_pin_cs = 10;
static int g_pin_sck = 12;
static int g_pin_mosi = 11;
static int g_pin_miso = 13;
static int g_pin_gdo0 = 14;
static int g_pin_gdo2 = 21;

static SPIClass g_cc1101_spi(FSPI);
static cc1101_hw_state_t g_hw_state = {
    .frequency_mhz = 433.92f,
    .modulation = CC1101_MOD_OOK,
    .tx_power_dbm = 10,
    .is_detected = false,
    .chip_version = 0,
    .chip_partnum = 0
};

// ISR Capture Buffer
static volatile uint32_t isr_last_time_us = 0;
static volatile uint16_t isr_pulse_count = 0;
static volatile uint32_t isr_durations[CC1101_RAW_BUF_SIZE];
static volatile uint8_t isr_levels[CC1101_RAW_BUF_SIZE];
static volatile bool isr_is_recording = false;

static void IRAM_ATTR cc1101_gdo0_isr(void) {
    if (!isr_is_recording) return;
    uint32_t now = micros();
    uint32_t diff = now - isr_last_time_us;
    isr_last_time_us = now;

    if (diff < 50) return; // Glitch filter

    uint16_t idx = isr_pulse_count;
    if (idx < CC1101_RAW_BUF_SIZE) {
        isr_durations[idx] = diff;
        isr_levels[idx] = digitalRead(g_pin_gdo0);
        isr_pulse_count = idx + 1;
    }
}

static uint8_t spi_read_reg(uint8_t addr) {
    digitalWrite(g_pin_cs, LOW);
    while (digitalRead(g_pin_miso) == HIGH); // Wait for MISO ready
    g_cc1101_spi.transfer(addr | 0x80);
    uint8_t val = g_cc1101_spi.transfer(0x00);
    digitalWrite(g_pin_cs, HIGH);
    return val;
}

static void spi_write_reg(uint8_t addr, uint8_t val) {
    digitalWrite(g_pin_cs, LOW);
    while (digitalRead(g_pin_miso) == HIGH);
    g_cc1101_spi.transfer(addr);
    g_cc1101_spi.transfer(val);
    digitalWrite(g_pin_cs, HIGH);
}

static uint8_t spi_strobe(uint8_t strobe) {
    digitalWrite(g_pin_cs, LOW);
    while (digitalRead(g_pin_miso) == HIGH);
    uint8_t status = g_cc1101_spi.transfer(strobe);
    digitalWrite(g_pin_cs, HIGH);
    return status;
}

static void cc1101_reset(void) {
    digitalWrite(g_pin_cs, HIGH);
    delayMicroseconds(5);
    digitalWrite(g_pin_cs, LOW);
    delayMicroseconds(10);
    digitalWrite(g_pin_cs, HIGH);
    delayMicroseconds(45);
    spi_strobe(CC1101_SRES);
    delay(5);
}

bool cc1101_hw_probe(void) {
    uint8_t ver = spi_read_reg(CC1101_VERSION);
    uint8_t part = spi_read_reg(CC1101_PARTNUM);

    // CC1101 version register is 0x14 or 0x04 (PARTNUM is 0x00)
    if (ver == 0x14 || ver == 0x04) {
        g_hw_state.is_detected = true;
        g_hw_state.chip_version = ver;
        g_hw_state.chip_partnum = part;
        return true;
    }
    g_hw_state.is_detected = false;
    return false;
}

bool cc1101_hw_init(int cs_pin, int sck_pin, int mosi_pin, int miso_pin, int gdo0_pin, int gdo2_pin) {
    g_pin_cs = cs_pin;
    g_pin_sck = sck_pin;
    g_pin_mosi = mosi_pin;
    g_pin_miso = miso_pin;
    g_pin_gdo0 = gdo0_pin;
    g_pin_gdo2 = gdo2_pin;

    pinMode(g_pin_cs, OUTPUT);
    digitalWrite(g_pin_cs, HIGH);
    pinMode(g_pin_miso, INPUT);
    pinMode(g_pin_gdo0, INPUT);
    pinMode(g_pin_gdo2, INPUT);

    g_cc1101_spi.begin(g_pin_sck, g_pin_miso, g_pin_mosi, g_pin_cs);

    cc1101_reset();

    if (!cc1101_hw_probe()) {
        return false;
    }

    // Default Configuration for RAW Asynchronous OOK Sub-GHz (433.92MHz)
    spi_write_reg(CC1101_IOCFG0, 0x0D);   // GDO0 = Serial Asynchronous Data Output / Input
    spi_write_reg(CC1101_IOCFG2, 0x2E);   // GDO2 = High Impedance / Carrier Sense
    spi_write_reg(CC1101_FIFOTHR, 0x47);
    spi_write_reg(CC1101_PKTCTRL0, 0x32); // Asynchronous serial mode, infinite packet length
    spi_write_reg(CC1101_FSCTRL1, 0x06);
    spi_write_reg(CC1101_MDMCFG4, 0x87);
    spi_write_reg(CC1101_MDMCFG3, 0x32);
    spi_write_reg(CC1101_MDMCFG2, CC1101_MOD_OOK); // OOK / ASK modulation
    spi_write_reg(CC1101_MDMCFG1, 0x22);
    spi_write_reg(CC1101_MDMCFG0, 0xF8);
    spi_write_reg(CC1101_MCSM0, 0x18);
    spi_write_reg(CC1101_FOCCFG, 0x16);
    spi_write_reg(CC1101_AGCCTRL2, 0x43);
    spi_write_reg(CC1101_AGCCTRL1, 0x40);
    spi_write_reg(CC1101_AGCCTRL0, 0x91);
    spi_write_reg(CC1101_FREND0, 0x11);
    spi_write_reg(CC1101_FSCAL3, 0xE9);
    spi_write_reg(CC1101_FSCAL2, 0x2A);
    spi_write_reg(CC1101_FSCAL1, 0x00);
    spi_write_reg(CC1101_FSCAL0, 0x1F);
    spi_write_reg(CC1101_TEST2, 0x88);
    spi_write_reg(CC1101_TEST1, 0x31);
    spi_write_reg(CC1101_TEST0, 0x09);

    cc1101_hw_set_frequency(433.92f);

    return true;
}

bool cc1101_hw_set_frequency(float freq_mhz) {
    if (!g_hw_state.is_detected) return false;

    // Freq formula: FREQ = (freq_mhz * 65536) / 26.0
    uint32_t freq_reg = (uint32_t)((freq_mhz * 65536.0f) / 26.0f);
    uint8_t f2 = (freq_reg >> 16) & 0xFF;
    uint8_t f1 = (freq_reg >> 8) & 0xFF;
    uint8_t f0 = freq_reg & 0xFF;

    spi_strobe(CC1101_SIDLE);
    spi_write_reg(CC1101_FREQ2, f2);
    spi_write_reg(CC1101_FREQ1, f1);
    spi_write_reg(CC1101_FREQ0, f0);
    spi_strobe(CC1101_SCAL);

    g_hw_state.frequency_mhz = freq_mhz;
    return true;
}

bool cc1101_hw_set_modulation(cc1101_modulation_t mod) {
    if (!g_hw_state.is_detected) return false;
    spi_strobe(CC1101_SIDLE);
    spi_write_reg(CC1101_MDMCFG2, mod);
    g_hw_state.modulation = mod;
    return true;
}

void cc1101_hw_start_rx_raw(void) {
    if (!g_hw_state.is_detected) return;

    spi_strobe(CC1101_SIDLE);
    spi_strobe(CC1101_SFRX);
    spi_write_reg(CC1101_IOCFG0, 0x0D); // GDO0 = Serial Data Output

    isr_pulse_count = 0;
    isr_last_time_us = micros();
    isr_is_recording = true;

    attachInterrupt(digitalPinToInterrupt(g_pin_gdo0), cc1101_gdo0_isr, CHANGE);

    spi_strobe(CC1101_SRX);
}

uint16_t cc1101_hw_stop_rx_raw(cc1101_raw_signal_t* out_signal) {
    detachInterrupt(digitalPinToInterrupt(g_pin_gdo0));
    isr_is_recording = false;
    spi_strobe(CC1101_SIDLE);

    if (out_signal) {
        out_signal->count = isr_pulse_count;
        out_signal->frequency_mhz = g_hw_state.frequency_mhz;
        for (uint16_t i = 0; i < isr_pulse_count && i < CC1101_RAW_BUF_SIZE; i++) {
            out_signal->durations_us[i] = isr_durations[i];
            out_signal->levels[i] = isr_levels[i];
        }
    }
    return isr_pulse_count;
}

bool cc1101_hw_tx_raw_signal(const cc1101_raw_signal_t* signal) {
    if (!g_hw_state.is_detected || !signal || signal->count == 0) return false;

    spi_strobe(CC1101_SIDLE);
    spi_strobe(CC1101_SFTX);
    spi_write_reg(CC1101_IOCFG0, 0x2D); // GDO0 = Asynchronous TX Input

    pinMode(g_pin_gdo0, OUTPUT);
    digitalWrite(g_pin_gdo0, LOW);

    spi_strobe(CC1101_STX);
    delayMicroseconds(500); // Allow PLL lock

    // Real physical microsecond pulse transmission
    for (uint16_t i = 0; i < signal->count; i++) {
        digitalWrite(g_pin_gdo0, signal->levels[i] ? HIGH : LOW);
        delayMicroseconds(signal->durations_us[i]);
    }

    digitalWrite(g_pin_gdo0, LOW);
    pinMode(g_pin_gdo0, INPUT);
    spi_strobe(CC1101_SIDLE);

    return true;
}

int8_t cc1101_hw_get_rssi_dbm(void) {
    if (!g_hw_state.is_detected) return -100;
    uint8_t raw = spi_read_reg(CC1101_RSSI);
    int8_t rssi_dbm;
    if (raw >= 128) {
        rssi_dbm = (int8_t)(((int16_t)raw - 256) / 2) - 74;
    } else {
        rssi_dbm = (int8_t)((int16_t)raw / 2) - 74;
    }
    return rssi_dbm;
}

void cc1101_hw_sleep(void) {
    if (!g_hw_state.is_detected) return;
    spi_strobe(CC1101_SIDLE);
    spi_strobe(CC1101_SPWD);
}

#else
// Fallback stub for desktop WASM / Posix simulator
bool cc1101_hw_init(int a, int b, int c, int d, int e, int f) { return true; }
bool cc1101_hw_probe(void) { return true; }
bool cc1101_hw_set_frequency(float f) { return true; }
bool cc1101_hw_set_modulation(cc1101_modulation_t m) { return true; }
void cc1101_hw_start_rx_raw(void) {}
uint16_t cc1101_hw_stop_rx_raw(cc1101_raw_signal_t* s) { return 0; }
bool cc1101_hw_tx_raw_signal(const cc1101_raw_signal_t* s) { return true; }
int8_t cc1101_hw_get_rssi_dbm(void) { return -85; }
void cc1101_hw_sleep(void) {}
#endif
