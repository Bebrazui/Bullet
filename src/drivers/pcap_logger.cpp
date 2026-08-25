/**
 * @file pcap_logger.cpp
 * @brief Real Physical PCAP Logger for ESP32 with SD-Card and LittleFS Storage
 */

#include "pcap_logger.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

static File g_pcap_file;
static bool g_is_recording = false;
static bool g_sd_mounted = false;
static uint32_t g_pcap_packets = 0;
static uint32_t g_pcap_bytes = 0;
static char g_filename[64] = "/capture.pcap";

#define SD_CS_PIN 4

bool pcap_logger_init(void) {
    // Try mounting SD card on SPI
    if (SD.begin(SD_CS_PIN)) {
        g_sd_mounted = true;
    } else {
        g_sd_mounted = false;
    }
    return true;
}

bool pcap_logger_start(const char* filename) {
    pcap_logger_stop();

    if (filename && strlen(filename) > 0) {
        strncpy(g_filename, filename, sizeof(g_filename) - 1);
    } else {
        strncpy(g_filename, "/capture.pcap", sizeof(g_filename) - 1);
    }

    if (g_sd_mounted) {
        g_pcap_file = SD.open(g_filename, FILE_WRITE);
    } else {
        g_pcap_file = LittleFS.open(g_filename, "w");
    }

    if (!g_pcap_file) {
        return false;
    }

    // Write Standard libpcap 2.4 Global Header
    pcap_global_header_t gh;
    gh.magic_number = 0xa1b2c3d4;
    gh.version_major = 2;
    gh.version_minor = 4;
    gh.thiszone = 0;
    gh.sigfigs = 0;
    gh.snaplen = 65535;
    gh.network = PCAP_LINKTYPE_IEEE802_11; // 105 = 802.11 Wi-Fi link layer

    size_t written = g_pcap_file.write((const uint8_t*)&gh, sizeof(gh));
    if (written != sizeof(gh)) {
        g_pcap_file.close();
        return false;
    }

    g_pcap_packets = 0;
    g_pcap_bytes = sizeof(gh);
    g_is_recording = true;

    return true;
}

void pcap_logger_stop(void) {
    if (g_is_recording && g_pcap_file) {
        g_pcap_file.flush();
        g_pcap_file.close();
    }
    g_is_recording = false;
}

bool pcap_logger_is_recording(void) {
    return g_is_recording;
}

void pcap_logger_log_packet(const uint8_t* data, size_t len, size_t orig_len) {
    if (!g_is_recording || !g_pcap_file || !data || len == 0) return;

    uint32_t now_us = micros();
    uint32_t sec = now_us / 1000000;
    uint32_t usec = now_us % 1000000;

    pcap_packet_header_t ph;
    ph.ts_sec = sec;
    ph.ts_usec = usec;
    ph.incl_len = (uint32_t)len;
    ph.orig_len = (uint32_t)orig_len;

    g_pcap_file.write((const uint8_t*)&ph, sizeof(ph));
    g_pcap_file.write(data, len);

    g_pcap_packets++;
    g_pcap_bytes += (sizeof(ph) + len);

    // Periodic sync every 32 packets
    if ((g_pcap_packets % 32) == 0) {
        g_pcap_file.flush();
    }
}

void pcap_logger_get_status(pcap_status_t* out_status) {
    if (!out_status) return;
    out_status->is_recording = g_is_recording;
    out_status->total_packets = g_pcap_packets;
    out_status->total_bytes = g_pcap_bytes;
    out_status->sd_mounted = g_sd_mounted;
    strncpy(out_status->current_filename, g_filename, sizeof(out_status->current_filename) - 1);
}

void pcap_logger_clear(void) {
    pcap_logger_stop();
    if (g_sd_mounted) {
        SD.remove(g_filename);
    } else {
        LittleFS.remove(g_filename);
    }
    g_pcap_packets = 0;
    g_pcap_bytes = 0;
}

#else
// Desktop stub
bool pcap_logger_init(void) { return true; }
bool pcap_logger_start(const char* filename) { return true; }
void pcap_logger_stop(void) {}
bool pcap_logger_is_recording(void) { return false; }
void pcap_logger_log_packet(const uint8_t* data, size_t len, size_t orig_len) {}
void pcap_logger_get_status(pcap_status_t* out_status) {
    if (out_status) {
        out_status->is_recording = false;
        out_status->total_packets = 0;
        out_status->total_bytes = 0;
        out_status->sd_mounted = false;
        out_status->current_filename[0] = '\0';
    }
}
void pcap_logger_clear(void) {}
#endif
