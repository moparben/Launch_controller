@echo off
echo ========================================
echo    ESP32-P4 ROCKET LAUNCHER BUILD
echo ========================================
echo.

REM Set ESP-IDF paths
set IDF_PATH=C:\Users\mopar\.espressif\esp-idf-v5.4.2
set IDF_PYTHON_ENV_PATH=C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env

REM Check if Python 3.13 environment exists
if not exist "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" (
    echo ERROR: Python 3.13 environment not found at %IDF_PYTHON_ENV_PATH%
    echo Please run ESP-IDF installation first.
    pause
    exit /b 1
)

echo Using Python environment: %IDF_PYTHON_ENV_PATH%
echo IDF Path: %IDF_PATH%
echo.

REM Change to project directory
cd /d "%~dp0"

REM Try to activate ESP-IDF environment
echo Activating ESP-IDF environment...
call "%IDF_PATH%\export.bat"

if %errorlevel% neq 0 (
    echo.
    echo WARNING: ESP-IDF export failed, trying alternative approach...
    
    REM Set paths manually
    set PATH=%IDF_PATH%\tools\cmake\3.30.2\bin;%PATH%
    set PATH=%IDF_PATH%\tools\ninja\1.12.1;%PATH%
    set PATH=%IDF_PATH%\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%
    set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%
    
    echo Manual paths set.
)

echo.
echo Building rocket launcher system...
echo.

REM Build the project using full path to python
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build

if %errorlevel% eq 0 (
    echo.
    echo ========================================
    echo    BUILD SUCCESSFUL! 🚀
    echo ========================================
    echo.
    echo Firmware files created:
    echo   - build\rocket_display_controller.bin
    echo   - build\bootloader\bootloader.bin
    echo   - build\partition_table\partition-table.bin
    echo.
    echo To flash to ESP32-P4:
    echo   build_rocket.bat flash COM6
    echo.
) else (
    echo.
    echo ========================================
    echo    BUILD FAILED! ❌
    echo ========================================
    echo.
)

if "%1"=="flash" (
    if "%2"=="" (
        echo ERROR: Please specify COM port. Example: build_rocket.bat flash COM6
        pause
        exit /b 1
    )
    
    echo.
    echo ========================================
    echo    FLASHING TO %2
    echo ========================================
    echo.
    
    "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" -m esptool --chip esp32p4 -p %2 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x2000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\rocket_display_controller.bin
    
    if %errorlevel% eq 0 (
        echo.
        echo ========================================
        echo    FLASH SUCCESSFUL! 🎯
        echo ========================================
        echo.
        echo Your ESP32-P4 rocket launcher is ready!
        echo Connect to serial monitor to see system startup.
    ) else (
        echo.
        echo ========================================
        echo    FLASH FAILED! ❌
        echo ========================================
        echo.
    )
)

pause