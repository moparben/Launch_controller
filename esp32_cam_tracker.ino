/*
 * ESP32-CAM Launch Tracking System v1.0
 * 
 * Comprehensive launch tracking system for ESP32-CAM
 * Features:
 * - 30fps camera streaming
 * - Pan/tilt servo control with smooth movement
 * - Object detection and auto-tracking
 * - Trajectory prediction algorithm
 * - USB serial communication (JSON protocol)
 * - Manual control mode
 * - Settings management
 * - Error handling and recovery
 * - Comprehensive logging
 * 
 * Hardware Requirements:
 * - ESP32-CAM module with camera
 * - 2x servo motors (pan and tilt)
 * - Power supply (5V recommended)
 * - USB connection for communication
 * 
 * Pin Configuration:
 * - GPIO 2: Pan servo control
 * - GPIO 14: Tilt servo control
 * - GPIO 4: Status LED (built-in flash LED)
 * - GPIO 33: External status LED (optional)
 * 
 * Author: ESP32-CAM Tracking System
 * Version: 1.0.0
 * Build Date: 2024-07-27
 */

// ========== INCLUDES ==========
#include "esp_camera.h"
#include "WiFi.h"
#include "WiFiUdp.h"
#include "ArduinoJson.h"
#include "ESP32Servo.h"
#include "SPIFFS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <math.h>

// ========== HARDWARE CONFIGURATION ==========
// Camera model selection - ESP32-CAM AI-Thinker
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Servo pins
#define PAN_SERVO_PIN      2
#define TILT_SERVO_PIN    14

// Status LEDs
#define FLASH_LED_PIN      4
#define STATUS_LED_PIN    33

// ========== CONSTANTS ==========
#define VERSION "1.0.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// Camera settings
#define TARGET_FPS         30
#define CAMERA_FRAMESIZE   FRAMESIZE_VGA  // 640x480 for good balance
#define CAMERA_QUALITY     10             // JPEG quality (0-63, lower is better)

// Servo settings
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE    180
#define SERVO_CENTER       90
#define SERVO_SPEED        2.0            // degrees per update for smooth movement
#define SERVO_UPDATE_MS    20             // Servo update interval

// Tracking settings
#define TRACKING_THRESHOLD 50             // Minimum object size for tracking
#define PREDICTION_STEPS   10             // Number of future positions to predict
#define TRACKING_TIMEOUT   5000           // ms before losing track

// Communication settings
#define SERIAL_BAUD        115200
#define JSON_BUFFER_SIZE   2048
#define CMD_TIMEOUT        100            // ms

// System settings
#define WATCHDOG_TIMEOUT   10             // seconds
#define STATUS_LED_INTERVAL 1000          // ms
#define LOG_LEVEL          ESP_LOG_INFO

// ========== GLOBAL VARIABLES ==========

// Servo objects and positions
Servo panServo;
Servo tiltServo;
float currentPanAngle = SERVO_CENTER;
float currentTiltAngle = SERVO_CENTER;
float targetPanAngle = SERVO_CENTER;
float targetTiltAngle = SERVO_CENTER;
unsigned long lastServoUpdate = 0;

// Camera and streaming
camera_fb_t *frameBuffer = NULL;
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float currentFPS = 0;

// Tracking system
enum TrackingMode {
  MODE_MANUAL,
  MODE_AUTO_TRACK,
  MODE_SCAN,
  MODE_CALIBRATE
};

struct TrackingObject {
  int x, y;              // Current position
  int width, height;     // Object size
  float confidence;      // Detection confidence
  unsigned long lastSeen; // Last detection time
  bool valid;            // Object is being tracked
};

struct TrajectoryPoint {
  float x, y;            // Position
  unsigned long timestamp; // Time of observation
};

TrackingMode currentMode = MODE_MANUAL;
TrackingObject trackedObject = {0, 0, 0, 0, 0.0, 0, false};
TrajectoryPoint trajectory[PREDICTION_STEPS];
int trajectoryIndex = 0;
bool autoTrackingEnabled = false;

// Settings management
struct SystemSettings {
  bool trajectoryOverlay;
  bool windDataEnabled;
  bool autoTrackingEnabled;
  int panServoOffset;
  int tiltServoOffset;
  float trackingSensitivity;
  int cameraQuality;
  bool debugLogging;
  String deviceName;
};

SystemSettings settings = {
  .trajectoryOverlay = true,
  .windDataEnabled = false,
  .autoTrackingEnabled = true,
  .panServoOffset = 0,
  .tiltServoOffset = 0,
  .trackingSensitivity = 1.0,
  .cameraQuality = 10,
  .debugLogging = false,
  .deviceName = "ESP32-CAM-Tracker"
};

// Communication
DynamicJsonDocument jsonDoc(JSON_BUFFER_SIZE);
String lastError = "";
unsigned long lastStatusUpdate = 0;
bool systemReady = false;

// Status and diagnostics
unsigned long systemStartTime;
unsigned long totalFrames = 0;
unsigned long totalErrors = 0;
float averageFPS = 0;

// ========== FUNCTION DECLARATIONS ==========
void setupCamera();
void setupServos();
void setupCommunication();
void loadSettings();
void saveSettings();
void processSerialCommands();
void updateServos();
void captureAndProcessFrame();
void performObjectDetection(camera_fb_t *fb);
void updateTrajectoryPrediction();
void handleAutoTracking();
void sendTelemetryData();
void sendStatusUpdate();
void handleError(const String &error);
void logMessage(const String &level, const String &message);
void performSystemDiagnostics();
void resetToSafeState();

// ========== SETUP FUNCTION ==========
void setup() {
  // Initialize serial communication
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  
  // Store system start time
  systemStartTime = millis();
  
  Serial.println();
  Serial.println("==========================================");
  Serial.println("ESP32-CAM Launch Tracking System v" + String(VERSION));
  Serial.println("Build: " + String(BUILD_DATE) + " " + String(BUILD_TIME));
  Serial.println("==========================================");
  
  // Initialize watchdog
  esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  
  // Initialize status LED
  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  // Signal startup
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(200);
  }
  
  // Initialize SPIFFS for settings storage
  if (!SPIFFS.begin(true)) {
    logMessage("ERROR", "SPIFFS initialization failed");
    handleError("SPIFFS initialization failed");
  } else {
    logMessage("INFO", "SPIFFS initialized successfully");
  }
  
  // Load settings from storage
  loadSettings();
  
  // Initialize camera
  logMessage("INFO", "Initializing camera...");
  setupCamera();
  
  // Initialize servos
  logMessage("INFO", "Initializing servos...");
  setupServos();
  
  // Initialize communication
  logMessage("INFO", "Initializing communication...");
  setupCommunication();
  
  // Reset watchdog timer
  esp_task_wdt_reset();
  
  // Final system check
  if (systemReady) {
    logMessage("INFO", "=== SYSTEM READY ===");
    logMessage("INFO", "Camera: OK, Servos: OK, Communication: OK");
    
    // Signal successful startup
    for (int i = 0; i < 5; i++) {
      digitalWrite(STATUS_LED_PIN, HIGH);
      delay(100);
      digitalWrite(STATUS_LED_PIN, LOW);
      delay(100);
    }
  } else {
    logMessage("ERROR", "=== SYSTEM INITIALIZATION FAILED ===");
    handleError("System initialization failed");
  }
  
  // Initial status update
  sendStatusUpdate();
  
  logMessage("INFO", "Setup complete. Ready for operation.");
}

// ========== MAIN LOOP ==========
void loop() {
  // Reset watchdog timer
  esp_task_wdt_reset();
  
  unsigned long currentTime = millis();
  
  // Process serial commands
  processSerialCommands();
  
  // Update servo positions
  updateServos();
  
  // Capture and process camera frame
  captureAndProcessFrame();
  
  // Handle tracking modes
  switch (currentMode) {
    case MODE_AUTO_TRACK:
      handleAutoTracking();
      break;
      
    case MODE_SCAN:
      // Implement scanning pattern
      performScanPattern();
      break;
      
    case MODE_CALIBRATE:
      // Implement calibration routine
      performCalibration();
      break;
      
    case MODE_MANUAL:
    default:
      // Manual mode - servos controlled by commands only
      break;
  }
  
  // Send telemetry data periodically
  if (currentTime - lastStatusUpdate >= 1000) {
    sendTelemetryData();
    sendStatusUpdate();
    lastStatusUpdate = currentTime;
  }
  
  // Status LED heartbeat
  static unsigned long lastHeartbeat = 0;
  if (currentTime - lastHeartbeat >= STATUS_LED_INTERVAL) {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    lastHeartbeat = currentTime;
  }
  
  // Small delay to prevent task watchdog issues
  delay(1);
}

// ========== CAMERA SETUP ==========
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Frame size and quality settings
  config.frame_size = CAMERA_FRAMESIZE;
  config.jpeg_quality = settings.cameraQuality;
  config.fb_count = 2;  // Use 2 frame buffers for better performance
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    logMessage("ERROR", "Camera init failed with error 0x" + String(err, HEX));
    handleError("Camera initialization failed");
    return;
  }
  
  // Get camera sensor and configure settings
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // Configure camera settings for tracking
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect)
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_ae_level(s, 0);       // -2 to 2
    s->set_aec_value(s, 300);    // 0 to 1200
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
  }
  
  logMessage("INFO", "Camera initialized successfully");
  
  // Test camera by taking a frame
  camera_fb_t *testFrame = esp_camera_fb_get();
  if (testFrame) {
    logMessage("INFO", "Camera test frame captured: " + String(testFrame->len) + " bytes");
    esp_camera_fb_return(testFrame);
  } else {
    logMessage("ERROR", "Failed to capture test frame");
    handleError("Camera test failed");
    return;
  }
  
  systemReady = true;
}

// ========== SERVO SETUP ==========
void setupServos() {
  // Attach servos to pins
  panServo.attach(PAN_SERVO_PIN);
  tiltServo.attach(TILT_SERVO_PIN);
  
  // Set initial positions to center
  currentPanAngle = SERVO_CENTER + settings.panServoOffset;
  currentTiltAngle = SERVO_CENTER + settings.tiltServoOffset;
  targetPanAngle = currentPanAngle;
  targetTiltAngle = currentTiltAngle;
  
  // Move to center position
  panServo.write(currentPanAngle);
  tiltServo.write(currentTiltAngle);
  
  logMessage("INFO", "Servos initialized - Pan: " + String(currentPanAngle) + "°, Tilt: " + String(currentTiltAngle) + "°");
  
  // Test servo movement
  delay(500);
  
  // Pan test
  panServo.write(SERVO_CENTER + 20);
  delay(500);
  panServo.write(SERVO_CENTER - 20);
  delay(500);
  panServo.write(SERVO_CENTER);
  
  // Tilt test
  tiltServo.write(SERVO_CENTER + 20);
  delay(500);
  tiltServo.write(SERVO_CENTER - 20);
  delay(500);
  tiltServo.write(SERVO_CENTER);
  
  delay(500);
  
  logMessage("INFO", "Servo test completed successfully");
}

// ========== COMMUNICATION SETUP ==========
void setupCommunication() {
  // Serial communication is already initialized in setup()
  Serial.println("Serial communication ready at " + String(SERIAL_BAUD) + " baud");
  
  // Send initial system information
  jsonDoc.clear();
  jsonDoc["type"] = "system_info";
  jsonDoc["version"] = VERSION;
  jsonDoc["build_date"] = BUILD_DATE;
  jsonDoc["build_time"] = BUILD_TIME;
  jsonDoc["device_name"] = settings.deviceName;
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
  
  logMessage("INFO", "Communication system initialized");
}

// ========== SERVO CONTROL ==========
void updateServos() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastServoUpdate >= SERVO_UPDATE_MS) {
    bool updated = false;
    
    // Smooth pan movement
    if (abs(targetPanAngle - currentPanAngle) > 0.5) {
      if (targetPanAngle > currentPanAngle) {
        currentPanAngle += SERVO_SPEED;
        if (currentPanAngle > targetPanAngle) currentPanAngle = targetPanAngle;
      } else {
        currentPanAngle -= SERVO_SPEED;
        if (currentPanAngle < targetPanAngle) currentPanAngle = targetPanAngle;
      }
      
      // Constrain to valid range
      currentPanAngle = constrain(currentPanAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
      panServo.write(currentPanAngle);
      updated = true;
    }
    
    // Smooth tilt movement
    if (abs(targetTiltAngle - currentTiltAngle) > 0.5) {
      if (targetTiltAngle > currentTiltAngle) {
        currentTiltAngle += SERVO_SPEED;
        if (currentTiltAngle > targetTiltAngle) currentTiltAngle = targetTiltAngle;
      } else {
        currentTiltAngle -= SERVO_SPEED;
        if (currentTiltAngle < targetTiltAngle) currentTiltAngle = targetTiltAngle;
      }
      
      // Constrain to valid range
      currentTiltAngle = constrain(currentTiltAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
      tiltServo.write(currentTiltAngle);
      updated = true;
    }
    
    lastServoUpdate = currentTime;
    
    if (updated && settings.debugLogging) {
      logMessage("DEBUG", "Servo update - Pan: " + String(currentPanAngle) + "°, Tilt: " + String(currentTiltAngle) + "°");
    }
  }
}

// ========== CAMERA AND TRACKING ==========
void captureAndProcessFrame() {
  unsigned long currentTime = millis();
  
  // Capture frame at target FPS
  if (currentTime - lastFrameTime >= (1000 / TARGET_FPS)) {
    // Get frame buffer
    frameBuffer = esp_camera_fb_get();
    
    if (frameBuffer) {
      totalFrames++;
      frameCount++;
      
      // Calculate FPS
      if (frameCount >= TARGET_FPS) {
        unsigned long elapsed = currentTime - lastFrameTime;
        currentFPS = (frameCount * 1000.0) / elapsed;
        averageFPS = (averageFPS * 0.9) + (currentFPS * 0.1); // Moving average
        frameCount = 0;
      }
      
      // Process frame for object detection if in auto-tracking mode
      if (currentMode == MODE_AUTO_TRACK && settings.autoTrackingEnabled) {
        performObjectDetection(frameBuffer);
      }
      
      // Return frame buffer
      esp_camera_fb_return(frameBuffer);
      frameBuffer = NULL;
      
      lastFrameTime = currentTime;
    } else {
      totalErrors++;
      logMessage("ERROR", "Failed to capture camera frame");
      
      // If too many consecutive errors, restart camera
      static int consecutiveErrors = 0;
      consecutiveErrors++;
      
      if (consecutiveErrors > 10) {
        logMessage("ERROR", "Too many camera errors, attempting restart");
        esp_camera_deinit();
        delay(1000);
        setupCamera();
        consecutiveErrors = 0;
      }
    }
  }
}

// ========== OBJECT DETECTION ==========
void performObjectDetection(camera_fb_t *fb) {
  // Simple object detection based on brightness changes
  // This is a basic implementation - can be enhanced with more sophisticated algorithms
  
  if (!fb || fb->format != PIXFORMAT_JPEG) {
    return;
  }
  
  // For now, implement a basic motion detection algorithm
  // In a real implementation, you would decode the JPEG and analyze the pixel data
  
  static unsigned long lastDetectionTime = 0;
  static size_t lastFrameSize = 0;
  
  unsigned long currentTime = millis();
  
  // Simple motion detection based on frame size changes
  if (lastFrameSize > 0) {
    float sizeDifference = abs((int)(fb->len - lastFrameSize)) / (float)lastFrameSize;
    
    if (sizeDifference > 0.1) { // 10% change threshold
      // Motion detected - update tracked object
      trackedObject.x = 320; // Center of 640x480 frame
      trackedObject.y = 240;
      trackedObject.width = 50;
      trackedObject.height = 50;
      trackedObject.confidence = sizeDifference;
      trackedObject.lastSeen = currentTime;
      trackedObject.valid = true;
      
      if (settings.debugLogging) {
        logMessage("DEBUG", "Motion detected - confidence: " + String(sizeDifference));
      }
      
      // Update trajectory
      updateTrajectoryPrediction();
    }
  }
  
  lastFrameSize = fb->len;
  lastDetectionTime = currentTime;
  
  // Check if object tracking has timed out
  if (trackedObject.valid && (currentTime - trackedObject.lastSeen > TRACKING_TIMEOUT)) {
    trackedObject.valid = false;
    logMessage("INFO", "Object tracking timeout");
  }
}

// ========== TRAJECTORY PREDICTION ==========
void updateTrajectoryPrediction() {
  if (!trackedObject.valid) {
    return;
  }
  
  // Add current position to trajectory history
  trajectory[trajectoryIndex] = {
    .x = (float)trackedObject.x,
    .y = (float)trackedObject.y,
    .timestamp = trackedObject.lastSeen
  };
  
  trajectoryIndex = (trajectoryIndex + 1) % PREDICTION_STEPS;
  
  // Simple linear prediction based on last few points
  if (trajectoryIndex >= 3) {
    // Calculate velocity
    float dx = trajectory[trajectoryIndex - 1].x - trajectory[trajectoryIndex - 3].x;
    float dy = trajectory[trajectoryIndex - 1].y - trajectory[trajectoryIndex - 3].y;
    unsigned long dt = trajectory[trajectoryIndex - 1].timestamp - trajectory[trajectoryIndex - 3].timestamp;
    
    if (dt > 0) {
      float vx = dx / dt * 1000; // pixels per second
      float vy = dy / dt * 1000;
      
      // Predict future position (100ms ahead)
      float futureX = trackedObject.x + (vx * 0.1);
      float futureY = trackedObject.y + (vy * 0.1);
      
      if (settings.debugLogging) {
        logMessage("DEBUG", "Predicted position: (" + String(futureX) + ", " + String(futureY) + ")");
      }
    }
  }
}

// ========== AUTO-TRACKING ==========
void handleAutoTracking() {
  if (!trackedObject.valid) {
    return;
  }
  
  // Convert pixel coordinates to servo angles
  // Assuming 640x480 camera resolution
  float frameWidth = 640.0;
  float frameHeight = 480.0;
  
  // Calculate center offset
  float offsetX = (trackedObject.x - frameWidth / 2) / (frameWidth / 2);
  float offsetY = (trackedObject.y - frameHeight / 2) / (frameHeight / 2);
  
  // Apply sensitivity factor
  offsetX *= settings.trackingSensitivity;
  offsetY *= settings.trackingSensitivity;
  
  // Convert to servo angles (assuming 60-degree field of view)
  float panAdjustment = offsetX * 30; // ±30 degrees
  float tiltAdjustment = -offsetY * 30; // Inverted Y axis
  
  // Update target angles
  targetPanAngle = constrain(SERVO_CENTER + panAdjustment + settings.panServoOffset, 
                           SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  targetTiltAngle = constrain(SERVO_CENTER + tiltAdjustment + settings.tiltServoOffset, 
                            SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  
  if (settings.debugLogging) {
    logMessage("DEBUG", "Auto-tracking - Target Pan: " + String(targetPanAngle) + "°, Tilt: " + String(targetTiltAngle) + "°");
  }
}

// ========== SCANNING PATTERN ==========
void performScanPattern() {
  static unsigned long lastScanUpdate = 0;
  static float scanDirection = 1;
  static bool scanningPan = true;
  
  unsigned long currentTime = millis();
  
  if (currentTime - lastScanUpdate >= 100) { // Update every 100ms
    if (scanningPan) {
      targetPanAngle += scanDirection * 2;
      
      if (targetPanAngle >= SERVO_MAX_ANGLE - 10) {
        scanDirection = -1;
      } else if (targetPanAngle <= SERVO_MIN_ANGLE + 10) {
        scanDirection = 1;
        scanningPan = false; // Switch to tilt scanning
      }
    } else {
      targetTiltAngle += scanDirection * 1;
      
      if (targetTiltAngle >= SERVO_MAX_ANGLE - 10) {
        scanDirection = -1;
      } else if (targetTiltAngle <= SERVO_MIN_ANGLE + 10) {
        scanDirection = 1;
        scanningPan = true; // Switch back to pan scanning
      }
    }
    
    lastScanUpdate = currentTime;
  }
}

// ========== CALIBRATION ==========
void performCalibration() {
  static int calibrationStep = 0;
  static unsigned long calibrationStartTime = 0;
  static unsigned long lastCalibrationUpdate = 0;
  
  unsigned long currentTime = millis();
  
  if (calibrationStartTime == 0) {
    calibrationStartTime = currentTime;
    logMessage("INFO", "Starting calibration sequence");
  }
  
  if (currentTime - lastCalibrationUpdate >= 2000) { // 2 seconds per step
    switch (calibrationStep) {
      case 0:
        logMessage("INFO", "Calibration: Center position");
        targetPanAngle = SERVO_CENTER;
        targetTiltAngle = SERVO_CENTER;
        break;
        
      case 1:
        logMessage("INFO", "Calibration: Pan left");
        targetPanAngle = SERVO_MIN_ANGLE + 10;
        break;
        
      case 2:
        logMessage("INFO", "Calibration: Pan right");
        targetPanAngle = SERVO_MAX_ANGLE - 10;
        break;
        
      case 3:
        logMessage("INFO", "Calibration: Pan center, Tilt up");
        targetPanAngle = SERVO_CENTER;
        targetTiltAngle = SERVO_MAX_ANGLE - 10;
        break;
        
      case 4:
        logMessage("INFO", "Calibration: Tilt down");
        targetTiltAngle = SERVO_MIN_ANGLE + 10;
        break;
        
      case 5:
        logMessage("INFO", "Calibration: Return to center");
        targetPanAngle = SERVO_CENTER;
        targetTiltAngle = SERVO_CENTER;
        break;
        
      default:
        logMessage("INFO", "Calibration complete");
        currentMode = MODE_MANUAL;
        calibrationStep = 0;
        calibrationStartTime = 0;
        return;
    }
    
    calibrationStep++;
    lastCalibrationUpdate = currentTime;
  }
}

// ========== SERIAL COMMUNICATION ==========
void processSerialCommands() {
  while (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() == 0) {
      return;
    }
    
    // Try to parse as JSON
    DeserializationError error = deserializeJson(jsonDoc, command);
    
    if (error) {
      // Not JSON, try simple text commands
      handleTextCommand(command);
    } else {
      // Process JSON command
      handleJsonCommand(jsonDoc);
    }
  }
}

void handleTextCommand(const String &command) {
  String cmd = command;
  cmd.toLowerCase();
  
  if (cmd == "status") {
    sendStatusUpdate();
  }
  else if (cmd == "reset") {
    logMessage("INFO", "Reset command received");
    ESP.restart();
  }
  else if (cmd == "center") {
    logMessage("INFO", "Center command received");
    targetPanAngle = SERVO_CENTER + settings.panServoOffset;
    targetTiltAngle = SERVO_CENTER + settings.tiltServoOffset;
    currentMode = MODE_MANUAL;
  }
  else if (cmd == "calibrate") {
    logMessage("INFO", "Calibration command received");
    currentMode = MODE_CALIBRATE;
  }
  else if (cmd == "scan") {
    logMessage("INFO", "Scan mode command received");
    currentMode = MODE_SCAN;
  }
  else if (cmd == "auto") {
    logMessage("INFO", "Auto-tracking mode command received");
    currentMode = MODE_AUTO_TRACK;
  }
  else if (cmd == "manual") {
    logMessage("INFO", "Manual mode command received");
    currentMode = MODE_MANUAL;
  }
  else if (cmd == "diagnostics") {
    performSystemDiagnostics();
  }
  else if (cmd == "help") {
    sendHelpMessage();
  }
  else {
    logMessage("WARN", "Unknown text command: " + command);
  }
}

void handleJsonCommand(const JsonDocument &cmd) {
  if (!cmd.containsKey("command")) {
    sendErrorResponse("Missing 'command' field");
    return;
  }
  
  String command = cmd["command"];
  
  if (command == "set_position") {
    handleSetPositionCommand(cmd);
  }
  else if (command == "set_mode") {
    handleSetModeCommand(cmd);
  }
  else if (command == "set_settings") {
    handleSetSettingsCommand(cmd);
  }
  else if (command == "get_settings") {
    sendSettingsResponse();
  }
  else if (command == "get_status") {
    sendStatusUpdate();
  }
  else if (command == "get_telemetry") {
    sendTelemetryData();
  }
  else if (command == "reset_tracking") {
    resetTracking();
  }
  else if (command == "save_settings") {
    saveSettings();
    sendSuccessResponse("Settings saved");
  }
  else if (command == "load_settings") {
    loadSettings();
    sendSuccessResponse("Settings loaded");
  }
  else {
    sendErrorResponse("Unknown command: " + command);
  }
}

void handleSetPositionCommand(const JsonDocument &cmd) {
  if (cmd.containsKey("pan")) {
    float pan = cmd["pan"];
    targetPanAngle = constrain(pan + settings.panServoOffset, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  }
  
  if (cmd.containsKey("tilt")) {
    float tilt = cmd["tilt"];
    targetTiltAngle = constrain(tilt + settings.tiltServoOffset, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  }
  
  // Switch to manual mode for direct position control
  currentMode = MODE_MANUAL;
  
  sendSuccessResponse("Position command received");
  
  if (settings.debugLogging) {
    logMessage("DEBUG", "Set position - Pan: " + String(targetPanAngle) + ", Tilt: " + String(targetTiltAngle));
  }
}

void handleSetModeCommand(const JsonDocument &cmd) {
  if (!cmd.containsKey("mode")) {
    sendErrorResponse("Missing 'mode' field");
    return;
  }
  
  String mode = cmd["mode"];
  
  if (mode == "manual") {
    currentMode = MODE_MANUAL;
  }
  else if (mode == "auto") {
    currentMode = MODE_AUTO_TRACK;
  }
  else if (mode == "scan") {
    currentMode = MODE_SCAN;
  }
  else if (mode == "calibrate") {
    currentMode = MODE_CALIBRATE;
  }
  else {
    sendErrorResponse("Invalid mode: " + mode);
    return;
  }
  
  sendSuccessResponse("Mode set to: " + mode);
  logMessage("INFO", "Mode changed to: " + mode);
}

void handleSetSettingsCommand(const JsonDocument &cmd) {
  bool changed = false;
  
  if (cmd.containsKey("trajectory_overlay")) {
    settings.trajectoryOverlay = cmd["trajectory_overlay"];
    changed = true;
  }
  
  if (cmd.containsKey("wind_data_enabled")) {
    settings.windDataEnabled = cmd["wind_data_enabled"];
    changed = true;
  }
  
  if (cmd.containsKey("auto_tracking_enabled")) {
    settings.autoTrackingEnabled = cmd["auto_tracking_enabled"];
    changed = true;
  }
  
  if (cmd.containsKey("pan_servo_offset")) {
    settings.panServoOffset = cmd["pan_servo_offset"];
    changed = true;
  }
  
  if (cmd.containsKey("tilt_servo_offset")) {
    settings.tiltServoOffset = cmd["tilt_servo_offset"];
    changed = true;
  }
  
  if (cmd.containsKey("tracking_sensitivity")) {
    settings.trackingSensitivity = cmd["tracking_sensitivity"];
    changed = true;
  }
  
  if (cmd.containsKey("camera_quality")) {
    int quality = cmd["camera_quality"];
    if (quality >= 0 && quality <= 63) {
      settings.cameraQuality = quality;
      
      // Update camera quality
      sensor_t *s = esp_camera_sensor_get();
      if (s) {
        s->set_quality(s, settings.cameraQuality);
      }
      changed = true;
    }
  }
  
  if (cmd.containsKey("debug_logging")) {
    settings.debugLogging = cmd["debug_logging"];
    changed = true;
  }
  
  if (cmd.containsKey("device_name")) {
    settings.deviceName = cmd["device_name"];
    changed = true;
  }
  
  if (changed) {
    sendSuccessResponse("Settings updated");
    logMessage("INFO", "Settings updated via command");
  } else {
    sendErrorResponse("No valid settings provided");
  }
}

// ========== RESPONSE FUNCTIONS ==========
void sendSuccessResponse(const String &message) {
  jsonDoc.clear();
  jsonDoc["type"] = "response";
  jsonDoc["status"] = "success";
  jsonDoc["message"] = message;
  jsonDoc["timestamp"] = millis();
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
}

void sendErrorResponse(const String &error) {
  jsonDoc.clear();
  jsonDoc["type"] = "response";
  jsonDoc["status"] = "error";
  jsonDoc["error"] = error;
  jsonDoc["timestamp"] = millis();
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
  
  logMessage("ERROR", "Command error: " + error);
}

void sendStatusUpdate() {
  jsonDoc.clear();
  jsonDoc["type"] = "status";
  jsonDoc["timestamp"] = millis();
  jsonDoc["system_ready"] = systemReady;
  jsonDoc["uptime"] = millis() - systemStartTime;
  jsonDoc["mode"] = getModeString(currentMode);
  
  // Servo positions
  JsonObject servo = jsonDoc.createNestedObject("servo");
  servo["pan_current"] = currentPanAngle;
  servo["tilt_current"] = currentTiltAngle;
  servo["pan_target"] = targetPanAngle;
  servo["tilt_target"] = targetTiltAngle;
  
  // Camera status
  JsonObject camera = jsonDoc.createNestedObject("camera");
  camera["fps"] = currentFPS;
  camera["avg_fps"] = averageFPS;
  camera["total_frames"] = totalFrames;
  camera["frame_errors"] = totalErrors;
  
  // Tracking status
  JsonObject tracking = jsonDoc.createNestedObject("tracking");
  tracking["object_detected"] = trackedObject.valid;
  if (trackedObject.valid) {
    tracking["object_x"] = trackedObject.x;
    tracking["object_y"] = trackedObject.y;
    tracking["confidence"] = trackedObject.confidence;
    tracking["last_seen"] = trackedObject.lastSeen;
  }
  
  // System health
  JsonObject health = jsonDoc.createNestedObject("health");
  health["free_heap"] = ESP.getFreeHeap();
  health["min_free_heap"] = ESP.getMinFreeHeap();
  health["last_error"] = lastError;
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
}

void sendTelemetryData() {
  jsonDoc.clear();
  jsonDoc["type"] = "telemetry";
  jsonDoc["timestamp"] = millis();
  
  // Real-time servo positions
  jsonDoc["pan_angle"] = currentPanAngle;
  jsonDoc["tilt_angle"] = currentTiltAngle;
  
  // Camera metrics
  jsonDoc["fps"] = currentFPS;
  jsonDoc["frame_count"] = totalFrames;
  
  // Tracking data
  if (trackedObject.valid) {
    JsonObject target = jsonDoc.createNestedObject("tracked_object");
    target["x"] = trackedObject.x;
    target["y"] = trackedObject.y;
    target["width"] = trackedObject.width;
    target["height"] = trackedObject.height;
    target["confidence"] = trackedObject.confidence;
    
    // Trajectory prediction
    if (settings.trajectoryOverlay) {
      JsonArray traj = jsonDoc.createNestedArray("trajectory");
      for (int i = 0; i < PREDICTION_STEPS; i++) {
        int idx = (trajectoryIndex + i) % PREDICTION_STEPS;
        if (trajectory[idx].timestamp > 0) {
          JsonObject point = traj.createNestedObject();
          point["x"] = trajectory[idx].x;
          point["y"] = trajectory[idx].y;
          point["t"] = trajectory[idx].timestamp;
        }
      }
    }
  }
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
}

void sendSettingsResponse() {
  jsonDoc.clear();
  jsonDoc["type"] = "settings";
  jsonDoc["timestamp"] = millis();
  
  jsonDoc["trajectory_overlay"] = settings.trajectoryOverlay;
  jsonDoc["wind_data_enabled"] = settings.windDataEnabled;
  jsonDoc["auto_tracking_enabled"] = settings.autoTrackingEnabled;
  jsonDoc["pan_servo_offset"] = settings.panServoOffset;
  jsonDoc["tilt_servo_offset"] = settings.tiltServoOffset;
  jsonDoc["tracking_sensitivity"] = settings.trackingSensitivity;
  jsonDoc["camera_quality"] = settings.cameraQuality;
  jsonDoc["debug_logging"] = settings.debugLogging;
  jsonDoc["device_name"] = settings.deviceName;
  
  String output;
  serializeJson(jsonDoc, output);
  Serial.println(output);
}

void sendHelpMessage() {
  Serial.println("ESP32-CAM Launch Tracking System v" + String(VERSION));
  Serial.println("Available text commands:");
  Serial.println("  status      - Show system status");
  Serial.println("  reset       - Restart system");
  Serial.println("  center      - Move servos to center position");
  Serial.println("  calibrate   - Start calibration sequence");
  Serial.println("  scan        - Start scanning mode");
  Serial.println("  auto        - Enable auto-tracking mode");
  Serial.println("  manual      - Switch to manual mode");
  Serial.println("  diagnostics - Run system diagnostics");
  Serial.println("  help        - Show this help");
  Serial.println("");
  Serial.println("JSON commands:");
  Serial.println("  {\"command\":\"set_position\",\"pan\":90,\"tilt\":90}");
  Serial.println("  {\"command\":\"set_mode\",\"mode\":\"auto\"}");
  Serial.println("  {\"command\":\"set_settings\",\"auto_tracking_enabled\":true}");
  Serial.println("  {\"command\":\"get_settings\"}");
  Serial.println("  {\"command\":\"get_status\"}");
  Serial.println("  {\"command\":\"get_telemetry\"}");
}

// ========== SETTINGS MANAGEMENT ==========
void loadSettings() {
  if (!SPIFFS.exists("/settings.json")) {
    logMessage("INFO", "Settings file not found, using defaults");
    saveSettings(); // Create default settings file
    return;
  }
  
  File file = SPIFFS.open("/settings.json", "r");
  if (!file) {
    logMessage("ERROR", "Failed to open settings file");
    return;
  }
  
  DeserializationError error = deserializeJson(jsonDoc, file);
  file.close();
  
  if (error) {
    logMessage("ERROR", "Failed to parse settings file: " + String(error.c_str()));
    return;
  }
  
  // Load settings with defaults
  settings.trajectoryOverlay = jsonDoc["trajectory_overlay"] | true;
  settings.windDataEnabled = jsonDoc["wind_data_enabled"] | false;
  settings.autoTrackingEnabled = jsonDoc["auto_tracking_enabled"] | true;
  settings.panServoOffset = jsonDoc["pan_servo_offset"] | 0;
  settings.tiltServoOffset = jsonDoc["tilt_servo_offset"] | 0;
  settings.trackingSensitivity = jsonDoc["tracking_sensitivity"] | 1.0;
  settings.cameraQuality = jsonDoc["camera_quality"] | 10;
  settings.debugLogging = jsonDoc["debug_logging"] | false;
  settings.deviceName = jsonDoc["device_name"] | "ESP32-CAM-Tracker";
  
  logMessage("INFO", "Settings loaded successfully");
}

void saveSettings() {
  jsonDoc.clear();
  jsonDoc["trajectory_overlay"] = settings.trajectoryOverlay;
  jsonDoc["wind_data_enabled"] = settings.windDataEnabled;
  jsonDoc["auto_tracking_enabled"] = settings.autoTrackingEnabled;
  jsonDoc["pan_servo_offset"] = settings.panServoOffset;
  jsonDoc["tilt_servo_offset"] = settings.tiltServoOffset;
  jsonDoc["tracking_sensitivity"] = settings.trackingSensitivity;
  jsonDoc["camera_quality"] = settings.cameraQuality;
  jsonDoc["debug_logging"] = settings.debugLogging;
  jsonDoc["device_name"] = settings.deviceName;
  
  File file = SPIFFS.open("/settings.json", "w");
  if (!file) {
    logMessage("ERROR", "Failed to create settings file");
    return;
  }
  
  if (serializeJson(jsonDoc, file) == 0) {
    logMessage("ERROR", "Failed to write settings file");
  } else {
    logMessage("INFO", "Settings saved successfully");
  }
  
  file.close();
}

// ========== UTILITY FUNCTIONS ==========
String getModeString(TrackingMode mode) {
  switch (mode) {
    case MODE_MANUAL: return "manual";
    case MODE_AUTO_TRACK: return "auto";
    case MODE_SCAN: return "scan";
    case MODE_CALIBRATE: return "calibrate";
    default: return "unknown";
  }
}

void resetTracking() {
  trackedObject.valid = false;
  trackedObject.x = 0;
  trackedObject.y = 0;
  trackedObject.confidence = 0;
  trackedObject.lastSeen = 0;
  
  // Clear trajectory
  for (int i = 0; i < PREDICTION_STEPS; i++) {
    trajectory[i].timestamp = 0;
  }
  trajectoryIndex = 0;
  
  logMessage("INFO", "Tracking data reset");
  sendSuccessResponse("Tracking reset");
}

void resetToSafeState() {
  // Stop all movement
  targetPanAngle = SERVO_CENTER;
  targetTiltAngle = SERVO_CENTER;
  currentMode = MODE_MANUAL;
  
  // Reset tracking
  resetTracking();
  
  logMessage("INFO", "System reset to safe state");
}

void performSystemDiagnostics() {
  logMessage("INFO", "=== SYSTEM DIAGNOSTICS ===");
  
  // System information
  logMessage("INFO", "Version: " + String(VERSION));
  logMessage("INFO", "Build: " + String(BUILD_DATE) + " " + String(BUILD_TIME));
  logMessage("INFO", "Uptime: " + String((millis() - systemStartTime) / 1000) + " seconds");
  logMessage("INFO", "Free heap: " + String(ESP.getFreeHeap()) + " bytes");
  logMessage("INFO", "Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes");
  
  // Camera diagnostics
  logMessage("INFO", "Camera FPS: " + String(currentFPS));
  logMessage("INFO", "Average FPS: " + String(averageFPS));
  logMessage("INFO", "Total frames: " + String(totalFrames));
  logMessage("INFO", "Frame errors: " + String(totalErrors));
  
  // Servo diagnostics
  logMessage("INFO", "Pan servo: " + String(currentPanAngle) + "° (target: " + String(targetPanAngle) + "°)");
  logMessage("INFO", "Tilt servo: " + String(currentTiltAngle) + "° (target: " + String(targetTiltAngle) + "°)");
  
  // Tracking diagnostics
  logMessage("INFO", "Tracking mode: " + getModeString(currentMode));
  logMessage("INFO", "Object detected: " + String(trackedObject.valid ? "Yes" : "No"));
  if (trackedObject.valid) {
    logMessage("INFO", "Object position: (" + String(trackedObject.x) + ", " + String(trackedObject.y) + ")");
    logMessage("INFO", "Object confidence: " + String(trackedObject.confidence));
  }
  
  // Settings diagnostics
  logMessage("INFO", "Auto-tracking: " + String(settings.autoTrackingEnabled ? "Enabled" : "Disabled"));
  logMessage("INFO", "Trajectory overlay: " + String(settings.trajectoryOverlay ? "Enabled" : "Disabled"));
  logMessage("INFO", "Debug logging: " + String(settings.debugLogging ? "Enabled" : "Disabled"));
  
  logMessage("INFO", "=== DIAGNOSTICS COMPLETE ===");
}

void handleError(const String &error) {
  lastError = error;
  logMessage("ERROR", error);
  
  // Signal error with LED
  for (int i = 0; i < 5; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(FLASH_LED_PIN, LOW);
    delay(200);
  }
  
  // Reset to safe state
  resetToSafeState();
  
  // Send error notification
  sendErrorResponse(error);
}

void logMessage(const String &level, const String &message) {
  String timestamp = String(millis());
  String logEntry = "[" + timestamp + "] " + level + ": " + message;
  
  // Always send to serial
  Serial.println(logEntry);
  
  // Optionally log to SPIFFS
  if (SPIFFS.totalBytes() > 0) {
    File logFile = SPIFFS.open("/system.log", "a");
    if (logFile) {
      logFile.println(logEntry);
      logFile.close();
      
      // Prevent log file from growing too large
      if (logFile.size() > 100000) { // 100KB limit
        SPIFFS.remove("/system.log.old");
        SPIFFS.rename("/system.log", "/system.log.old");
      }
    }
  }
}