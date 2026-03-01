// Command handling task
// Reads serial input and updates the target duty cycle accordingly.
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
          Serial.println("Stopping (Ramping to 0%)");
          break;

        case 'R':  // Reset power meter
          powerMeter.reset();
          lastCommandTime = millis();
          Serial.println("Power Meter Reset");
          break;

        case 'H':  // Help commands
          Serial.println("\nCommands:");
          Serial.println("  0-100: Set duty cycle (%)");
          Serial.println("  S: Stop (set duty cycle to 0%)");
          Serial.println("  R: Reset power meter");
          break;

        default:
          // Check if it's a numeric value for duty cycle
          if (command >= '0' && command <= '9') {
            Serial.println((String)"Received: " + command);
            String dutyCycleStr = String(command);

            // Read remaining digits
            delay(10);  // Small delay to allow full number to arrive
            while (Serial.available() > 0) {
              char nextChar = Serial.read();
              if ((nextChar >= '0' && nextChar <= '9') || nextChar == '.') {
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
              Serial.print("Target Duty Cycle Set to: ");
              Serial.print(targetDutyCycle, 2);
              Serial.println("%");
            } else {
              Serial.println("Error: Duty cycle must be between 0 and 100");
            }
          } else {
            Serial.println("Unknown Command");
            Serial.println("\nCommands:");
            Serial.println("  0-100: Set duty cycle (%)");
            Serial.println("  S: Stop (set duty cycle to 0%)");
            Serial.println("  R: Reset power meter");
          }
          break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to prevent task hogging CPU
  }
}
