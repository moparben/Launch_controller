# ESP32-CAM Tracker Example Usage

## Quick Start Guide

### 1. Flash the Firmware
Upload `esp32_cam_tracker.ino` to your ESP32-CAM module using Arduino IDE or PlatformIO.

### 2. Connect via Serial
Open serial monitor at 115200 baud and you should see:
```
==========================================
ESP32-CAM Launch Tracking System v1.0.0
Build: Jul 27 2024 13:44:00
==========================================
[1000] INFO: SPIFFS initialized successfully
[1200] INFO: Initializing camera...
[2500] INFO: Camera initialized successfully
[2600] INFO: Initializing servos...
[3100] INFO: Servo test completed successfully
[3200] INFO: Initializing communication...
[3300] INFO: Communication system initialized
[3400] INFO: === SYSTEM READY ===
[3500] INFO: Setup complete. Ready for operation.
```

### 3. Basic Commands

#### Text Commands
```
status      # Show current system status
center      # Move servos to center position
auto        # Enable auto-tracking mode
manual      # Switch to manual mode
help        # Show all available commands
```

#### JSON Commands
```json
{"command":"set_position","pan":45,"tilt":90}
{"command":"set_mode","mode":"scan"}
{"command":"get_status"}
```

### 4. Example Communication Session

```
> help
ESP32-CAM Launch Tracking System v1.0.0
Available text commands:
  status      - Show system status
  center      - Move servos to center position
  calibrate   - Start calibration sequence
  ...

> status
{"type":"status","timestamp":15423,"system_ready":true,"uptime":15423,"mode":"manual","servo":{"pan_current":90.0,"tilt_current":90.0,"pan_target":90.0,"tilt_target":90.0},"camera":{"fps":29.2,"avg_fps":28.8,"total_frames":456,"frame_errors":0},"tracking":{"object_detected":false},"health":{"free_heap":234567,"min_free_heap":198432,"last_error":""}}

> {"command":"set_position","pan":45,"tilt":135}
{"type":"response","status":"success","message":"Position command received","timestamp":16234}

> auto
{"type":"response","status":"success","message":"Mode set to: auto","timestamp":17001}
```

### 5. Tracking Example

When an object is detected, telemetry data includes tracking information:

```json
{
  "type": "telemetry",
  "timestamp": 25000,
  "pan_angle": 67.5,
  "tilt_angle": 102.3,
  "fps": 29.8,
  "frame_count": 745,
  "tracked_object": {
    "x": 384,
    "y": 267,
    "width": 45,
    "height": 38,
    "confidence": 0.73
  },
  "trajectory": [
    {"x": 375, "y": 260, "t": 24500},
    {"x": 380, "y": 264, "t": 24750},
    {"x": 384, "y": 267, "t": 25000}
  ]
}
```

### 6. Settings Configuration

```json
{
  "command": "set_settings",
  "auto_tracking_enabled": true,
  "tracking_sensitivity": 1.2,
  "camera_quality": 8,
  "trajectory_overlay": true,
  "debug_logging": true
}
```

### 7. Calibration Sequence

```
> calibrate
[30000] INFO: Starting calibration sequence
[30000] INFO: Calibration: Center position
[32000] INFO: Calibration: Pan left
[34000] INFO: Calibration: Pan right
[36000] INFO: Calibration: Pan center, Tilt up
[38000] INFO: Calibration: Tilt down
[40000] INFO: Calibration: Return to center
[42000] INFO: Calibration complete
```

### 8. Troubleshooting

#### Camera not working:
```
> diagnostics
[45000] INFO: === SYSTEM DIAGNOSTICS ===
[45000] INFO: Camera FPS: 0.0
[45000] INFO: Frame errors: 15
...
```

#### Reset system:
```
> reset
[System restarts...]
```

#### Manual servo control:
```
> {"command":"set_mode","mode":"manual"}
> {"command":"set_position","pan":0,"tilt":180}
> {"command":"set_position","pan":180,"tilt":0}
> center
```

## Integration Examples

### Python Control Script
```python
import serial
import json
import time

# Connect to ESP32-CAM
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)

# Enable auto-tracking
command = {"command": "set_mode", "mode": "auto"}
ser.write((json.dumps(command) + '\n').encode())

# Monitor telemetry
while True:
    if ser.in_waiting:
        line = ser.readline().decode().strip()
        try:
            data = json.loads(line)
            if data.get('type') == 'telemetry':
                print(f"Pan: {data['pan_angle']:.1f}°, Tilt: {data['tilt_angle']:.1f}°")
                if 'tracked_object' in data:
                    obj = data['tracked_object']
                    print(f"Object at ({obj['x']}, {obj['y']}) confidence: {obj['confidence']:.2f}")
        except json.JSONDecodeError:
            print(f"Log: {line}")
    time.sleep(0.1)
```

### Arduino/ESP32 Controller
```cpp
// Example code to control ESP32-CAM tracker from another ESP32
#include <HardwareSerial.h>
#include <ArduinoJson.h>

HardwareSerial tracker(2); // Use Serial2

void setup() {
  Serial.begin(115200);
  tracker.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  // Enable auto-tracking
  tracker.println("{\"command\":\"set_mode\",\"mode\":\"auto\"}");
}

void loop() {
  // Read tracker data
  if (tracker.available()) {
    String response = tracker.readStringUntil('\n');
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      if (doc["type"] == "telemetry") {
        float pan = doc["pan_angle"];
        float tilt = doc["tilt_angle"];
        Serial.printf("Tracker position: Pan=%.1f°, Tilt=%.1f°\n", pan, tilt);
      }
    }
  }
  
  delay(100);
}
```

## Performance Notes

- **Camera FPS**: Typically achieves 28-30fps in good lighting
- **Servo Response**: ~50ms for small movements, ~2s for full range
- **Tracking Latency**: <100ms from detection to servo command
- **Memory Usage**: ~150KB free heap during normal operation
- **Power Consumption**: ~1.2A at 5V under full operation

## Hardware Tips

- Use quality servo motors for smooth tracking
- Ensure adequate power supply (5V/2A minimum)
- Add heat sinks for extended operation
- Use proper mounting to minimize vibration
- Consider lens quality for better object detection

## Software Customization

The code is designed to be modular and extensible:

- **Object Detection**: Enhance `performObjectDetection()` with computer vision libraries
- **Trajectory Prediction**: Improve algorithm in `updateTrajectoryPrediction()`
- **Communication**: Add WiFi/Bluetooth in `setupCommunication()`
- **UI**: Implement web interface for remote control
- **Sensors**: Add IMU, GPS, or other sensors for advanced tracking