@echo off
REM Simple ESP-IDF build script for Waveshare implementation

echo Setting up ESP-IDF environment...
set IDF_PATH=C:\Users\mopar\Downloads\node-v22.21.0-win-x64\esp-idf
set IDF_PYTHON_ENV_PATH=C:\Users\mopar\.espressif\python_env\idf5.4_py3.14_env
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_PATH%\tools;%PATH%

echo Building Waveshare implementation...
cd /d C:\Users\mopar\Downloads\node-v22.21.0-win-x64\rocket_projects\rocket_launcher_system\projects\display_controller

echo Using ESP-IDF Python environment...
%IDF_PYTHON_ENV_PATH%\Scripts\python.exe %IDF_PATH%\tools\idf.py build

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Ready to flash.
    echo To flash: %IDF_PYTHON_ENV_PATH%\Scripts\python.exe %IDF_PATH%\tools\idf.py -p COM6 flash monitor
) else (
    echo Build failed with error code %ERRORLEVEL%
)

pause