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
if (!fs.existsSync(STORAGE_DIR)) {
    fs.mkdirSync(STORAGE_DIR, { recursive: true });
}

// Seed realistic LittleFS default folder structure if empty
function seedLittleFSDirectories() {
    const defaultDirs = ['captures', 'subghz', 'config', 'logs'];
    for (const d of defaultDirs) {
        const dp = path.join(STORAGE_DIR, d);
        if (!fs.existsSync(dp)) fs.mkdirSync(dp, { recursive: true });
    }

    const sampleFiles = [
        { path: 'config/bullet.cfg', content: '# Bullet OS System Configuration\nversion=0.2.1\nlanguage=ru\ntheme=cyan\ndisp_mode=ips_240\nwifi_power=20\nids_guard_enabled=1\n' },
        { path: 'config/wifi_whitelist.json', content: '{\n  "trusted_ssids": ["Home_WiFi", "Office_5G"],\n  "ignored_bssid": ["00:11:22:33:44:55"]\n}\n' },
        { path: 'logs/ids_guard.log', content: '[2026-08-25 18:20:01] IDS initialized. 802.11 Promiscuous RX ready.\n[2026-08-25 18:20:15] Channel hopping active CH 1..13\n[2026-08-25 18:21:40] Deauth probe baseline clean. 0 bursts.\n' },
        { path: 'logs/boot.log', content: '[ESP-IDF v5.1.2] Bootloader started @ 240MHz\n[PSRAM] 8MB Octal SPI OK\n[LittleFS] 5.6MB Flash mounted /littlefs\n[CC1101] SPI Transceiver 433.92MHz ready\n' },
        { path: 'subghz/garage_door_433.92.raw', content: 'RAW_SIGNAL_DATA_433920000Hz_PULSES_180\n+350 -700 +350 -700 +700 -350 +700 -350\n+350 -700 +700 -350 +350 -700 +700 -350\n' },
        { path: 'subghz/car_fob_keeloq.sub', content: 'Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\nPreset: FuriHalSubGhzPreset2FSKDev238k\nProtocol: Keeloq 64bit\n' },
        { path: 'captures/deauth_burst.pcap', content: 'PCAP_SAMPLE_BURST_CAPTURE_80211_FRAMES' }
    ];

    for (const f of sampleFiles) {
        const fp = path.join(STORAGE_DIR, f.path);
        if (!fs.existsSync(fp)) {
            fs.writeFileSync(fp, f.content, 'utf-8');
        }
    }
}
seedLittleFSDirectories();

function sanitizePath(reqPath) {
    let clean = path.normalize(reqPath || '/').replace(/^(\.\.[\/\\])+/, '');
    if (clean.startsWith('/') || clean.startsWith('\\')) clean = clean.substring(1);
    return path.join(STORAGE_DIR, clean);
}

function getRelativePath(fullPath) {
    let rel = path.relative(STORAGE_DIR, fullPath).replace(/\\/g, '/');
    if (!rel || rel === '.') return '/';
    return '/' + rel;
}

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
        const reqPath = parsedUrl.searchParams.get('path') || '/';
        const targetDir = sanitizePath(reqPath);
        try {
            if (!fs.existsSync(targetDir) || !fs.statSync(targetDir).isDirectory()) {
                res.writeHead(404, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Directory not found' }));
                return;
            }
            const entries = fs.readdirSync(targetDir).map(name => {
                const fullItemPath = path.join(targetDir, name);
                const stat = fs.statSync(fullItemPath);
                const isDir = stat.isDirectory();
                return {
                    name,
                    path: getRelativePath(fullItemPath),
                    isDir,
                    size: isDir ? 0 : stat.size,
                    mtime: stat.mtimeMs,
                    type: isDir ? 'dir' : (path.extname(name).replace('.', '').toLowerCase() || 'bin')
                };
            });
            // Sort: directories first, then alphabetically
            entries.sort((a, b) => {
                if (a.isDir && !b.isDir) return -1;
                if (!a.isDir && b.isDir) return 1;
                return a.name.localeCompare(b.name);
            });
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                currentPath: getRelativePath(targetDir),
                items: entries
            }));
        } catch (e) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: e.message }));
        }
    } else if (pathname === '/api/files/read') {
        const reqPath = parsedUrl.searchParams.get('path') || '';
        const targetFile = sanitizePath(reqPath);
        if (fs.existsSync(targetFile) && fs.statSync(targetFile).isFile()) {
            const ext = path.extname(targetFile).toLowerCase();
            const textExtensions = ['.txt', '.cfg', '.json', '.log', '.csv', '.sub', '.raw', '.sh', '.ini', '.md', '.html', '.js', '.c', '.h', '.cpp'];
            const isText = textExtensions.includes(ext);

            if (isText) {
                const textContent = fs.readFileSync(targetFile, 'utf-8');
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({
                    name: path.basename(targetFile),
                    path: getRelativePath(targetFile),
                    isBinary: false,
                    size: fs.statSync(targetFile).size,
                    content: textContent
                }));
            } else {
                // Read binary hex preview (first 256 bytes)
                const buf = fs.readFileSync(targetFile);
                const hexLines = [];
                for (let i = 0; i < Math.min(buf.length, 256); i += 16) {
                    const chunk = buf.slice(i, i + 16);
                    const hex = Array.from(chunk).map(b => b.toString(16).padStart(2, '0')).join(' ');
                    const ascii = Array.from(chunk).map(b => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '.')).join('');
                    hexLines.push(`${i.toString(16).padStart(4, '0')}:  ${hex.padEnd(48, ' ')}  |${ascii}|`);
                }
                if (buf.length > 256) hexLines.push(`... [${buf.length - 256} more bytes truncated in preview]`);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({
                    name: path.basename(targetFile),
                    path: getRelativePath(targetFile),
                    isBinary: true,
                    size: buf.length,
                    content: hexLines.join('\n')
                }));
            }
        } else {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'File not found' }));
        }
    } else if (pathname === '/api/files/download') {
        const reqPath = parsedUrl.searchParams.get('path') || parsedUrl.searchParams.get('name') || '';
        const targetFile = sanitizePath(reqPath);
        if (fs.existsSync(targetFile) && fs.statSync(targetFile).isFile()) {
            const filename = path.basename(targetFile);
            res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
            res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
            fs.createReadStream(targetFile).pipe(res);
        } else {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('File not found');
        }
    } else if (pathname === '/api/files/upload' && req.method === 'POST') {
        const reqDir = parsedUrl.searchParams.get('path') || '/';
        const filename = path.basename(parsedUrl.searchParams.get('name') || `upload_${Date.now()}.bin`);
        const targetDir = sanitizePath(reqDir);
        if (!fs.existsSync(targetDir)) fs.mkdirSync(targetDir, { recursive: true });
        const filepath = path.join(targetDir, filename);

        const fileStream = fs.createWriteStream(filepath);
        req.pipe(fileStream);
        fileStream.on('finish', () => {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'OK', name: filename, path: getRelativePath(filepath) }));
        });
        fileStream.on('error', (err) => {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: err.message }));
        });
    } else if (pathname === '/api/files/mkdir') {
        const reqPath = parsedUrl.searchParams.get('path') || '';
        const folderName = parsedUrl.searchParams.get('name') || '';
        const targetDir = sanitizePath(path.join(reqPath, folderName));
        try {
            fs.mkdirSync(targetDir, { recursive: true });
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'OK', path: getRelativePath(targetDir) }));
        } catch (e) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: e.message }));
        }
    } else if (pathname === '/api/files/delete') {
        const reqPath = parsedUrl.searchParams.get('path') || parsedUrl.searchParams.get('name') || '';
        const targetPath = sanitizePath(reqPath);
        if (fs.existsSync(targetPath)) {
            const stat = fs.statSync(targetPath);
            if (stat.isDirectory()) {
                fs.rmSync(targetPath, { recursive: true, force: true });
            } else {
                fs.unlinkSync(targetPath);
            }
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'DELETED', path: reqPath }));
        } else {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'File not found' }));
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
