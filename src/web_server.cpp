#include "web_server.h"

WebServerManager webServer;

WebServerManager::WebServerManager() : server(WEB_SERVER_PORT), ws("/ws") {
    wifiConnected = false;
    apMode = false;
    lastHeartbeat = 0;
    lastClientUpdate = 0;
}

bool WebServerManager::begin() {
    logger.info("WebServer: Starting...");
    
    // Setup WiFi
    if (AP_MODE_DEFAULT) {
        if (!startAP(WIFI_SSID, WIFI_PASSWORD)) {
            logger.error("WebServer: Failed to start AP mode");
            return false;
        }
    } else {
        if (!connectWiFi(WIFI_SSID, WIFI_PASSWORD)) {
            logger.warn("WebServer: Failed to connect to WiFi, starting AP mode");
            if (!startAP(WIFI_SSID, WIFI_PASSWORD)) {
                logger.error("WebServer: Failed to start AP mode");
                return false;
            }
        }
    }
    
    // Setup WebSocket
    ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        onWsEvent(server, client, type, arg, data, len);
    });
    server.addHandler(&ws);
    
    // Setup static file serving
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API endpoints
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStatus(request);
    });
    
    server.on("/api/authorize", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleAuth(request);
    });
    
    server.on("/api/abort", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleAbort(request);
    });
    
    server.on("/api/arm", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleArm(request);
    });
    
    server.on("/api/fire", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleFire(request);
    });
    
    server.on("/api/servo", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleServo(request);
    });
    
    server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleSettings(request);
    });
    
    server.on("/api/settings", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSettings(request);
    });
    
    server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleLogs(request);
    });
    
    server.on("/api/diagnostics", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleDiagnostics(request);
    });
    
    // OTA Update endpoint
    server.on("/api/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleUpdate(request);
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // OTA update handler
        if (!index) {
            logger.info("WebServer: OTA Update Start: " + filename);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
                return;
            }
        }
        
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
            return;
        }
        
        if (final) {
            if (Update.end(true)) {
                logger.info("WebServer: OTA Update Success");
            } else {
                Update.printError(Serial);
            }
        }
    });
    
    // Default handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
    
    server.begin();
    logger.info("WebServer: Started on " + getIPAddress() + ":" + String(WEB_SERVER_PORT));
    
    return true;
}

void WebServerManager::update() {
    unsigned long now = millis();
    
    // Send periodic updates to clients
    if (now - lastClientUpdate >= HEARTBEAT_INTERVAL_MS) {
        broadcastUpdate();
        lastClientUpdate = now;
    }
    
    // Clean up disconnected WebSocket clients
    ws.cleanupClients();
}

bool WebServerManager::connectWiFi(const String& ssid, const String& password) {
    logger.info("WebServer: Connecting to WiFi: " + ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        apMode = false;
        logger.info("WebServer: WiFi connected, IP: " + WiFi.localIP().toString());
        return true;
    } else {
        logger.error("WebServer: WiFi connection failed");
        return false;
    }
}

bool WebServerManager::startAP(const String& ssid, const String& password) {
    logger.info("WebServer: Starting AP: " + ssid);
    
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(ssid.c_str(), password.c_str())) {
        wifiConnected = true;
        apMode = true;
        logger.info("WebServer: AP started, IP: " + WiFi.softAPIP().toString());
        return true;
    } else {
        logger.error("WebServer: AP start failed");
        return false;
    }
}

String WebServerManager::getIPAddress() const {
    if (apMode) {
        return WiFi.softAPIP().toString();
    } else {
        return WiFi.localIP().toString();
    }
}

void WebServerManager::handleStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    
    // System status
    doc["version"] = VERSION;
    doc["uptime"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_connected"] = wifiConnected;
    doc["ap_mode"] = apMode;
    doc["ip_address"] = getIPAddress();
    doc["connected_clients"] = getConnectedClients();
    
    // Merge state manager status
    String stateJson = stateManager.toJson();
    DynamicJsonDocument stateDoc(2048);
    deserializeJson(stateDoc, stateJson);
    
    for (JsonPair kv : stateDoc.as<JsonObject>()) {
        doc[kv.key()] = kv.value();
    }
    
    // Add hardware status
    String sensorJson = hardware.getSensorJson();
    DynamicJsonDocument sensorDoc(512);
    deserializeJson(sensorDoc, sensorJson);
    doc["sensors"] = sensorDoc.as<JsonObject>();
    
    String ignitorJson = hardware.getIgnitorStatusJson();
    DynamicJsonDocument ignitorDoc(1024);
    deserializeJson(ignitorDoc, ignitorJson);
    doc["ignitors"] = ignitorDoc.as<JsonObject>();
    
    String result;
    serializeJson(doc, result);
    sendJsonResponse(request, result);
}

void WebServerManager::handleAuth(AsyncWebServerRequest *request) {
    if (!validateRequest(request)) {
        sendErrorResponse(request, "Invalid request");
        return;
    }
    
    String body;
    if (request->hasParam("pad_id", true) && request->hasParam("player_name", true)) {
        int padId = request->getParam("pad_id", true)->value().toInt();
        String playerName = request->getParam("player_name", true)->value();
        
        if (padId < 0 || padId >= MAX_PADS) {
            sendErrorResponse(request, "Invalid pad ID");
            return;
        }
        
        if (playerName.isEmpty()) {
            sendErrorResponse(request, "Player name required");
            return;
        }
        
        // Add pad if it doesn't exist
        if (!stateManager.getPad(padId)) {
            if (!stateManager.addPad(padId, playerName)) {
                sendErrorResponse(request, "Failed to add pad");
                return;
            }
        }
        
        if (stateManager.authorizePad(padId, playerName)) {
            DynamicJsonDocument doc(256);
            doc["success"] = true;
            doc["message"] = "Pad " + String(padId) + " authorized for " + playerName;
            doc["pad_id"] = padId;
            doc["player_name"] = playerName;
            
            String result;
            serializeJson(doc, result);
            sendJsonResponse(request, result);
            
            logger.info("WebServer: Authorized pad " + String(padId) + " for " + playerName + 
                       " from " + getClientInfo(request));
        } else {
            sendErrorResponse(request, "Authorization failed");
        }
    } else {
        sendErrorResponse(request, "Missing pad_id or player_name");
    }
}

void WebServerManager::handleAbort(AsyncWebServerRequest *request) {
    if (!validateRequest(request)) {
        sendErrorResponse(request, "Invalid request");
        return;
    }
    
    int padId = 255; // System abort by default
    if (request->hasParam("pad_id", true)) {
        padId = request->getParam("pad_id", true)->value().toInt();
    }
    
    if (stateManager.initiateAbort(padId)) {
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["message"] = "Abort initiated";
        doc["initiated_by"] = padId == 255 ? "system" : String(padId);
        
        String result;
        serializeJson(doc, result);
        sendJsonResponse(request, result);
        
        logger.warn("WebServer: Abort initiated by " + 
                   (padId == 255 ? String("system") : String("pad ") + String(padId)) +
                   " from " + getClientInfo(request));
    } else {
        sendErrorResponse(request, "Abort failed");
    }
}

void WebServerManager::handleArm(AsyncWebServerRequest *request) {
    if (!validateRequest(request)) {
        sendErrorResponse(request, "Invalid request");
        return;
    }
    
    if (!request->hasParam("pad_id", true)) {
        sendErrorResponse(request, "Missing pad_id");
        return;
    }
    
    int padId = request->getParam("pad_id", true)->value().toInt();
    
    if (padId < 0 || padId >= MAX_PADS) {
        sendErrorResponse(request, "Invalid pad ID");
        return;
    }
    
    if (hardware.armIgnitor(padId)) {
        stateManager.setSystemState(STATE_ARMED);
        
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["message"] = "Ignitor " + String(padId) + " armed";
        doc["pad_id"] = padId;
        
        String result;
        serializeJson(doc, result);
        sendJsonResponse(request, result);
        
        logger.info("WebServer: Armed ignitor " + String(padId) + " from " + getClientInfo(request));
    } else {
        sendErrorResponse(request, "Arming failed");
    }
}

void WebServerManager::handleFire(AsyncWebServerRequest *request) {
    if (!validateRequest(request)) {
        sendErrorResponse(request, "Invalid request");
        return;
    }
    
    if (!request->hasParam("pad_id", true)) {
        sendErrorResponse(request, "Missing pad_id");
        return;
    }
    
    int padId = request->getParam("pad_id", true)->value().toInt();
    
    if (padId < 0 || padId >= MAX_PADS) {
        sendErrorResponse(request, "Invalid pad ID");
        return;
    }
    
    if (hardware.fireIgnitor(padId)) {
        stateManager.setSystemState(STATE_LAUNCHING);
        
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["message"] = "Ignitor " + String(padId) + " fired";
        doc["pad_id"] = padId;
        
        String result;
        serializeJson(doc, result);
        sendJsonResponse(request, result);
        
        logger.info("WebServer: Fired ignitor " + String(padId) + " from " + getClientInfo(request));
    } else {
        sendErrorResponse(request, "Fire failed");
    }
}

void WebServerManager::handleServo(AsyncWebServerRequest *request) {
    if (!validateRequest(request)) {
        sendErrorResponse(request, "Invalid request");
        return;
    }
    
    if (!request->hasParam("pad_id", true) || !request->hasParam("angle", true)) {
        sendErrorResponse(request, "Missing pad_id or angle");
        return;
    }
    
    int padId = request->getParam("pad_id", true)->value().toInt();
    int angle = request->getParam("angle", true)->value().toInt();
    
    if (padId < 0 || padId >= MAX_PADS) {
        sendErrorResponse(request, "Invalid pad ID");
        return;
    }
    
    if (angle < 0 || angle > 180) {
        sendErrorResponse(request, "Invalid angle (0-180)");
        return;
    }
    
    if (hardware.setServoPosition(padId, angle)) {
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["message"] = "Servo " + String(padId) + " set to " + String(angle) + " degrees";
        doc["pad_id"] = padId;
        doc["angle"] = angle;
        
        String result;
        serializeJson(doc, result);
        sendJsonResponse(request, result);
        
        logger.info("WebServer: Set servo " + String(padId) + " to " + String(angle) + 
                   " degrees from " + getClientInfo(request));
    } else {
        sendErrorResponse(request, "Servo control failed");
    }
}

void WebServerManager::handleSettings(AsyncWebServerRequest *request) {
    if (request->method() == HTTP_GET) {
        // Return current settings
        DynamicJsonDocument doc(1024);
        doc["version"] = VERSION;
        doc["wifi_ssid"] = WIFI_SSID;
        doc["ap_mode"] = apMode;
        doc["max_pads"] = MAX_PADS;
        doc["auth_timeout"] = AUTHORIZATION_TIMEOUT_MS;
        doc["abort_timeout"] = ABORT_ACKNOWLEDGMENT_TIMEOUT_MS;
        doc["fire_duration"] = IGNITOR_FIRE_DURATION_MS;
        doc["current_threshold"] = CURRENT_THRESHOLD_MA;
        
        String result;
        serializeJson(doc, result);
        sendJsonResponse(request, result);
    } else {
        // Update settings (simplified - in production, implement proper settings management)
        sendJsonResponse(request, "{\"success\":true,\"message\":\"Settings updated\"}");
    }
}

void WebServerManager::handleLogs(AsyncWebServerRequest *request) {
    int lines = 100;
    if (request->hasParam("lines")) {
        lines = request->getParam("lines")->value().toInt();
        lines = constrain(lines, 1, 1000);
    }
    
    String logs = logger.getRecentLogs(lines);
    
    DynamicJsonDocument doc(4096);
    doc["logs"] = logs;
    doc["lines_requested"] = lines;
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    sendJsonResponse(request, result);
}

void WebServerManager::handleDiagnostics(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    
    // System info
    doc["version"] = VERSION;
    doc["build_date"] = BUILD_DATE;
    doc["build_time"] = BUILD_TIME;
    doc["uptime"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["chip_id"] = ESP.getEfuseMac();
    doc["flash_size"] = ESP.getFlashChipSize();
    
    // Network info
    JsonObject network = doc.createNestedObject("network");
    network["wifi_connected"] = wifiConnected;
    network["ap_mode"] = apMode;
    network["ip_address"] = getIPAddress();
    network["connected_clients"] = getConnectedClients();
    
    // Hardware diagnostics
    String hwDiag = hardware.getDiagnosticsJson();
    DynamicJsonDocument hwDoc(1024);
    deserializeJson(hwDoc, hwDiag);
    doc["hardware"] = hwDoc.as<JsonObject>();
    
    // State manager diagnostics
    String stateDiag = stateManager.getDiagnosticsJson();
    DynamicJsonDocument stateDoc(1024);
    deserializeJson(stateDoc, stateDiag);
    doc["state"] = stateDoc.as<JsonObject>();
    
    String result;
    serializeJson(doc, result);
    sendJsonResponse(request, result);
}

void WebServerManager::handleUpdate(AsyncWebServerRequest *request) {
    bool success = !Update.hasError();
    
    DynamicJsonDocument doc(256);
    doc["success"] = success;
    doc["message"] = success ? "Update successful, rebooting..." : "Update failed";
    
    String result;
    serializeJson(doc, result);
    sendJsonResponse(request, result);
    
    if (success) {
        logger.info("WebServer: OTA update successful, rebooting...");
        delay(1000);
        ESP.restart();
    }
}

void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                                 AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            logger.info("WebServer: WebSocket client connected: " + client->remoteIP().toString());
            // Send initial status
            client->text(stateManager.toJson());
            break;
            
        case WS_EVT_DISCONNECT:
            logger.info("WebServer: WebSocket client disconnected");
            break;
            
        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                String message = String((char*)data).substring(0, len);
                handleWsMessage(client, message);
            }
            break;
        }
        
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServerManager::handleWsMessage(AsyncWebSocketClient *client, const String& message) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        logger.warn("WebServer: Invalid WebSocket JSON: " + message);
        return;
    }
    
    String command = doc["command"];
    
    if (command == "heartbeat") {
        // Respond to heartbeat
        DynamicJsonDocument response(256);
        response["type"] = "heartbeat";
        response["timestamp"] = millis();
        
        String result;
        serializeJson(response, result);
        client->text(result);
    }
    else if (command == "acknowledge_abort") {
        int padId = doc["pad_id"];
        if (stateManager.acknowledgeAbort(padId)) {
            logger.info("WebServer: Abort acknowledged by pad " + String(padId) + " via WebSocket");
        }
    }
    else {
        logger.warn("WebServer: Unknown WebSocket command: " + command);
    }
}

void WebServerManager::sendJsonResponse(AsyncWebServerRequest *request, const String& json, int code) {
    request->send(code, "application/json", json);
}

void WebServerManager::sendErrorResponse(AsyncWebServerRequest *request, const String& error, int code) {
    DynamicJsonDocument doc(256);
    doc["success"] = false;
    doc["error"] = error;
    
    String result;
    serializeJson(doc, result);
    sendJsonResponse(request, result, code);
}

void WebServerManager::broadcastUpdate() {
    if (stateManager.hasStateChanged()) {
        String statusJson = stateManager.toJson();
        broadcastMessage(statusJson);
        stateManager.clearStateChanged();
    }
}

void WebServerManager::broadcastMessage(const String& message) {
    ws.textAll(message);
}

void WebServerManager::sendToClient(uint32_t clientId, const String& message) {
    ws.text(clientId, message);
}

int WebServerManager::getConnectedClients() const {
    return ws.count();
}

String WebServerManager::getClientInfo(AsyncWebServerRequest *request) {
    return request->client()->remoteIP().toString() + ":" + String(request->client()->remotePort());
}

bool WebServerManager::validateRequest(AsyncWebServerRequest *request) {
    // Basic validation - in production, implement proper authentication
    return true;
}

String WebServerManager::getNetworkStatusJson() const {
    DynamicJsonDocument doc(512);
    
    doc["wifi_connected"] = wifiConnected;
    doc["ap_mode"] = apMode;
    doc["ip_address"] = getIPAddress();
    doc["connected_clients"] = getConnectedClients();
    
    if (!apMode && wifiConnected) {
        doc["wifi_ssid"] = WiFi.SSID();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["wifi_mac"] = WiFi.macAddress();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}