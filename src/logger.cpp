#include "logger.h"
#include <ESPAsyncWebServer.h>

Logger logger;

Logger::Logger() : sdAvailable(false), spiffsAvailable(false), 
                   logLevel(DEFAULT_LOG_LEVEL), lastRotationCheck(0) {
    currentLogFile = "/launch_controller.log";
}

bool Logger::begin() {
    // Try to initialize SD card first
    if (SD.begin(SD_CS_PIN)) {
        sdAvailable = true;
        Serial.println("Logger: SD card initialized");
    } else {
        Serial.println("Logger: SD card initialization failed");
    }
    
    // Initialize SPIFFS as fallback
    if (SPIFFS.begin(true)) {
        spiffsAvailable = true;
        Serial.println("Logger: SPIFFS initialized");
    } else {
        Serial.println("Logger: SPIFFS initialization failed");
    }
    
    if (!sdAvailable && !spiffsAvailable) {
        Serial.println("Logger: No file system available, using serial only");
        return false;
    }
    
    info("Logger: System started, version " + String(VERSION));
    return true;
}

String Logger::getTimestamp() {
    unsigned long ms = millis();
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    return String(hours % 24) + ":" + 
           String((minutes % 60) < 10 ? "0" : "") + String(minutes % 60) + ":" +
           String((seconds % 60) < 10 ? "0" : "") + String(seconds % 60) + "." +
           String((ms % 1000) < 100 ? (ms % 1000) < 10 ? "00" : "0" : "") + String(ms % 1000);
}

void Logger::writeToSerial(const String& message) {
    Serial.println(message);
}

void Logger::writeToFile(const String& message) {
    FS* fs = nullptr;
    
    if (sdAvailable) {
        fs = &SD;
    } else if (spiffsAvailable) {
        fs = &SPIFFS;
    } else {
        return;
    }
    
    File file = fs->open(currentLogFile, FILE_APPEND);
    if (file) {
        file.println(message);
        file.close();
        
        // Check for log rotation
        unsigned long now = millis();
        if (now - lastRotationCheck > 60000) { // Check every minute
            lastRotationCheck = now;
            rotateLogFiles();
        }
    }
}

void Logger::rotateLogFiles() {
    FS* fs = nullptr;
    
    if (sdAvailable) {
        fs = &SD;
    } else if (spiffsAvailable) {
        fs = &SPIFFS;
    } else {
        return;
    }
    
    File file = fs->open(currentLogFile, FILE_READ);
    if (file && file.size() > LOG_FILE_MAX_SIZE) {
        file.close();
        
        // Rotate existing log files
        for (int i = LOG_ROTATION_COUNT - 1; i > 0; i--) {
            String oldFile = currentLogFile + "." + String(i);
            String newFile = currentLogFile + "." + String(i + 1);
            
            if (fs->exists(oldFile)) {
                if (fs->exists(newFile)) {
                    fs->remove(newFile);
                }
                fs->rename(oldFile, newFile);
            }
        }
        
        // Move current log to .1
        String backupFile = currentLogFile + ".1";
        if (fs->exists(backupFile)) {
            fs->remove(backupFile);
        }
        fs->rename(currentLogFile, backupFile);
        
        info("Logger: Log files rotated");
    }
}

void Logger::log(int level, const String& message) {
    if (level < logLevel) {
        return;
    }
    
    String levelStr;
    switch (level) {
        case LOG_LEVEL_DEBUG: levelStr = "DEBUG"; break;
        case LOG_LEVEL_INFO:  levelStr = "INFO "; break;
        case LOG_LEVEL_WARN:  levelStr = "WARN "; break;
        case LOG_LEVEL_ERROR: levelStr = "ERROR"; break;
        default: levelStr = "UNKN "; break;
    }
    
    String fullMessage = "[" + getTimestamp() + "] " + levelStr + ": " + message;
    
    writeToSerial(fullMessage);
    writeToFile(fullMessage);
}

void Logger::debug(const String& message) {
    log(LOG_LEVEL_DEBUG, message);
}

void Logger::info(const String& message) {
    log(LOG_LEVEL_INFO, message);
}

void Logger::warn(const String& message) {
    log(LOG_LEVEL_WARN, message);
}

void Logger::error(const String& message) {
    log(LOG_LEVEL_ERROR, message);
}

String Logger::getRecentLogs(int lines) {
    FS* fs = nullptr;
    
    if (sdAvailable) {
        fs = &SD;
    } else if (spiffsAvailable) {
        fs = &SPIFFS;
    } else {
        return "No file system available";
    }
    
    File file = fs->open(currentLogFile, FILE_READ);
    if (!file) {
        return "Cannot open log file";
    }
    
    String result = "";
    String line = "";
    int lineCount = 0;
    
    // Simple implementation - read all and return last N lines
    // For production, implement a more efficient tail-like function
    std::vector<String> allLines;
    
    while (file.available()) {
        char c = file.read();
        if (c == '\n') {
            allLines.push_back(line);
            line = "";
        } else {
            line += c;
        }
    }
    
    if (!line.isEmpty()) {
        allLines.push_back(line);
    }
    
    file.close();
    
    // Return last N lines
    int startIndex = max(0, (int)allLines.size() - lines);
    for (int i = startIndex; i < allLines.size(); i++) {
        result += allLines[i] + "\n";
    }
    
    return result;
}