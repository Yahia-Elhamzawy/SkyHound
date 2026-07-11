@echo off
chcp 65001 >nul
echo ========================================
echo   FALCON - Drone Command Server
echo ========================================
echo.
echo   IMPORTANT: Make sure you are connected
echo   to WiFi: SkyHund (pass: 12345678)
echo.

cd /d "%~dp0server"

echo [1/2] Installing Python packages...
pip install -r requirements.txt -q

echo.
echo [2/2] Starting unified server...
echo.
echo   Open: http://localhost:5000
echo   Drone IP:  192.168.4.1
echo   Camera IP: 192.168.4.2
echo   Press Ctrl+C to stop
echo ========================================
echo.

python server.py
pause
