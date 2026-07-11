@echo off
chcp 65001 >nul
echo ========================================
echo   🚁 ESP32-CAM Drone Camera - Upload
echo ========================================
echo.

set CLI="%~dp0arduino-cli-bin\arduino-cli.exe"
set SKETCH="%~dp0ESP32_Drone_Cam"
set PORT=COM7
set FQBN=esp32:esp32:esp32cam
set PARTITIONS=huge_app

echo [1/4] Installing ESP32 board core...
%CLI% core install esp32:esp32 --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

echo.
echo [2/4] Compiling firmware...
%CLI% compile --fqbn %FQBN% --board-options "PartitionScheme=%PARTITIONS%" %SKETCH%
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo [3/4] Uploading to %PORT%...
%CLI% upload -p %PORT% --fqbn %FQBN% %SKETCH%
if errorlevel 1 (
    echo.
    echo [ERROR] Upload failed! Check COM port.
    pause
    exit /b 1
)

echo.
echo [4/4] Opening Serial Monitor...
echo ========================================
echo   ✅ Upload complete! Monitoring...
echo   Press Ctrl+C to exit
echo ========================================
%CLI% monitor -p %PORT% -c baudrate=115200
pause
