// ─── Sensor Reading Task ──────────────────────────────────────────────────────
// Runs every 100 ms on Core 0.
// Reads the INA780 power meter and the A2 motor current sensor.
// The INA780 current reading is written to the shared measuredCurrent variable
// so that PWMTask can use it for current-aware ramp limiting.
void sensorTask(void *parameter) {
  while (1) {
    // ── Read sensors ──
    float voltage     = powerMeter.getBusVoltage();
    float current     = powerMeter.getCurrent();
    double power      = powerMeter.getPower();
    double energy     = powerMeter.getEnergy();
    float temperature = powerMeter.getTemperature();

    // A2: motor current sensor output voltage (0–3.3 V raw)
    float motCurrent = analogRead(A2) * (3.3f / 4095.0f);

    // ── Publish measured current for PWMTask ──
    xSemaphoreTake(currentLimitMutex, portMAX_DELAY);
    measuredCurrent = current;
    float localLimit = currentLimit;
    xSemaphoreGive(currentLimitMutex);

    // ── Read duty cycle state ──
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    float localDutyCycle       = currentDutyCycle;
    float localTargetDutyCycle = targetDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    // ── Print CSV format ──
    Serial.print("DATA,");
    Serial.print(voltage, 3);
    Serial.print(",");
    Serial.print(current, 3);
    Serial.print(",");
    Serial.print(power, 3);
    Serial.print(",");
    Serial.print(energy, 3);
    Serial.print(",");
    Serial.print(temperature, 2);
    Serial.print(",");
    Serial.print(motCurrent, 4);
    Serial.print(",");
    Serial.print(localDutyCycle, 2);
    Serial.print(",");
    Serial.println(localTargetDutyCycle, 2);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
