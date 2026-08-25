/**
 * @file main.cpp
 * @brief Bullet OS - Real Hardware Driver for ESP32-S3 (N16R8) & QEMU
 * @details Self-contained firmware with native WebServer, Wi-Fi Scan & OLED/IPS UI Engine.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "wifi_oled_ui.h"
#include "drivers/cc1101_driver.h"
#include "drivers/pcap_logger.h"

// Hardware AP Configuration
const char *ap_ssid = "Bullet-Setup";
const char *ap_pass = ""; // Open network

WebServer server(80);

unsigned long lastTelemetryMs = 0;
unsigned long lastUiTickMs = 0;
unsigned long lastHopMs = 0;
uint8_t currentHopChannel = 1;
bool isScanningWifi = false;

#pragma pack(push, 1)
typedef struct {
    int16_t fctl;
    int16_t duration;
    uint8_t da[6];
    uint8_t sa[6];
    uint8_t bssid[6];
    int16_t seqctl;
    unsigned char payload[0];
} wifi_mgmt_hdr_t;
#pragma pack(pop)

void IRAM_ATTR wifi_promiscuous_rx_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    wifi_mgmt_hdr_t* mgmt = (wifi_mgmt_hdr_t*)pkt->payload;

    int channel = pkt->rx_ctrl.channel;
    int8_t rssi = pkt->rx_ctrl.rssi;

    wifi_ui_feed_sniffer_packet(channel, rssi);

    // Stream directly into live PCAP file on SD card / LittleFS
    if (pcap_logger_is_recording()) {
        pcap_logger_log_packet((const uint8_t*)pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.sig_len);
    }

    uint16_t fc = mgmt->fctl;
    // Deauth (0x00C0) or Disassociation (0x00A0)
    if ((fc & 0x00F0) == 0x00C0 || (fc & 0x00F0) == 0x00A0) {
        char target_mac[18], bssid_str[18];
        snprintf(target_mac, sizeof(target_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mgmt->da[0], mgmt->da[1], mgmt->da[2], mgmt->da[3], mgmt->da[4], mgmt->da[5]);
        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mgmt->bssid[0], mgmt->bssid[1], mgmt->bssid[2], mgmt->bssid[3], mgmt->bssid[4], mgmt->bssid[5]);
        wifi_ui_add_deauth_alert(target_mac, bssid_str, channel, rssi);
    }
    // Probe Request (0x0040)
    else if ((fc & 0x00F0) == 0x0040) {
        char client_mac[18];
        snprintf(client_mac, sizeof(client_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mgmt->sa[0], mgmt->sa[1], mgmt->sa[2], mgmt->sa[3], mgmt->sa[4], mgmt->sa[5]);
        uint8_t* tags = (uint8_t*)mgmt->payload;
        int len = pkt->rx_ctrl.sig_len - sizeof(wifi_mgmt_hdr_t);
        char req_ssid[33] = "Wildcard (Broadcast)";
        if (len > 2 && tags[0] == 0 && tags[1] > 0 && tags[1] <= 32) {
            memcpy(req_ssid, &tags[2], tags[1]);
            req_ssid[tags[1]] = '\0';
        }
        wifi_ui_add_probe_request(client_mac, req_ssid, rssi);
    }
}

// Real I2C Bus Scanner
void performRealHardwareBusScan() {
    Wire.begin(/*SDA=*/8, /*SCL=*/9);
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (addr == 0x3C || addr == 0x3D) wifi_ui_set_hw_device_detected(8, true);  // SSD1306 OLED
            if (addr == 0x76 || addr == 0x77) wifi_ui_set_hw_device_detected(9, true);  // BMP280
            if (addr == 0x68 || addr == 0x69) wifi_ui_set_hw_device_detected(10, true); // MPU6050
            if (addr == 0x48)                 wifi_ui_set_hw_device_detected(11, true); // ADS1115
            if (addr == 0x24)                 wifi_ui_set_hw_device_detected(7, true);  // PN532
        }
    }
}

// Scan surrounding real Wi-Fi networks and feed directly into UI
void performRealWifiScan() {
    isScanningWifi = true;
    wifi_ui_set_scan_status(true);
    wifi_ui_clear_networks();

    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    if (n > 0) {
        for (int i = 0; i < n && i < MAX_NETWORKS_CAPACITY; ++i) {
            bool is_sec = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            wifi_ui_add_network(WiFi.SSID(i).c_str(), WiFi.RSSI(i), is_sec);
        }
    }
    WiFi.scanDelete();

    wifi_ui_set_scan_status(false);
    isScanningWifi = false;
}

void updateHardwareTelemetry() {
    const char* chip_model = ESP.getChipModel();
    uint8_t chip_cores = ESP.getChipCores();
    uint16_t cpu_freq = ESP.getCpuFreqMHz();

    uint32_t total_psram = 0;
    uint32_t free_psram = 0;
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        total_psram = ESP.getPsramSize();
        free_psram = ESP.getFreePsram();
    }
#endif

    uint32_t total_sram = ESP.getHeapSize();
    uint32_t free_sram = ESP.getFreeHeap();
    uint32_t total_flash = ESP.getFlashChipSize();
    uint32_t free_flash = total_flash - (LittleFS.totalBytes() > 0 ? LittleFS.usedBytes() : 0);

    float real_temp = 0.0f;
#if defined(SOC_TEMP_SENSOR_SUPPORTED) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S2)
    real_temp = temperatureRead();
#endif
    uint32_t uptime = millis() / 1000;

    wifi_ui_set_sys_telemetry_ex(
        chip_model,
        chip_cores,
        cpu_freq,
        total_psram,
        free_psram,
        total_sram,
        free_sram,
        total_flash,
        free_flash,
        real_temp,
        uptime
    );

    if (WiFi.status() == WL_CONNECTED) {
        wifi_ui_set_connection_info(
            WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.macAddress().c_str(),
            WiFi.RSSI()
        );
    } else {
        wifi_ui_set_connection_info(
            "Disconnected",
            "0.0.0.0",
            "0.0.0.0",
            WiFi.macAddress().c_str(),
            0
        );
    }
}

// REST Endpoints for Web Companion
void handleRoot() {
    server.send(200, "text/html", "<html><body><h1>Bullet OS Active</h1></body></html>");
}

void handleTelemetry() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"chip\":\"%s\",\"cores\":%d,\"freq\":%d,\"temp\":%.1f,\"free_sram\":%lu,\"free_psram\":%lu,\"uptime\":%lu,\"ip\":\"%s\",\"mac\":\"%s\"}",
        ESP.getChipModel(),
        ESP.getChipCores(),
        ESP.getCpuFreqMHz(),
        temperatureRead(),
        (unsigned long)ESP.getFreeHeap(),
        (unsigned long)ESP.getFreePsram(),
        (unsigned long)(millis() / 1000),
        WiFi.localIP().toString().c_str(),
        WiFi.macAddress().c_str()
    );
    server.send(200, "application/json", json);
}

void handleKnob() {
    if (server.hasArg("dir")) {
        int dir = server.arg("dir").toInt();
        hw_knob_rotate(dir);
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing dir");
    }
}

void handleButton() {
    if (server.hasArg("action")) {
        int action = server.arg("action").toInt();
        hw_button_press(action);
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing action");
    }
}

void handlePcapDownload() {
    pcap_status_t st;
    pcap_logger_get_status(&st);
    File f;
    if (st.sd_mounted) {
        f = SD.open(st.current_filename, FILE_READ);
    } else {
        f = LittleFS.open(st.current_filename, "r");
    }

    if (!f) {
        server.send(404, "text/plain", "No PCAP capture found. Run 'pcap start' first.");
        return;
    }

    server.sendHeader("Content-Disposition", "attachment; filename=\"capture.pcap\"");
    server.streamFile(f, "application/vnd.tcpdump.pcap");
    f.close();
}

void handlePcapStart() {
    pcap_logger_start("/capture.pcap");
    server.send(200, "application/json", "{\"status\":\"RECORDING\",\"file\":\"/capture.pcap\"}");
}

void handlePcapStop() {
    pcap_logger_stop();
    pcap_status_t st;
    pcap_logger_get_status(&st);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"STOPPED\",\"packets\":%lu,\"bytes\":%lu}", (unsigned long)st.total_packets, (unsigned long)st.total_bytes);
    server.send(200, "application/json", buf);
}

void handlePcapStatus() {
    pcap_status_t st;
    pcap_logger_get_status(&st);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"recording\":%s,\"packets\":%lu,\"bytes\":%lu,\"sd_mounted\":%s}",
             st.is_recording ? "true" : "false", (unsigned long)st.total_packets, (unsigned long)st.total_bytes, st.sd_mounted ? "true" : "false");
    server.send(200, "application/json", buf);
}

void handleReboot() {
    server.send(200, "text/plain", "Rebooting...");
    delay(100);
    esp_restart();
}

void setup() {
    Serial.begin(115200);
    delay(400);

#ifdef QEMU_EMULATION
    disableCore0WDT();
    disableCore1WDT();
    disableLoopWDT();
#endif

    Serial.println("\n==========================================");
    Serial.printf("   Bullet OS v0.2.1 - %s (%d Cores @ %dMHz)\n", ESP.getChipModel(), ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("   Flash: %d MB | PSRAM: %d MB\n", (int)(ESP.getFlashChipSize() / (1024 * 1024)), (int)(ESP.getPsramSize() / (1024 * 1024)));
    Serial.println("==========================================");

    // Initialize PSRAM if board supports it
#ifdef BOARD_HAS_PSRAM
    if (psramInit()) {
        Serial.printf("[PSRAM] Detected PSRAM: %d MB Total\n", (int)(ESP.getPsramSize() / (1024 * 1024)));
    }
#endif

#ifndef QEMU_EMULATION
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS Mount Failed");
    } else {
        Serial.println("[FS] LittleFS Mounted");
    }
#else
    Serial.println("[FS] LittleFS Initialized (QEMU VFS)");
#endif

    // Initialize C UI Engine
    oled_init();

#ifndef QEMU_EMULATION
    Serial.println("[SPI] Probing CC1101 Sub-GHz Transceiver (CS=10, SCK=12, MOSI=11, MISO=13)...");
    bool cc_found = cc1101_hw_init(/*cs=*/10, /*sck=*/12, /*mosi=*/11, /*miso=*/13, /*gdo0=*/14, /*gdo2=*/21);
    if (cc_found) {
        Serial.println("[CC1101] Hardware detected & initialized (433.92MHz OOK/ASK Mode)");
        wifi_ui_set_cc1101_detected(true);
    } else {
        Serial.println("[CC1101] Not detected on SPI bus (Pin CS=10). Module optional.");
        wifi_ui_set_cc1101_detected(false);
    }
#else
    Serial.println("[CC1101] Initialized (QEMU Virtual Transceiver)");
    wifi_ui_set_cc1101_detected(true);
#endif

#ifndef QEMU_EMULATION
    // Start SoftAP & Station mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_pass);
    Serial.printf("[Wi-Fi] SoftAP '%s' started. IP: %s\n", ap_ssid, WiFi.softAPIP().toString().c_str());

    if (MDNS.begin("bullet")) {
        Serial.println("[mDNS] Responder at http://bullet.local");
    }

    // Start Real 802.11 Promiscuous RX & IDS
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_callback);
    Serial.println("[IDS] 802.11 Promiscuous Sniffer & Attack Guard started");

    // Perform hardware bus scan
    performRealHardwareBusScan();

    pcap_logger_init();

    // Web endpoints
    server.on("/", handleRoot);
    server.on("/capture.pcap", handlePcapDownload);
    server.on("/api/pcap/download", handlePcapDownload);
    server.on("/api/pcap/start", handlePcapStart);
    server.on("/api/pcap/stop", handlePcapStop);
    server.on("/api/pcap/status", handlePcapStatus);
    server.on("/api/telemetry", handleTelemetry);
    server.on("/api/knob", handleKnob);
    server.on("/api/btn", handleButton);
    server.on("/api/reboot", handleReboot);
    server.begin();
    Serial.println("[HTTP] Web Server & PCAP Exporter started");
#else
    Serial.println("[QEMU] Running in QEMU Emulator!");
    Serial.println("[Control] 'a'/'s'=Knob Left, 'd'/'w'=Knob Right, ' '=Click, 'q'=Back, 'p'=Screen Dump");
#endif

    updateHardwareTelemetry();
}

static void dumpScreenToSerial() {
    uint8_t* fb = oled_get_fb();
    int w = oled_get_width();
    int h = oled_get_height();
    if (!fb) return;

    Serial.printf("\n--- DISPLAY FRAMEBUFFER (%dx%d) ---\n", w, h);
    int step_x = (w > 128) ? 4 : 2;
    int step_y = (h > 64) ? 8 : 4;

    for (int y = 0; y < h; y += step_y) {
        for (int x = 0; x < w; x += step_x) {
            uint32_t* p = (uint32_t*)fb;
            uint32_t pixel = p[y * w + x];
            uint8_t r = pixel & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            if (r > 30 || g > 30 || b > 30) {
                Serial.print("#");
            } else {
                Serial.print(" ");
            }
        }
        Serial.println();
    }
    Serial.println("------------------------------------");
}

void loop() {
#ifndef QEMU_EMULATION
    server.handleClient();

    // Channel hopping every 150ms for 2.4GHz RF spectrum and promiscuous sniffing
    unsigned long now_ms = millis();
    if (now_ms - lastHopMs >= 150) {
        lastHopMs = now_ms;
        currentHopChannel = (currentHopChannel % 13) + 1;
        esp_wifi_set_channel(currentHopChannel, WIFI_SECOND_CHAN_NONE);
    }
#endif

    // Interactive Serial Keyboard Pipeline
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'a' || c == 's' || c == 'A' || c == 'S') {
            hw_knob_rotate(0);
            Serial.println("[Knob] Left ◄");
        } else if (c == 'd' || c == 'w' || c == 'D' || c == 'W') {
            hw_knob_rotate(1);
            Serial.println("[Knob] Right ►");
        } else if (c == ' ' || c == '\r' || c == '\n' || c == 'e' || c == 'E') {
            hw_button_press(0);
            Serial.println("[Button] Click (ENTER)");
        } else if (c == 'q' || c == 'Q' || c == 27 || c == 8) {
            hw_button_press(2);
            Serial.println("[Button] Hold (BACK)");
        } else if (c == 'p' || c == 'P') {
            dumpScreenToSerial();
        } else if (c >= 32 && c <= 126) {
            oled_char_input(c);
        }
    }

    // Tick UI rendering at 30-60 FPS
    unsigned long now = millis();
    if (now - lastUiTickMs >= 33) {
        lastUiTickMs = now;
        oled_render();
    }

    // Telemetry updates
    if (now - lastTelemetryMs > 1000) {
        lastTelemetryMs = now;
        updateHardwareTelemetry();
    }
}
