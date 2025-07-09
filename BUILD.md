# Launch Controller Build Instructions

## Prerequisites

1. Install PlatformIO Core or Arduino IDE
2. Install required libraries:
   - ArduinoJson (6.21.3+)
   - AsyncTCP (1.1.1+)
   - ESPAsyncWebServer (1.2.3+)
   - DHT sensor library (1.4.4+)
   - Adafruit Unified Sensor (1.1.9+)
   - ESP32Servo (0.13.0+)
   - ESP32CAN (1.0.1+)

## Build Process

### Using PlatformIO
```bash
# Install dependencies
pio lib install

# Upload filesystem (SPIFFS)
pio run --target uploadfs

# Build and upload firmware
pio run --target upload

# Monitor serial output
pio device monitor
```

### Using Arduino IDE
1. Install ESP32 board support package
2. Install required libraries via Library Manager
3. Set board to "ESP32 Dev Module"
4. Upload SPIFFS data using ESP32 Sketch Data Upload tool
5. Compile and upload sketch

## Configuration

1. Edit `src/config.h` for your specific hardware configuration
2. Adjust pin assignments as needed
3. Configure WiFi settings
4. Set safety parameters

## Testing

The system includes comprehensive diagnostics accessible via:
- Serial monitor commands
- Web interface diagnostics tab
- `/api/diagnostics` endpoint

## First Run

1. Connect to ESP32 AP: "Launch_Controller_AP" 
2. Password: "RocketLaunch2024"
3. Navigate to 192.168.4.1
4. Configure system settings
5. Test hardware components
6. Run authorization and abort tests

## Validation Checklist

- [ ] All pin configurations correct
- [ ] No pin conflicts detected
- [ ] WiFi connectivity established
- [ ] Web interface accessible
- [ ] Sensor readings valid
- [ ] Ignitor continuity checks working
- [ ] Authorization system functional
- [ ] Abort system tested
- [ ] Safety checks operational
- [ ] Logging system working

The implementation is ready for deployment as version 3.5.0709.