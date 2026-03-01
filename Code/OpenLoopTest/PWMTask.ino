// PWM control task
// Ramps currentDutyCycle toward targetDutyCycle and updates the MCPWM output.
void pwmTask(void *parameter) {

  while (1) {
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
