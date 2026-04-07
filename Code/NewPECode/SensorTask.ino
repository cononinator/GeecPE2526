// ─── Sensor Reading Task ──────────────────────────────────────────────────────
// Runs every 100 ms on Core 0.
// Reads the INA780 power meter and the A2 motor current sensor.
// The INA780 current reading is written to the shared measuredCurrent variable
// so that PWMTask can use it for current-aware ramp limiting.
// All sensor readings are also written to sens* variables for CANTask.
void sensorTask(void *parameter) {
  while (1) {
    // ── Read sensors ──
    float voltage     = powerMeter.getBusVoltage();
    float current     = powerMeter.getCurrent();
    double power      = powerMeter.getPower();
    double energy     = powerMeter.getEnergy();
    float temperature = powerMeter.getTemperature();

    // A0: motor voltage  Vadc = 0.053764*Vmot + 0.439522
    float motorVoltageADC = analogRead(A0) * (3.3f / 4095.0f);
    float motorVoltage    = (motorVoltageADC - 0.439522f) / 0.053764f;

    // A2: motor current sensor output voltage (0–3.3 V raw)
    float motCurrent = analogRead(A2) * (3.3f / 4095.0f);

    // ── Publish all sensor readings for CANTask ──
    xSemaphoreTake(sensorDataMutex, portMAX_DELAY);
    sensBusVoltage   = voltage;
    sensPower        = (float)power;
    sensEnergy       = (float)energy;
    sensTemperature  = temperature;
    sensMotorVoltage = motorVoltage;
    sensMotorCurrent = motCurrent;
    xSemaphoreGive(sensorDataMutex);

    // ── Read duty cycle state ──
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    float localDutyCycle       = currentDutyCycle;
    float localTargetDutyCycle = targetDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    // ── Read wheel speed from CAN ──
    xSemaphoreTake(canRxMutex, portMAX_DELAY);
    float localWheelSpeed = canSpeed;
    xSemaphoreGive(canRxMutex);

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
    Serial.print(motorVoltage, 3);
    Serial.print(",");
    Serial.print(motCurrent, 4);
    Serial.print(",");
    Serial.print(localDutyCycle, 2);
    Serial.print(",");
    Serial.print(localTargetDutyCycle, 2);
    Serial.print(",");
    Serial.println(localWheelSpeed, 3);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
