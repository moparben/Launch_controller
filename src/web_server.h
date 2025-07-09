#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "config.h"
#include "state_manager.h"
#include "hardware.h"
#include "logger.h"

class WebServerManager {
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    
    bool wifiConnected;
    bool apMode;
    unsigned long lastHeartbeat;
    unsigned long lastClientUpdate;
    
    // Request handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleAPI(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);
    void handleAuth(AsyncWebServerRequest *request);
    void handleAbort(AsyncWebServerRequest *request);
    void handleArm(AsyncWebServerRequest *request);
    void handleFire(AsyncWebServerRequest *request);
    void handleServo(AsyncWebServerRequest *request);
    void handleSettings(AsyncWebServerRequest *request);
    void handleLogs(AsyncWebServerRequest *request);
    void handleUpdate(AsyncWebServerRequest *request);
    void handleDiagnostics(AsyncWebServerRequest *request);
    
    // WebSocket handlers
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                   AwsEventType type, void *arg, uint8_t *data, size_t len);
    void handleWsMessage(AsyncWebSocketClient *client, const String& message);
    
    // Utility methods
    void sendJsonResponse(AsyncWebServerRequest *request, const String& json, int code = 200);
    void sendErrorResponse(AsyncWebServerRequest *request, const String& error, int code = 400);
    void broadcastUpdate();
    String getClientInfo(AsyncWebServerRequest *request);
    bool validateRequest(AsyncWebServerRequest *request);
    
public:
    WebServerManager();
    
    bool begin();
    void update();
    void stop();
    
    // WiFi management
    bool connectWiFi(const String& ssid, const String& password);
    bool startAP(const String& ssid, const String& password);
    bool isConnected() const { return wifiConnected; }
    bool isAPMode() const { return apMode; }
    String getIPAddress() const;
    
    // Client management
    void broadcastMessage(const String& message);
    void sendToClient(uint32_t clientId, const String& message);
    int getConnectedClients() const;
    
    // Status
    String getNetworkStatusJson() const;
};

extern WebServerManager webServer;

#endif // WEB_SERVER_H