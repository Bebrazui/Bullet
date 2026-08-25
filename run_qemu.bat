@echo off
title Bullet OS - QEMU ESP32 Emulator
echo ===================================================
echo   Launching Bullet OS v0.2.1 in QEMU ESP32 Emulator
echo ===================================================
echo Controls:
echo   [A] / [S]     - Rotate Knob Left
echo   [D] / [W]     - Rotate Knob Right
echo   [Space/Enter] - Knob Click (Select / Enter)
echo   [Q]           - Long-Press (Back to Menu)
echo   [P]           - Framebuffer Screen Dump
echo   Ctrl+A, X     - Exit QEMU
echo ===================================================
echo.

"C:\Users\ttt79\Projects\s3node\tools\qemu\bin\qemu-system-xtensa.exe" -nographic -machine esp32 -drive file="%~dp0esp32_firmware\qemu_esp32.bin",if=mtd,format=raw -serial mon:stdio
pause
