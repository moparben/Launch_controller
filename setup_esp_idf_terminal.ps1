# ESP-IDF Terminal Setup Script
# Save this as setup_esp_idf_terminal.ps1 in your project directory
# Run with: .\setup_esp_idf_terminal.ps1

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "    ESP-IDF DEVELOPMENT ENVIRONMENT" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan

# ESP-IDF Configuration
$IDF_PATH = "C:\Users\mopar\Downloads\node-v22.21.0-win-x64\esp-idf"
$IDF_PYTHON_ENV = "C:\esp32_projects\espressif\python_env\idf5.4_py3.13_env"
$PROJECT_DIR = "C:\esp32_projects\display_controller"

# Verify paths exist
if (-not (Test-Path $IDF_PATH)) {
    Write-Host "ERROR: ESP-IDF not found at $IDF_PATH" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $IDF_PYTHON_ENV)) {
    Write-Host "ERROR: Python environment not found at $IDF_PYTHON_ENV" -ForegroundColor Red
    exit 1
}

# Set environment variables
$env:IDF_PATH = $IDF_PATH
$env:IDF_PYTHON_ENV_PATH = $IDF_PYTHON_ENV
$env:IDF_TOOLS_PATH = "C:\Users\mopar\.espressif"

# Add ESP-IDF tools to PATH
$toolsPaths = @(
    "C:\Users\mopar\.espressif\tools\cmake\3.30.2\bin",
    "C:\Users\mopar\.espressif\tools\ninja\1.12.1", 
    "C:\Users\mopar\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin",
    "C:\Users\mopar\.espressif\tools\esp32ulp-elf\2.35_20220830\esp32ulp-elf\bin",
    "$IDF_PYTHON_ENV\Scripts",
    "$IDF_PATH\tools"
)

foreach ($toolPath in $toolsPaths) {
    if (Test-Path $toolPath) {
        $env:PATH = "$toolPath;$env:PATH"
        Write-Host "Added: $toolPath" -ForegroundColor Green
    } else {
        Write-Host "Missing: $toolPath" -ForegroundColor Yellow
    }
}

# Navigate to project directory
Set-Location $PROJECT_DIR
Write-Host ""
Write-Host "Current directory: $(Get-Location)" -ForegroundColor Cyan

# Verify ESP-IDF is working
Write-Host ""
Write-Host "Verifying ESP-IDF installation..." -ForegroundColor Yellow

try {
    $idfVersion = & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" --version 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "ESP-IDF Version: $idfVersion" -ForegroundColor Green
    } else {
        Write-Host "ESP-IDF version check failed" -ForegroundColor Yellow
    }
} catch {
    Write-Host "Could not verify ESP-IDF version" -ForegroundColor Yellow
}

# Create convenient aliases
function idf-build { & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" build }
function idf-flash { & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" -p COM6 flash }
function idf-monitor { & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" -p COM6 monitor }
function idf-clean { & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" fullclean }
function idf-menuconfig { & "$IDF_PYTHON_ENV\Scripts\python.exe" "$IDF_PATH\tools\idf.py" menuconfig }

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "    ESP-IDF TERMINAL READY!" -ForegroundColor Green  
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Available commands:" -ForegroundColor Cyan
Write-Host "  idf-build       - Build the project" -ForegroundColor White
Write-Host "  idf-flash       - Flash to COM6" -ForegroundColor White
Write-Host "  idf-monitor     - Open serial monitor" -ForegroundColor White
Write-Host "  idf-clean       - Clean build files" -ForegroundColor White
Write-Host "  idf-menuconfig  - Open configuration menu" -ForegroundColor White
Write-Host ""
Write-Host "Direct commands:" -ForegroundColor Cyan
Write-Host "  .\build_rocket.ps1        - Full build with environment setup" -ForegroundColor White
Write-Host "  .\build_rocket.ps1 flash COM6  - Build and flash" -ForegroundColor White
Write-Host ""
Write-Host "ESP32-P4 Display Controller Project Ready!" -ForegroundColor Yellow