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
// REST Endpoints & Web Companion Portal for Phone/PC (ESP-SCRCPY + File Explorer)
void handleRoot() {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP-SCRCPY &bullet; Bullet OS Control</title>
  <style>
    :root { --bg: #070a0f; --card: #0f1722; --border: #1e293b; --cyan: #38ef7d; --blue: #38bdf8; --amber: #f59e0b; --rose: #f43f5e; --text: #f1f5f9; --muted: #64748b; }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; -webkit-tap-highlight-color: transparent; }
    body { background: var(--bg); color: var(--text); padding: 10px; max-width: 600px; margin: 0 auto; user-select: none; }
    .header { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid var(--border); }
    .logo { font-size: 18px; font-weight: 900; letter-spacing: 1px; color: var(--cyan); text-shadow: 0 0 12px rgba(56,239,125,0.4); }
    .status-badge { background: #064e3b; color: #6ee7b7; font-size: 10px; font-weight: bold; padding: 3px 8px; border-radius: 12px; border: 1px solid #059669; }
    .nav-tabs { display: flex; gap: 6px; margin: 12px 0 8px 0; }
    .tab-btn { flex: 1; background: var(--card); border: 1px solid var(--border); color: var(--muted); padding: 8px 0; font-size: 12px; font-weight: bold; border-radius: 6px; cursor: pointer; text-align: center; }
    .tab-btn.active { background: #1e293b; color: var(--cyan); border-color: var(--cyan); }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    
    /* Screen Mirror */
    .screen-wrap { background: #000; border: 2px solid var(--border); border-radius: 12px; overflow: hidden; position: relative; width: 100%; aspect-ratio: 1/1; max-height: 380px; margin-bottom: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.6); }
    .screen-img { width: 100%; height: 100%; object-fit: contain; image-rendering: pixelated; display: block; }
    
    /* Controls */
    .dpad-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 12px; }
    .btn-ctrl { background: var(--card); border: 1px solid var(--border); color: var(--text); padding: 14px 6px; border-radius: 8px; font-size: 13px; font-weight: bold; cursor: pointer; text-align: center; box-shadow: 0 2px 4px rgba(0,0,0,0.3); }
    .btn-ctrl:active { background: var(--cyan); color: #000; }
    .btn-click { border-color: var(--cyan); color: var(--cyan); }
    .btn-back { border-color: var(--rose); color: var(--rose); }
    .btn-amber { border-color: var(--amber); color: var(--amber); }
    
    /* Explorer */
    .file-card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 12px; margin-bottom: 10px; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #1e293b; font-size: 13px; }
    .file-item:last-child { border-bottom: none; }
    .file-name { font-weight: bold; color: var(--blue); }
    .file-meta { font-size: 11px; color: var(--muted); }
    .file-actions { display: flex; gap: 6px; }
    .btn-sm { background: #1e293b; border: 1px solid var(--border); color: var(--text); padding: 4px 8px; border-radius: 4px; font-size: 11px; cursor: pointer; text-decoration: none; }
    .btn-sm:active { background: var(--blue); color: #000; }
    .btn-del { border-color: var(--rose); color: var(--rose); }
    .dropzone { border: 2px dashed var(--border); border-radius: 8px; padding: 18px; text-align: center; color: var(--muted); font-size: 12px; margin-bottom: 10px; cursor: pointer; }
    .dropzone:active { border-color: var(--cyan); color: var(--cyan); }

    /* Terminal */
    .term-box { background: #05080c; border: 1px solid var(--border); border-radius: 8px; padding: 10px; height: 260px; overflow-y: auto; font-family: monospace; font-size: 12px; color: #94a3b8; white-space: pre-wrap; line-height: 1.4; margin-bottom: 8px; }
    .term-input-box { display: flex; gap: 6px; }
    .term-in { flex: 1; background: var(--card); border: 1px solid var(--border); border-radius: 6px; color: #fff; padding: 10px; font-family: monospace; outline: none; font-size: 13px; }
    .term-in:focus { border-color: var(--cyan); }
  </style>
</head>
<body>
  <div class="header">
    <div class="logo">⚡ ESP-SCRCPY</div>
    <div class="status-badge" id="status-pill">ESP32 ONLINE ●</div>
  </div>

  <div class="nav-tabs">
    <div class="tab-btn active" onclick="switchTab('screen')">📱 SCRCPY</div>
    <div class="tab-btn" onclick="switchTab('files')">📁 Проводник</div>
    <div class="tab-btn" onclick="switchTab('term')">💻 Терминал</div>
  </div>

  <!-- TAB 1: SCRCPY LIVE MIRROR & TOUCH PAD -->
  <div id="tab-screen" class="tab-content active">
    <div class="screen-wrap" onclick="handleScreenTap(event)">
      <img id="scr" class="screen-img" src="/api/screen" alt="ESP32 Display">
    </div>

    <div class="dpad-grid">
      <button class="btn-ctrl" onclick="sendKnob(0)">◄ LEFT</button>
      <button class="btn-ctrl btn-click" onclick="sendBtn(0)">● CLICK</button>
      <button class="btn-ctrl" onclick="sendKnob(1)">RIGHT ►</button>
      <button class="btn-ctrl btn-amber" onclick="sendBtn(1)">2x DBL</button>
      <button class="btn-ctrl" onclick="runCmd('matrix')">MATRIX</button>
      <button class="btn-ctrl btn-back" onclick="sendBtn(2)">↩ BACK</button>
    </div>
  </div>

  <!-- TAB 2: FILE EXPLORER & STORAGE -->
  <div id="tab-files" class="tab-content">
    <div class="dropzone" onclick="document.getElementById('file-upload').click()">
      📤 Нажмите для загрузки файла на ESP32 (LittleFS / SD)
      <input type="file" id="file-upload" style="display:none" onchange="uploadFile(this.files[0])">
    </div>

    <div class="file-card">
      <div style="font-size:12px; font-weight:bold; color:var(--muted); margin-bottom:8px;">ХРАНИЛИЩЕ LittleFS / SD КАРТА</div>
      <div id="file-list">Загрузка файлов...</div>
    </div>
  </div>

  <!-- TAB 3: LIVE TERMINAL -->
  <div id="tab-term" class="tab-content">
    <div class="term-box" id="t-log">ESP32 Terminal Ready. Type commands below...</div>
    <div class="term-input-box">
      <input type="text" id="cmd-in" class="term-in" placeholder="Command (e.g. 'wifi scan', 'pcap start')..." onkeydown="if(event.key==='Enter') execCmd();">
      <button class="btn-ctrl btn-click" style="padding:8px 14px;" onclick="execCmd();">SEND</button>
    </div>
  </div>

  <script>
    function switchTab(t) {
      document.querySelectorAll('.tab-btn').forEach((b, i) => b.classList.toggle('active', ['screen','files','term'][i] === t));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      document.getElementById('tab-' + t).classList.add('active');
      if (t === 'files') loadFiles();
    }

    // High speed screen streamer (30 FPS)
    const imgEl = document.getElementById('scr');
    let isStreaming = true;
    function pollFrame() {
      if (!isStreaming) return;
      const nextImg = new Image();
      nextImg.src = '/api/screen?t=' + Date.now();
      nextImg.onload = () => {
        imgEl.src = nextImg.src;
        setTimeout(pollFrame, 33);
      };
      nextImg.onerror = () => setTimeout(pollFrame, 150);
    }
    pollFrame();

    function handleScreenTap(e) {
      const rect = e.currentTarget.getBoundingClientRect();
      const x = (e.clientX - rect.left) / rect.width;
      const y = (e.clientY - rect.top) / rect.height;
      if (y > 0.8) sendBtn(2); // Bottom strip = Back
      else if (x < 0.35) sendKnob(0); // Left = CCW
      else if (x > 0.65) sendKnob(1); // Right = CW
      else sendBtn(0); // Center = Click
    }

    function sendKnob(d) { fetch('/api/knob?dir=' + d).catch(e=>{}); }
    function sendBtn(a) { fetch('/api/btn?action=' + a).catch(e=>{}); }

    function log(msg) {
      const el = document.getElementById('t-log');
      el.textContent += '\n' + msg;
      el.scrollTop = el.scrollHeight;
    }

    function runCmd(c) {
      log('esp32:~$ ' + c);
      fetch('/api/cmd?c=' + encodeURIComponent(c))
        .then(r => r.json())
        .then(d => log('[OK] Output received: ' + c))
        .catch(e => log('[Error] Failed to execute'));
    }

    function execCmd() {
      const inp = document.getElementById('cmd-in');
      const val = inp.value.trim();
      if (!val) return;
      runCmd(val);
      inp.value = '';
    }

    function loadFiles() {
      const listEl = document.getElementById('file-list');
      fetch('/api/files/list')
        .then(r => r.json())
        .then(files => {
          if (!files || files.length === 0) {
            listEl.innerHTML = '<div style="color:var(--muted); padding:10px;">Хранилище пусто. Загрузите файл или запустите PCAP.</div>';
            return;
          }
          listEl.innerHTML = files.map(f => `
            <div class="file-item">
              <div>
                <div class="file-name">${f.name}</div>
                <div class="file-meta">${(f.size/1024).toFixed(1)} KB &bullet; ${f.type.toUpperCase()}</div>
              </div>
              <div class="file-actions">
                <a class="btn-sm" href="/api/files/download?name=${encodeURIComponent(f.name)}" download>⬇ Скачать</a>
                <button class="btn-sm btn-del" onclick="deleteFile('${f.name}')">🗑</button>
              </div>
            </div>
          `).join('');
        })
        .catch(e => { listEl.innerHTML = '<div style="color:var(--rose);">Ошибка загрузки файлов</div>'; });
    }

    function uploadFile(file) {
      if (!file) return;
      fetch('/api/files/upload?name=' + encodeURIComponent(file.name), {
        method: 'POST',
        body: file
      })
      .then(r => r.json())
      .then(d => {
        alert('Файл ' + file.name + ' успешно загружен на ESP32!');
        loadFiles();
      })
      .catch(e => alert('Ошибка загрузки: ' + e));
    }

    function deleteFile(name) {
      if (!confirm('Удалить файл ' + name + ' с ESP32?')) return;
      fetch('/api/files/delete?name=' + encodeURIComponent(name))
        .then(r => r.json())
        .then(() => loadFiles())
        .catch(e => alert('Ошибка удаления'));
    }
  </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}

void handleScreen() {
    uint8_t* fb = oled_get_fb();
    int w = oled_get_width();
    int h = oled_get_height();
    if (!fb || w <= 0 || h <= 0) {
        server.send(503, "text/plain", "No Framebuffer");
        return;
    }

    uint32_t image_size = w * h * 4;
    uint32_t file_size = 54 + image_size;

    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    header[2] = (uint8_t)(file_size);
    header[3] = (uint8_t)(file_size >> 8);
    header[4] = (uint8_t)(file_size >> 16);
    header[5] = (uint8_t)(file_size >> 24);
    header[10] = 54;

    header[14] = 40;
    header[18] = (uint8_t)(w);
    header[19] = (uint8_t)(w >> 8);
    header[22] = (uint8_t)(-h);
    header[23] = (uint8_t)((-h) >> 8);
    header[24] = (uint8_t)((-h) >> 16);
    header[25] = (uint8_t)((-h) >> 24);
    header[26] = 1;
    header[28] = 32;
    header[34] = (uint8_t)(image_size);
    header[35] = (uint8_t)(image_size >> 8);
    header[36] = (uint8_t)(image_size >> 16);
    header[37] = (uint8_t)(image_size >> 24);

    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/bmp");
    client.println("Cache-Control: no-store, no-cache, must-revalidate, max-age=0");
    client.print("Content-Length: ");
    client.println(file_size);
    client.println("Connection: close");
    client.println();

    client.write(header, 54);
    client.write(fb, image_size);
}

void handleFilesList() {
    String path = server.hasArg("path") ? server.arg("path") : "/";
    if (!path.startsWith("/")) path = "/" + path;

    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) {
        server.send(404, "application/json", "{\"error\":\"Directory not found\"}");
        return;
    }

    String json = "{\"currentPath\":\"" + path + "\",\"items\":[";
    bool first = true;

    File f = root.openNextFile();
    while (f) {
        if (!first) json += ",";
        first = false;
        String name = String(f.name());
        // LittleFS on ESP32 sometimes returns full path or relative
        if (name.lastIndexOf('/') >= 0) name = name.substring(name.lastIndexOf('/') + 1);
        bool isDir = f.isDirectory();
        String itemPath = path == "/" ? ("/" + name) : (path + "/" + name);
        String ext = isDir ? "dir" : name.substring(name.lastIndexOf('.') + 1);

        json += "{\"name\":\"" + name + "\",\"path\":\"" + itemPath + "\",\"isDir\":" + (isDir ? "true" : "false") + ",\"size\":" + String(f.size()) + ",\"type\":\"" + ext + "\"}";
        f = root.openNextFile();
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleFilesRead() {
    if (!server.hasArg("path")) {
        server.send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }
    String path = server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;

    if (!LittleFS.exists(path)) {
        server.send(404, "application/json", "{\"error\":\"File not found\"}");
        return;
    }

    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
        server.send(400, "application/json", "{\"error\":\"Cannot read directory as file\"}");
        return;
    }

    size_t sz = f.size();
    String name = path.substring(path.lastIndexOf('/') + 1);
    String ext = name.substring(name.lastIndexOf('.') + 1);
    ext.toLowerCase();

    bool isText = (ext == "txt" || ext == "cfg" || ext == "json" || ext == "log" || ext == "sub" || ext == "raw" || ext == "ini" || ext == "csv" || ext == "sh");

    if (isText) {
        String content = "";
        while (f.available() && content.length() < 16384) {
            content += (char)f.read();
        }
        f.close();
        // Escape JSON quotes
        content.replace("\\", "\\\\");
        content.replace("\"", "\\\"");
        content.replace("\n", "\\n");
        content.replace("\r", "");
        content.replace("\t", "  ");

        String json = "{\"name\":\"" + name + "\",\"path\":\"" + path + "\",\"isBinary\":false,\"size\":" + String(sz) + ",\"content\":\"" + content + "\"}";
        server.send(200, "application/json", json);
    } else {
        // Hex preview
        String hexContent = "";
        uint8_t buf[16];
        size_t offset = 0;
        size_t previewLen = sz < 256 ? sz : 256;
        while (f.available() && offset < previewLen) {
            int n = f.read(buf, 16);
            if (n <= 0) break;
            char line[128];
            char hexBuf[48] = {0};
            char ascBuf[17] = {0};
            for (int i = 0; i < n; i++) {
                sprintf(hexBuf + i * 3, "%02X ", buf[i]);
                ascBuf[i] = (buf[i] >= 32 && buf[i] <= 126) ? (char)buf[i] : '.';
            }
            snprintf(line, sizeof(line), "%04X:  %-48s |%s|\\n", (unsigned int)offset, hexBuf, ascBuf);
            hexContent += String(line);
            offset += n;
        }
        f.close();
        if (sz > 256) hexContent += "... [" + String(sz - 256) + " more bytes in file]\\n";

        String json = "{\"name\":\"" + name + "\",\"path\":\"" + path + "\",\"isBinary\":true,\"size\":" + String(sz) + ",\"content\":\"" + hexContent + "\"}";
        server.send(200, "application/json", json);
    }
}

void handleFilesMkdir() {
    String path = server.hasArg("path") ? server.arg("path") : "/";
    String name = server.hasArg("name") ? server.arg("name") : "";
    if (name.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"Missing folder name\"}");
        return;
    }
    String target = path == "/" ? ("/" + name) : (path + "/" + name);
    LittleFS.mkdir(target);
    server.send(200, "application/json", "{\"status\":\"OK\",\"path\":\"" + target + "\"}");
}

void handleCmd() {
    if (server.hasArg("c")) {
        String c = server.arg("c");
        // Forward command string to C terminal engine
        for (size_t i = 0; i < c.length(); i++) {
            oled_char_input(c[i]);
        }
        oled_enter();
        server.send(200, "application/json", "{\"status\":\"OK\",\"cmd\":\"" + c + "\"}");
    } else {
        server.send(400, "text/plain", "Missing c");
    }
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
    // Initialize persistent random AP password
    wifi_ui_init_ap_password();
    const char* ap_pass = wifi_ui_get_ap_password();

    // Start Secure SoftAP & Station mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_pass);
    Serial.printf("[Wi-Fi] Secure Hotspot '%s' started! Password: '%s' | IP: %s\n", ap_ssid, ap_pass, WiFi.softAPIP().toString().c_str());

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

    // Web endpoints (ESP-SCRCPY & File Explorer)
    server.on("/", handleRoot);
    server.on("/api/screen", handleScreen);
    server.on("/api/files/list", handleFilesList);
    server.on("/api/files/read", handleFilesRead);
    server.on("/api/files/mkdir", handleFilesMkdir);
    server.on("/capture.pcap", handlePcapDownload);
    server.on("/api/pcap/download", handlePcapDownload);
    server.on("/api/pcap/start", handlePcapStart);
    server.on("/api/pcap/stop", handlePcapStop);
    server.on("/api/pcap/status", handlePcapStatus);
    server.on("/api/cmd", handleCmd);
    server.on("/api/telemetry", handleTelemetry);
    server.on("/api/knob", handleKnob);
    server.on("/api/btn", handleButton);
    server.on("/api/reboot", handleReboot);
    server.begin();
    Serial.println("[HTTP] ESP-SCRCPY Web Server & File Explorer started");
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

    // FreeRTOS CPU yield for power optimization & IDLE watchdog
    delay(2);
}
