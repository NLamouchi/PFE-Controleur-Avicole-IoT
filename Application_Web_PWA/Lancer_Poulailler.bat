@echo off
chcp 65001 >nul
title Poulailler intelligent - lanceur
cd /d "%~dp0"

echo Demarrage du broker Mosquitto (ports 1883 et 9001)...
start "Mosquitto" "E:\Mosquitto\mosquitto.exe" -c "%~dp0mosquitto_windows.conf" -v

echo Demarrage du serveur web (port 8000)...
start "Serveur web" python -m http.server 8000 --bind 0.0.0.0 --directory "%~dp0."

echo.
echo Sur le telephone, ouvrir : http://ADRESSE-IPv4-CI-DESSOUS:8000
ipconfig | findstr /i "IPv4"
echo.
echo Pour tout arreter : fermer les fenetres "Mosquitto" et "Serveur web".
pause
