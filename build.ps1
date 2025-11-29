# ESP-IDF Build Script
Set-Location C:\esp32_projects\display_controller

$env:IDF_PATH = "C:\esp32_projects\esp_idf"
$env:IDF_PYTHON_ENV_PATH = "C:\esp32_projects\espressif\python_env\idf5.4_py3.13_env"
$env:PATH = "C:\esp32_projects\espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\esp32_projects\espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin;C:\esp32_projects\espressif\tools\cmake\3.30.2\bin;C:\esp32_projects\espressif\tools\ninja\1.12.1;" + ("$env:IDF_PYTHON_ENV_PATH\Scripts;" + $env:PATH)

Write-Host "Building from directory: $(Get-Location)"
Write-Host "IDF_PATH: $env:IDF_PATH"

& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" build
