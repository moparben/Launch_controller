# ESP32-CAM Launch Tracking System Documentation

## Overview

The ESP32-CAM Launch Tracking System is a comprehensive Arduino (.ino) implementation that provides real-time object tracking capabilities for launch monitoring applications. The system uses an ESP32-CAM module with servo-controlled pan/tilt mechanics to automatically track and predict object trajectories.

## Features

### Core Functionality
- **30fps Camera Streaming**: High-speed camera capture and processing
- **Servo Control**: Smooth pan and tilt movement with configurable speed
- **Object Detection**: Basic motion-based object detection algorithm
- **Auto-Tracking**: Automatic servo positioning to follow detected objects
- **Trajectory Prediction**: Linear prediction algorithm for future object positions
- **Manual Control**: Direct servo positioning via commands
- **Settings Management**: Persistent configuration storage in SPIFFS

### Communication Protocol
- **USB Serial**: 115200 baud JSON-based communication
- **Real-time Telemetry**: Continuous data streaming
- **Command Interface**: Both text and JSON command support
- **Status Reporting**: Comprehensive system health monitoring

### Advanced Features
- **Multiple Operating Modes**: Manual, Auto-tracking, Scan, and Calibration
- **Error Handling**: Comprehensive error detection and recovery
- **Logging System**: Debug and system logs with SPIFFS storage
- **Watchdog Protection**: Automatic recovery from system hangs
- **Memory Management**: Heap monitoring and optimization

## Hardware Requirements

### Essential Components
- **ESP32-CAM Module**: AI-Thinker ESP32-CAM or compatible
- **Servo Motors**: 2x standard servos (SG90 or similar)
- **Power Supply**: 5V/2A recommended for stable operation
- **MicroSD Card**: Optional, for extended logging (SPIFFS used as fallback)

### Pin Configuration
```
GPIO 2  - Pan Servo Control
GPIO 14 - Tilt Servo Control
GPIO 4  - Flash LED (built-in)
GPIO 33 - Status LED (external, optional)

Camera pins (ESP32-CAM AI-Thinker):
GPIO 0  - XCLK
GPIO 26 - SIOD (SDA)
GPIO 27 - SIOC (SCL)
GPIO 25 - VSYNC
GPIO 23 - HREF
GPIO 22 - PCLK
GPIO 21 - Y5
GPIO 19 - Y4
GPIO 18 - Y3
GPIO 5  - Y2
GPIO 36 - Y6
GPIO 39 - Y7
GPIO 34 - Y8
GPIO 35 - Y9
GPIO 32 - PWDN
```

### Power Considerations
- ESP32-CAM: ~200mA at 3.3V
- Servo motors: ~500mA each at 5V (under load)
- Total recommended: 5V/2A power supply
- Use proper voltage regulation for ESP32-CAM (3.3V)

## Software Architecture

### Main Components

#### 1. Camera System
```cpp
void setupCamera()           // Initialize ESP32-CAM
void captureAndProcessFrame() // 30fps frame capture
void performObjectDetection() // Motion-based detection
```

#### 2. Servo Control
```cpp
void setupServos()           // Initialize servo objects
void updateServos()          // Smooth movement control
void handleAutoTracking()    // Automatic positioning
```

#### 3. Communication Protocol
```cpp
void processSerialCommands() // Parse incoming commands
void handleJsonCommand()     // Process JSON commands
void sendTelemetryData()     // Real-time data output
```

#### 4. Tracking Engine
```cpp
void updateTrajectoryPrediction() // Linear prediction
void performScanPattern()    // Systematic scanning
void resetTracking()         // Clear tracking data
```

## Communication Protocol

### Text Commands
Simple text commands for basic operation:
```
status      - Show system status
reset       - Restart system
center      - Move servos to center
calibrate   - Start calibration sequence
scan        - Enable scanning mode
auto        - Enable auto-tracking
manual      - Switch to manual mode
diagnostics - Run system diagnostics
help        - Show command help
```

### JSON Commands

#### Set Position
```json
{
  "command": "set_position",
  "pan": 90,
  "tilt": 45
}
```

#### Change Mode
```json
{
  "command": "set_mode",
  "mode": "auto"
}
```

#### Update Settings
```json
{
  "command": "set_settings",
  "auto_tracking_enabled": true,
  "tracking_sensitivity": 1.5,
  "camera_quality": 10
}
```

#### Get System Information
```json
{
  "command": "get_status"
}
```

### Response Format

#### Status Response
```json
{
  "type": "status",
  "timestamp": 12345678,
  "system_ready": true,
  "uptime": 60000,
  "mode": "auto",
  "servo": {
    "pan_current": 90.0,
    "tilt_current": 45.0,
    "pan_target": 92.0,
    "tilt_target": 47.0
  },
  "camera": {
    "fps": 29.5,
    "avg_fps": 28.8,
    "total_frames": 1750,
    "frame_errors": 2
  },
  "tracking": {
    "object_detected": true,
    "object_x": 320,
    "object_y": 240,
    "confidence": 0.85,
    "last_seen": 12345670
  },
  "health": {
    "free_heap": 145678,
    "min_free_heap": 98432,
    "last_error": ""
  }
}
```

#### Telemetry Data
```json
{
  "type": "telemetry",
  "timestamp": 12345678,
  "pan_angle": 92.0,
  "tilt_angle": 47.0,
  "fps": 29.5,
  "frame_count": 1750,
  "tracked_object": {
    "x": 320,
    "y": 240,
    "width": 50,
    "height": 50,
    "confidence": 0.85
  },
  "trajectory": [
    {"x": 315, "y": 238, "t": 12345500},
    {"x": 318, "y": 239, "t": 12345600},
    {"x": 320, "y": 240, "t": 12345670}
  ]
}
```

## Configuration Options

### Settings Structure
```cpp
struct SystemSettings {
  bool trajectoryOverlay;      // Enable trajectory display
  bool windDataEnabled;        // Enable wind data integration
  bool autoTrackingEnabled;    // Enable automatic tracking
  int panServoOffset;          // Pan servo calibration offset
  int tiltServoOffset;         // Tilt servo calibration offset
  float trackingSensitivity;   // Tracking response sensitivity
  int cameraQuality;           // JPEG quality (0-63)
  bool debugLogging;           // Enable debug output
  String deviceName;           // Device identifier
};
```

### Default Values
- Trajectory Overlay: Enabled
- Wind Data: Disabled
- Auto-tracking: Enabled
- Servo Offsets: 0 degrees
- Tracking Sensitivity: 1.0
- Camera Quality: 10 (good balance)
- Debug Logging: Disabled
- Device Name: "ESP32-CAM-Tracker"

## Operating Modes

### 1. Manual Mode
- Direct servo control via commands
- No automatic movement
- Full user control

### 2. Auto-Tracking Mode
- Automatic object detection
- Servo positioning to follow objects
- Trajectory prediction
- Timeout-based object loss handling

### 3. Scan Mode
- Systematic pan/tilt scanning
- Covers full servo range
- Alternating horizontal/vertical patterns

### 4. Calibration Mode
- Automated servo testing
- Range verification
- Center position validation
- Movement smoothness check

## Installation and Setup

### 1. Hardware Assembly
1. Mount ESP32-CAM in housing
2. Connect servo motors to pan/tilt mechanism
3. Wire servos to GPIO pins 2 and 14
4. Connect 5V power supply
5. Optional: Add external status LED to GPIO 33

### 2. Software Installation
1. Install Arduino IDE or PlatformIO
2. Install ESP32 board package
3. Install required libraries:
   - ArduinoJson (6.21.3+)
   - ESP32Servo (0.13.0+)
   - ESP32_Camera (2.0.4+)
4. Upload `esp32_cam_tracker.ino` to ESP32-CAM

### 3. Initial Configuration
1. Connect to ESP32-CAM via USB serial (115200 baud)
2. Send "calibrate" command to test servo movement
3. Adjust servo offsets if needed
4. Test camera functionality with "status" command
5. Configure tracking sensitivity as required

## Troubleshooting

### Common Issues

#### Camera Initialization Failed
- Check power supply (5V/2A minimum)
- Verify camera module connection
- Reset ESP32-CAM and retry

#### Servo Movement Issues
- Check servo power (5V required)
- Verify GPIO pin connections
- Test with "center" command
- Adjust servo offsets in settings

#### Poor Tracking Performance
- Increase tracking sensitivity
- Improve lighting conditions
- Check camera quality settings
- Verify object contrast

#### Communication Problems
- Check serial connection (115200 baud)
- Verify JSON command format
- Check for message buffer overflow
- Reset system if needed

### Debug Commands
```
diagnostics  - Full system diagnostic
status       - Current system state
reset        - System restart
help         - Command reference
```

### Log Analysis
- System logs stored in SPIFFS
- Access via serial monitor
- Debug logging can be enabled in settings
- Automatic log rotation at 100KB

## Performance Optimization

### Camera Settings
- Frame size: VGA (640x480) recommended
- Quality: 10 for good balance
- Frame rate: 30fps target
- Buffer count: 2 for smooth operation

### Servo Control
- Update rate: 20ms (50Hz)
- Movement speed: 2°/update for smoothness
- Range limits: 0-180° with safety margins

### Memory Management
- JSON buffer: 2KB for commands
- Heap monitoring: Continuous
- Watchdog: 10-second timeout
- Log file: 100KB maximum

## Future Enhancements

### Planned Features
- Advanced computer vision algorithms
- Multi-object tracking
- Kalman filter for trajectory prediction
- WiFi streaming capabilities
- Web-based control interface
- Integration with external sensors

### Extension Points
- Additional GPIO pins available
- I2C/SPI expansion possible
- External memory support
- CAN bus integration potential
- Custom protocol development

## API Reference

### Complete Command List

#### Text Commands
| Command | Description | Parameters |
|---------|-------------|------------|
| status | Show system status | None |
| reset | Restart system | None |
| center | Center servos | None |
| calibrate | Start calibration | None |
| scan | Enable scan mode | None |
| auto | Enable auto-tracking | None |
| manual | Enable manual mode | None |
| diagnostics | Run diagnostics | None |
| help | Show help | None |

#### JSON Commands
| Command | Description | Required Fields | Optional Fields |
|---------|-------------|----------------|-----------------|
| set_position | Set servo angles | command | pan, tilt |
| set_mode | Change operating mode | command, mode | None |
| set_settings | Update configuration | command | [various settings] |
| get_settings | Get current settings | command | None |
| get_status | Get system status | command | None |
| get_telemetry | Get telemetry data | command | None |
| reset_tracking | Clear tracking data | command | None |
| save_settings | Save to SPIFFS | command | None |
| load_settings | Load from SPIFFS | command | None |

## License and Support

This ESP32-CAM Launch Tracking System is part of the Launch Controller project and follows the same licensing terms. For support, issues, and feature requests, please refer to the project repository.

**Version**: 1.0.0  
**Build Date**: 2024-07-27  
**Compatibility**: ESP32-CAM (AI-Thinker), Arduino IDE, PlatformIO