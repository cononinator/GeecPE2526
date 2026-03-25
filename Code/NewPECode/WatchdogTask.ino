// ─── Watchdog Task ─────────────────────────────────────────────────────────────
// Runs every 1 s on Core 0.
// Forces targetDutyCycle to 0 if no serial command is received within 20 s.
// This prevents the system from running unattended if the host disconnects.
void watchdogTask(void *parameter) {
  const unsigned long timeoutMs = 20000;  // 20 seconds

  while (1) {
    unsigned long timeSinceLastCommand = millis() - lastCommandTime;

    if (timeSinceLastCommand > timeoutMs) {
      xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
      bool wasRunning = (targetDutyCycle != 0.0f);
      if (wasRunning) {
        targetDutyCycle = 0.0f;
      }
      xSemaphoreGive(dutyCycleMutex);

      if (wasRunning) {
        Serial.println("\n*** WATCHDOG TIMEOUT: No command in 20 s — ramping to 0% ***\n");
      }
      lastCommandTime = millis();  // Reset to prevent repeated messages
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
