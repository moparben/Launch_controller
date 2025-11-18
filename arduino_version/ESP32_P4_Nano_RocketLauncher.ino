/*******************************************************************************
 * ESP32-P4-Nano Rocket Launcher - Arduino Version
 * 
 * This version uses Arduino IDE for easier compilation and flashing
 * Based on Waveshare ESP32-P4-Nano documentation examples
 * 
 * Hardware: Waveshare ESP32-P4-Nano 
 * Board Selection: ESP32P4 DEV Module
 * 
 * Features:
 * - WiFi Access Point (ESP32-C6 via SDIO)
 * - HTTP Web Server for control interface
 * - GPIO controls for LEDs and fire mechanism
 * - Safety systems and status monitoring
 ******************************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiAP.h>

// Configuration
const char* ssid = "RocketLauncher-P4-Nano";
const char* password = "rocket123";

// GPIO Configuration for ESP32-P4-Nano
const int LED_RED = 15;
const int LED_GREEN = 16; 
const int FIRE_GPIO = 17;
const int SAFETY_GPIO = 18;

// System status
struct SystemStatus {
  bool wifi_connected = false;
  bool safety_enabled = false;
  unsigned long boot_time = 0;
  unsigned long fire_count = 0;
} status;

WebServer server(80);

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-P4-Nano Rocket Launcher Starting...");
  
  status.boot_time = millis();
  
  // Initialize GPIO
  initGPIO();
  
  // Initialize WiFi
  initWiFi();
  
  // Start HTTP server
  initWebServer();
  
  Serial.println("System Ready!");
  Serial.printf("WiFi SSID: %s\n", ssid);
  Serial.printf("WiFi Password: %s\n", password);
  Serial.println("Connect and browse to: http://192.168.4.1");
}

void loop() {
  server.handleClient();
  
  // Update system status
  status.safety_enabled = digitalRead(SAFETY_GPIO);
  
  // Update status LEDs
  updateStatusLEDs();
  
  // Log status every 5 seconds
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 5000) {
    logSystemStatus();
    lastLog = millis();
  }
  
  delay(100);
}

void initGPIO() {
  Serial.println("Initializing GPIO controls...");
  
  // Configure LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  
  // Configure fire control
  pinMode(FIRE_GPIO, OUTPUT);
  
  // Configure safety input  
  pinMode(SAFETY_GPIO, INPUT_PULLUP);
  
  // Initialize states
  digitalWrite(LED_RED, HIGH);   // Red LED on (not ready)
  digitalWrite(LED_GREEN, LOW);  // Green LED off
  digitalWrite(FIRE_GPIO, LOW);  // Fire control off
  
  Serial.println("GPIO initialization complete");
}

void initWiFi() {
  Serial.println("Initializing WiFi AP via ESP32-C6 SDIO...");
  
  // Configure Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  // Wait for AP to start
  delay(2000);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.printf("AP IP address: %s\n", IP.toString().c_str());
  
  // Verify WiFi MAC to check ESP32-C6 communication
  String mac = WiFi.macAddress();
  Serial.printf("WiFi MAC Address: %s\n", mac.c_str());
  
  if (mac != "00:00:00:00:00:00") {
    status.wifi_connected = true;
    Serial.printf("WiFi AP '%s' started successfully\n", ssid);
  } else {
    Serial.println("WARNING: WiFi MAC shows all zeros - ESP32-C6 communication issue!");
    status.wifi_connected = false;
  }
}

void initWebServer() {
  Serial.println("Starting HTTP server...");
  
  // Status page
  server.on("/", handleRoot);
  
  // Fire rocket endpoint
  server.on("/fire", handleFire);
  
  // Status API endpoint
  server.on("/status", handleStatus);
  
  server.begin();
  Serial.println("HTTP server started on port 80");
}

void handleRoot() {
  String html = "<!DOCTYPE html>";
  html += "<html><head><title>ESP32-P4-Nano Rocket Launcher</title>";
  html += "<style>body{font-family:Arial;margin:40px}";
  html += ".status{background:#f0f0f0;padding:10px;margin:10px 0}";
  html += ".fire-btn{background:#ff4444;color:white;padding:20px;font-size:20px;border:none;cursor:pointer}";
  html += ".fire-btn:hover{background:#ff0000}</style></head>";
  html += "<body><h1>🚀 ESP32-P4-Nano Rocket Launcher</h1>";
  
  // System status
  html += "<div class='status'>";
  html += "<h3>System Status</h3>";
  html += "<p>WiFi: " + String(status.wifi_connected ? "Connected ✅" : "Disconnected ❌") + "</p>";
  html += "<p>Safety: " + String(status.safety_enabled ? "Enabled ✅" : "DISABLED ❌") + "</p>";
  html += "<p>MAC Address: " + WiFi.macAddress() + "</p>";
  html += "<p>Fire Count: " + String(status.fire_count) + "</p>";
  html += "<p>Uptime: " + String((millis() - status.boot_time) / 1000) + " seconds</p>";
  html += "</div>";
  
  // Fire control
  if (status.safety_enabled) {
    html += "<button class='fire-btn' onclick=\"location.href='/fire'\">🔥 FIRE ROCKET 🔥</button>";
  } else {
    html += "<p style='color:red;font-size:18px'>⚠️ Safety switch must be enabled to fire!</p>";
  }
  
  html += "<p><a href='/status'>JSON Status</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleFire() {
  Serial.println("ROCKET FIRE COMMAND RECEIVED!");
  
  String response;
  
  // Safety check
  if (!status.safety_enabled) {
    Serial.println("FIRE ABORTED - Safety not enabled!");
    response = "FIRE ABORTED - Safety switch not enabled!";
    server.send(400, "text/plain", response);
    return;
  }
  
  // Fire sequence
  Serial.println("Initiating fire sequence...");
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, LOW);
  
  // Fire pulse
  digitalWrite(FIRE_GPIO, HIGH);
  delay(500); // 500ms fire pulse
  digitalWrite(FIRE_GPIO, LOW);
  
  status.fire_count++;
  
  // Status LEDs
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);
  
  Serial.printf("Fire sequence complete! Total fires: %lu\n", status.fire_count);
  
  response = "🚀 ROCKET FIRED SUCCESSFULLY! 🚀\n";
  response += "Fire count: " + String(status.fire_count);
  
  server.send(200, "text/plain", response);
}

void handleStatus() {
  String json = "{";
  json += "\"wifi_connected\":" + String(status.wifi_connected ? "true" : "false") + ",";
  json += "\"safety_enabled\":" + String(status.safety_enabled ? "true" : "false") + ",";
  json += "\"fire_count\":" + String(status.fire_count) + ",";
  json += "\"uptime_seconds\":" + String((millis() - status.boot_time) / 1000) + ",";
  json += "\"mac_address\":\"" + WiFi.macAddress() + "\",";
  json += "\"ip_address\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void updateStatusLEDs() {
  if (status.wifi_connected) {
    // System ready - green LED blink
    static unsigned long lastBlink = 0;
    static bool ledState = false;
    
    if (millis() - lastBlink > 1000) {
      ledState = !ledState;
      digitalWrite(LED_GREEN, ledState);
      digitalWrite(LED_RED, !ledState);
      lastBlink = millis();
    }
  } else {
    // System not ready - red LED solid
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, LOW);
  }
}

void logSystemStatus() {
  Serial.printf("Status - WiFi:%s Safety:%s Fires:%lu Uptime:%lu\n",
                status.wifi_connected ? "OK" : "FAIL",
                status.safety_enabled ? "ON" : "OFF",
                status.fire_count,
                (millis() - status.boot_time) / 1000);
}