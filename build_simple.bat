@echo off
echo ========================================
echo    ESP32-P4 ROCKET LAUNCHER BUILD
echo ========================================
echo.

REM Change to project directory
cd /d "%~dp0"

echo Building with direct Python path (bypassing export.bat issues)...
echo.

REM Use the Python environment that was working in the terminal history
C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe -c "import sys; import os; os.environ['IDF_PATH']='C:\\Users\\mopar\\.espressif\\esp-idf-v5.4.2'; sys.path.insert(0, 'C:\\Users\\mopar\\.espressif\\esp-idf-v5.4.2\\tools'); import idf; idf.main(['build'])"

if %errorlevel% eq 0 (
    echo.
    echo ========================================
    echo    BUILD SUCCESSFUL! 🚀
    echo ========================================
    echo.
    echo Firmware ready at: build\rocket_display_controller.bin
    echo.
    echo To flash: build_simple.bat flash COM6
    echo.
) else (
    echo.
    echo Build failed, trying alternative method...
    echo.
    
    REM Set minimal required environment and try again
    set IDF_PATH=C:\Users\mopar\.espressif\esp-idf-v5.4.2
    set PATH=C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts;%PATH%
    
    C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe C:\Users\mopar\.espressif\esp-idf-v5.4.2\tools\idf.py build
    
    if %errorlevel% eq 0 (
        echo.
        echo ========================================
        echo    BUILD SUCCESSFUL! 🚀 (Alternative method)
        echo ========================================
        echo.
    ) else (
        echo.
        echo ========================================
        echo    BUILD FAILED! ❌
        echo ========================================
        echo.
        echo Try running: C:\Users\mopar\.espressif\esp-idf-v5.4.2\install.bat
        echo Then retry this build script.
    )
)

if "%1"=="flash" (
    if "%2"=="" (
        echo ERROR: Please specify COM port. Example: build_simple.bat flash COM6
        pause
        exit /b 1
    )
    
    echo.
    echo Flashing to %2...
    echo.
    
    C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe -m esptool --chip esp32p4 -p %2 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x2000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\rocket_display_controller.bin
    
    if %errorlevel% eq 0 (
        echo.
        echo ========================================
        echo    FLASH SUCCESSFUL! 🎯
        echo ========================================
        echo.
        echo ESP32-P4 Rocket Launcher is ready!
    )
)

pause