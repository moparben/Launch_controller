# ESP32-P4 Rocket Launcher - Resolved Build Environment
# This script handles the Python environment issues and provides reliable build/flash

param(
    [string]$Action = "build",
    [string]$ComPort = ""
)

Write-Host "🚀 ESP32-P4 ULTIMATE ROCKET LAUNCHER" -ForegroundColor Yellow
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$pythonPath = "C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe"
$idfPath = "C:\Users\mopar\.espressif\esp-idf-v5.4.2"

# Check if firmware already exists
$firmwarePath = "build\rocket_display_controller.bin"
$bootloaderPath = "build\bootloader\bootloader.bin"
$partitionPath = "build\partition_table\partition-table.bin"

if (Test-Path $firmwarePath) {
    $buildTime = (Get-Item $firmwarePath).LastWriteTime
    Write-Host "✅ Existing firmware found:" -ForegroundColor Green
    Write-Host "   Build time: $buildTime" -ForegroundColor White
    Write-Host "   Size: $((Get-Item $firmwarePath).Length) bytes" -ForegroundColor White
    Write-Host ""
}

if ($Action -eq "build" -or $Action -eq "rebuild") {
    Write-Host "🔧 Attempting to build rocket launcher..." -ForegroundColor Yellow
    
    # Try method 1: Direct idf.py with environment
    $env:IDF_PATH = $idfPath
    Write-Host "Method 1: Using idf.py directly..." -ForegroundColor DarkGray
    
    $buildProcess = Start-Process -FilePath $pythonPath -ArgumentList @("$idfPath\tools\idf.py", "build") -Wait -PassThru -NoNewWindow
    
    if ($buildProcess.ExitCode -eq 0) {
        Write-Host "✅ BUILD SUCCESSFUL!" -ForegroundColor Green
    } else {
        Write-Host "❌ Build failed with direct method" -ForegroundColor Red
        
        if (Test-Path $firmwarePath) {
            $existingBuildTime = (Get-Item $firmwarePath).LastWriteTime
            Write-Host "Using existing firmware (built: $existingBuildTime)" -ForegroundColor Yellow
        } else {
            Write-Host "🔧 SOLUTION: Run this in CMD to fix environment:" -ForegroundColor Cyan
            Write-Host "   cd `"$PWD`"" -ForegroundColor White
            Write-Host "   C:\Users\mopar\.espressif\esp-idf-v5.4.2\install.bat" -ForegroundColor White
            Write-Host "   C:\Users\mopar\.espressif\esp-idf-v5.4.2\export.bat" -ForegroundColor White
            Write-Host "   idf.py build" -ForegroundColor White
            exit 1
        }
    }
}

if ($Action -eq "flash" -or ($Action -eq "build" -and $ComPort -ne "")) {
    if ($ComPort -eq "") {
        $ComPort = Read-Host "Enter COM port (e.g., COM6)"
    }
    
    # Verify all required files exist
    $requiredFiles = @($firmwarePath, $bootloaderPath, $partitionPath)
    $missingFiles = $requiredFiles | Where-Object { -not (Test-Path $_) }
    
    if ($missingFiles.Count -gt 0) {
        Write-Host "❌ Missing firmware files:" -ForegroundColor Red
        $missingFiles | ForEach-Object { Write-Host "   $_" -ForegroundColor Red }
        Write-Host "Please run build first." -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host "🔥 FLASHING TO $ComPort..." -ForegroundColor Yellow
    Write-Host "============================================" -ForegroundColor Cyan
    
    $flashArgs = @(
        "-m", "esptool",
        "--chip", "esp32p4",
        "-p", $ComPort,
        "-b", "460800",
        "--before=default_reset",
        "--after=hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_size", "4MB",
        "--flash_freq", "80m",
        "0x2000", $bootloaderPath,
        "0x8000", $partitionPath,
        "0x10000", $firmwarePath
    )
    
    $flashProcess = Start-Process -FilePath $pythonPath -ArgumentList $flashArgs -Wait -PassThru -NoNewWindow
    
    if ($flashProcess.ExitCode -eq 0) {
        Write-Host ""
        Write-Host "🎯 FLASH SUCCESSFUL!" -ForegroundColor Green
        Write-Host "============================================" -ForegroundColor Green
        Write-Host "🚀 ESP32-P4 Ultimate Rocket Launcher is ready!" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "📊 System Features:" -ForegroundColor Cyan
        Write-Host "   📺 10.1'' Waveshare Display" -ForegroundColor White
        Write-Host "   📶 WiFi 6 (ESP32-C6 via SDIO)" -ForegroundColor White
        Write-Host "   🔊 ES8311 Audio System" -ForegroundColor White
        Write-Host "   🔧 I2C, SDMMC Support" -ForegroundColor White
        Write-Host "   🌐 REST API Control" -ForegroundColor White
        Write-Host ""
        Write-Host "🔗 Connect to serial monitor to see system startup" -ForegroundColor Yellow
        Write-Host "🌐 Access control panel at: http://<ESP32-IP>/api/status" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "❌ FLASH FAILED!" -ForegroundColor Red
        Write-Host "Check COM port and ESP32 connection" -ForegroundColor Yellow
    }
}

if ($Action -eq "monitor") {
    if ($ComPort -eq "") {
        $ComPort = Read-Host "Enter COM port for monitoring (e.g., COM6)"
    }
    
    Write-Host "📊 Starting serial monitor on $ComPort..." -ForegroundColor Yellow
    Write-Host "Press Ctrl+C to exit" -ForegroundColor DarkGray
    Write-Host ""
    
    & $pythonPath -m serial.tools.miniterm $ComPort 115200
}

Write-Host ""
Write-Host "Usage examples:" -ForegroundColor Cyan
Write-Host "   .\rocket.ps1 build              - Build firmware" -ForegroundColor White
Write-Host "   .\rocket.ps1 flash COM6         - Flash to COM6" -ForegroundColor White
Write-Host "   .\rocket.ps1 build COM6         - Build and flash" -ForegroundColor White
Write-Host "   .\rocket.ps1 monitor COM6       - Serial monitor" -ForegroundColor White