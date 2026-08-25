@echo off
title Bullet OS - Open Firewall Port 8080
echo ===================================================
echo   Bullet OS: Opening Inbound Port 8080 in Firewall
echo ===================================================
echo.
powershell -Command "Start-Process cmd -ArgumentList '/c netsh advfirewall firewall add rule name=\"\"BulletOS\"\" dir=in action=allow protocol=TCP localport=8080 & echo. & echo [OK] Port 8080 is now OPEN for Phone and Local Network! & pause' -Verb runAs"

