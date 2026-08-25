/**
 * @file pcap_logger.h
 * @brief Standard libpcap 2.4 File Format Writer & Live Stream Engine
 * @details Records IEEE 802.11 frames to SD card / LittleFS and serves to PC/phone via HTTP/Wi-Fi
 */

#ifndef PCAP_LOGGER_H
#define PCAP_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Link Layer Types in PCAP
#define PCAP_LINKTYPE_ETHERNET  1
#define PCAP_LINKTYPE_IEEE802_11 105
#define PCAP_LINKTYPE_IEEE802_11_RADIOTAP 127

#pragma pack(push, 1)
typedef struct {
    uint32_t magic_number;   // 0xa1b2c3d4
    uint16_t version_major;  // 2
    uint16_t version_minor;  // 4
    int32_t  thiszone;       // GMT to local correction
    uint32_t sigfigs;        // accuracy of timestamps
    uint32_t snaplen;        // max length of captured packets (65535)
    uint32_t network;        // data link type (105 = 802.11)
} pcap_global_header_t;

typedef struct {
    uint32_t ts_sec;         // timestamp seconds
    uint32_t ts_usec;        // timestamp microseconds
    uint32_t incl_len;       // number of octets of packet saved in file
    uint32_t orig_len;       // actual length of packet
} pcap_packet_header_t;
#pragma pack(pop)

typedef struct {
    bool is_recording;
    uint32_t total_packets;
    uint32_t total_bytes;
    bool sd_mounted;
    char current_filename[64];
} pcap_status_t;

/**
 * @brief Initialize PCAP logging engine (SD card or LittleFS fallback)
 */
bool pcap_logger_init(void);

/**
 * @brief Start recording packets into a new .pcap file
 * @param filename File path (e.g. "/capture.pcap")
 */
bool pcap_logger_start(const char* filename);

/**
 * @brief Stop current recording and flush file buffers
 */
void pcap_logger_stop(void);

/**
 * @brief Check if PCAP logger is actively recording
 */
bool pcap_logger_is_recording(void);

/**
 * @brief Feed raw packet into PCAP logger
 * @param data Packet payload
 * @param len Captured length
 * @param orig_len Original wire length
 */
void pcap_logger_log_packet(const uint8_t* data, size_t len, size_t orig_len);

/**
 * @brief Get status telemetry of PCAP logger
 */
void pcap_logger_get_status(pcap_status_t* out_status);

/**
 * @brief Delete capture file from storage
 */
void pcap_logger_clear(void);

#ifdef __cplusplus
}
#endif

#endif // PCAP_LOGGER_H
