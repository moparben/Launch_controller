@echo off
title ESP-IDF Terminal - ESP32-P4 Display Controller

echo ========================================
echo     ESP-IDF DEVELOPMENT TERMINAL
echo ========================================
echo.

REM Set ESP-IDF environment variables
set "IDF_PATH=C:\Users\mopar\.espressif\esp-idf-v5.4.2"
set "IDF_PYTHON_ENV_PATH=C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env"
set "IDF_TOOLS_PATH=C:\Users\mopar\.espressif"

REM Add tools to PATH
set "PATH=C:\Users\mopar\.espressif\tools\cmake\3.30.2\bin;%PATH%"
set "PATH=C:\Users\mopar\.espressif\tools\ninja\1.12.1;%PATH%"
set "PATH=C:\Users\mopar\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%"
set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%"
set "PATH=%IDF_PATH%\tools;%PATH%"

REM Navigate to project directory
cd /d "C:\esp32_projects\display_controller"

echo Current directory: %CD%
echo ESP-IDF Path: %IDF_PATH%
echo Python Environment: %IDF_PYTHON_ENV_PATH%
echo.

REM Verify ESP-IDF
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" --version 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [✓] ESP-IDF is ready!
) else (
    echo [!] ESP-IDF verification failed
)

echo.
echo Available shortcuts:
echo   build       - idf.py build
echo   flash       - idf.py -p COM6 flash  
echo   monitor     - idf.py -p COM6 monitor
echo   clean       - idf.py fullclean
echo   menuconfig  - idf.py menuconfig
echo.

REM Create doskey macros for shortcuts
doskey build="%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build
doskey flash="%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p COM6 flash
doskey monitor="%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p COM6 monitor
doskey clean="%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" fullclean
doskey menuconfig="%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" menuconfig

echo ESP32-P4 Display Controller - Ready for development!
echo.

REM Start command prompt
cmd /k