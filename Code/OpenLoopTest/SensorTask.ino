// Sensor reading task
// Reads INA780 and motor current sensor every second and prints results to serial.
void sensorTask(void *parameter) {
  while (1) {
    // Read all sensor values
    float voltage     = powerMeter.getBusVoltage();
    float current     = powerMeter.getCurrent();
    double power      = powerMeter.getPower();
    double energy     = powerMeter.getEnergy();
    float temperature = powerMeter.getTemperature();
    float motCurrent  = analogRead(A1) * (3.3 / 4095.0);

    // Print sensor readings
    Serial.println("===== Sensor Readings =====");

    Serial.print("Voltage: ");
    Serial.print(voltage, 3);
    Serial.println(" V");

    Serial.print("Current: ");
    Serial.print(current, 3);
    Serial.println(" A");

    Serial.print("Power: ");
    Serial.print(power, 3);
    Serial.println(" W");

    Serial.print("Energy: ");
    Serial.print(energy, 3);
    Serial.println(" J");

    Serial.print("Temperature: ");
    Serial.print(temperature, 2);
    Serial.println(" °C");

    Serial.print("Motor Current (raw): ");
    Serial.print(motCurrent, 4);
    Serial.println(" V");

    // Get current duty cycle
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    float localDutyCycle       = currentDutyCycle;
    float localTargetDutyCycle = targetDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    Serial.print("Duty Cycle: ");
    Serial.print(localDutyCycle, 2);
    Serial.print("% (Target: ");
    Serial.print(localTargetDutyCycle, 2);
    Serial.println("%)");
    Serial.println("===========================\n");

    vTaskDelay(pdMS_TO_TICKS(1000));  // Read sensors every 1 second
  }
}
