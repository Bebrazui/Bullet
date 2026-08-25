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
    } else {
        // Serve Mobile SCRCPY & Web Companion Portal
        const compPath = path.join(__dirname, 'web_sim', 'companion.html');
        if (fs.existsSync(compPath)) {
            res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            fs.createReadStream(compPath).pipe(res);
        } else {
            const indexPath = path.join(__dirname, 'web_sim', 'index.html');
            res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            fs.createReadStream(indexPath).pipe(res);
        }
    }
});

httpServer.listen(8080, '0.0.0.0', () => {
    console.log(`[ESP-SCRCPY Server] Live at http://${localIp}:8080 and http://localhost:8080`);
});

server.listen(50555, '127.0.0.1', () => {
    console.log('EMULATOR_BACKEND_READY');
});
