#include "driver/mcpwm.h"
#include "INA780.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Define GPIO pins
#define PWM_PIN_A 7  // MCPWM0A output pin
#define PWM_PIN_B 17  // MCPWM0B output pin
#define SOFT_START_PIN 8 // Soft start pin and Enable pin
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
TaskHandle_t commandTaskHandle = NULL;\
TaskHandle_t pwmTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t watchdogTaskHandle = NULL;

// Command handling task
void commandTask(void *parameter) {
  char command;
  float dutyCycle;
  
  while (1) {
    if (Serial.available() > 0) {
      command = Serial.read();
      
      switch (command) {
        case 'S':  // Stop - Set duty cycle to 0%
          xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
          targetDutyCycle = 0.0;
          xSemaphoreGive(dutyCycleMutex);
          lastCommandTime = millis();
          // Serial.println("Stopping (Ramping to 0%)");
          break;
          
        case 'R':  // Reset power meter
          powerMeter.reset();
          lastCommandTime = millis();
          // Serial.println("Power Meter Reset");
          break;

        case 'H': // Help commands
          // Serial.println("\nCommands:");
          // Serial.println("  0-100: Set duty cycle (%)");
          // Serial.println("  S: Stop (set duty cycle to 0%)");
          // Serial.println("  R: Reset power meter");
          
        default:
          // Check if it's a numeric value for duty cycle
          if (command >= '0' && command <= '9') {
            // Put the character back and read the full number
            // Serial.println((String)"Received: " + command);
            String dutyCycleStr = String(command);
            
            // Read remaining digits
            delay(10);  // Small delay to allow full number to arrive
            while (Serial.available() > 0) {
              char nextChar = Serial.read();
              if (nextChar >= '0' && nextChar <= '9' || nextChar == '.') {
                dutyCycleStr += nextChar;
              } else {
                break;
              }
            }
            
            dutyCycle = dutyCycleStr.toFloat();
            
            // Validate duty cycle range (0-100)
            if (dutyCycle >= 0.0 && dutyCycle <= 100.0) {
              xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
              targetDutyCycle = dutyCycle;
              xSemaphoreGive(dutyCycleMutex);
              lastCommandTime = millis();
              // Serial.print("Target Duty Cycle Set to: ");
              // Serial.print(targetDutyCycle, 2);
              // Serial.println("%");
            } else {
              // Serial.println("Error: Duty cycle must be between 0 and 100");
            }
          } else {
            // Serial.println("Unknown Command");
            // Serial.println("\nCommands:");
            // Serial.println("  0-100: Set duty cycle (%)");
            // Serial.println("  S: Stop (set duty cycle to 0%)");
            // Serial.println("  R: Reset power meter");
          
          }
          break;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to prevent task hogging CPU
  }
}

// PWM control task
void pwmTask(void *parameter) {
  
  while (1) {
    // Get the current duty cycle value safely
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    
    // Ramp logic
    if (currentDutyCycle < targetDutyCycle) {
      if (fabs(targetDutyCycle - currentDutyCycle) < RAMP_UP_STEP) {
        currentDutyCycle = targetDutyCycle;
      } else {
        currentDutyCycle += RAMP_UP_STEP;
      }
    } else if (currentDutyCycle > targetDutyCycle) {
      if (fabs(currentDutyCycle - targetDutyCycle) < RAMP_DOWN_STEP) {
        currentDutyCycle = targetDutyCycle;
      } else {
        currentDutyCycle -= RAMP_DOWN_STEP;
      }
    }
    
    float localDutyCycle = currentDutyCycle;
    xSemaphoreGive(dutyCycleMutex);
    
    // Update PWM duty cycle
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, localDutyCycle);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, localDutyCycle);
    
    vTaskDelay(pdMS_TO_TICKS(10));  // Update PWM every 10ms
  }
}

// Watchdog task - sets PWM to 0 if no command received for 20 seconds
void watchdogTask(void *parameter) {
  const unsigned long timeoutMs = 20000;  // 20 seconds
  
  while (1) {
    unsigned long timeSinceLastCommand = millis() - lastCommandTime;
    
    if (timeSinceLastCommand > timeoutMs) {
      xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
      if (targetDutyCycle != 0.0) {
        targetDutyCycle = 0.0; // Ramp down on timeout
        xSemaphoreGive(dutyCycleMutex);
        Serial.println("DATA,WATCHDOG_TIMEOUT,0,0,0,0,0"); // Notify watchdog timeout
      } else {
        xSemaphoreGive(dutyCycleMutex);
      }
      lastCommandTime = millis();  // Reset to prevent repeated messages
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
  }
}

// Sensor reading task
void sensorTask(void *parameter) {
  while (1) {
    // Read all sensor values
    float voltage = powerMeter.getBusVoltage();
    float current = powerMeter.getCurrent();
    double power = powerMeter.getPower();
    double energy = powerMeter.getEnergy();
    float temperature = powerMeter.getTemperature();
    float motCurrent =  analogRead(A1) * (3.3 / 4095.0);
    
    // Get current duty cycle
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    float localDutyCycle = currentDutyCycle;
    float localTargetDutyCycle = targetDutyCycle;
    xSemaphoreGive(dutyCycleMutex);
    
    // Print in CSV format: DATA,Voltage,Current,Power,Energy,Temperature,DutyCycle,TargetDutyCycle
    Serial.print("DATA,");
    Serial.print(voltage, 4);
    Serial.print(",");
    Serial.print(current, 4);
    Serial.print(",");
    Serial.print(power, 4);
    Serial.print(",");
    Serial.print(energy, 4);
    Serial.print(",");
    Serial.print(temperature, 2);
    Serial.print(",");
    Serial.print(motCurrent, 4);
    Serial.print(",");
    Serial.print(localDutyCycle, 2);
    Serial.print(",");
    Serial.println(localTargetDutyCycle, 2);
    
    vTaskDelay(pdMS_TO_TICKS(250));  // Read sensors every 250ms (faster for better resolution during sweep)
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Allow time for serial monitor to connect
  // while(!Serial); // Don't block if serial not connected immediately
  Serial.println("System Starting...");
  
  // Initialize watchdog timer
  lastCommandTime = millis();
  
  // Initialize I2C for INA780
  // Wire.begin();
  Wire.begin(A5, A4);
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

  // Configure A1 as analog input for motor current sensor
  pinMode(A1, INPUT);
  pinMode(SOFT_START_PIN, OUTPUT);
  digitalWrite(SOFT_START_PIN, LOW); // Ensure soft start is low at startup
  
  delay(500); // 200ms delay is required but a longer wait is better for capacitor charging
  digitalWrite(SOFT_START_PIN, HIGH); // Enable motor driver after soft start delay

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
  pwm_config.cmpr_b = 0;            // Initial duty cycle of PWM0B = 0%
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
  
  Serial.println("FreeRTOS Tasks Created");
  Serial.println("System Ready!");
}

void loop() {
  // Empty loop - all work is done in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}
