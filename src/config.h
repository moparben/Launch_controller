#ifndef CONFIG_H
#define CONFIG_H

// Version information
#define VERSION "3.5.0709"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// WiFi Configuration
#define WIFI_SSID "Launch_Controller_AP"
#define WIFI_PASSWORD "RocketLaunch2024"
#define AP_MODE_DEFAULT true
#define WIFI_TIMEOUT_MS 30000

// Hardware Pin Definitions
// Ignitor Control Pins (MOSFETs)
#define IGNITOR_1_PIN 25
#define IGNITOR_2_PIN 26
#define IGNITOR_3_PIN 27
#define IGNITOR_4_PIN 32

// Current Sensing Pins (ACS712)
#define CURRENT_1_PIN 34
#define CURRENT_2_PIN 35
#define CURRENT_3_PIN 36
#define CURRENT_4_PIN 39

// Servo Control Pins
#define SERVO_1_PIN 16
#define SERVO_2_PIN 17
#define SERVO_3_PIN 18
#define SERVO_4_PIN 19

// Sensor Pins
#define DHT_PIN 21
#define DHT_TYPE DHT11
#define THERMISTOR_1_PIN 33
#define THERMISTOR_2_PIN 32
#define WIND_SENSOR_PIN 4
#define VOLTAGE_MONITOR_PIN 2

// Display Pins (SPI)
#define TFT_CS 5
#define TFT_DC 22
#define TFT_RST 23
#define TFT_TOUCH_CS 15

// CAN Bus Pins
#define CAN_TX_PIN 13
#define CAN_RX_PIN 14

// SD Card Pins
#define SD_CS_PIN 12
#define SD_MOSI_PIN 23
#define SD_MISO_PIN 19
#define SD_SCK_PIN 18

// Status LED Pin
#define STATUS_LED_PIN 2

// Safety and Timing Configuration
#define IGNITOR_FIRE_DURATION_MS 2000
#define IGNITOR_SAFETY_TIMEOUT_MS 30000
#define CURRENT_THRESHOLD_MA 100
#define ABORT_ACKNOWLEDGMENT_TIMEOUT_MS 60000
#define HEARTBEAT_INTERVAL_MS 1000
#define SENSOR_READ_INTERVAL_MS 500

// Group Authorization Configuration
#define MAX_PADS 4
#define MAX_PLAYERS 8
#define AUTHORIZATION_TIMEOUT_MS 300000  // 5 minutes

// Web Server Configuration
#define WEB_SERVER_PORT 80
#define WEBSOCKET_PORT 81
#define MAX_CONCURRENT_CLIENTS 10

// Logging Configuration
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_ERROR 3
#define DEFAULT_LOG_LEVEL LOG_LEVEL_INFO
#define LOG_FILE_MAX_SIZE 1048576  // 1MB
#define LOG_ROTATION_COUNT 5

// Game Mode Configuration
#define GAME_MODE_SINGLE 0
#define GAME_MODE_GROUP 1
#define GAME_MODE_TOURNAMENT 2
#define DEFAULT_GAME_MODE GAME_MODE_GROUP

// Safety Limits
#define MAX_VOLTAGE_V 15.0
#define MIN_VOLTAGE_V 10.0
#define MAX_TEMPERATURE_C 60.0
#define MIN_TEMPERATURE_C -20.0
#define MAX_WIND_SPEED_MS 15.0

#endif // CONFIG_H