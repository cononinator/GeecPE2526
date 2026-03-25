// ─── Command Handling Task ────────────────────────────────────────────────────
// Runs every 10 ms on Core 1.
// Parses serial input and updates shared state accordingly.
//
// Command summary:
//   0–100      Set target duty cycle (%)
//   S          Stop (ramp to 0%)
//   R          Reset INA780 energy/charge accumulators
//   C <amps>   Set current limit in amperes (updates DAC)
//   H          Print help

void commandTask(void *parameter) {
  while (1) {
    if (Serial.available() > 0) {
      char command = Serial.read();

      switch (command) {

        // ── Stop ──────────────────────────────────────────────────────────────
        case 'S':
        case 's':
          xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
          targetDutyCycle = 0.0f;
          xSemaphoreGive(dutyCycleMutex);
          lastCommandTime = millis();
          Serial.println("Stop — ramping to 0%");
          break;

        // ── Reset power meter ─────────────────────────────────────────────────
        case 'R':
        case 'r':
          powerMeter.reset();
          lastCommandTime = millis();
          Serial.println("Power meter reset");
          break;

        // ── Current limit (C <amps>) ──────────────────────────────────────────
        case 'C':
        case 'c': {
          delay(20);  // Allow remainder of number to arrive
          String valStr = "";
          while (Serial.available() > 0) {
            char ch = Serial.read();
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ' ') {
              if (ch != ' ' || valStr.length() > 0) valStr += ch;  // trim leading space
            } else {
              break;
            }
          }
          valStr.trim();
          float newLimit = valStr.toFloat();
          if (newLimit > 0.0f) {
            xSemaphoreTake(currentLimitMutex, portMAX_DELAY);
            currentLimit = newLimit;
            xSemaphoreGive(currentLimitMutex);
            setCurrentLimitDAC(newLimit);
            lastCommandTime = millis();
            Serial.print("Current limit set to ");
            Serial.print(newLimit, 2);
            Serial.println(" A");
          } else {
            Serial.println("Error: current limit must be > 0 (e.g. 'C 15.5')");
          }
          break;
        }

        // ── Help ──────────────────────────────────────────────────────────────
        case 'H':
        case 'h':
          printHelp();
          lastCommandTime = millis();
          break;

        // ── Numeric duty cycle (0–100) ────────────────────────────────────────
        default:
          if (command >= '0' && command <= '9') {
            String dutyCycleStr = String(command);
            delay(10);
            while (Serial.available() > 0) {
              char nextChar = Serial.read();
              if ((nextChar >= '0' && nextChar <= '9') || nextChar == '.') {
                dutyCycleStr += nextChar;
              } else {
                break;
              }
            }
            float dutyCycle = dutyCycleStr.toFloat();
            if (dutyCycle >= 0.0f && dutyCycle <= 100.0f) {
              xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
              targetDutyCycle = dutyCycle;
              xSemaphoreGive(dutyCycleMutex);
              lastCommandTime = millis();
              Serial.print("Target duty cycle: ");
              Serial.print(dutyCycle, 2);
              Serial.println("%");
            } else {
              Serial.println("Error: duty cycle must be 0–100");
            }
          } else if (command != '\n' && command != '\r' && command != ' ') {
            Serial.print("Unknown command: '");
            Serial.print(command);
            Serial.println("'  — send 'H' for help");
          }
          break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
