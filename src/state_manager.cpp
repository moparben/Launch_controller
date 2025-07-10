#include "state_manager.h"
#include "logger.h"

StateManager stateManager;

StateManager::StateManager() {
    lastStateUpdate = 0;
    stateChanged = false;
    
    // Initialize system status
    systemStatus.state = STATE_INITIALIZING;
    systemStatus.groupAuthorized = false;
    systemStatus.abortActive = false;
    systemStatus.abortTime = 0;
    systemStatus.allPadsReady = false;
    systemStatus.hardwareSafe = false;
    systemStatus.systemVoltage = 0.0;
    systemStatus.temperature = 0.0;
    systemStatus.humidity = 0.0;
    systemStatus.windSpeed = 0.0;
    systemStatus.lastError = "";
    
    // Initialize pads
    pads.clear();
    pads.reserve(MAX_PADS);
    
    // Initialize game settings
    gameSettings["game_mode"] = String(DEFAULT_GAME_MODE);
    gameSettings["max_pads"] = String(MAX_PADS);
    gameSettings["auth_timeout"] = String(AUTHORIZATION_TIMEOUT_MS);
    gameSettings["abort_timeout"] = String(ABORT_ACKNOWLEDGMENT_TIMEOUT_MS);
}

void StateManager::initialize() {
    logger.info("StateManager: Initializing...");
    
    // Reset all states
    systemStatus.state = STATE_IDLE;
    systemStatus.groupAuthorized = false;
    systemStatus.abortActive = false;
    systemStatus.allPadsReady = false;
    systemStatus.lastError = "";
    
    // Clear all pads
    pads.clear();
    
    stateChanged = true;
    lastStateUpdate = millis();
    
    logger.info("StateManager: Initialization complete");
}

void StateManager::update() {
    unsigned long now = millis();
    
    // Check for periodic updates
    if (now - lastStateUpdate >= HEARTBEAT_INTERVAL_MS) {
        checkAuthorizationTimeouts();
        checkAbortTimeout();
        performSafetyChecks();
        
        lastStateUpdate = now;
    }
}

void StateManager::setSystemState(SystemState newState) {
    if (systemStatus.state != newState) {
        SystemState oldState = systemStatus.state;
        systemStatus.state = newState;
        stateChanged = true;
        
        logger.info("StateManager: State changed from " + 
                   getSystemStateString() + " to " + String((int)newState));
        
        // Handle state transitions
        switch (newState) {
            case STATE_ABORT:
                systemStatus.abortActive = true;
                systemStatus.abortTime = millis();
                // Mark all pads as needing abort acknowledgment
                for (auto& pad : pads) {
                    pad.abortAcknowledged = false;
                }
                break;
                
            case STATE_IDLE:
                systemStatus.abortActive = false;
                systemStatus.groupAuthorized = false;
                // Reset all pad authorizations
                for (auto& pad : pads) {
                    if (pad.authState == AUTH_AUTHORIZED) {
                        pad.authState = AUTH_NONE;
                    }
                    pad.abortAcknowledged = false;
                }
                break;
                
            case STATE_ERROR:
                systemStatus.abortActive = true;
                systemStatus.groupAuthorized = false;
                break;
                
            default:
                break;
        }
    }
}

String StateManager::getSystemStateString() const {
    switch (systemStatus.state) {
        case STATE_INITIALIZING: return "INITIALIZING";
        case STATE_IDLE: return "IDLE";
        case STATE_ARMING: return "ARMING";
        case STATE_ARMED: return "ARMED";
        case STATE_LAUNCHING: return "LAUNCHING";
        case STATE_ABORT: return "ABORT";
        case STATE_ERROR: return "ERROR";
        case STATE_MAINTENANCE: return "MAINTENANCE";
        default: return "UNKNOWN";
    }
}

bool StateManager::addPad(uint8_t padId, const String& playerName) {
    if (padId >= MAX_PADS) {
        logger.error("StateManager: Invalid pad ID " + String(padId));
        return false;
    }
    
    // Check if pad already exists
    for (auto& pad : pads) {
        if (pad.padId == padId) {
            logger.warn("StateManager: Pad " + String(padId) + " already exists");
            return false;
        }
    }
    
    if (pads.size() >= MAX_PADS) {
        logger.error("StateManager: Maximum pads reached");
        return false;
    }
    
    PadInfo newPad;
    newPad.padId = padId;
    newPad.playerName = playerName;
    newPad.state = PAD_IDLE;
    newPad.authState = AUTH_NONE;
    newPad.authTime = 0;
    newPad.abortAcknowledged = false;
    newPad.lastHeartbeat = millis();
    newPad.ignitorContinuity = false;
    newPad.currentReading = 0.0;
    newPad.hardwareSafe = false;
    
    pads.push_back(newPad);
    stateChanged = true;
    
    logger.info("StateManager: Added pad " + String(padId) + " for player " + playerName);
    return true;
}

bool StateManager::removePad(uint8_t padId) {
    for (auto it = pads.begin(); it != pads.end(); ++it) {
        if (it->padId == padId) {
            logger.info("StateManager: Removed pad " + String(padId));
            pads.erase(it);
            stateChanged = true;
            return true;
        }
    }
    return false;
}

PadInfo* StateManager::getPad(uint8_t padId) {
    for (auto& pad : pads) {
        if (pad.padId == padId) {
            return &pad;
        }
    }
    return nullptr;
}

uint8_t StateManager::getActivePadCount() const {
    uint8_t count = 0;
    for (const auto& pad : pads) {
        if (pad.state != PAD_OFFLINE && pad.state != PAD_ERROR) {
            count++;
        }
    }
    return count;
}

bool StateManager::authorizePad(uint8_t padId, const String& playerName) {
    PadInfo* pad = getPad(padId);
    if (!pad) {
        logger.error("StateManager: Cannot authorize unknown pad " + String(padId));
        return false;
    }
    
    if (systemStatus.abortActive) {
        logger.warn("StateManager: Cannot authorize during abort");
        return false;
    }
    
    pad->playerName = playerName;
    pad->authState = AUTH_AUTHORIZED;
    pad->authTime = millis();
    pad->state = PAD_AUTHORIZED;
    
    stateChanged = true;
    
    logger.info("StateManager: Authorized pad " + String(padId) + " for " + playerName);
    
    // Check if group is now authorized
    systemStatus.groupAuthorized = isGroupAuthorized();
    
    return true;
}

bool StateManager::deauthorizePad(uint8_t padId) {
    PadInfo* pad = getPad(padId);
    if (!pad) {
        return false;
    }
    
    pad->authState = AUTH_NONE;
    pad->authTime = 0;
    if (pad->state == PAD_AUTHORIZED) {
        pad->state = PAD_IDLE;
    }
    
    stateChanged = true;
    systemStatus.groupAuthorized = isGroupAuthorized();
    
    logger.info("StateManager: Deauthorized pad " + String(padId));
    return true;
}

bool StateManager::isGroupAuthorized() const {
    if (pads.empty()) {
        return false;
    }
    
    for (const auto& pad : pads) {
        if (pad.state != PAD_OFFLINE && pad.authState != AUTH_AUTHORIZED) {
            return false;
        }
    }
    
    return true;
}

void StateManager::checkAuthorizationTimeouts() {
    unsigned long now = millis();
    bool changed = false;
    
    for (auto& pad : pads) {
        if (pad.authState == AUTH_AUTHORIZED) {
            if (now - pad.authTime > AUTHORIZATION_TIMEOUT_MS) {
                pad.authState = AUTH_EXPIRED;
                if (pad.state == PAD_AUTHORIZED) {
                    pad.state = PAD_IDLE;
                }
                changed = true;
                logger.warn("StateManager: Authorization expired for pad " + String(pad.padId));
            }
        }
    }
    
    if (changed) {
        stateChanged = true;
        systemStatus.groupAuthorized = isGroupAuthorized();
    }
}

bool StateManager::initiateAbort(uint8_t padId) {
    if (systemStatus.abortActive) {
        logger.warn("StateManager: Abort already active");
        return false;
    }
    
    setSystemState(STATE_ABORT);
    
    String initiator = (padId == 255) ? "SYSTEM" : "PAD_" + String(padId);
    logger.warn("StateManager: Abort initiated by " + initiator);
    
    return true;
}

bool StateManager::acknowledgeAbort(uint8_t padId) {
    if (!systemStatus.abortActive) {
        return false;
    }
    
    PadInfo* pad = getPad(padId);
    if (!pad) {
        return false;
    }
    
    pad->abortAcknowledged = true;
    stateChanged = true;
    
    logger.info("StateManager: Abort acknowledged by pad " + String(padId));
    
    // Check if all pads have acknowledged
    if (areAllAbortsAcknowledged()) {
        logger.info("StateManager: All pads acknowledged abort, returning to idle");
        setSystemState(STATE_IDLE);
    }
    
    return true;
}

bool StateManager::areAllAbortsAcknowledged() const {
    for (const auto& pad : pads) {
        if (pad.state != PAD_OFFLINE && !pad.abortAcknowledged) {
            return false;
        }
    }
    return true;
}

void StateManager::checkAbortTimeout() {
    if (!systemStatus.abortActive) {
        return;
    }
    
    unsigned long now = millis();
    if (now - systemStatus.abortTime > ABORT_ACKNOWLEDGMENT_TIMEOUT_MS) {
        logger.error("StateManager: Abort timeout - not all pads acknowledged");
        setSystemState(STATE_ERROR);
        systemStatus.lastError = "Abort acknowledgment timeout";
    }
}

bool StateManager::performSafetyChecks() {
    bool safe = true;
    String errors = "";
    
    // Check system voltage
    if (systemStatus.systemVoltage < MIN_VOLTAGE_V || systemStatus.systemVoltage > MAX_VOLTAGE_V) {
        safe = false;
        errors += "Voltage out of range (" + String(systemStatus.systemVoltage) + "V); ";
    }
    
    // Check temperature
    if (systemStatus.temperature < MIN_TEMPERATURE_C || systemStatus.temperature > MAX_TEMPERATURE_C) {
        safe = false;
        errors += "Temperature out of range (" + String(systemStatus.temperature) + "C); ";
    }
    
    // Check wind speed
    if (systemStatus.windSpeed > MAX_WIND_SPEED_MS) {
        safe = false;
        errors += "Wind speed too high (" + String(systemStatus.windSpeed) + "m/s); ";
    }
    
    // Check pad safety
    for (const auto& pad : pads) {
        if (pad.state != PAD_OFFLINE && !pad.hardwareSafe) {
            safe = false;
            errors += "Pad " + String(pad.padId) + " hardware unsafe; ";
        }
    }
    
    systemStatus.hardwareSafe = safe;
    
    if (!safe && systemStatus.lastError != errors) {
        systemStatus.lastError = errors;
        logger.error("StateManager: Safety check failed: " + errors);
        stateChanged = true;
    }
    
    return safe;
}

bool StateManager::isLaunchSafe() const {
    return systemStatus.hardwareSafe && 
           systemStatus.groupAuthorized && 
           !systemStatus.abortActive &&
           systemStatus.state != STATE_ERROR;
}

void StateManager::updatePadSafety(uint8_t padId, bool ignitorContinuity, float current) {
    PadInfo* pad = getPad(padId);
    if (!pad) {
        return;
    }
    
    pad->ignitorContinuity = ignitorContinuity;
    pad->currentReading = current;
    pad->hardwareSafe = ignitorContinuity && (current < CURRENT_THRESHOLD_MA);
    pad->lastHeartbeat = millis();
    
    stateChanged = true;
}

void StateManager::updateSystemSafety(float voltage, float temp, float humidity, float wind) {
    systemStatus.systemVoltage = voltage;
    systemStatus.temperature = temp;
    systemStatus.humidity = humidity;
    systemStatus.windSpeed = wind;
    
    stateChanged = true;
}

bool StateManager::canArm() const {
    return systemStatus.state == STATE_IDLE &&
           systemStatus.groupAuthorized &&
           !systemStatus.abortActive &&
           systemStatus.hardwareSafe;
}

bool StateManager::canLaunch() const {
    return systemStatus.state == STATE_ARMED &&
           isLaunchSafe();
}

String StateManager::toJson() const {
    DynamicJsonDocument doc(2048);
    
    doc["version"] = VERSION;
    doc["timestamp"] = millis();
    doc["system_state"] = getSystemStateString();
    doc["group_authorized"] = systemStatus.groupAuthorized;
    doc["abort_active"] = systemStatus.abortActive;
    doc["hardware_safe"] = systemStatus.hardwareSafe;
    doc["can_arm"] = canArm();
    doc["can_launch"] = canLaunch();
    
    // System readings
    JsonObject readings = doc.createNestedObject("readings");
    readings["voltage"] = systemStatus.systemVoltage;
    readings["temperature"] = systemStatus.temperature;
    readings["humidity"] = systemStatus.humidity;
    readings["wind_speed"] = systemStatus.windSpeed;
    
    // Pads
    JsonArray padsArray = doc.createNestedArray("pads");
    for (const auto& pad : pads) {
        JsonObject padObj = padsArray.createNestedObject();
        padObj["pad_id"] = pad.padId;
        padObj["player_name"] = pad.playerName;
        padObj["state"] = (int)pad.state;
        padObj["auth_state"] = (int)pad.authState;
        padObj["ignitor_continuity"] = pad.ignitorContinuity;
        padObj["current"] = pad.currentReading;
        padObj["hardware_safe"] = pad.hardwareSafe;
        padObj["abort_acknowledged"] = pad.abortAcknowledged;
    }
    
    if (!systemStatus.lastError.isEmpty()) {
        doc["last_error"] = systemStatus.lastError;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String StateManager::getAllPadsJson() const {
    DynamicJsonDocument doc(1024);
    JsonArray padsArray = doc.createNestedArray("pads");
    
    for (const auto& pad : pads) {
        JsonObject padObj = padsArray.createNestedObject();
        padObj["pad_id"] = pad.padId;
        padObj["player_name"] = pad.playerName;
        padObj["state"] = (int)pad.state;
        padObj["auth_state"] = (int)pad.authState;
        padObj["ignitor_continuity"] = pad.ignitorContinuity;
        padObj["current"] = pad.currentReading;
        padObj["hardware_safe"] = pad.hardwareSafe;
        padObj["abort_acknowledged"] = pad.abortAcknowledged;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void StateManager::setGameSetting(const String& key, const String& value) {
    gameSettings[key] = value;
    stateChanged = true;
}

String StateManager::getGameSetting(const String& key, const String& defaultValue) const {
    auto it = gameSettings.find(key);
    return (it != gameSettings.end()) ? it->second : defaultValue;
}

void StateManager::logState() const {
    logger.debug("StateManager State: " + getSystemStateString() + 
                ", Pads: " + String(pads.size()) +
                ", Group Auth: " + String(systemStatus.groupAuthorized) +
                ", Abort: " + String(systemStatus.abortActive) +
                ", Safe: " + String(systemStatus.hardwareSafe));
}

String StateManager::getDiagnosticsJson() const {
    DynamicJsonDocument doc(1024);
    
    doc["system_state"] = getSystemStateString();
    doc["group_authorized"] = systemStatus.groupAuthorized;
    doc["abort_active"] = systemStatus.abortActive;
    doc["hardware_safe"] = systemStatus.hardwareSafe;
    doc["active_pads"] = getActivePadCount();
    doc["last_state_update"] = lastStateUpdate;
    doc["state_changed"] = stateChanged;
    
    // Pad states
    JsonArray padsArray = doc.createNestedArray("pads");
    for (const auto& pad : pads) {
        JsonObject padObj = padsArray.createNestedObject();
        padObj["pad_id"] = pad.padId;
        padObj["player"] = pad.playerName;
        padObj["state"] = (int)pad.state;
        padObj["auth_state"] = (int)pad.authState;
        padObj["hardware_safe"] = pad.hardwareSafe;
        padObj["last_heartbeat"] = pad.lastHeartbeat;
    }
    
    // Game settings
    JsonObject settings = doc.createNestedObject("game_settings");
    for (const auto& setting : gameSettings) {
        settings[setting.first] = setting.second;
    }
    
    if (!systemStatus.lastError.isEmpty()) {
        doc["last_error"] = systemStatus.lastError;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}