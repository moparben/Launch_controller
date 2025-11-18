# ESP32-P4 Rocket Launcher Build Script
# Usage: .\build_rocket.ps1 [flash] [COMx]

param(
    [string]$Action = "build",
    [string]$ComPort = ""
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "    ESP32-P4 ROCKET LAUNCHER BUILD" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Set ESP-IDF paths
$IDF_PATH = "C:\Users\mopar\.espressif\esp-idf-v5.4.2"
$IDF_PYTHON_ENV_PATH = "C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env"

# Check if Python 3.13 environment exists
$pythonExe = "$IDF_PYTHON_ENV_PATH\Scripts\python.exe"
if (-not (Test-Path $pythonExe)) {
    Write-Host "ERROR: Python 3.13 environment not found at $IDF_PYTHON_ENV_PATH" -ForegroundColor Red
    Write-Host "Please run ESP-IDF installation first." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Using Python environment: $IDF_PYTHON_ENV_PATH" -ForegroundColor Green
Write-Host "IDF Path: $IDF_PATH" -ForegroundColor Green
Write-Host ""

# Set environment variables
$env:IDF_PATH = $IDF_PATH
$env:IDF_PYTHON_ENV_PATH = $IDF_PYTHON_ENV_PATH

# Add ESP-IDF tools to PATH
$toolsPath = @(
    "C:\Users\mopar\.espressif\tools\cmake\3.30.2\bin",
    "C:\Users\mopar\.espressif\tools\ninja\1.12.1",
    "C:\Users\mopar\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin",
    "$IDF_PYTHON_ENV_PATH\Scripts"
)

foreach ($toolPath in $toolsPath) {
    if (Test-Path $toolPath) {
        $env:PATH = "$toolPath;$env:PATH"
        Write-Host "Added to PATH: $toolPath" -ForegroundColor DarkGray
    }
}

# Change to script directory
Set-Location $PSScriptRoot

Write-Host "Building rocket launcher system..." -ForegroundColor Yellow
Write-Host ""

# Build the project using full path to python
$buildResult = & "$pythonExe" "$IDF_PATH\tools\idf.py" build
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "    BUILD SUCCESSFUL! 🚀" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Firmware files created:" -ForegroundColor Cyan
    Write-Host "   - build\rocket_display_controller.bin" -ForegroundColor White
    Write-Host "   - build\bootloader\bootloader.bin" -ForegroundColor White
    Write-Host "   - build\partition_table\partition-table.bin" -ForegroundColor White
    Write-Host ""
    Write-Host "To flash to ESP32-P4:" -ForegroundColor Cyan
    Write-Host "   .\build_rocket.ps1 flash COM6" -ForegroundColor White
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "    BUILD FAILED! ❌" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit $buildExitCode
}

if ($Action -eq "flash") {
    if ($ComPort -eq "") {
        Write-Host "ERROR: Please specify COM port. Example: .\build_rocket.ps1 flash COM6" -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "    FLASHING TO $ComPort" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $flashResult = & "$pythonExe" -m esptool --chip esp32p4 -p $ComPort -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x2000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\rocket_display_controller.bin
    $flashExitCode = $LASTEXITCODE
    
    if ($flashExitCode -eq 0) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "    FLASH SUCCESSFUL! 🎯" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Your ESP32-P4 rocket launcher is ready!" -ForegroundColor Cyan
        Write-Host "Connect to serial monitor to see system startup." -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "    FLASH FAILED! ❌" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        Write-Host ""
    }
}

Write-Host ""
Read-Host "Press Enter to exit"