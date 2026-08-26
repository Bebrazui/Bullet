@echo off
title Bullet OS - Desktop Hardware Emulator
echo ====================================================
echo   Launching Bullet OS Native Desktop GUI Emulator
echo ====================================================
taskkill /F /IM node.exe >nul 2>&1
python "%~dp0desktop_emulator.py"
