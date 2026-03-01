// Watchdog task
// Sets targetDutyCycle to 0 if no serial command is received within 20 seconds.
void watchdogTask(void *parameter) {
  const unsigned long timeoutMs = 20000;  // 20 seconds

  while (1) {
    unsigned long timeSinceLastCommand = millis() - lastCommandTime;

    if (timeSinceLastCommand > timeoutMs) {
      xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
      if (targetDutyCycle != 0.0) {
        targetDutyCycle = 0.0;  // Ramp down on timeout
        xSemaphoreGive(dutyCycleMutex);
        Serial.println("\n*** WATCHDOG TIMEOUT: Ramping to 0% (no input for 20s) ***\n");
      } else {
        xSemaphoreGive(dutyCycleMutex);
      }
      lastCommandTime = millis();  // Reset to prevent repeated messages
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
  }
}
