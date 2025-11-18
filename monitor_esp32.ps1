# Quick ESP32 Monitor Script
# Activates ESP-IDF and monitors serial output

# Navigate to project directory
Set-Location "C:\esp32_projects\display_controller"

# Activate ESP-IDF environment
& "C:\Users\mopar\.espressif\esp-idf-v5.4.2\export.ps1"

# Start monitoring
Write-Host "Starting ESP32 monitor on COM6..."
Write-Host "Press Ctrl+] to exit monitor"
Write-Host ""

idf.py -p COM6 monitor