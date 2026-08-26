/**
 * @file github_downloader.h
 * @brief Real Chunked HTTPS GitHub Downloader for ESP32 & LittleFS
 */

#ifndef GITHUB_DOWNLOADER_H
#define GITHUB_DOWNLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_downloading;
    uint32_t total_bytes;
    uint32_t downloaded_bytes;
    int progress_percent;
    char current_url[128];
    char target_path[64];
    char status_msg[64];
    bool is_complete;
    bool has_error;
} github_download_state_t;

void github_downloader_init(void);
bool github_downloader_start(const char* url, const char* local_path);
void github_downloader_abort(void);
void github_downloader_poll(void);
github_download_state_t github_downloader_get_state(void);

#ifdef __cplusplus
}
#endif

#endif
