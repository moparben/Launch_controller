#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <CAN.h>
#include <ArduinoJson.h>
#include "config.h"
#include "logger.h"

// Sensor readings structure
struct SensorReadings {
    float temperature;
    float humidity;
    float voltage;
    float windSpeed;
    float thermistor1;
    float thermistor2;
    bool dhtValid;
    unsigned long timestamp;
};

// Ignitor status
struct IgnitorStatus {
    bool armed;
    bool firing;
    bool continuity;
    float current;
    unsigned long fireStartTime;
    unsigned long lastCurrentRead;
};

class HardwareManager {
private:
    DHT dht;
    Servo servos[MAX_PADS];
    IgnitorStatus ignitors[MAX_PADS];
    SensorReadings lastReadings;
    
    unsigned long lastSensorRead;
    unsigned long lastCurrentRead;
    bool initialized;
    
    // Private methods
    float readVoltage();
    float readThermistor(int pin);
    float readWindSpeed();
    float readCurrent(int pin);
    bool checkIgnitorContinuity(int pin);
    void updateIgnitorSafety();
    
public:
    HardwareManager();
    
    // Initialization
    bool begin();
    void update();
    
    // Sensor operations (non-blocking)
    bool readSensors();
    SensorReadings getLastReadings() const { return lastReadings; }
    String getSensorJson() const;
    
    // Ignitor operations
    bool armIgnitor(uint8_t padId);
    bool disarmIgnitor(uint8_t padId);
    bool fireIgnitor(uint8_t padId);
    bool isIgnitorArmed(uint8_t padId) const;
    bool isIgnitorFiring(uint8_t padId) const;
    bool hasIgnitorContinuity(uint8_t padId) const;
    float getIgnitorCurrent(uint8_t padId) const;
    void emergencyDisarmAll();
    String getIgnitorStatusJson() const;
    
    // Servo operations
    bool setServoPosition(uint8_t padId, int angle);
    int getServoPosition(uint8_t padId) const;
    bool isServoAttached(uint8_t padId) const;
    
    // CAN bus operations
    bool sendCANMessage(uint32_t id, const uint8_t* data, size_t length);
    bool receiveCANMessage(uint32_t& id, uint8_t* data, size_t& length);
    
    // Safety checks
    bool performHardwareSafetyCheck();
    bool isSystemSafe() const;
    String getSafetyStatusJson() const;
    
    // Status LED
    void setStatusLED(bool state);
    void blinkStatusLED(int count = 1, int delayMs = 100);
    
    // Diagnostics
    String getDiagnosticsJson() const;
    void runDiagnostics();
    
    // Pin management
    bool checkPinConflicts();
    String getPinMappingJson() const;
};

extern HardwareManager hardware;

#endif // HARDWARE_H