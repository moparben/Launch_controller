@echo off
echo 🚀 ESP32-P4 ROCKET LAUNCHER - Quick Flash
echo ========================================

if "%1"=="" (
    echo Usage: flash.bat COM6
    echo.
    echo Available firmware:
    dir build\*.bin 2>nul
    echo.
    pause
    exit /b 1
)

echo Flashing to %1...
C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe -m esptool --chip esp32p4 -p %1 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x2000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\rocket_display_controller.bin

if %errorlevel% eq 0 (
    echo.
    echo 🎯 SUCCESS! ESP32-P4 Ultimate Rocket Launcher is ready!
    echo 📊 Connect to serial monitor to see system startup
) else (
    echo.
    echo ❌ Flash failed - check COM port and connections
)

pause