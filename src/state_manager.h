#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include "config.h"

// System States
enum SystemState {
    STATE_INITIALIZING,
    STATE_IDLE,
    STATE_ARMING,
    STATE_ARMED,
    STATE_LAUNCHING,
    STATE_ABORT,
    STATE_ERROR,
    STATE_MAINTENANCE
};

// Pad States
enum PadState {
    PAD_OFFLINE,
    PAD_IDLE,
    PAD_AUTHORIZED,
    PAD_ARMED,
    PAD_FIRED,
    PAD_ERROR
};

// Authorization States
enum AuthState {
    AUTH_NONE,
    AUTH_PENDING,
    AUTH_AUTHORIZED,
    AUTH_EXPIRED
};

// Player/Pad Information
struct PadInfo {
    uint8_t padId;
    String playerName;
    PadState state;
    AuthState authState;
    unsigned long authTime;
    bool abortAcknowledged;
    unsigned long lastHeartbeat;
    bool ignitorContinuity;
    float currentReading;
    bool hardwareSafe;
};

// System Status
struct SystemStatus {
    SystemState state;
    bool groupAuthorized;
    bool abortActive;
    unsigned long abortTime;
    bool allPadsReady;
    bool hardwareSafe;
    float systemVoltage;
    float temperature;
    float humidity;
    float windSpeed;
    String lastError;
};

class StateManager {
private:
    SystemStatus systemStatus;
    std::vector<PadInfo> pads;
    std::map<String, String> gameSettings;
    unsigned long lastStateUpdate;
    bool stateChanged;

public:
    StateManager();
    
    // System State Management
    void initialize();
    void update();
    SystemState getSystemState() const { return systemStatus.state; }
    void setSystemState(SystemState newState);
    String getSystemStateString() const;
    
    // Pad Management
    bool addPad(uint8_t padId, const String& playerName);
    bool removePad(uint8_t padId);
    PadInfo* getPad(uint8_t padId);
    std::vector<PadInfo>& getAllPads() { return pads; }
    uint8_t getActivePadCount() const;
    
    // Authorization Management
    bool authorizePad(uint8_t padId, const String& playerName);
    bool deauthorizePad(uint8_t padId);
    bool isGroupAuthorized() const;
    void checkAuthorizationTimeouts();
    
    // Abort Management
    bool initiateAbort(uint8_t padId = 255);
    bool acknowledgeAbort(uint8_t padId);
    bool isAbortActive() const { return systemStatus.abortActive; }
    bool areAllAbortsAcknowledged() const;
    void checkAbortTimeout();
    
    // Safety Checks
    bool performSafetyChecks();
    bool isLaunchSafe() const;
    void updatePadSafety(uint8_t padId, bool ignitorContinuity, float current);
    void updateSystemSafety(float voltage, float temp, float humidity, float wind);
    
    // State Queries
    bool canArm() const;
    bool canLaunch() const;
    bool hasStateChanged() const { return stateChanged; }
    void clearStateChanged() { stateChanged = false; }
    
    // Serialization
    String toJson() const;
    bool fromJson(const String& json);
    String getPadStatusJson(uint8_t padId) const;
    String getAllPadsJson() const;
    
    // Game Settings
    void setGameSetting(const String& key, const String& value);
    String getGameSetting(const String& key, const String& defaultValue = "") const;
    
    // Diagnostics
    void logState() const;
    String getDiagnosticsJson() const;
};

extern StateManager stateManager;

#endif // STATE_MANAGER_H