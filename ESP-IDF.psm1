# ESP-IDF PowerShell Module
# Save as ESP-IDF.psm1 and import with: Import-Module .\ESP-IDF.psm1

$Global:ESP_IDF_CONFIG = @{
    IDF_PATH = "C:\Users\mopar\.espressif\esp-idf-v5.4.2"
    PYTHON_ENV = "C:\Users\mopar\.espressif\python_env\idf5.4_py3.13_env"
    PROJECT_DIR = "C:\esp32_projects\display_controller"
    SERIAL_PORT = "COM6"
    BOARD = "esp32p4"
}

function Initialize-ESP-IDF {
    [CmdletBinding()]
    param()
    
    Write-Host "🔧 Initializing ESP-IDF Environment..." -ForegroundColor Cyan
    
    # Set environment variables
    $env:IDF_PATH = $Global:ESP_IDF_CONFIG.IDF_PATH
    $env:IDF_PYTHON_ENV_PATH = $Global:ESP_IDF_CONFIG.PYTHON_ENV
    
    # Build PATH
    $toolsPaths = @(
        "C:\Users\mopar\.espressif\tools\cmake\3.30.2\bin",
        "C:\Users\mopar\.espressif\tools\ninja\1.12.1",
        "C:\Users\mopar\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin",
        "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts",
        "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools"
    )
    
    foreach ($path in $toolsPaths) {
        if (Test-Path $path) {
            $env:PATH = "$path;$env:PATH"
        }
    }
    
    # Navigate to project
    Set-Location $Global:ESP_IDF_CONFIG.PROJECT_DIR
    
    Write-Host "✅ ESP-IDF Environment Ready!" -ForegroundColor Green
    Write-Host "📁 Project Directory: $(Get-Location)" -ForegroundColor Yellow
}

function Invoke-ESPBuild {
    [CmdletBinding()]
    param()
    
    Write-Host "🔨 Building ESP32-P4 Display Controller..." -ForegroundColor Yellow
    & "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts\python.exe" "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools\idf.py" build
}

function Invoke-ESPFlash {
    [CmdletBinding()]
    param(
        [string]$Port = $Global:ESP_IDF_CONFIG.SERIAL_PORT
    )
    
    Write-Host "📡 Flashing to $Port..." -ForegroundColor Yellow
    & "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts\python.exe" "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools\idf.py" -p $Port flash
}

function Invoke-ESPMonitor {
    [CmdletBinding()]
    param(
        [string]$Port = $Global:ESP_IDF_CONFIG.SERIAL_PORT
    )
    
    Write-Host "📺 Opening serial monitor on $Port..." -ForegroundColor Yellow
    & "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts\python.exe" "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools\idf.py" -p $Port monitor
}

function Invoke-ESPClean {
    [CmdletBinding()]
    param()
    
    Write-Host "🧹 Cleaning build files..." -ForegroundColor Yellow
    & "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts\python.exe" "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools\idf.py" fullclean
}

function Invoke-ESPMenuconfig {
    [CmdletBinding()]
    param()
    
    Write-Host "⚙️ Opening menuconfig..." -ForegroundColor Yellow
    & "$($Global:ESP_IDF_CONFIG.PYTHON_ENV)\Scripts\python.exe" "$($Global:ESP_IDF_CONFIG.IDF_PATH)\tools\idf.py" menuconfig
}

function Start-ESPSession {
    [CmdletBinding()]
    param()
    
    Initialize-ESP-IDF
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "    ESP-IDF SESSION ACTIVE! 🚀" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Available Commands:" -ForegroundColor Cyan
    Write-Host "  Invoke-ESPBuild       (or: espbuild)" -ForegroundColor White
    Write-Host "  Invoke-ESPFlash       (or: espflash)" -ForegroundColor White  
    Write-Host "  Invoke-ESPMonitor     (or: espmonitor)" -ForegroundColor White
    Write-Host "  Invoke-ESPClean       (or: espclean)" -ForegroundColor White
    Write-Host "  Invoke-ESPMenuconfig  (or: espconfig)" -ForegroundColor White
    Write-Host ""
}

# Create aliases for easier use
New-Alias -Name espbuild -Value Invoke-ESPBuild -Force
New-Alias -Name espflash -Value Invoke-ESPFlash -Force
New-Alias -Name espmonitor -Value Invoke-ESPMonitor -Force
New-Alias -Name espclean -Value Invoke-ESPClean -Force
New-Alias -Name espconfig -Value Invoke-ESPMenuconfig -Force
New-Alias -Name espinit -Value Initialize-ESP-IDF -Force
New-Alias -Name espstart -Value Start-ESPSession -Force

# Export functions
Export-ModuleMember -Function * -Alias *

# Auto-initialize when module is imported
Start-ESPSession