#include "driver/mcpwm.h"

// Define GPIO pins
#define PWM_PIN_A 7  // MCPWM0A output pin
#define PWM_PIN_B 17  // MCPWM0B output pin

void setup() {
  Serial.begin(115200);
  
  // Initialize MCPWM GPIO pins
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PWM_PIN_A);
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, PWM_PIN_B);
  
  // Enable pulldown resistors
  gpio_pulldown_en((gpio_num_t)PWM_PIN_A);
  gpio_pulldown_en((gpio_num_t)PWM_PIN_B);
  
  // Configure MCPWM parameters
  mcpwm_config_t pwm_config;
  pwm_config.frequency = 30000;      // Frequency in Hz (1 kHz)
  pwm_config.cmpr_a = 0;            // Initial duty cycle of PWM0A = 0%
  pwm_config.cmpr_b = 0;            // Initial duty cycle of PWM0B = 0%
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
  
  // Initialize MCPWM with the configuration
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
  
  Serial.println("MCPWM Initialized");
}

void loop() {
  // Increase duty cycle from 0% to 100%
  Serial.println("MCPWM Increasing");
  for (int duty = 1; duty <= 99; duty++) {
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, duty);
    delay(50);
  }
  Serial.println("MCPWM Decreasing");
  // Decrease duty cycle from 100% to 0%
  for (int duty = 99; duty >= 1; duty--) {
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, duty);
    delay(50);
  }
}