## Setting the IDF target

This project is targeted at the ESP32-P4 (IDF target `esp32p4`). If you see the message "IDF_TARGET is not set; guessed 'esp32p4' from sdkconfig", you can explicitly set the target with one of the options below:

- Run the helper script (recommended on Windows PowerShell):

```powershell
cd c:\esp32_projects\display_controller
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\set-idf-target.ps1 -Target esp32p4
```

- Or run the IDF command directly:

```powershell
. 'c:\esp32_projects\activate_idf.ps1'
idf.py set-target esp32p4
```

After setting the target the first time, the IDF tooling will remember the selection in the project and CMake will no longer need to guess from `sdkconfig`.

# ESP32-P4 Display Controller with GT911 Touch

## 🚀 **Project Status: Operational Display + Touch Debugging**

This project implements a complete display controller system for the ESP32-P4 Waveshare development board with:
- ✅ **10.1" MIPI-DSI Display** (800x1280) - Fully operational
- 🔧 **GT911 Capacitive Touch** - Hardware conflict resolution in progress
- ✅ **LVGL v9.2.2 Framework** - Complete graphics library integration
- ✅ **Multi-MCU CAN Bus** - Rocket launcher system communication
- ✅ **Clean Build System** - Optimized dependencies

## 📊 **Current Achievement Level**

| Component | Status | Details |
|-----------|---------|---------|
| Display System | ✅ **Operational** | JD9365 MIPI-DSI working perfectly |
| Touch Controller | 🔧 **Debugging** | GT911 I2C conflict resolution in progress |
| LVGL Graphics | ✅ **Ready** | 1000+ source files built successfully |
| Build System | ✅ **Clean** | Deprecated configs fixed, unused deps removed |
| Version Control | ✅ **Linked** | Connected to Launch_controller repository |

## 🏗️ **Architecture**

### Hardware Platform
- **MCU**: ESP32-P4 (360MHz dual-core RISC-V)
- **Display**: Waveshare 10.1" MIPI-DSI (JD9365 controller)  
- **Touch**: GT911 capacitive touch controller
- **Memory**: 32MB PSRAM, 16MB Flash
- **Communication**: CAN bus for multi-MCU rocket system

### I2C Bus Design (Sequential Sharing)
```
JD9365 Display Controller:
  1. Creates I2C_NUM_1 bus (GPIO7/8)
  2. Initializes display (~1.25 seconds)  
  3. Deinitializes I2C bus

GT911 Touch Controller:
  4. Recreates I2C_NUM_1 bus (same pins)
  5. Initializes touch controller
  6. Maintains bus for ongoing touch operations
```

### Software Stack
- **ESP-IDF**: v5.4.2 (latest stable)
- **LVGL**: v9.2.2 (advanced graphics library)
- **Components**: Waveshare JD9365, Espressif GT911 touch driver
- **Build System**: ESP-IDF managed components (dependency-optimized)

## 🔧 **Recent Fixes & Optimizations**

### 1. I2C Bus Conflict Resolution
**Problem**: GT911 touch controller failed to initialize due to JD9365 not properly releasing I2C bus
```
E (6799) i2c.common: I2C bus id(1) has already been acquired
```

**Solution**: Extended JD9365 cleanup delay from 500ms to 2000ms
- Based on v3.04 working code analysis  
- JD9365 requires ~1.25 seconds to fully release I2C resources
- Simplified approach removes complex retry logic

### 2. Build System Optimization  
**Problem**: Deprecated configuration warnings and unnecessary dependencies
```
warning: unknown kconfig symbol 'ESP32P4_DATA_CACHE_SIZE_64KB'
```

**Solution**: 
- Removed deprecated ESP32P4 cache configuration options
- Updated SPIRAM config: `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`
- Cleaned up unused display controller dependencies

### 3. Version Control Integration
- Linked to `https://github.com/moparben/Launch_controller`
- Branch: `esp32_p4_display_controller`
- Git configuration: moparben@yahoo.com

## How to use the example

### Hardware Required

* An ESP development board, which with MIPI DSI peripheral supported
* A general MIPI DSI LCD panel, with 2 data lanes and 1 clock lane, this example support [ILI9881C](https://components.espressif.com/components/espressif/esp_lcd_ili9881c) and [EK79007](https://components.espressif.com/components/espressif/esp_lcd_ek79007)
* An USB cable for power supply and programming

### Hardware Connection

The connection between ESP Board and the LCD is as follows:

```text
       ESP Board                         MIPI DSI LCD Panel
+-----------------------+              +-------------------+
|                   GND +--------------+ GND               |
|                       |              |                   |
|                   3V3 +--------------+ VCC               |
|                       |              |                   |
|             DSI_CLK_P +--------------+ DSI_CLK_P         |
|             DSI_CLK_N +              + DSI_CLK_N         |
|                       |              |                   |
|            DSI_DAT0_P +--------------+ DSI_DAT0_P        |
|            DAI_DAT0_N +              + DAI_DAT0_N        |
|                       |              |                   |
|            DSI_DAT1_P +--------------+ DSI_DAT1_P        |
|            DSI_DAT1_N +              + DSI_DAT1_N        |
|                       |              |                   |
|                       |              |                   |
|              BK_LIGHT +--------------+ BLK               |
|                       |              |                   |
|                 Reset +--------------+ Reset             |
|                       |              |                   |
+-----------------------+              +-------------------+
```

Before testing your LCD, you also need to read your LCD spec carefully, and then adjust the values like "resolution" and "blank time" in the [main](./main/mipi_dsi_lcd_example_main.c) file.

### Configure

Run `idf.py menuconfig` and go to `Example Configuration`:

* Choose the LCD model in `Select MIPI LCD model` according to your board.
* Choose whether to `Use DMA2D to copy draw buffer to frame buffer` asynchronously. If you choose `No`, the draw buffer will be copied to the frame buffer synchronously by CPU.
* Choose if you want to `Monitor Refresh Rate by GPIO`. If you choose `Yes`, then you can attach an oscilloscope or logic analyzer to the GPIO pin to monitor the Refresh Rate of the display.
  Please note, the actual Refresh Rate should be **double** the square wave frequency.

### Build and Flash

Run `idf.py -p PORT build flash monitor` to build, flash and monitor the project. A LVGL widget should show up on the LCD as expected.

The first time you run `idf.py` for the example will cost extra time as the build system needs to address the component dependencies and downloads the missing components from the ESP Component Registry into `managed_components` folder.

(To exit the serial monitor, type ``Ctrl-]``.)

### Select Display MCU Version (v3_02 / v3_04)

This repository contains multiple display MCU file versions for historical/testing purposes. By default, the build will select v3_04. To build a specific version use a CMake variable:

```
idf.py -DBUILD_DISPLAY_VERSION=v3_02 build
```

or for v3_04 (default):

```
idf.py -DBUILD_DISPLAY_VERSION=v3_04 build
```

Only one version is compiled at a time; the `main/CMakeLists.txt` now selects the correct source file based on the `BUILD_DISPLAY_VERSION` CMake variable to avoid accidental inclusion of multiple versions in the same build.


See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

### Example Output

```bash
...
I (1629) example: MIPI DSI PHY Powered on
I (1629) example: Install MIPI DSI LCD control panel
I (1639) ili9881c: ID1: 0x98, ID2: 0x81, ID3: 0x5c
I (1779) example: Install MIPI DSI LCD data panel
I (1799) example: Initialize LVGL library
I (1799) example: Allocate separate LVGL draw buffers from PSRAM
I (1809) example: Use esp_timer as LVGL tick timer
I (1809) example: Create LVGL task
I (1809) example: Starting LVGL task
I (1919) example: Display LVGL Meter Widget
...
```

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
