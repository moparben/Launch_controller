#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>

static const char *TAG = "ROCKET_LAUNCHER_BASIC";

/* GPIO pins for LEDs and buttons */
#define LED_ROCKET_STATUS    GPIO_NUM_15
#define LED_LAUNCH_INDICATOR GPIO_NUM_16
#define BUTTON_LAUNCH        GPIO_NUM_0
#define BUTTON_RESET         GPIO_NUM_1

/* Rocket launcher state */
typedef struct {
    bool launched;
    bool armed;
    uint32_t launch_count;
    char status[64];
    uint64_t last_launch_time;
} rocket_state_t;

static rocket_state_t rocket_state = {
    .launched = false,
    .armed = false,
    .launch_count = 0,
    .status = "Ready",
    .last_launch_time = 0
};

void init_gpio(void)
{
    ESP_LOGI(TAG, "Initializing GPIO for Basic Rocket Launcher");
    
    // Initialize LED pins
    gpio_reset_pin(LED_ROCKET_STATUS);
    gpio_reset_pin(LED_LAUNCH_INDICATOR);
    gpio_set_direction(LED_ROCKET_STATUS, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LAUNCH_INDICATOR, GPIO_MODE_OUTPUT);
    
    // Initialize button pins
    gpio_reset_pin(BUTTON_LAUNCH);
    gpio_reset_pin(BUTTON_RESET);
    gpio_set_direction(BUTTON_LAUNCH, GPIO_MODE_INPUT);
    gpio_set_direction(BUTTON_RESET, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_LAUNCH, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BUTTON_RESET, GPIO_PULLUP_ONLY);
    
    // Turn on status LED to show system is ready
    gpio_set_level(LED_ROCKET_STATUS, 1);
    ESP_LOGI(TAG, "GPIO initialization complete - Rocket Ready!");
}

void rocket_launch_sequence(void)
{
    if (!rocket_state.armed) {
        ESP_LOGW(TAG, "Launch failed - rocket not armed!");
        strcpy(rocket_state.status, "Launch failed - not armed");
        return;
    }
    
    ESP_LOGI(TAG, "🚀 INITIATING LAUNCH SEQUENCE! 🚀");
    strcpy(rocket_state.status, "LAUNCHING!");
    
    // Countdown sequence with LED flashing
    for (int countdown = 5; countdown > 0; countdown--) {
        ESP_LOGI(TAG, "Launch in %d...", countdown);
        gpio_set_level(LED_LAUNCH_INDICATOR, 1);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        gpio_set_level(LED_LAUNCH_INDICATOR, 0);
        vTaskDelay(700 / portTICK_PERIOD_MS);
    }
    
    // LAUNCH!
    ESP_LOGI(TAG, "🎆 ROCKET LAUNCHED! 🎆");
    rocket_state.launched = true;
    rocket_state.launch_count++;
    rocket_state.last_launch_time = esp_timer_get_time();
    
    // Rapid flash sequence for launch
    for (int i = 0; i < 20; i++) {
        gpio_set_level(LED_LAUNCH_INDICATOR, i % 2);
        gpio_set_level(LED_ROCKET_STATUS, (i + 1) % 2);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    // Return LEDs to normal state
    gpio_set_level(LED_ROCKET_STATUS, 1);
    gpio_set_level(LED_LAUNCH_INDICATOR, 0);
    
    strcpy(rocket_state.status, "Launch complete!");
    ESP_LOGI(TAG, "Launch complete! Total launches: %lu", rocket_state.launch_count);
}

void rocket_arm_system(void)
{
    rocket_state.armed = true;
    strcpy(rocket_state.status, "Armed and ready");
    ESP_LOGI(TAG, "💣 ROCKET SYSTEM ARMED! Ready for launch");
    
    // Slow flash sequence to indicate armed status
    for (int i = 0; i < 6; i++) {
        gpio_set_level(LED_ROCKET_STATUS, 0);
        gpio_set_level(LED_LAUNCH_INDICATOR, 1);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        gpio_set_level(LED_ROCKET_STATUS, 1);
        gpio_set_level(LED_LAUNCH_INDICATOR, 0);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void rocket_reset_system(void)
{
    rocket_state.launched = false;
    rocket_state.armed = false;
    strcpy(rocket_state.status, "Reset - ready");
    ESP_LOGI(TAG, "🔄 ROCKET SYSTEM RESET! Ready for next mission");
    
    // Reset sequence - all LEDs off then on
    gpio_set_level(LED_ROCKET_STATUS, 0);
    gpio_set_level(LED_LAUNCH_INDICATOR, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(LED_ROCKET_STATUS, 1);
    gpio_set_level(LED_LAUNCH_INDICATOR, 0);
}

void serial_command_task(void *pvParameter)
{
    char command_buffer[100];
    int buffer_index = 0;
    
    ESP_LOGI(TAG, "Serial command interface ready!");
    ESP_LOGI(TAG, "Commands: 'arm', 'launch', 'reset', 'status'");
    
    while (1) {
        int c = getchar();
        if (c != EOF && c != '\0') {
            if (c == '\n' || c == '\r') {
                command_buffer[buffer_index] = '\0';
                
                if (strcmp(command_buffer, "arm") == 0) {
                    rocket_arm_system();
                } else if (strcmp(command_buffer, "launch") == 0) {
                    rocket_launch_sequence();
                } else if (strcmp(command_buffer, "reset") == 0) {
                    rocket_reset_system();
                } else if (strcmp(command_buffer, "status") == 0) {
                    ESP_LOGI(TAG, "Status: %s | Armed: %s | Launched: %s | Count: %lu", 
                             rocket_state.status,
                             rocket_state.armed ? "YES" : "NO",
                             rocket_state.launched ? "YES" : "NO",
                             rocket_state.launch_count);
                } else if (strlen(command_buffer) > 0) {
                    ESP_LOGI(TAG, "Unknown command: %s", command_buffer);
                    ESP_LOGI(TAG, "Available commands: arm, launch, reset, status");
                }
                
                buffer_index = 0;
            } else if (buffer_index < sizeof(command_buffer) - 1) {
                command_buffer[buffer_index++] = c;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void button_task(void *pvParameter)
{
    bool last_launch_state = true;
    bool last_reset_state = true;
    
    while (1) {
        bool launch_pressed = (gpio_get_level(BUTTON_LAUNCH) == 0);
        bool reset_pressed = (gpio_get_level(BUTTON_RESET) == 0);
        
        // Launch button with debouncing
        if (launch_pressed && last_launch_state) {
            ESP_LOGI(TAG, "Launch button pressed!");
            if (rocket_state.armed) {
                rocket_launch_sequence();
            } else {
                ESP_LOGW(TAG, "Launch button pressed but system not armed!");
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Debounce delay
        }
        last_launch_state = launch_pressed;
        
        // Reset button with debouncing
        if (reset_pressed && last_reset_state) {
            ESP_LOGI(TAG, "Reset button pressed!");
            rocket_reset_system();
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Debounce delay
        }
        last_reset_state = reset_pressed;
        
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Check buttons every 50ms
    }
}

void status_task(void *pvParameter)
{
    while (1) {
        ESP_LOGI(TAG, "🚀 Rocket Status: %s | Armed: %s | Launched: %s | Total: %lu", 
                 rocket_state.status,
                 rocket_state.armed ? "✅" : "❌",
                 rocket_state.launched ? "✅" : "❌",
                 rocket_state.launch_count);
        
        // Heartbeat LED flash
        gpio_set_level(LED_ROCKET_STATUS, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        gpio_set_level(LED_ROCKET_STATUS, 1);
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);  // Status update every 5 seconds
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 ESP32-P4 BASIC ROCKET LAUNCHER SYSTEM 🚀");
    ESP_LOGI(TAG, "Starting Waveshare ESP32-P4-WIFI6-DEV-KIT Basic Rocket Launcher");
    ESP_LOGI(TAG, "Version: Basic - GPIO + LED + Serial Control");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    // Initialize GPIO
    init_gpio();
    
    ESP_LOGI(TAG, "🔥 BASIC ROCKET LAUNCHER READY FOR ACTION! 🔥");
    ESP_LOGI(TAG, "Controls:");
    ESP_LOGI(TAG, "  Serial: 'arm' - Arm the rocket");
    ESP_LOGI(TAG, "  Serial: 'launch' - Launch the rocket! 🚀");
    ESP_LOGI(TAG, "  Serial: 'reset' - Reset the system");
    ESP_LOGI(TAG, "  Serial: 'status' - Show current status");
    ESP_LOGI(TAG, "  Button GPIO0: Launch (if armed)");
    ESP_LOGI(TAG, "  Button GPIO1: Reset system");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════════");
    
    // Create tasks
    xTaskCreate(&serial_command_task, "serial_cmd", 4096, NULL, 5, NULL);
    xTaskCreate(&button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(&status_task, "status_task", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All systems go! Rocket launcher active! 🚀");
}