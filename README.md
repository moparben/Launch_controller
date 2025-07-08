# Launch_controller# Launch Controller Project

This is the repository for your ESP32-based multi-pad model rocket launch controller.

---

## Features

- Real sensor reading (DHT11, thermistors, wind, current, CAN)
- Ignitor arming/disarming, safe firing, current feedback
- Web UI: Live dashboard, controls, settings, OTA update, log download
- CAN bus support (for expansion, remote nodes, etc.)
- Logging (SD card preferred, SPIFFS fallback)
- OTA-ready, async operation, modular structure

---

## Wiring Diagram

```mermaid
flowchart TD
  subgraph Base_Layer["Base Layer (Bottom Deck)"]
    Sensors[Left Board<br/>Sensors & Inputs<br/>(DHT11, Thermistors, Wind, CAN)]
    Logic[Center Board<br/>ESP32, Display, Web, Settings]
    Power[Right Board<br/>MOSFETs, ACS712,<br/>Fans, Heatsink]
  end

  %% External connections
  Battery[LiPo / Power Module]
  Ignitors[Ignitor Outputs]
  Fans[Fans (Rear/Top Exhaust)]
  Display[TFT Display<br/>Touch (Front Panel)]
  CAN[CAN Bus]
  SensorsExt[External Sensors]

  %% Connections
  Battery -- "Power (V+/GND)" --> Power
  Battery -- "Logic Power" --> Logic
  Power -- "Control Signals" --> Logic
  Power -- "Status Signals" --> Logic
  Power -- "Current Sense" --> Logic
  Power -- "Ignitor Wires" --> Ignitors
  Power -- "Fan Power" --> Fans
  Power -- "Heatsink Mount" --- Heatsink[Heatsink Exposed<br/>on Box Bottom]
  Logic -- "Sensor Bus" --> Sensors
  Sensors -- "Sensor Data" --> Logic
  Logic -- "Display SPI/I2C" --> Display
  Sensors -- "CAN Bus" --> CAN
  Sensors -- "External Sensors" --> SensorsExt
```

---

## Project Structure

- `src/` - Main code (Arduino/C++ style)
- `data/` - Web UI files (for ESP32 web server)
- `docs/` - Documentation, diagrams, and images

---

## Quick Start

1. Open `src/launch_controller.ino` in Arduino IDE or PlatformIO.
2. Copy `/data` to your ESP32's SPIFFS (for web UI).
3. Edit `config.h` for your WiFi and settings.
4. Upload and launch!

---
