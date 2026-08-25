# 💀 Bullet OS v0.2.1 — Advanced ESP32 Dual-Display Security OS & Micro-ADB Suite

**Bullet OS** — это модульная, высокопроизводительная графическая операционная система на чистом Си для микроконтроллеров **ESP32** (ESP32-S3 N16R8, ESP32-WROOM, ESP32-C3 RISC-V) с адаптивной поддержкой экранов (OLED 128x64, IPS 240x240, CYD 320x240, HVGA 480x320) и управлением через **1 энкодер + 1 кнопку**.

---

## ⚡ Основные возможности

### 📱 1. Встроенный автономный стек Micro-ADB (`src/micro_adb/`)
- Чистая реализация протокола **Android Debug Bridge v1.0 (Wire Protocol)** на C без сторонних программ.
- Прямое управление смартфоном по **Wi-Fi TCP (`5555`)** или через **USB OTG Host (ESP32-S3 GPIO 19/20)**.
- **Действия**: Вкл/Выкл экран (`keyevent 26`), Домой, Назад, Свайп разблокировки, Громкость +/-, Перезагрузка в **Recovery / TWRP** (`reboot recovery`), Перезагрузка в **Fastboot / Bootloader** (`reboot bootloader`), Скриншот, Открытие браузера по URL.
- **Живая телеметрия**: определение реальной модели телефона (`ro.product.model`), версии Android (`ro.build.version.release`), уровня заряда батареи и статуса экрана.

### 🛡️ 2. Безопасность и RF Анализ Эфира
- **Attack Detector (IDS)**: детекция атак глушения Wi-Fi (Deauth Flood / Disassociation) со звуковой и визуальной тревогой.
- **Probe Request Sniffer**: перехват сетевых отпечатков смартфонов рядом.
- **RF 2.4G Channel Waterfall**: анализ активности и зашумленности 14 каналов Wi-Fi в реальном времени.
- **BLE Radar**: сканер маяков Apple AirTag, iBeacon и Bluetooth Low Energy устройств с определением дистанции по RSSI.

### 🎮 3. Игры и Демонстрации
- **Retro Pong**: классический Понг с AI-соперником и 4 уровнями сложности (Easy, Normal, Hard, Insane).
- **Chrome Dino**: аркадный бесконечный раннер с кактусами и птеродактилями.
- **Retro Kart**: аркадные гонки с нитро-ускорением под поворот энкодера.
- **Matrix Rain**: плавный цифровой неоновый дождь в стиле Матрицы.
- **Audio Spectrum**: 16-полосный визуализатор звукового спектра FFT.

### 🖥️ 4. Linux CLI Терминал & Утилиты
- Встроенная командная строка: `neofetch` с пиксельным логотипом Bullet OS, `adb`, `hw scan`, `wifi scan`, `ping`, `curl`, `free`, `df`, `uname`, `dmesg`, `cat /proc/cpuinfo`.
- **Device Scanner**: автоматическое сканирование шин I2C, SPI и USB для обнаружения подключенных модулей (CC1101, NRF24L01, PN532 RFID, MPU6050, SSD1306, ST7789).

---

## 🚀 Запуск и Тестирование

### 1. Нативный Десктопный Эмулятор (GUI-окно 60 FPS)
```cmd
run_emulator.bat
```
Открывает автономное десктопное окно с экраном устройства, виртуальным энкодером, кнопками и клавиатурой.

### 2. Запуск в QEMU ESP32
```cmd
run_qemu.bat
```
Запускает реальный бинарник прошивки в эмуляторе QEMU с интерактивным управлением через консоль.

### 3. Веб-Симулятор в браузере
Откройте `web_sim/index.html` или запустите локальный сервер:
```bash
python -m http.server 8080 --directory web_sim
```
И перейдите по адресу: **`http://localhost:8080/`**.

---

## 🛠️ Сборка и Прошивка через PlatformIO

В `esp32_firmware/platformio.ini` настроены профили под разные чипы:

```bash
# 1. ESP32-S3 с 16MB Flash и 8MB Octal PSRAM
pio run -d esp32_firmware -e esp32s3_n16r8 -t upload

# 2. Стандартный ESP32 (WROOM-32 / CYD 4MB)
pio run -d esp32_firmware -e esp32_wroom -t upload

# 3. ESP32-C3 RISC-V Single Core
pio run -d esp32_firmware -e esp32c3 -t upload
```

---

## 📁 Структура Репозитория

```
Bullet/
├── src/
│   ├── micro_adb/            # Встроенный стек протокола Micro-ADB (C)
│   │   ├── micro_adb.h
│   │   └── micro_adb.c
│   ├── ui/                   # Графический движок UI и приложения
│   │   ├── wifi_oled_ui.h
│   │   ├── wifi_oled_ui.c
│   │   └── logo_bitmap.h     # Пиксельный логотип Bullet OS
│   ├── app_core/             # Жизненный цикл ОС и шина событий
│   └── hal/                  # Hardware Abstraction Layer
├── esp32_firmware/           # Проект PlatformIO для физических плат
│   ├── platformio.ini        # Конфигурация S3, WROOM, C3, QEMU
│   └── src/main.cpp          # Точка входа прошивки
├── web_sim/                  # Браузерный WebAssembly симулятор
├── desktop_emulator.py       # Нативный GUI-эмулятор экрана
├── emulator_backend.js       # Движок эмуляции
├── run_emulator.bat          # Скрипт запуска десктопного эмулятора
└── run_qemu.bat              # Скрипт запуска QEMU эмулятора
```
