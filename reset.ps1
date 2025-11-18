# Quick ESP32 Reset and Monitor
Write-Host "🚀 ESP32-P4 Rocket Launcher Reset" -ForegroundColor Yellow
Write-Host "=================================" -ForegroundColor Cyan

# Reset the ESP32
Write-Host "Resetting ESP32..." -ForegroundColor Yellow
python -m esptool --chip esp32p4 -p COM6 --before default-reset run 2>$null

Start-Sleep -Seconds 3

Write-Host "Starting monitor..." -ForegroundColor Green
Write-Host "Press Ctrl+] to exit when done" -ForegroundColor DarkGray
Write-Host ""

# Start monitoring
python -m serial.tools.miniterm COM6 115200