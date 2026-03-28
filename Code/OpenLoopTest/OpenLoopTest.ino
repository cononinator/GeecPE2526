#include "driver/mcpwm.h"
#include "INA780.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Define GPIO pins
#define PWM_PIN_A 6  // MCPWM0A output pin
#define PWM_PIN_B D2  // MCPWM0B output pin
#define SOFT_START_PIN D5 // Soft start pin
#define ENABLE_PIN D4 // Enable pin
#define INA780_ADDRESS 0x40  // Default I2C address for INA780

// Global variables
INA780 powerMeter(INA780_ADDRESS);
volatile float currentDutyCycle = 0.0;
volatile float targetDutyCycle = 0.0;
volatile unsigned long lastCommandTime = 0;  // Timestamp of last command
SemaphoreHandle_t dutyCycleMutex;

// Constants
const float RAMP_UP_STEP = 0.5;   // 0.5% change per 10ms (100% in 2s)
const float RAMP_DOWN_STEP = 2.0; // 2.0% change per 10ms (100% in 0.5s)

// Task handles
TaskHandle_t commandTaskHandle = NULL;
TaskHandle_t pwmTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t watchdogTaskHandle = NULL;

void setup() {
  Serial.begin(115200);
  delay(1000);  // Allow time for serial monitor to connect
  while(!Serial);
  Serial.println("System Starting...");
  
  // Initialize watchdog timer
  lastCommandTime = millis();
  
  // Initialize I2C for INA780
  // Wire.begin();
  Wire.begin();
  // powerMeter.begin();
  
  // Check if INA780 is connected
  if (powerMeter.isConnected()) {
    Serial.println("INA780 Power Meter Connected");
    
    // Configure INA780 for continuous measurement
    powerMeter.setMode(INA780::MODE_CONTINUOUS_SHUNT_BUS);
    powerMeter.configureADC(INA780::AVG_16, INA780::CONV_1052us, INA780::CONV_1052us);
  } else {
    Serial.println("Warning: INA780 Power Meter Not Found!");
  }

  // Configure A2 as analog input for motor current sensor
  pinMode(A2, INPUT);
  pinMode(SOFT_START_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(SOFT_START_PIN, LOW); // Ensure soft start is low at startup
  digitalWrite(ENABLE_PIN, LOW); // Ensure soft start is low at startup

  delay(500); // 200ms delay is required but a longer wait is better for capacitor charging
  digitalWrite(SOFT_START_PIN, HIGH); // Enable motor driver after soft start delay
  digitalWrite(ENABLE_PIN, HIGH);

  Serial.println("Soft Start Enabled, Motor Driver Activated");

  // Initialize MCPWM GPIO pins
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PWM_PIN_A);
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, PWM_PIN_B);
  
  // Enable pulldown resistors
  gpio_pulldown_en((gpio_num_t)PWM_PIN_A);
  gpio_pulldown_en((gpio_num_t)PWM_PIN_B);
  
  // Set Group resolution to 80MHz
  mcpwm_group_set_resolution(MCPWM_UNIT_0, 80000000);

  // Set timer resolution to 80MHz (same as group for maximum resolution)
  mcpwm_timer_set_resolution(MCPWM_UNIT_0, MCPWM_TIMER_0, 80000000);
  
  // Configure MCPWM parameters
  mcpwm_config_t pwm_config;
  pwm_config.frequency = 30000;      // Frequency in Hz (30 kHz)
  pwm_config.cmpr_a = 0;            // Initial duty cycle of PWM0A = 0%
  pwm_config.cmpr_b = 10;            // Initial duty cycle of PWM0B = 10%
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
  
  // Initialize MCPWM with the configuration
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
  
  Serial.println("MCPWM Initialized");
  
  // Create mutex for duty cycle protection
  dutyCycleMutex = xSemaphoreCreateMutex();
  
  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(
    commandTask,       // Task function
    "CommandTask",     // Task name
    4096,              // Stack size
    NULL,              // Parameters
    2,                 // Priority (higher priority for command handling)
    &commandTaskHandle, // Task handle
    1                  // Core ID (core 1)
  );
  
  xTaskCreatePinnedToCore(
    pwmTask,           // Task function
    "PWMTask",         // Task name
    2048,              // Stack size
    NULL,              // Parameters
    1,                 // Priority
    &pwmTaskHandle,    // Task handle
    1                  // Core ID (core 1)
  );
  
  xTaskCreatePinnedToCore(
    sensorTask,        // Task function
    "SensorTask",      // Task name
    4096,              // Stack size
    NULL,              // Parameters
    1,                 // Priority
    &sensorTaskHandle, // Task handle
    0                  // Core ID (core 0)
  );
  
  xTaskCreatePinnedToCore(
    watchdogTask,      // Task function
    "WatchdogTask",    // Task name
    2048,              // Stack size
    NULL,              // Parameters
    2,                 // Priority (high priority for safety)
    &watchdogTaskHandle, // Task handle
    0                  // Core ID (core 0)
  );
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 10);
  Serial.println("FreeRTOS Tasks Created");
  Serial.println("\nCommands:");
  Serial.println("  0-100: Set duty cycle (%)");
  Serial.println("  S: Stop (set duty cycle to 0%)");
  Serial.println("  R: Reset power meter");
  Serial.println("\nSystem Ready!");
}

void loop() {
  // Empty loop - all work is done in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}