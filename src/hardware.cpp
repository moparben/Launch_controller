#include "hardware.h"
#include "state_manager.h"

HardwareManager hardware;

HardwareManager::HardwareManager() : dht(DHT_PIN, DHT_TYPE) {
    lastSensorRead = 0;
    lastCurrentRead = 0;
    initialized = false;
    
    // Initialize ignitor status
    for (int i = 0; i < MAX_PADS; i++) {
        ignitors[i].armed = false;
        ignitors[i].firing = false;
        ignitors[i].continuity = false;
        ignitors[i].current = 0.0;
        ignitors[i].fireStartTime = 0;
        ignitors[i].lastCurrentRead = 0;
    }
    
    // Initialize sensor readings
    lastReadings.temperature = 0.0;
    lastReadings.humidity = 0.0;
    lastReadings.voltage = 0.0;
    lastReadings.windSpeed = 0.0;
    lastReadings.thermistor1 = 0.0;
    lastReadings.thermistor2 = 0.0;
    lastReadings.dhtValid = false;
    lastReadings.timestamp = 0;
}

bool HardwareManager::begin() {
    logger.info("Hardware: Initializing...");
    
    // Check for pin conflicts first
    if (!checkPinConflicts()) {
        logger.error("Hardware: Pin conflicts detected!");
        return false;
    }
    
    // Initialize ignitor pins
    const int ignitorPins[] = {IGNITOR_1_PIN, IGNITOR_2_PIN, IGNITOR_3_PIN, IGNITOR_4_PIN};
    for (int i = 0; i < MAX_PADS; i++) {
        pinMode(ignitorPins[i], OUTPUT);
        digitalWrite(ignitorPins[i], LOW);
        logger.debug("Hardware: Ignitor " + String(i) + " pin " + String(ignitorPins[i]) + " initialized");
    }
    
    // Initialize current sensing pins
    const int currentPins[] = {CURRENT_1_PIN, CURRENT_2_PIN, CURRENT_3_PIN, CURRENT_4_PIN};
    for (int i = 0; i < MAX_PADS; i++) {
        pinMode(currentPins[i], INPUT);
        logger.debug("Hardware: Current sensor " + String(i) + " pin " + String(currentPins[i]) + " initialized");
    }
    
    // Initialize servo pins
    const int servoPins[] = {SERVO_1_PIN, SERVO_2_PIN, SERVO_3_PIN, SERVO_4_PIN};
    for (int i = 0; i < MAX_PADS; i++) {
        if (servos[i].attach(servoPins[i])) {
            servos[i].write(90); // Center position
            logger.debug("Hardware: Servo " + String(i) + " attached to pin " + String(servoPins[i]));
        } else {
            logger.error("Hardware: Failed to attach servo " + String(i));
        }
    }
    
    // Initialize DHT sensor
    dht.begin();
    delay(2000); // DHT needs time to stabilize
    
    // Initialize other sensor pins
    pinMode(THERMISTOR_1_PIN, INPUT);
    pinMode(THERMISTOR_2_PIN, INPUT);
    pinMode(WIND_SENSOR_PIN, INPUT);
    pinMode(VOLTAGE_MONITOR_PIN, INPUT);
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    setStatusLED(false);
    
    // Initialize CAN bus
    CAN.setPins(CAN_RX_PIN, CAN_TX_PIN);
    if (CAN.begin(500E3)) {
        logger.info("Hardware: CAN bus initialized at 500kbps");
    } else {
        logger.warn("Hardware: CAN bus initialization failed");
    }
    
    initialized = true;
    logger.info("Hardware: Initialization complete");
    
    // Run initial diagnostics
    runDiagnostics();
    
    return true;
}

void HardwareManager::update() {
    if (!initialized) {
        return;
    }
    
    unsigned long now = millis();
    
    // Read sensors periodically
    if (now - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
        readSensors();
        lastSensorRead = now;
    }
    
    // Read current more frequently for safety
    if (now - lastCurrentRead >= 100) { // 10Hz
        for (int i = 0; i < MAX_PADS; i++) {
            const int currentPins[] = {CURRENT_1_PIN, CURRENT_2_PIN, CURRENT_3_PIN, CURRENT_4_PIN};
            ignitors[i].current = readCurrent(currentPins[i]);
            ignitors[i].continuity = checkIgnitorContinuity(currentPins[i]);
        }
        lastCurrentRead = now;
        updateIgnitorSafety();
    }
    
    // Handle ignitor firing timeouts
    for (int i = 0; i < MAX_PADS; i++) {
        if (ignitors[i].firing) {
            if (now - ignitors[i].fireStartTime >= IGNITOR_FIRE_DURATION_MS) {
                disarmIgnitor(i);
                logger.info("Hardware: Ignitor " + String(i) + " fire duration complete");
            }
        }
    }
}

bool HardwareManager::readSensors() {
    if (!initialized) {
        return false;
    }
    
    // Read DHT sensor
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    lastReadings.dhtValid = !isnan(temp) && !isnan(humidity);
    if (lastReadings.dhtValid) {
        lastReadings.temperature = temp;
        lastReadings.humidity = humidity;
    }
    
    // Read other sensors
    lastReadings.voltage = readVoltage();
    lastReadings.windSpeed = readWindSpeed();
    lastReadings.thermistor1 = readThermistor(THERMISTOR_1_PIN);
    lastReadings.thermistor2 = readThermistor(THERMISTOR_2_PIN);
    lastReadings.timestamp = millis();
    
    // Update state manager with readings
    stateManager.updateSystemSafety(
        lastReadings.voltage,
        lastReadings.temperature,
        lastReadings.humidity,
        lastReadings.windSpeed
    );
    
    return true;
}

float HardwareManager::readVoltage() {
    int raw = analogRead(VOLTAGE_MONITOR_PIN);
    // Voltage divider: 15V -> 3.3V (4.55:1 ratio)
    // ADC: 0-4095 -> 0-3.3V
    float voltage = (raw / 4095.0) * 3.3 * 4.55;
    return voltage;
}

float HardwareManager::readThermistor(int pin) {
    int raw = analogRead(pin);
    // Basic thermistor calculation (10k NTC)
    // This is simplified - use proper Steinhart-Hart equation for accuracy
    float resistance = 10000.0 * ((4095.0 / raw) - 1.0);
    float temp = 1.0 / (log(resistance / 10000.0) / 3950.0 + 1.0 / 298.15) - 273.15;
    return temp;
}

float HardwareManager::readWindSpeed() {
    int raw = analogRead(WIND_SENSOR_PIN);
    // Convert to wind speed based on sensor specification
    // This is sensor-specific, adjust for your wind sensor
    float voltage = (raw / 4095.0) * 3.3;
    float windSpeed = voltage * 10.0; // Example conversion
    return windSpeed;
}

float HardwareManager::readCurrent(int pin) {
    int raw = analogRead(pin);
    // ACS712 current sensor: 2.5V at 0A, 185mV/A for 5A model
    float voltage = (raw / 4095.0) * 3.3;
    float current = (voltage - 2.5) / 0.185; // Convert to amps
    return abs(current * 1000.0); // Return as milliamps, absolute value
}

bool HardwareManager::checkIgnitorContinuity(int pin) {
    // Simple continuity check based on current reading
    float current = readCurrent(pin);
    return current > 5.0 && current < 50.0; // Expect small current for continuity
}

void HardwareManager::updateIgnitorSafety() {
    for (int i = 0; i < MAX_PADS; i++) {
        bool safe = ignitors[i].continuity && (ignitors[i].current < CURRENT_THRESHOLD_MA);
        stateManager.updatePadSafety(i, ignitors[i].continuity, ignitors[i].current);
    }
}

bool HardwareManager::armIgnitor(uint8_t padId) {
    if (padId >= MAX_PADS) {
        logger.error("Hardware: Invalid pad ID for arming: " + String(padId));
        return false;
    }
    
    if (!stateManager.canArm()) {
        logger.warn("Hardware: Cannot arm ignitor " + String(padId) + " - system not ready");
        return false;
    }
    
    if (!ignitors[padId].continuity) {
        logger.error("Hardware: Cannot arm ignitor " + String(padId) + " - no continuity");
        return false;
    }
    
    ignitors[padId].armed = true;
    logger.info("Hardware: Armed ignitor " + String(padId));
    
    return true;
}

bool HardwareManager::disarmIgnitor(uint8_t padId) {
    if (padId >= MAX_PADS) {
        return false;
    }
    
    const int ignitorPins[] = {IGNITOR_1_PIN, IGNITOR_2_PIN, IGNITOR_3_PIN, IGNITOR_4_PIN};
    
    ignitors[padId].armed = false;
    ignitors[padId].firing = false;
    digitalWrite(ignitorPins[padId], LOW);
    
    logger.info("Hardware: Disarmed ignitor " + String(padId));
    return true;
}

bool HardwareManager::fireIgnitor(uint8_t padId) {
    if (padId >= MAX_PADS) {
        logger.error("Hardware: Invalid pad ID for firing: " + String(padId));
        return false;
    }
    
    if (!stateManager.canLaunch()) {
        logger.warn("Hardware: Cannot fire ignitor " + String(padId) + " - system not ready for launch");
        return false;
    }
    
    if (!ignitors[padId].armed) {
        logger.error("Hardware: Cannot fire ignitor " + String(padId) + " - not armed");
        return false;
    }
    
    if (!ignitors[padId].continuity) {
        logger.error("Hardware: Cannot fire ignitor " + String(padId) + " - no continuity");
        return false;
    }
    
    const int ignitorPins[] = {IGNITOR_1_PIN, IGNITOR_2_PIN, IGNITOR_3_PIN, IGNITOR_4_PIN};
    
    ignitors[padId].firing = true;
    ignitors[padId].fireStartTime = millis();
    digitalWrite(ignitorPins[padId], HIGH);
    
    logger.info("Hardware: Fired ignitor " + String(padId));
    
    return true;
}

void HardwareManager::emergencyDisarmAll() {
    logger.warn("Hardware: Emergency disarm all ignitors");
    
    for (int i = 0; i < MAX_PADS; i++) {
        disarmIgnitor(i);
    }
    
    stateManager.initiateAbort(255); // System-initiated abort
}

bool HardwareManager::setServoPosition(uint8_t padId, int angle) {
    if (padId >= MAX_PADS) {
        return false;
    }
    
    if (!servos[padId].attached()) {
        logger.error("Hardware: Servo " + String(padId) + " not attached");
        return false;
    }
    
    angle = constrain(angle, 0, 180);
    servos[padId].write(angle);
    
    logger.debug("Hardware: Set servo " + String(padId) + " to " + String(angle) + " degrees");
    return true;
}

int HardwareManager::getServoPosition(uint8_t padId) const {
    if (padId >= MAX_PADS || !servos[padId].attached()) {
        return -1;
    }
    return servos[padId].read();
}

bool HardwareManager::isServoAttached(uint8_t padId) const {
    if (padId >= MAX_PADS) {
        return false;
    }
    return servos[padId].attached();
}

bool HardwareManager::sendCANMessage(uint32_t id, const uint8_t* data, size_t length) {
    if (!CAN.beginPacket(id)) {
        return false;
    }
    
    for (size_t i = 0; i < length && i < 8; i++) {
        CAN.write(data[i]);
    }
    
    return CAN.endPacket() == 1;
}

bool HardwareManager::receiveCANMessage(uint32_t& id, uint8_t* data, size_t& length) {
    if (CAN.parsePacket() == 0) {
        return false;
    }
    
    id = CAN.packetId();
    length = 0;
    
    while (CAN.available() && length < 8) {
        data[length++] = CAN.read();
    }
    
    return true;
}

bool HardwareManager::performHardwareSafetyCheck() {
    bool safe = true;
    String errors = "";
    
    // Check voltage
    if (lastReadings.voltage < MIN_VOLTAGE_V || lastReadings.voltage > MAX_VOLTAGE_V) {
        safe = false;
        errors += "Voltage: " + String(lastReadings.voltage) + "V; ";
    }
    
    // Check temperature
    if (lastReadings.dhtValid) {
        if (lastReadings.temperature < MIN_TEMPERATURE_C || lastReadings.temperature > MAX_TEMPERATURE_C) {
            safe = false;
            errors += "Temperature: " + String(lastReadings.temperature) + "C; ";
        }
    }
    
    // Check wind
    if (lastReadings.windSpeed > MAX_WIND_SPEED_MS) {
        safe = false;
        errors += "Wind: " + String(lastReadings.windSpeed) + "m/s; ";
    }
    
    // Check ignitor safety
    for (int i = 0; i < MAX_PADS; i++) {
        if (ignitors[i].current > CURRENT_THRESHOLD_MA) {
            safe = false;
            errors += "Ignitor " + String(i) + " current: " + String(ignitors[i].current) + "mA; ";
        }
    }
    
    if (!safe) {
        logger.error("Hardware safety check failed: " + errors);
    }
    
    return safe;
}

bool HardwareManager::isSystemSafe() const {
    return performHardwareSafetyCheck();
}

void HardwareManager::setStatusLED(bool state) {
    digitalWrite(STATUS_LED_PIN, state ? HIGH : LOW);
}

void HardwareManager::blinkStatusLED(int count, int delayMs) {
    for (int i = 0; i < count; i++) {
        setStatusLED(true);
        delay(delayMs);
        setStatusLED(false);
        if (i < count - 1) {
            delay(delayMs);
        }
    }
}

void HardwareManager::runDiagnostics() {
    logger.info("Hardware: Running diagnostics...");
    
    // Test status LED
    blinkStatusLED(3, 100);
    
    // Test sensors
    readSensors();
    logger.info("Hardware: Sensor readings - V:" + String(lastReadings.voltage) + 
               "V, T:" + String(lastReadings.temperature) + 
               "C, H:" + String(lastReadings.humidity) + 
               "%, W:" + String(lastReadings.windSpeed) + "m/s");
    
    // Test ignitor continuity
    for (int i = 0; i < MAX_PADS; i++) {
        logger.info("Hardware: Ignitor " + String(i) + 
                   " - Continuity: " + String(ignitors[i].continuity) +
                   ", Current: " + String(ignitors[i].current) + "mA");
    }
    
    // Test servos
    for (int i = 0; i < MAX_PADS; i++) {
        if (servos[i].attached()) {
            servos[i].write(45);
            delay(200);
            servos[i].write(135);
            delay(200);
            servos[i].write(90);
            logger.info("Hardware: Servo " + String(i) + " test complete");
        }
    }
    
    logger.info("Hardware: Diagnostics complete");
}

bool HardwareManager::checkPinConflicts() {
    // Simple pin conflict check
    // In a real implementation, create a comprehensive pin mapping check
    std::vector<int> usedPins;
    
    // Add all pins to list
    const int ignitorPins[] = {IGNITOR_1_PIN, IGNITOR_2_PIN, IGNITOR_3_PIN, IGNITOR_4_PIN};
    const int currentPins[] = {CURRENT_1_PIN, CURRENT_2_PIN, CURRENT_3_PIN, CURRENT_4_PIN};
    const int servoPins[] = {SERVO_1_PIN, SERVO_2_PIN, SERVO_3_PIN, SERVO_4_PIN};
    
    for (int i = 0; i < MAX_PADS; i++) {
        usedPins.push_back(ignitorPins[i]);
        usedPins.push_back(currentPins[i]);
        usedPins.push_back(servoPins[i]);
    }
    
    usedPins.push_back(DHT_PIN);
    usedPins.push_back(THERMISTOR_1_PIN);
    usedPins.push_back(THERMISTOR_2_PIN);
    usedPins.push_back(WIND_SENSOR_PIN);
    usedPins.push_back(VOLTAGE_MONITOR_PIN);
    usedPins.push_back(TFT_CS);
    usedPins.push_back(TFT_DC);
    usedPins.push_back(TFT_RST);
    usedPins.push_back(TFT_TOUCH_CS);
    usedPins.push_back(CAN_TX_PIN);
    usedPins.push_back(CAN_RX_PIN);
    usedPins.push_back(SD_CS_PIN);
    usedPins.push_back(STATUS_LED_PIN);
    
    // Check for duplicates
    std::sort(usedPins.begin(), usedPins.end());
    for (size_t i = 1; i < usedPins.size(); i++) {
        if (usedPins[i] == usedPins[i-1]) {
            logger.error("Hardware: Pin conflict detected on pin " + String(usedPins[i]));
            return false;
        }
    }
    
    return true;
}

String HardwareManager::getSensorJson() const {
    DynamicJsonDocument doc(512);
    
    doc["temperature"] = lastReadings.temperature;
    doc["humidity"] = lastReadings.humidity;
    doc["voltage"] = lastReadings.voltage;
    doc["wind_speed"] = lastReadings.windSpeed;
    doc["thermistor1"] = lastReadings.thermistor1;
    doc["thermistor2"] = lastReadings.thermistor2;
    doc["dht_valid"] = lastReadings.dhtValid;
    doc["timestamp"] = lastReadings.timestamp;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String HardwareManager::getIgnitorStatusJson() const {
    DynamicJsonDocument doc(1024);
    JsonArray ignitorArray = doc.createNestedArray("ignitors");
    
    for (int i = 0; i < MAX_PADS; i++) {
        JsonObject ignitor = ignitorArray.createNestedObject();
        ignitor["pad_id"] = i;
        ignitor["armed"] = ignitors[i].armed;
        ignitor["firing"] = ignitors[i].firing;
        ignitor["continuity"] = ignitors[i].continuity;
        ignitor["current"] = ignitors[i].current;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String HardwareManager::getDiagnosticsJson() const {
    DynamicJsonDocument doc(1024);
    
    doc["initialized"] = initialized;
    doc["system_safe"] = isSystemSafe();
    doc["last_sensor_read"] = lastSensorRead;
    doc["last_current_read"] = lastCurrentRead;
    
    // Add sensor status
    JsonObject sensors = doc.createNestedObject("sensors");
    sensors["dht_valid"] = lastReadings.dhtValid;
    sensors["voltage"] = lastReadings.voltage;
    sensors["temperature"] = lastReadings.temperature;
    sensors["humidity"] = lastReadings.humidity;
    sensors["wind_speed"] = lastReadings.windSpeed;
    
    // Add ignitor status
    JsonArray ignitorArray = doc.createNestedArray("ignitors");
    for (int i = 0; i < MAX_PADS; i++) {
        JsonObject ignitor = ignitorArray.createNestedObject();
        ignitor["pad_id"] = i;
        ignitor["continuity"] = ignitors[i].continuity;
        ignitor["current"] = ignitors[i].current;
        ignitor["armed"] = ignitors[i].armed;
        ignitor["firing"] = ignitors[i].firing;
    }
    
    // Add servo status
    JsonArray servoArray = doc.createNestedArray("servos");
    for (int i = 0; i < MAX_PADS; i++) {
        JsonObject servo = servoArray.createNestedObject();
        servo["pad_id"] = i;
        servo["attached"] = servos[i].attached();
        servo["position"] = servos[i].attached() ? servos[i].read() : -1;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}