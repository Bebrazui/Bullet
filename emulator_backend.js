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

server.listen(50555, '127.0.0.1', () => {
    console.log('EMULATOR_BACKEND_READY');
});
