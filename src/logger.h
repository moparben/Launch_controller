#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>
#include "config.h"

class Logger {
private:
    bool sdAvailable;
    bool spiffsAvailable;
    String currentLogFile;
    int logLevel;
    unsigned long lastRotationCheck;
    
    void rotateLogFiles();
    String getTimestamp();
    void writeToFile(const String& message);
    void writeToSerial(const String& message);
    
public:
    Logger();
    
    bool begin();
    void setLogLevel(int level) { logLevel = level; }
    
    void debug(const String& message);
    void info(const String& message);
    void warn(const String& message);
    void error(const String& message);
    
    void log(int level, const String& message);
    
    String getLogFile() const { return currentLogFile; }
    bool isSDAvailable() const { return sdAvailable; }
    bool isSPIFFSAvailable() const { return spiffsAvailable; }
    
    // For web interface
    String getRecentLogs(int lines = 100);
    void downloadLogs(AsyncWebServerRequest* request);
};

extern Logger logger;

#endif // LOGGER_H