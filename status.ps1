# ESP32-P4 Rocket Launcher Status Monitor
# Quick status check and control panel

Write-Host "🚀 ESP32-P4 ULTIMATE ROCKET LAUNCHER - STATUS MONITOR" -ForegroundColor Yellow
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

# Try to detect the ESP32 IP address from common ranges
$commonIPs = @("192.168.1", "192.168.0", "10.0.0", "172.16.0")
$foundIP = $null

Write-Host "🔍 Scanning for rocket launcher on network..." -ForegroundColor Yellow

foreach ($subnet in $commonIPs) {
    for ($i = 100; $i -le 200; $i++) {
        $testIP = "$subnet.$i"
        try {
            $response = Invoke-WebRequest -Uri "http://$testIP/api/status" -TimeoutSec 1 -ErrorAction SilentlyContinue
            if ($response.StatusCode -eq 200) {
                $foundIP = $testIP
                break
            }
        } catch {
            # Continue searching
        }
    }
    if ($foundIP) { break }
}

if ($foundIP) {
    Write-Host "🎯 FOUND ROCKET LAUNCHER AT: $foundIP" -ForegroundColor Green
    Write-Host "======================================" -ForegroundColor Green
    Write-Host ""
    
    try {
        $status = Invoke-RestMethod -Uri "http://$foundIP/api/status"
        Write-Host "📊 SYSTEM STATUS:" -ForegroundColor Cyan
        Write-Host "   Device: $($status.device_name)" -ForegroundColor White
        Write-Host "   Firmware: $($status.firmware_version)" -ForegroundColor White
        Write-Host "   Display: $(if($status.display_ready){'✅ READY'}else{'❌ ERROR'})" -ForegroundColor White
        Write-Host "   WiFi: $(if($status.wifi_connected){'✅ CONNECTED'}else{'❌ DISCONNECTED'})" -ForegroundColor White
        Write-Host "   Audio: $(if($status.audio_ready){'✅ READY'}else{'❌ ERROR'})" -ForegroundColor White
        Write-Host "   I2C: $(if($status.i2c_ready){'✅ READY'}else{'❌ ERROR'})" -ForegroundColor White
        Write-Host "   SDMMC: $(if($status.sdmmc_ready){'✅ READY'}else{'❌ ERROR'})" -ForegroundColor White
        Write-Host "   Launches: $($status.launch_count)" -ForegroundColor Yellow
        Write-Host ""
        
        Write-Host "🌐 CONTROL PANEL:" -ForegroundColor Cyan
        Write-Host "   Status:  http://$foundIP/api/status" -ForegroundColor White
        Write-Host "   Launch:  curl -X POST http://$foundIP/api/launch" -ForegroundColor White
        Write-Host "   Audio:   curl -X POST -H 'Content-Type: application/json' \\" -ForegroundColor White
        Write-Host "            -d '{\"volume\":0.7}' http://$foundIP/api/audio" -ForegroundColor White
        Write-Host ""
        
        # Quick test launch
        $launch = Read-Host "🚀 Test rocket launch? (y/N)"
        if ($launch -eq "y" -or $launch -eq "Y") {
            Write-Host "🔥 LAUNCHING ROCKET..." -ForegroundColor Red
            $launchResult = Invoke-RestMethod -Uri "http://$foundIP/api/launch" -Method POST
            Write-Host "   Result: $($launchResult.message)" -ForegroundColor Yellow
        }
        
    } catch {
        Write-Host "⚠️  Connected but API not responding yet" -ForegroundColor Yellow
        Write-Host "   System may still be initializing..." -ForegroundColor DarkGray
    }
    
} else {
    Write-Host "⏳ Rocket launcher not found on network yet" -ForegroundColor Yellow
    Write-Host "   System may still be starting up or connecting to WiFi" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "💡 Check serial monitor (COM6) for WiFi connection status" -ForegroundColor Cyan
    Write-Host "   Once connected, re-run this script to find the IP address" -ForegroundColor White
}

Write-Host ""
Write-Host "📊 Serial Monitor: python -m serial.tools.miniterm COM6 115200" -ForegroundColor DarkGray