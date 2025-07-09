/*
 * Launch Controller v3.5.0709
 * 
 * Multi-pad model rocket launch controller with robust group authorization and abort logic
 * Features:
 * - Group pre-authorization system for multiple pads/players
 * - Any-player abort with 1-minute acknowledgment requirement
 * - Real-time sensor monitoring (DHT11, thermistors, wind, current, voltage)
 * - Ignitor control with safety checks and current feedback
 * - Web UI with live status updates via WebSocket
 * - CAN bus support for expansion
 * - SD card logging with SPIFFS fallback
 * - OTA firmware updates
 * - Servo control for physical mechanisms
 * - Touchscreen interface
 * - Non-blocking, state-driven operation
 * 
 * Hardware Requirements:
 * - ESP32 development board
 * - DHT11 temperature/humidity sensor
 * - ACS712 current sensors (4x)
 * - MOSFETs for ignitor control (4x)
 * - Servo motors (4x)
 * - TFT display with touch
 * - CAN transceiver
 * - SD card module
 * - Various analog sensors (thermistors, wind, voltage)
 */

#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "config.h"
#include "logger.h"
#include "state_manager.h"
#include "hardware.h"
#include "web_server.h"

// System timing
unsigned long lastSystemUpdate = 0;
unsigned long lastStatusLED = 0;
bool statusLEDState = false;

// System status
bool systemInitialized = false;
String lastError = "";

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("=========================================");
    Serial.println("Launch Controller v" + String(VERSION));
    Serial.println("Build: " + String(BUILD_DATE) + " " + String(BUILD_TIME));
    Serial.println("=========================================");
    
    // Initialize file system
    if (!SPIFFS.begin(true)) {
        Serial.println("ERROR: SPIFFS initialization failed!");
        while (1) delay(1000);
    }
    
    // Initialize logger
    if (!logger.begin()) {
        Serial.println("WARNING: Logger initialization failed, using serial only");
    }
    
    logger.info("=== SYSTEM STARTUP ===");
    logger.info("Launch Controller v" + String(VERSION));
    logger.info("Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    // Initialize state manager
    stateManager.initialize();
    
    // Initialize hardware
    if (!hardware.begin()) {
        logger.error("Hardware initialization failed!");
        stateManager.setSystemState(STATE_ERROR);
        lastError = "Hardware initialization failed";
    } else {
        logger.info("Hardware initialized successfully");
    }
    
    // Initialize web server
    if (!webServer.begin()) {
        logger.error("Web server initialization failed!");
        stateManager.setSystemState(STATE_ERROR);
        lastError = "Web server initialization failed";
    } else {
        logger.info("Web server initialized successfully");
    }
    
    // Final system check
    if (stateManager.getSystemState() != STATE_ERROR) {
        systemInitialized = true;
        stateManager.setSystemState(STATE_IDLE);
        logger.info("=== SYSTEM READY ===");
        logger.info("Web interface: http://" + webServer.getIPAddress());
        
        // Signal successful startup
        hardware.blinkStatusLED(5, 200);
    } else {
        logger.error("=== SYSTEM FAILED TO INITIALIZE ===");
        logger.error("Error: " + lastError);
        
        // Signal error
        for (int i = 0; i < 10; i++) {
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            delay(100);
        }
    }
    
    lastSystemUpdate = millis();
    lastStatusLED = millis();
}

void loop() {
    unsigned long now = millis();
    
    // Handle system updates
    if (now - lastSystemUpdate >= 100) { // 10Hz system update
        updateSystem();
        lastSystemUpdate = now;
    }
    
    // Handle status LED
    if (now - lastStatusLED >= 1000) { // 1Hz LED blink
        updateStatusLED();
        lastStatusLED = now;
    }
    
    // Small delay to prevent watchdog issues
    delay(1);
}

void updateSystem() {
    if (!systemInitialized) {
        return;
    }
    
    // Update all subsystems
    stateManager.update();
    hardware.update();
    webServer.update();
    
    // Handle state-based logic
    handleSystemStates();
    
    // Handle safety monitoring
    handleSafetyMonitoring();
    
    // Handle emergency conditions
    handleEmergencyConditions();
}

void handleSystemStates() {
    SystemState currentState = stateManager.getSystemState();
    
    switch (currentState) {
        case STATE_INITIALIZING:
            // Should not reach here after setup
            break;
            
        case STATE_IDLE:
            // Normal idle state - system ready for authorization
            break;
            
        case STATE_ARMING:
            // Transitional state - not used in current implementation
            stateManager.setSystemState(STATE_ARMED);
            break;
            
        case STATE_ARMED:
            // System armed - ready to launch
            // Check if we should disarm due to safety issues
            if (!stateManager.canLaunch()) {
                logger.warn("System: Auto-disarming due to safety conditions");
                hardware.emergencyDisarmAll();
                stateManager.setSystemState(STATE_IDLE);
            }
            break;
            
        case STATE_LAUNCHING:
            // Launch in progress - monitor for completion
            // This state will be cleared by hardware when firing completes
            break;
            
        case STATE_ABORT:
            // Abort active - ensure all ignitors are disarmed
            hardware.emergencyDisarmAll();
            
            // Check if abort has been resolved
            if (stateManager.areAllAbortsAcknowledged()) {
                logger.info("System: Abort resolved, returning to idle");
                stateManager.setSystemState(STATE_IDLE);
            }
            break;
            
        case STATE_ERROR:
            // Error state - emergency shutdown
            hardware.emergencyDisarmAll();
            break;
            
        case STATE_MAINTENANCE:
            // Maintenance mode - limited functionality
            break;
    }
}

void handleSafetyMonitoring() {
    // Perform continuous safety checks
    if (!hardware.performHardwareSafetyCheck()) {
        SystemState currentState = stateManager.getSystemState();
        
        // If we're in a critical state, initiate abort
        if (currentState == STATE_ARMED || currentState == STATE_LAUNCHING) {
            logger.error("System: Safety check failed during critical operation, initiating abort");
            stateManager.initiateAbort(255); // System-initiated abort
        }
    }
    
    // Check for authorization timeouts
    stateManager.checkAuthorizationTimeouts();
    
    // Check for abort timeouts
    stateManager.checkAbortTimeout();
}

void handleEmergencyConditions() {
    // Check for hardware failures
    SensorReadings readings = hardware.getLastReadings();
    
    // Critical voltage check
    if (readings.voltage > 0 && (readings.voltage < MIN_VOLTAGE_V * 0.9 || readings.voltage > MAX_VOLTAGE_V * 1.1)) {
        logger.error("System: Critical voltage condition: " + String(readings.voltage) + "V");
        hardware.emergencyDisarmAll();
        stateManager.setSystemState(STATE_ERROR);
        lastError = "Critical voltage: " + String(readings.voltage) + "V";
    }
    
    // Critical temperature check
    if (readings.dhtValid && (readings.temperature < MIN_TEMPERATURE_C * 1.2 || readings.temperature > MAX_TEMPERATURE_C * 0.9)) {
        logger.error("System: Critical temperature condition: " + String(readings.temperature) + "C");
        hardware.emergencyDisarmAll();
        stateManager.setSystemState(STATE_ERROR);
        lastError = "Critical temperature: " + String(readings.temperature) + "C";
    }
    
    // Check for excessive current on any ignitor
    for (int i = 0; i < MAX_PADS; i++) {
        float current = hardware.getIgnitorCurrent(i);
        if (current > CURRENT_THRESHOLD_MA * 2) {
            logger.error("System: Excessive current on ignitor " + String(i) + ": " + String(current) + "mA");
            hardware.emergencyDisarmAll();
            stateManager.initiateAbort(255);
        }
    }
    
    // Check WiFi connection and restart if needed
    if (!webServer.isConnected()) {
        static unsigned long lastWiFiCheck = 0;
        if (millis() - lastWiFiCheck > 30000) { // Check every 30 seconds
            logger.warn("System: WiFi disconnected, attempting reconnect");
            // Web server will handle reconnection
            lastWiFiCheck = millis();
        }
    }
    
    // Memory check
    if (ESP.getFreeHeap() < 10000) { // Less than 10KB free
        logger.warn("System: Low memory warning: " + String(ESP.getFreeHeap()) + " bytes free");
    }
}

void updateStatusLED() {
    SystemState currentState = stateManager.getSystemState();
    
    switch (currentState) {
        case STATE_INITIALIZING:
            // Fast blink during initialization
            statusLEDState = !statusLEDState;
            hardware.setStatusLED(statusLEDState);
            break;
            
        case STATE_IDLE:
            // Slow blink when idle
            statusLEDState = !statusLEDState;
            hardware.setStatusLED(statusLEDState);
            break;
            
        case STATE_ARMED:
            // Solid on when armed
            hardware.setStatusLED(true);
            break;
            
        case STATE_LAUNCHING:
            // Rapid blink during launch
            for (int i = 0; i < 5; i++) {
                hardware.setStatusLED(true);
                delay(50);
                hardware.setStatusLED(false);
                delay(50);
            }
            break;
            
        case STATE_ABORT:
            // Double blink pattern for abort
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            delay(100);
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            break;
            
        case STATE_ERROR:
            // Triple blink pattern for error
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            delay(100);
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            delay(100);
            hardware.setStatusLED(true);
            delay(100);
            hardware.setStatusLED(false);
            break;
            
        case STATE_MAINTENANCE:
            // Very slow blink for maintenance
            static unsigned long lastMaintBlink = 0;
            if (millis() - lastMaintBlink > 2000) {
                statusLEDState = !statusLEDState;
                hardware.setStatusLED(statusLEDState);
                lastMaintBlink = millis();
            }
            break;
    }
}

// Debug and diagnostic functions
void printSystemStatus() {
    logger.info("=== SYSTEM STATUS ===");
    logger.info("State: " + stateManager.getSystemStateString());
    logger.info("Free Heap: " + String(ESP.getFreeHeap()));
    logger.info("Uptime: " + String(millis() / 1000) + " seconds");
    logger.info("WiFi: " + (webServer.isConnected() ? "Connected" : "Disconnected"));
    logger.info("IP: " + webServer.getIPAddress());
    logger.info("Clients: " + String(webServer.getConnectedClients()));
    
    SensorReadings readings = hardware.getLastReadings();
    logger.info("Voltage: " + String(readings.voltage) + "V");
    logger.info("Temperature: " + String(readings.temperature) + "C");
    logger.info("Humidity: " + String(readings.humidity) + "%");
    logger.info("Wind: " + String(readings.windSpeed) + "m/s");
    
    for (int i = 0; i < MAX_PADS; i++) {
        logger.info("Pad " + String(i) + " - Armed: " + String(hardware.isIgnitorArmed(i)) +
                   ", Continuity: " + String(hardware.hasIgnitorContinuity(i)) +
                   ", Current: " + String(hardware.getIgnitorCurrent(i)) + "mA");
    }
    logger.info("====================");
}

// Serial command interface for debugging
void handleSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "status") {
            printSystemStatus();
        }
        else if (command == "reset") {
            logger.info("System: Reset requested via serial");
            ESP.restart();
        }
        else if (command == "abort") {
            logger.info("System: Abort requested via serial");
            stateManager.initiateAbort(255);
        }
        else if (command == "disarm") {
            logger.info("System: Disarm all requested via serial");
            hardware.emergencyDisarmAll();
        }
        else if (command == "test") {
            logger.info("System: Hardware test requested via serial");
            hardware.runDiagnostics();
        }
        else if (command.startsWith("log")) {
            // Change log level
            String level = command.substring(4);
            if (level == "debug") logger.setLogLevel(LOG_LEVEL_DEBUG);
            else if (level == "info") logger.setLogLevel(LOG_LEVEL_INFO);
            else if (level == "warn") logger.setLogLevel(LOG_LEVEL_WARN);
            else if (level == "error") logger.setLogLevel(LOG_LEVEL_ERROR);
            else {
                Serial.println("Usage: log [debug|info|warn|error]");
            }
        }
        else if (command == "help") {
            Serial.println("Available commands:");
            Serial.println("  status  - Show system status");
            Serial.println("  reset   - Restart system");
            Serial.println("  abort   - Initiate system abort");
            Serial.println("  disarm  - Emergency disarm all");
            Serial.println("  test    - Run hardware diagnostics");
            Serial.println("  log X   - Set log level (debug/info/warn/error)");
            Serial.println("  help    - Show this help");
        }
        else {
            Serial.println("Unknown command: " + command);
            Serial.println("Type 'help' for available commands");
        }
    }
}