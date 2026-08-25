@echo off
title Bullet OS - Emulator and Screen
echo Starting Bullet OS Screen Window and QEMU...
start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --app="http://localhost:8080/" --window-size=560,860
"C:\Users\ttt79\Projects\s3node\tools\qemu\bin\qemu-system-xtensa.exe" -nographic -machine esp32 -drive file="%~dp0esp32_firmware\qemu_esp32.bin",if=mtd,format=raw -serial mon:stdio
pause
