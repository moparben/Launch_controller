/*
 * Rocket Launch Controller v3.7 (Optimized, Bug-Checked, Reliable) - FIXED
 * For ESP32-S3-DevKitC-1-N8R8
 * 
 * See changelog/comments for fixes to issues 1-8, 10-13.
 * 
 * FIXES:
 * 1. IMU runtime fault handling
 * 2. Servo settle timeout
 * 3. MP3 track map fallback
 * 4. Watchdog coverage
 * 5. Wind sensor voltage documented
 * 6. Current sense logic clarified
 * 7. RTC initialization comment
 * 8. SD error fallback (Serial logging)
 * 10. Pad naming clarified in comments
 * 11. Settings API: explicit type conversion/check
 * 12. Touchscreen calibration: user offset support
 * 13. Servo wind compensation: placeholder logic
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7796S.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_FT6206.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include <RTClib.h>
#include <DHT.h>
#include <sh2.h>
#include <sh2_err.h>
#include "esp_task_wdt.h"

// --- Pad Naming (10) ---
// If config file missing/incomplete, these are the defaults and will be used.
// Settings UI should show current names and allow editing all four.
String padNames[4] = {"Pad 1", "Pad 2", "Pad 3", "Pad 4"};

// --- Touchscreen Calibration/Offset (12) ---
int touchOffsetX = 0;
int touchOffsetY = 0;

// --- Pin Assignments ---
// Wind sensors: 0-3.3V ONLY! If your sensors are 5V, use a resistive divider (see comments at WINDSPEED_PIN, WINDDIR_PIN) (5).
#define WINDSPEED_PIN   1
#define WINDDIR_PIN     2
#define DHT_PIN         3
const int PHOTOEYE_PIN[4]  = {13, 14, 15, 16};
const int LIMITSW_PIN[4]   = {9, 10, 11, 12};
const int CURRENT_PIN[4]   = {5, 6, 7, 8}; // (6) See comment below
const int MOSFET_PIN[4]    = {33, 34, 35, 36};
#define LCD_CS_PIN   37
#define LCD_DC_PIN   38
#define LCD_RST_PIN  39
#define SD_CS_PIN    40
#define SPI_SCK_PIN  41
#define SPI_MOSI_PIN 42
#define SPI_MISO_PIN 21
#define I2C_SDA_PIN  46
#define I2C_SCL_PIN  45
#define MP3_RX_PIN   43
#define MP3_TX_PIN   44
#define FAN_RELAY_PIN 47

#define NUM_PADS 4
#define PCA9685_ADDR 0x40
#define TCA9548A_ADDR 0x70

const uint8_t servoPCA9685Map[NUM_PADS][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}};

#define DHTTYPE DHT11
DHT dhtBox(DHT_PIN, DHTTYPE);

Adafruit_ST7796S lcd(LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN);
Adafruit_FT6206 ts;
Adafruit_PWMServoDriver pca9685(PCA9685_ADDR);
Adafruit_BNO08x bnoPad[NUM_PADS];
DFRobotDFPlayerMini mp3;
RTC_DS3231 rtc;

// --- I2C Multiplexer ---
void tcaSelect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCA9548A_ADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

// --- State/Settings ---
enum SystemMode { MODE_INDIVIDUAL, MODE_GROUP, MODE_GAME };
enum PadState { LST_IDLE, LST_ARMING, LST_COUNTDOWN, LST_LAUNCH, LST_POSTLAUNCH, LST_ABORT, LST_WIN, LST_LOSE };

SystemMode systemMode = MODE_INDIVIDUAL;
SystemMode lastSystemMode = MODE_INDIVIDUAL;
PadState padState[NUM_PADS] = {LST_IDLE, LST_IDLE, LST_IDLE, LST_IDLE};
int gameWinnerPad = -1;
bool gameActive = false;
String voiceType = "male";

bool padEnabled[NUM_PADS] = {true, true, true, true};

struct Pad {
  bool present = false;
  bool ready = false;
  bool fault = false;
  bool launched = false;
  String faultDesc = "";
  float yaw = 0, pitch = 0, roll = 0;
  unsigned long lastAction = 0;
  unsigned long armingStart = 0; // (2) Timeout for ARMING
};
Pad pad[NUM_PADS];

struct ServoState {
  float currentAzimuth = 0;
  float targetAzimuth = 90;
  float currentTilt = 0;
  float targetTilt = 90;
  bool moving = false;
};
ServoState servoState[NUM_PADS];

// --- Central Sensors/Permissives ---
float windSpeed = 0;
float windDirection = 0;
float boxTempC = 0;
unsigned long lastDHTRead = 0;
bool sdGood = true;

// --- Settings with Validation ---
unsigned long armingDelay = 3000;
unsigned long countdownTime = 10000;
unsigned long ignitorTime = 2000;
unsigned long postLaunchDelay = 30000;
bool windSpeedPermissiveEnabled = true;
float windSpeedThreshold = 10.0;
float windDirMin = 0;
float windDirMax = 360;
int windDeadband = 10;

bool photoeyeEnabled[NUM_PADS]     = {true, false, false, false};
bool limitswEnabled[NUM_PADS]      = {true, false, false, false};
bool currentSenseEnabled[NUM_PADS] = {true, false, false, false};
bool imuEnabled[NUM_PADS]          = {true, false, false, false};

int currentThresholds[NUM_PADS] = {1500, 1500, 1500, 1500};
int minTempPermissive = 0;
int maxTempPermissive = 60;
int fanOnTemp = 40;
bool padIMUReady[NUM_PADS] = {false};
AsyncWebServer server(80);
#define JSON_DOC_SIZE 32768
DynamicJsonDocument doc(JSON_DOC_SIZE);

unsigned long lastLoop = 0, lastHeapLog = 0;
int lastSecond[NUM_PADS] = {0, 0, 0, 0};

// --- Audio Track Map (3) ---
#include <map>
std::map<String, int> soundMap = {
  {"/mp3/arming_male.mp3", 1},
  {"/mp3/arming_female.mp3", 2},
  {"/mp3/warning.mp3", 3},
  // Add all used sound mappings here
};

#define API_SECRET "changeme123"   // Set a secure secret for API access

// --- Helper/Validation Functions ---
int clampInt(int v, int minV, int maxV) { return v < minV ? minV : (v > maxV ? maxV : v); }
float clampFloat(float v, float minV, float maxV) { return v < minV ? minV : (v > maxV ? maxV : v); }
void validateSettings() {
  armingDelay = clampInt(armingDelay, 100, 60000);
  countdownTime = clampInt(countdownTime, 1000, 60000);
  ignitorTime = clampInt(ignitorTime, 100, 10000);
  postLaunchDelay = clampInt(postLaunchDelay, 1000, 60000);
  windSpeedThreshold = clampFloat(windSpeedThreshold, 0, 100);
  windDeadband = clampInt(windDeadband, 0, 90);
  minTempPermissive = clampInt(minTempPermissive, -40, 120);
  maxTempPermissive = clampInt(maxTempPermissive, -40, 120);
  fanOnTemp = clampInt(fanOnTemp, -40, 120);
  for (uint8_t i = 0; i < NUM_PADS; i++) {
    currentThresholds[i] = clampInt(currentThresholds[i], 0, 4095);
  }
}

// --- Permissive Helper Functions ---
bool checkPhotoeye(uint8_t p) {
  return !photoeyeEnabled[p] || (digitalRead(PHOTOEYE_PIN[p]) == LOW);
}
bool checkLimitSwitch(uint8_t p) {
  return !limitswEnabled[p] || (digitalRead(LIMITSW_PIN[p]) == HIGH);
}
bool checkCurrentSense(uint8_t p) {
  // (6) Current sense for continuity: check continuity (current < threshold) before launch; not checked during LAUNCH
  if (!currentSenseEnabled[p]) return true;
  return (padState[p] != LST_LAUNCH) ? (analogRead(CURRENT_PIN[p]) < currentThresholds[p]) : true;
}
bool checkIMU(uint8_t p) {
  // (1) IMU runtime check: padIMUReady[p] is set false if IMU fails at runtime
  if (!imuEnabled[p]) return true;
  tcaSelect(p);
  bool imuOk = padIMUReady[p];
  sh2_SensorValue_t tmpEvent;
  if (!bnoPad[p].getSensorEvent(&tmpEvent)) {
    padIMUReady[p] = false; // Mark as dead if error
    imuOk = false;
  }
  return imuOk;
}

// --- Watchdog Setup (4) ---
#define WDT_TIMEOUT 5
void setupWatchdog() {
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
}

// --- Output Safe State ---
void setAllOutputsSafe() {
  for (uint8_t p = 0; p < NUM_PADS; p++) {
    digitalWrite(MOSFET_PIN[p], LOW);
    setServo(p, 0, 90);
    setServo(p, 1, 90);
  }
  digitalWrite(FAN_RELAY_PIN, LOW);
}

// --- API Authentication ---
bool checkApiAuth(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) return false;
  String auth = request->header("Authorization");
  return auth == API_SECRET;
}

// --- System Functions ---
void logEvent(uint8_t pad, const String& msg, bool fault = false);
String getTimeString();
void playSound(const String& file);
void setServo(uint8_t pad, uint8_t axis, float deg);
void groupAbort(const String& reason);
void playCountdownAudio(int second, uint8_t pad);
void loadConfig();
void saveConfig();
void webSetup();
void hardwareSetup();
void updateIMUs();
void updatePermissives();
void handlePadState();
void monitorHeap();

// --- Central Sensor Functions ---
float readWindSpeed() {
  // (5) 0-3.3V sensor ONLY! For 5V sensors, use a voltage divider to avoid ESP32 damage!
  int raw = analogRead(WINDSPEED_PIN);
  float voltage = raw * (3.3f / 4095.0f);
  float speed = (voltage / 3.3f) * 30.0f;
  return speed;
}
float readWindDirection() {
  int raw = analogRead(WINDDIR_PIN);
  float voltage = raw * (3.3f / 4095.0f);
  float deg = (voltage / 3.3f) * 360.0f;
  deg = fmod(deg, 360.0f);
  if (deg < 0) deg += 360.0f;
  return deg;
}
bool windSpeedPermissive(float speed) {
  return speed <= windSpeedThreshold;
}
bool windDirectionPermissive(float dir) {
  float minD = fmod(windDirMin, 360.0f);
  float maxD = fmod(windDirMax, 360.0f);
  float db = windDeadband;
  float center = fmod((minD + maxD) / 2.0f, 360.0f);
  float halfRange = fmod((maxD - minD + 360.0f), 360.0f) / 2.0f + db;
  float delta = fmod(dir - center + 540.0f, 360.0f) - 180.0f;
  return abs(delta) <= halfRange;
}

// --- Main Setup ---
void setup() {
  Serial.begin(115200);
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  WiFi.softAP("RocketLaunch", "password");
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  sdGood = SD.begin(SD_CS_PIN);
  if (!sdGood) Serial.println("SD card init failed!");
  // (7) If RTC battery is missing, the time may be wrong; set RTC via /api/settime after boot
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1);
  }
  hardwareSetup();
  setAllOutputsSafe();
  loadConfig();
  validateSettings();
  Serial2.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN);
  if (!mp3.begin(Serial2)) Serial.println("MP3 player not found!");
  pinMode(FAN_RELAY_PIN, OUTPUT);
  digitalWrite(FAN_RELAY_PIN, LOW);
  setupWatchdog();
  webSetup();
}

// --- Hardware Setup ---
void hardwareSetup() {
  if (!pca9685.begin(PCA9685_ADDR)) {
    logEvent(0, "PCA9685 initialization failed", true);
    while (1);
  }
  pca9685.setPWMFreq(50);
  lcd.begin();
  lcd.setRotation(1);
  lcd.fillScreen(0x0000);
  ts.begin(40);

  for (uint8_t p = 0; p < NUM_PADS; p++) {
    pinMode(MOSFET_PIN[p], OUTPUT);
    digitalWrite(MOSFET_PIN[p], LOW);
    pinMode(PHOTOEYE_PIN[p], INPUT_PULLUP);
    pinMode(LIMITSW_PIN[p], INPUT_PULLUP);
    pinMode(CURRENT_PIN[p], INPUT);
    padState[p] = LST_IDLE;
    pad[p].launched = pad[p].fault = false;
    pad[p].ready = false;
    padIMUReady[p] = false;
  }
  dhtBox.begin();

  for (uint8_t p = 0; p < NUM_PADS; p++) {
    if (!imuEnabled[p]) continue;
    tcaSelect(p);
    if (!bnoPad[p].begin(0x4A)) {
      logEvent(p, "IMU init failed for pad " + String(p), true);
    } else {
      bnoPad[p].enableReport(SH2_ROTATION_VECTOR);
      padIMUReady[p] = true;
    }
  }
  tcaSelect(0);
}

// --- Main Loop (4) ---
void loop() {
  esp_task_wdt_reset();
  if (millis() - lastLoop < 40) return;
  lastLoop = millis();
  updateIMUs();
  updatePermissives();
  handlePadState();
  monitorHeap();
  esp_task_wdt_reset(); // (4) Coverage: always call at end of loop, even if above returns early next pass
}

// --- Permissives (Optimized) ---
void updatePermissives() {
  if (millis() - lastDHTRead > 1000) {
    float t = dhtBox.readTemperature();
    if (!isnan(t)) boxTempC = t;
    lastDHTRead = millis();
  }
  windSpeed = readWindSpeed();
  windDirection = readWindDirection();

  bool windSpeedOk = windSpeedPermissiveEnabled ? windSpeedPermissive(windSpeed) : true;
  bool windDirOk = windDirectionPermissive(windDirection);
  bool tempOk = (boxTempC >= minTempPermissive && boxTempC <= maxTempPermissive);

  for (uint8_t p = 0; p < NUM_PADS; p++) {
    if (!padEnabled[p]) {
      pad[p].present = false;
      pad[p].ready = false;
      continue;
    }
    bool limitOk = checkLimitSwitch(p);
    bool photoOk = checkPhotoeye(p);
    bool currentOk = checkCurrentSense(p);
    bool imuOk = checkIMU(p);

    pad[p].present = limitOk && photoOk;
    pad[p].ready = pad[p].present && windSpeedOk && windDirOk && currentOk && tempOk && imuOk && !pad[p].fault;

    pad[p].faultDesc = "";
    if (!limitOk) pad[p].faultDesc += "Limit ";
    if (!photoOk) pad[p].faultDesc += "Photoeye ";
    if (!windSpeedOk) pad[p].faultDesc += "WindSpd ";
    if (!windDirOk) pad[p].faultDesc += "WindDir ";
    if (!currentOk) pad[p].faultDesc += "Current ";
    if (!tempOk) pad[p].faultDesc += "Temp ";
    if (!imuOk) pad[p].faultDesc += "IMU ";
    pad[p].fault = pad[p].faultDesc.length() > 0;
  }
}

// --- IMU/Servo Update ---
void updateIMUs() {
  for (uint8_t p = 0; p < NUM_PADS; p++) {
    if (!imuEnabled[p] || !padIMUReady[p]) continue;
    tcaSelect(p);
    sh2_SensorValue_t event;
    if (bnoPad[p].getSensorEvent(&event)) {
      float qw = event.un.rotationVector.real;
      float qx = event.un.rotationVector.i;
      float qy = event.un.rotationVector.j;
      float qz = event.un.rotationVector.k;
      pad[p].yaw = atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz)) * 180.0 / PI;
      pad[p].pitch = asin(2.0 * (qw * qy - qz * qx)) * 180.0 / PI;
      pad[p].roll = atan2(2.0 * (qw * qx + qy * qz), 1.0 - 2.0 * (qx * qx + qy * qy)) * 180.0 / PI;
      servoState[p].currentAzimuth = pad[p].yaw;
      servoState[p].currentTilt = pad[p].pitch;
      servoState[p].moving = (abs(servoState[p].currentAzimuth - servoState[p].targetAzimuth) > 1.0 ||
                              abs(servoState[p].currentTilt - servoState[p].targetTilt) > 1.0);
    }
  }
}

// --- State Machine (Optimized) ---
void handlePadState() {
  unsigned long now = millis();

  if (systemMode != lastSystemMode) {
    for (uint8_t p = 0; p < NUM_PADS; p++) {
      padState[p] = LST_IDLE;
      pad[p].launched = pad[p].fault = false;
      pad[p].faultDesc = "";
      lastSecond[p] = 0;
    }
    lastSystemMode = systemMode;
    gameActive = (systemMode == MODE_GAME);
    gameWinnerPad = -1;
    if (systemMode == MODE_GAME) playSound("/mp3/game_start.mp3");
  }

  digitalWrite(FAN_RELAY_PIN, boxTempC >= fanOnTemp ? HIGH : LOW);

  static bool globalAbort = false;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    // (12) Touchscreen offset support
    int tx = map(p.y, 0, 320, 0, lcd.width()) + touchOffsetX;
    int ty = map(p.x, 0, 240, lcd.height(), 0) + touchOffsetY;
    if (ty < 16 && tx > lcd.width() / 2) globalAbort = true;
  }
  if (globalAbort) {
    groupAbort("User abort from touch");
    globalAbort = false;
  }

  static bool groupReady = false;
  if (systemMode == MODE_GROUP) {
    groupReady = true;
    for (uint8_t p = 0; p < NUM_PADS; p++) {
      if (padEnabled[p] && (!pad[p].ready || !pad[p].present || pad[p].fault)) groupReady = false;
    }
    if (groupReady) {
      for (uint8_t p = 0; p < NUM_PADS; p++) {
        if (padEnabled[p] && padState[p] == LST_IDLE) {
          padState[p] = LST_ARMING;
          pad[p].lastAction = now;
          pad[p].armingStart = now; // (2)
          playSound("/mp3/arming_" + voiceType + ".mp3");
        }
      }
    }
  }

  if (gameActive && systemMode == MODE_GAME) {
    int launches = 0, firstPad = -1;
    for (uint8_t p = 0; p < NUM_PADS; p++) {
      if (padEnabled[p] && padState[p] == LST_POSTLAUNCH && pad[p].launched) {
        launches++;
        if (firstPad == -1) firstPad = p;
      }
    }
    if (launches > 0 && gameWinnerPad == -1) {
      gameWinnerPad = firstPad;
      padState[gameWinnerPad] = LST_WIN;
      playSound("/mp3/player_win_" + padNames[gameWinnerPad] + ".mp3");
      logEvent(gameWinnerPad, "WIN in GAME MODE", false);
      for (uint8_t p = 0; p < NUM_PADS; p++) {
        if (padEnabled[p] && p != gameWinnerPad && padState[p] == LST_POSTLAUNCH) {
          padState[p] = LST_LOSE;
          playSound("/mp3/player_lose_" + padNames[p] + ".mp3");
          logEvent(p, "LOSE in GAME MODE", false);
        }
      }
      gameActive = false;
    }
  }

  for (uint8_t p = 0; p < NUM_PADS; p++) {
    if (!padEnabled[p]) continue;
    if (systemMode == MODE_GROUP && padState[p] == LST_ABORT) { groupAbort("Group fault on pad " + String(p)); break; }
    switch (padState[p]) {
      case LST_IDLE:
        pad[p].launched = false;
        if (systemMode != MODE_GROUP && pad[p].ready && pad[p].present && !pad[p].fault &&
            (systemMode == MODE_INDIVIDUAL || (systemMode == MODE_GAME && gameActive))) {
          padState[p] = LST_ARMING;
          pad[p].lastAction = now;
          pad[p].armingStart = now; // (2)
          playSound("/mp3/arming_" + voiceType + ".mp3");
        }
        break;
      case LST_ARMING:
        // (13) Servo wind compensation placeholder: adjust servos based on wind
        if (windSpeed > 0.1) {
          // Example: Slightly tilt pad to compensate for wind direction
          // (User should implement custom logic as needed)
          servoState[p].targetAzimuth = 90 + (windDirection - 180) / 10.0; // crude example
          servoState[p].targetTilt = 90 - windSpeed * 2.0; // crude example
        }
        setServo(p, 0, servoState[p].targetAzimuth);
        setServo(p, 1, servoState[p].targetTilt);

        // (2) Timeout/fault if servo never settles
        if (servoState[p].moving) {
          if (now - pad[p].armingStart > 5000) {
            pad[p].fault = true;
            pad[p].faultDesc = "Servo stuck";
            padState[p] = LST_ABORT;
            playSound("/mp3/warning.mp3");
          }
          break;
        }
        if (now - pad[p].lastAction >= armingDelay) {
          padState[p] = LST_COUNTDOWN;
          pad[p].lastAction = now;
          playSound("/mp3/countdown_" + voiceType + "_" + String(countdownTime / 1000) + ".mp3");
        }
        break;
      case LST_COUNTDOWN:
        {
          int remaining = (countdownTime / 1000) - ((now - pad[p].lastAction) / 1000);
          if (remaining > 0 && remaining != lastSecond[p]) {
            playCountdownAudio(remaining, p);
            lastSecond[p] = remaining;
          }
          if (now - pad[p].lastAction >= countdownTime) {
            padState[p] = LST_LAUNCH;
            pad[p].lastAction = now;
          }
        }
        break;
      case LST_LAUNCH:
        digitalWrite(MOSFET_PIN[p], HIGH);
        if (now - pad[p].lastAction >= ignitorTime) {
          digitalWrite(MOSFET_PIN[p], LOW);
          playSound("/mp3/liftoff_" + voiceType + ".mp3");
          logEvent(p, "LAUNCH SUCCESS", false);
          pad[p].launched = true;
          padState[p] = LST_POSTLAUNCH;
          pad[p].lastAction = now;
        }
        break;
      case LST_POSTLAUNCH:
        if (now - pad[p].lastAction >= postLaunchDelay) {
          padState[p] = LST_IDLE;
          pad[p].launched = false;
          pad[p].fault = false;
          pad[p].faultDesc = "";
          lastSecond[p] = 0;
        }
        break;
      case LST_ABORT:
        digitalWrite(MOSFET_PIN[p], LOW);
        setServo(p, 0, 90);
        setServo(p, 1, 90);
        playSound("/mp3/warning.mp3");
        logEvent(p, "ABORTED: " + pad[p].faultDesc, true);
        if (!pad[p].fault) padState[p] = LST_IDLE;
        break;
      case LST_WIN:
      case LST_LOSE:
        break;
    }
    if ((padState[p] == LST_ARMING || padState[p] == LST_COUNTDOWN || padState[p] == LST_LAUNCH) && pad[p].fault) {
      padState[p] = LST_ABORT;
      pad[p].faultDesc += "ABORTED by permissive ";
      playSound("/mp3/warning.mp3");
    }
  }
}

// --- Servo Helper ---
void setServo(uint8_t pad, uint8_t axis, float deg) {
  deg = clampFloat(deg, 0, 180);
  if (servoPCA9685Map[pad][axis] >= 0 && servoPCA9685Map[pad][axis] < 16) {
    int pulse = map(deg, 0, 180, 110, 520);
    pca9685.setPWM(servoPCA9685Map[pad][axis], 0, pulse);
  }
  if (axis == 0) servoState[pad].targetAzimuth = deg;
  else servoState[pad].targetTilt = deg;
  // Mark as not moving if close enough
  servoState[pad].moving = (abs(servoState[pad].currentAzimuth - servoState[pad].targetAzimuth) > 1.0 ||
                            abs(servoState[pad].currentTilt - servoState[pad].targetTilt) > 1.0);
}

// --- Audio/Logging ---
void playCountdownAudio(int second, uint8_t) {
  String file = "/mp3/countdown_" + voiceType + "_" + String(second) + ".mp3";
  playSound(file);
}
void playSound(const String& file) {
  // (3) Fallback to warning sound if not mapped
  if (soundMap.count(file)) mp3.play(soundMap[file]);
  else if (soundMap.count("/mp3/warning.mp3")) mp3.play(soundMap["/mp3/warning.mp3"]);
  else Serial.print("Play sound: "), Serial.println(file);
}
void logEvent(uint8_t pad, const String& msg, bool fault) {
  // (8) SD error fallback: log to Serial if SD fails, else to SD
  String now = getTimeString();
  String out = now + " [" + padNames[pad] + "] " + msg;
  if (!sdGood) { Serial.println(out); return; }
  String filename = fault ? "/faults.log" : "/launch.log";
  File f = SD.open(filename, FILE_APPEND);
  if (f) {
    f.println(out);
    f.close();
  }
  Serial.println(out);
}
String getTimeString() {
  DateTime now = rtc.now();
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buf);
}
void groupAbort(const String& reason) {
  setAllOutputsSafe();
  for (uint8_t p = 0; p < NUM_PADS; p++) {
    pad[p].fault = true;
    pad[p].faultDesc += String("ABORTED(") + reason + ") ";
    padState[p] = LST_ABORT;
    pad[p].launched = false;
    logEvent(p, "ABORTED: " + reason, true);
    playSound("/mp3/warning.mp3");
  }
}
void monitorHeap() {
  if (millis() - lastHeapLog > 5000) {
    Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());
    lastHeapLog = millis();
  }
  if (ESP.getFreeHeap() < 32768) { groupAbort("Low Memory"); }
}

// --- Config, Web Setup, and API (see issue 11 for explicit type check) ---
void loadConfig() {
  if (!sdGood) return;
  File f = SD.open("/config.json");
  if (!f) return;
  StaticJsonDocument<4096> cfg;
  DeserializationError err = deserializeJson(cfg, f);
  if (!err) {
    if (cfg.containsKey("armingDelay")) armingDelay = cfg["armingDelay"].as<unsigned long>();
    if (cfg.containsKey("countdownTime")) countdownTime = cfg["countdownTime"].as<unsigned long>();
    if (cfg.containsKey("ignitorTime")) ignitorTime = cfg["ignitorTime"].as<unsigned long>();
    if (cfg.containsKey("postLaunchDelay")) postLaunchDelay = cfg["postLaunchDelay"].as<unsigned long>();
    if (cfg.containsKey("minTempPermissive")) minTempPermissive = cfg["minTempPermissive"].as<int>();
    if (cfg.containsKey("maxTempPermissive")) maxTempPermissive = cfg["maxTempPermissive"].as<int>();
    if (cfg.containsKey("fanOnTemp")) fanOnTemp = cfg["fanOnTemp"].as<int>();
    if (cfg.containsKey("voiceType")) voiceType = cfg["voiceType"].as<String>();
    if (cfg.containsKey("windSpeedPermissiveEnabled")) windSpeedPermissiveEnabled = cfg["windSpeedPermissiveEnabled"].as<bool>();
    if (cfg.containsKey("windSpeedThreshold")) windSpeedThreshold = cfg["windSpeedThreshold"].as<float>();
    if (cfg.containsKey("windDirMin")) windDirMin = cfg["windDirMin"].as<float>();
    if (cfg.containsKey("windDirMax")) windDirMax = cfg["windDirMax"].as<float>();
    if (cfg.containsKey("windDeadband")) windDeadband = cfg["windDeadband"].as<int>();
    JsonArray names = cfg["padNames"];
    for (uint8_t i = 0; i < NUM_PADS && i < names.size(); i++) padNames[i] = names[i].as<String>();
    JsonArray enabled = cfg["padEnabled"];
    for (uint8_t i = 0; i < NUM_PADS && i < enabled.size(); i++) padEnabled[i] = enabled[i].as<bool>();
    JsonArray curr = cfg["currentThresholds"];
    for (uint8_t i = 0; i < NUM_PADS && i < curr.size(); i++) currentThresholds[i] = curr[i].as<int>();
    JsonArray photo = cfg["photoeyeEnabled"];
    for (uint8_t i = 0; i < NUM_PADS && i < photo.size(); i++) photoeyeEnabled[i] = photo[i].as<bool>();
    JsonArray limit = cfg["limitswEnabled"];
    for (uint8_t i = 0; i < NUM_PADS && i < limit.size(); i++) limitswEnabled[i] = limit[i].as<bool>();
    JsonArray curren = cfg["currentSenseEnabled"];
    for (uint8_t i = 0; i < NUM_PADS && i < curren.size(); i++) currentSenseEnabled[i] = curren[i].as<bool>();
    JsonArray imu = cfg["imuEnabled"];
    for (uint8_t i = 0; i < NUM_PADS && i < imu.size(); i++) imuEnabled[i] = imu[i].as<bool>();
    // (12) Touchscreen calibration
    if (cfg.containsKey("touchOffsetX")) touchOffsetX = cfg["touchOffsetX"].as<int>();
    if (cfg.containsKey("touchOffsetY")) touchOffsetY = cfg["touchOffsetY"].as<int>();
  }
  f.close();
  validateSettings();
}

void saveConfig() {
  if (!sdGood) return;
  File f = SD.open("/config.json", FILE_WRITE);
  if (f) {
    StaticJsonDocument<4096> cfg;
    cfg["armingDelay"] = armingDelay;
    cfg["countdownTime"] = countdownTime;
    cfg["ignitorTime"] = ignitorTime;
    cfg["postLaunchDelay"] = postLaunchDelay;
    cfg["minTempPermissive"] = minTempPermissive;
    cfg["maxTempPermissive"] = maxTempPermissive;
    cfg["fanOnTemp"] = fanOnTemp;
    cfg["voiceType"] = voiceType;
    cfg["windSpeedPermissiveEnabled"] = windSpeedPermissiveEnabled;
    cfg["windSpeedThreshold"] = windSpeedThreshold;
    cfg["windDirMin"] = windDirMin;
    cfg["windDirMax"] = windDirMax;
    cfg["windDeadband"] = windDeadband;
    JsonArray names = cfg.createNestedArray("padNames");
    for (uint8_t i = 0; i < NUM_PADS; i++) names.add(padNames[i]);
    JsonArray enabled = cfg.createNestedArray("padEnabled");
    for (uint8_t i = 0; i < NUM_PADS; i++) enabled.add(padEnabled[i]);
    JsonArray curr = cfg.createNestedArray("currentThresholds");
    for (uint8_t i = 0; i < NUM_PADS; i++) curr.add(currentThresholds[i]);
    JsonArray photo = cfg.createNestedArray("photoeyeEnabled");
    for (uint8_t i = 0; i < NUM_PADS; i++) photo.add(photoeyeEnabled[i]);
    JsonArray limit = cfg.createNestedArray("limitswEnabled");
    for (uint8_t i = 0; i < NUM_PADS; i++) limit.add(limitswEnabled[i]);
    JsonArray curren = cfg.createNestedArray("currentSenseEnabled");
    for (uint8_t i = 0; i < NUM_PADS; i++) curren.add(currentSenseEnabled[i]);
    JsonArray imu = cfg.createNestedArray("imuEnabled");
    for (uint8_t i = 0; i < NUM_PADS; i++) imu.add(imuEnabled[i]);
    // (12) Touchscreen calibration
    cfg["touchOffsetX"] = touchOffsetX;
    cfg["touchOffsetY"] = touchOffsetY;
    serializeJson(cfg, f);
    f.close();
  }
}

// --- Web/API (11: type conversion/check) ---
void webSetup() {
  // GET status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    doc.clear();
    doc["systemMode"] = (systemMode == MODE_GROUP ? "group" : (systemMode == MODE_GAME ? "game" : "individual"));
    doc["windSpeed"] = windSpeed;
    doc["windDirection"] = windDirection;
    doc["boxTempC"] = boxTempC;
    doc["touchOffsetX"] = touchOffsetX;
    doc["touchOffsetY"] = touchOffsetY;
    JsonArray pads = doc.createNestedArray("pads");
    for (uint8_t p = 0; p < NUM_PADS; p++) {
      JsonObject o = pads.createNestedObject();
      o["name"] = padNames[p];
      o["enabled"] = padEnabled[p];
      o["present"] = pad[p].present;
      o["ready"] = pad[p].ready;
      o["fault"] = pad[p].fault;
      o["faultDesc"] = pad[p].faultDesc;
      o["launched"] = pad[p].launched;
      o["state"] = padState[p];
      o["yaw"] = pad[p].yaw;
      o["pitch"] = pad[p].pitch;
      o["roll"] = pad[p].roll;
      o["photoeyeEnabled"] = photoeyeEnabled[p];
      o["limitswEnabled"] = limitswEnabled[p];
      o["currentSenseEnabled"] = currentSenseEnabled[p];
      o["imuEnabled"] = imuEnabled[p];
    }
    String out;
    if (serializeJson(doc, out) == 0) {
      request->send(500, "application/json", "{\"error\":\"JSON overflow\"}");
      logEvent(0, "JSON overflow in /api/status", true);
    } else {
      request->send(200, "application/json", out);
    }
  });

  // POST settings (explicit type check, 11)
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    if (request->hasParam("armingDelay", true)) armingDelay = request->getParam("armingDelay", true)->value().toInt();
    if (request->hasParam("countdownTime", true)) countdownTime = request->getParam("countdownTime", true)->value().toInt();
    if (request->hasParam("ignitorTime", true)) ignitorTime = request->getParam("ignitorTime", true)->value().toInt();
    if (request->hasParam("postLaunchDelay", true)) postLaunchDelay = request->getParam("postLaunchDelay", true)->value().toInt();
    if (request->hasParam("voiceType", true)) voiceType = request->getParam("voiceType", true)->value();
    if (request->hasParam("minTempPermissive", true)) minTempPermissive = request->getParam("minTempPermissive", true)->value().toInt();
    if (request->hasParam("maxTempPermissive", true)) maxTempPermissive = request->getParam("maxTempPermissive", true)->value().toInt();
    if (request->hasParam("fanOnTemp", true)) fanOnTemp = request->getParam("fanOnTemp", true)->value().toInt();
    if (request->hasParam("windSpeedPermissiveEnabled", true)) windSpeedPermissiveEnabled = request->getParam("windSpeedPermissiveEnabled", true)->value().toInt() > 0;
    if (request->hasParam("windSpeedThreshold", true)) windSpeedThreshold = request->getParam("windSpeedThreshold", true)->value().toFloat();
    if (request->hasParam("windDirMin", true)) windDirMin = request->getParam("windDirMin", true)->value().toFloat();
    if (request->hasParam("windDirMax", true)) windDirMax = request->getParam("windDirMax", true)->value().toFloat();
    if (request->hasParam("windDeadband", true)) windDeadband = request->getParam("windDeadband", true)->value().toInt();
    if (request->hasParam("touchOffsetX", true)) touchOffsetX = request->getParam("touchOffsetX", true)->value().toInt();
    if (request->hasParam("touchOffsetY", true)) touchOffsetY = request->getParam("touchOffsetY", true)->value().toInt();
    for (uint8_t p = 0; p < NUM_PADS; p++) {
      String nameParam = "name" + String(p);
      String enableParam = "enable" + String(p);
      String currParam = "current" + String(p);
      String photoParam = "photoeyeEnabled" + String(p);
      String limitParam = "limitswEnabled" + String(p);
      String currSenseParam = "currentSenseEnabled" + String(p);
      String imuParam = "imuEnabled" + String(p);

      if (request->hasParam(nameParam.c_str(), true)) padNames[p] = request->getParam(nameParam.c_str(), true)->value();
      if (request->hasParam(enableParam.c_str(), true)) padEnabled[p] = request->getParam(enableParam.c_str(), true)->value().toInt() > 0;
      if (request->hasParam(currParam.c_str(), true)) currentThresholds[p] = request->getParam(currParam.c_str(), true)->value().toInt();
      if (request->hasParam(photoParam.c_str(), true)) photoeyeEnabled[p] = request->getParam(photoParam.c_str(), true)->value().toInt() > 0;
      if (request->hasParam(limitParam.c_str(), true)) limitswEnabled[p] = request->getParam(limitParam.c_str(), true)->value().toInt() > 0;
      if (request->hasParam(currSenseParam.c_str(), true)) currentSenseEnabled[p] = request->getParam(currSenseParam.c_str(), true)->value().toInt() > 0;
      if (request->hasParam(imuParam.c_str(), true)) imuEnabled[p] = request->getParam(imuParam.c_str(), true)->value().toInt() > 0;
    }
    validateSettings();
    saveConfig();
    request->send(200, "application/json", "{\"result\":\"Settings updated.\"}");
  });

  // POST abort
  server.on("/api/abort", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    groupAbort("User abort from web");
    request->send(200, "application/json", "{\"result\":\"Abort triggered.\"}");
  });

  // POST mode change
  server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    if (request->hasParam("mode", true)) {
      String mode = request->getParam("mode", true)->value();
      if (mode == "group") systemMode = MODE_GROUP;
      else if (mode == "game") systemMode = MODE_GAME;
      else systemMode = MODE_INDIVIDUAL;
      request->send(200, "application/json", "{\"result\":\"Mode set.\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing mode param.\"}");
    }
  });

  // POST pad reset
  server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    if (request->hasParam("pad", true)) {
      int padNum = request->getParam("pad", true)->value().toInt();
      if (padNum >= 0 && padNum < NUM_PADS) {
        padState[padNum] = LST_IDLE;
        pad[padNum].fault = false;
        pad[padNum].faultDesc = "";
        request->send(200, "application/json", "{\"result\":\"Pad reset.\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Invalid pad number.\"}");
      }
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing pad param.\"}");
    }
  });

  // POST settime for RTC
  server.on("/api/settime", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkApiAuth(request)) { request->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }
    if (request->hasParam("datetime", true)) {
      String dt = request->getParam("datetime", true)->value();
      int year, month, day, hour, min, sec;
      if (sscanf(dt.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
        rtc.adjust(DateTime(year, month, day, hour, min, sec));
        request->send(200, "application/json", "{\"result\":\"RTC set.\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Invalid datetime format.\"}");
      }
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing datetime param.\"}");
    }
  });

  server.begin();
}
