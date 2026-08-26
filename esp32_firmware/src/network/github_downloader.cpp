/**
 * @file github_downloader.cpp
 * @brief Real Chunked HTTPS GitHub Downloader Implementation
 */

#include "github_downloader.h"
#include <string.h>
#include <stdio.h>

#if !defined(__EMSCRIPTEN__) && !defined(BULLET_DESKTOP_BUILD)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#endif

static github_download_state_t g_dl_state = { false, 0, 0, 0, "", "", "Ready", false, false };

void github_downloader_init(void) {
    memset(&g_dl_state, 0, sizeof(g_dl_state));
    strncpy(g_dl_state.status_msg, "Ready", sizeof(g_dl_state.status_msg) - 1);
}

bool github_downloader_start(const char* url, const char* local_path) {
    if (!url || !local_path) return false;
    
    g_dl_state.is_downloading = true;
    g_dl_state.total_bytes = 0;
    g_dl_state.downloaded_bytes = 0;
    g_dl_state.progress_percent = 0;
    g_dl_state.is_complete = false;
    g_dl_state.has_error = false;
    strncpy(g_dl_state.current_url, url, sizeof(g_dl_state.current_url) - 1);
    strncpy(g_dl_state.target_path, local_path, sizeof(g_dl_state.target_path) - 1);
    snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "Connecting to GitHub...");

#if !defined(__EMSCRIPTEN__) && !defined(BULLET_DESKTOP_BUILD)
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "Wi-Fi not connected");
        g_dl_state.has_error = true;
        g_dl_state.is_downloading = false;
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // GitHub raw CDN TLS
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            int len = http.getSize();
            g_dl_state.total_bytes = (len > 0) ? (uint32_t)len : 4096;
            
            File file = LittleFS.open(local_path, "w");
            if (!file) {
                snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "LittleFS Write Error");
                g_dl_state.has_error = true;
                g_dl_state.is_downloading = false;
                http.end();
                return false;
            }

            WiFiClient* stream = http.getStreamPtr();
            uint8_t buff[512];
            int bytesRead = 0;

            while (http.connected() && (len > 0 || len == -1)) {
                size_t size = stream->available();
                if (size) {
                    int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                    file.write(buff, c);
                    g_dl_state.downloaded_bytes += c;
                    if (g_dl_state.total_bytes > 0) {
                        g_dl_state.progress_percent = (int)((g_dl_state.downloaded_bytes * 100) / g_dl_state.total_bytes);
                    }
                    if (len > 0) len -= c;
                }
                delay(1);
            }
            file.close();
            g_dl_state.progress_percent = 100;
            g_dl_state.is_complete = true;
            g_dl_state.is_downloading = false;
            snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "Saved to %s", local_path);
            http.end();
            return true;
        } else {
            snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "HTTP %d Error", httpCode);
            g_dl_state.has_error = true;
            g_dl_state.is_downloading = false;
            http.end();
            return false;
        }
    }
#else
    // Desktop emulator mode
    g_dl_state.total_bytes = 4850;
    g_dl_state.downloaded_bytes = 4850;
    g_dl_state.progress_percent = 100;
    g_dl_state.is_complete = true;
    g_dl_state.is_downloading = false;
    snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "Downloaded OK");
#endif
    return true;
}

void github_downloader_abort(void) {
    g_dl_state.is_downloading = false;
    g_dl_state.has_error = true;
    snprintf(g_dl_state.status_msg, sizeof(g_dl_state.status_msg), "Download Cancelled");
}

void github_downloader_poll(void) {
    // Polling hook if async mode is needed
}

github_download_state_t github_downloader_get_state(void) {
    return g_dl_state;
}
