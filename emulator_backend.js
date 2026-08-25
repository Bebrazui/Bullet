const net = require('net');
const { exec, execSync } = require('child_process');
const Module = require('./web_sim/wasm/wifi_oled.js');

const ADB_BIN = 'C:\\Android\\sdk\\platform-tools\\adb.exe';

let isReady = false;
let currentMode = 1; // 240x240

globalThis.onAdbCommandTriggered = function(cmd) {
    if (!cmd) return;
    console.log(`[Real ADB] Executing: adb shell ${cmd}`);
    exec(`"${ADB_BIN}" shell ${cmd}`, (err, stdout) => {
        if (err) {
            console.log(`[Real ADB Error] ${err.message}`);
        } else {
            console.log(`[Real ADB Output] ${stdout || 'Command sent successfully'}`);
        }
        setTimeout(checkRealAdbDevice, 500);
    });
};

Module.onRuntimeInitialized = function() {
    Module._oled_init();
    Module._oled_set_disp_mode(currentMode);
    isReady = true;
    checkRealAdbDevice();
};

function checkRealAdbDevice() {
    if (!isReady) return;
    exec(`"${ADB_BIN}" devices`, (err, stdout) => {
        if (err || !stdout) {
            Module._wifi_ui_adb_set_device_info("No ADB Host", "OFFLINE", 0, false);
            return;
        }

        const lines = stdout.trim().split('\n');
        const devLines = lines.slice(1).filter(l => l.includes('\tdevice'));

        if (devLines.length === 0) {
            Module._wifi_ui_adb_set_device_info("No Device Attached", "OFFLINE", 0, false);
        } else {
            const devId = devLines[0].split('\t')[0].trim();
            // Fetch real model
            exec(`"${ADB_BIN}" -s ${devId} shell getprop ro.product.model`, (e1, out1) => {
                const model = (out1 || devId).trim();
                // Fetch real version
                exec(`"${ADB_BIN}" -s ${devId} shell getprop ro.build.version.release`, (e2, out2) => {
                    const ver = `Android ${(out2 || '?').trim()}`;
                    // Fetch real battery
                    exec(`"${ADB_BIN}" -s ${devId} shell dumpsys battery`, (e3, out3) => {
                        let bat = 75;
                        if (out3) {
                            const match = out3.match(/level:\s*(\d+)/);
                            if (match) bat = parseInt(match[1], 10);
                        }
                        console.log(`[Real ADB] Attached: ${model} (${ver}, Bat: ${bat}%)`);
                        Module._wifi_ui_adb_set_device_info(model, ver, bat, true);
                    });
                });
            });
        }
    });
}

// Periodically probe for real devices every 3 seconds
setInterval(checkRealAdbDevice, 3000);

const server = net.createServer((socket) => {
    socket.on('error', (err) => {});

    socket.on('data', (data) => {
        if (!isReady) return;
        const msg = data.toString().trim();
        const lines = msg.split('\n');

        for (const line of lines) {
            const parts = line.trim().split(' ');
            const cmd = parts[0];

            if (cmd === 'KNOB') {
                const dir = parseInt(parts[1], 10);
                Module._hw_knob_rotate(dir);
            } else if (cmd === 'BTN') {
                const action = parseInt(parts[1], 10);
                Module._hw_button_press(action);
            } else if (cmd === 'CHAR') {
                const code = parseInt(parts[1], 10);
                Module._oled_char_input(code);
            } else if (cmd === 'MODE') {
                const m = parseInt(parts[1], 10);
                currentMode = m;
                Module._oled_set_disp_mode(m);
            } else if (cmd === 'RENDER') {
                Module._oled_render();
                const w = Module._oled_get_width();
                const h = Module._oled_get_height();
                const ptr = Module._oled_get_fb();
                const byteLen = w * h * 4;
                const fbBuf = Buffer.from(Module.HEAPU8.buffer, ptr, byteLen);

                const hdr = Buffer.alloc(8);
                hdr.writeUInt32LE(w, 0);
                hdr.writeUInt32LE(h, 4);

                try {
                    socket.write(Buffer.concat([hdr, fbBuf]));
                } catch (e) {}
            }
        }
    });
});

const http = require('http');
const os = require('os');
const fs = require('fs');
const path = require('path');

function getLocalIp() {
    const interfaces = os.networkInterfaces();
    for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return '127.0.0.1';
}

const localIp = getLocalIp();
const STORAGE_DIR = path.join(__dirname, 'storage');
if (!fs.existsSync(STORAGE_DIR)) fs.mkdirSync(STORAGE_DIR, { recursive: true });

function getBmpBuffer() {
    if (!isReady) return null;
    Module._oled_render();
    const w = Module._oled_get_width();
    const h = Module._oled_get_height();
    const ptr = Module._oled_get_fb();
    const byteLen = w * h * 4;
    const fbBuf = Buffer.from(Module.HEAPU8.buffer, ptr, byteLen);

    const hdr = Buffer.alloc(54);
    hdr.write('BM', 0);
    hdr.writeUInt32LE(54 + byteLen, 2);
    hdr.writeUInt32LE(54, 10);
    hdr.writeUInt32LE(40, 14);
    hdr.writeInt32LE(w, 18);
    hdr.writeInt32LE(-h, 22); // Top-down
    hdr.writeUInt16LE(1, 26);
    hdr.writeUInt16LE(32, 28);
    hdr.writeUInt32LE(0, 30);
    hdr.writeUInt32LE(byteLen, 34);

    return Buffer.concat([hdr, fbBuf]);
}

// Start HTTP Companion Server (ESP-SCRCPY + File Explorer + Terminal)
const httpServer = http.createServer((req, res) => {
    const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const pathname = parsedUrl.pathname;

    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS, DELETE');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    if (pathname === '/api/screen') {
        const bmp = getBmpBuffer();
        if (bmp) {
            res.writeHead(200, {
                'Content-Type': 'image/bmp',
                'Cache-Control': 'no-store, no-cache, must-revalidate, max-age=0'
            });
            res.end(bmp);
        } else {
            res.writeHead(503, { 'Content-Type': 'text/plain' });
            res.end('WASM not initialized');
        }
    } else if (pathname === '/api/knob') {
        const dir = parseInt(parsedUrl.searchParams.get('dir') || '0', 10);
        if (isReady) Module._hw_knob_rotate(dir);
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end('OK');
    } else if (pathname === '/api/btn') {
        const action = parseInt(parsedUrl.searchParams.get('action') || '0', 10);
        if (isReady) Module._hw_button_press(action);
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end('OK');
    } else if (pathname === '/api/cmd') {
        const cmd = parsedUrl.searchParams.get('c') || '';
        if (isReady && cmd) {
            for (let i = 0; i < cmd.length; i++) {
                Module._oled_char_input(cmd.charCodeAt(i));
            }
            Module._oled_enter();
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'OK', cmd }));
    } else if (pathname === '/api/files/list') {
        try {
            const files = fs.readdirSync(STORAGE_DIR).map(name => {
                const stat = fs.statSync(path.join(STORAGE_DIR, name));
                return {
                    name,
                    size: stat.size,
                    mtime: stat.mtimeMs,
                    type: path.extname(name).replace('.', '') || 'bin'
                };
            });
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(files));
        } catch (e) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: e.message }));
        }
    } else if (pathname === '/api/files/download') {
        const filename = path.basename(parsedUrl.searchParams.get('name') || '');
        const filepath = path.join(STORAGE_DIR, filename);
        if (fs.existsSync(filepath) && fs.statSync(filepath).isFile()) {
            res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
            res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
            fs.createReadStream(filepath).pipe(res);
        } else {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('File not found');
        }
    } else if (pathname === '/api/files/upload' && req.method === 'POST') {
        const filename = path.basename(parsedUrl.searchParams.get('name') || `upload_${Date.now()}.bin`);
        const filepath = path.join(STORAGE_DIR, filename);
        const fileStream = fs.createWriteStream(filepath);
        req.pipe(fileStream);
        fileStream.on('finish', () => {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'OK', name: filename }));
        });
        fileStream.on('error', (err) => {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: err.message }));
        });
    } else if (pathname === '/api/files/delete') {
        const filename = path.basename(parsedUrl.searchParams.get('name') || '');
        const filepath = path.join(STORAGE_DIR, filename);
        if (fs.existsSync(filepath)) {
            fs.unlinkSync(filepath);
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'DELETED', name: filename }));
        } else {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('File not found');
        }
    } else if (pathname === '/api/telemetry') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            chip: 'ESP32-S3 (Bullet OS)',
            cores: 2,
            freq: 240,
            temp: 41.5,
            free_sram: 240800,
            free_psram: 8388608,
            uptime: Math.floor(process.uptime()),
            ip: localIp,
            mac: 'DC:A6:32:88:99:FF'
        }));
    } else {
        // Serve Mobile SCRCPY & Web Companion Portal
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(`<!DOCTYPE html>
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
    .touch-overlay { position: absolute; inset: 0; display: grid; grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
    .t-zone { opacity: 0; transition: background 0.1s; }
    .t-zone:active { background: rgba(56,239,125,0.15); opacity: 1; }
    
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
      📤 Нажмите или перетащите файл для загрузки на ESP32
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
        alert('Файл ' + file.name + ' успешно загружен на устройство!');
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
</html>`);
    }
});

httpServer.listen(8080, '0.0.0.0', () => {
    console.log(`[ESP-SCRCPY Server] Live at http://${localIp}:8080 and http://localhost:8080`);
});

server.listen(50555, '127.0.0.1', () => {
    console.log('EMULATOR_BACKEND_READY');
});
