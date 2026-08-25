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
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "wifi_oled_ui.h"

// Hardware AP Configuration
const char *ap_ssid = "Bullet-Setup";
const char *ap_pass = ""; // Open network

WebServer server(80);

unsigned long lastTelemetryMs = 0;
unsigned long lastUiTickMs = 0;
bool isScanningWifi = false;

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
    // Start SoftAP & Station mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_pass);
    Serial.printf("[Wi-Fi] SoftAP '%s' started. IP: %s\n", ap_ssid, WiFi.softAPIP().toString().c_str());

    if (MDNS.begin("bullet")) {
        Serial.println("[mDNS] Responder at http://bullet.local");
    }

    // Web endpoints
    server.on("/", handleRoot);
    server.on("/api/telemetry", handleTelemetry);
    server.on("/api/knob", handleKnob);
    server.on("/api/btn", handleButton);
    server.on("/api/reboot", handleReboot);
    server.begin();
    Serial.println("[HTTP] Web Server started");
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
