// ─── Command Handling Task ────────────────────────────────────────────────────
// Runs every 10 ms on Core 1.
//
// Control modes:
//   Throttle mode (default): A1 analog input sets targetDutyCycle (or current limit
//                            if THROTTLE_CONTROLS_CURRENT is true). Serial duty
//                            commands (0–100) are rejected.
//   Serial mode:             All serial commands are active. Throttle is ignored.
//
// Serial command summary:
//   M          Enable serial control (overrides throttle)
//   T          Return to throttle control, ramps output to 0 first
//   0–100      Set target duty cycle (%) [serial mode only]
//   S          Stop (ramp to 0%)
//   R          Reset INA780 energy/charge accumulators
//   C <amps>   Set current limit in amperes (updates DAC)
//   H          Print help

void commandTask(void *parameter) {
  while (1) {
    // ── Throttle input (active only when serial control is NOT enabled) ────────
    if (!serialControlEnabled) {
      float vThrottle = analogRead(THROTTLE_PIN) * (3.3f / 4095.0f);

      if (vThrottle < THROTTLE_OFF_V) {
        // Below safety threshold — force output off
        xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
        targetDutyCycle = 0.0f;
        xSemaphoreGive(dutyCycleMutex);
      } else {
        // Map THROTTLE_OFF_V–THROTTLE_MAX_V → 0–100%
        float mapped    = (vThrottle - THROTTLE_OFF_V) / (THROTTLE_MAX_V - THROTTLE_OFF_V) * 100.0f;
        float newTarget = constrain(mapped, 0.0f, 100.0f);

#if THROTTLE_CONTROLS_CURRENT
        // Scale throttle against the current 'C' limit; duty cycle holds at 100%
        xSemaphoreTake(currentLimitMutex, portMAX_DELAY);
        float scaledLimit = newTarget / 100.0f * currentLimit;
        xSemaphoreGive(currentLimitMutex);
        setCurrentLimitDAC(scaledLimit);

        xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
        targetDutyCycle = 100.0f;
        xSemaphoreGive(dutyCycleMutex);
#else
        xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
        targetDutyCycle = newTarget;
        xSemaphoreGive(dutyCycleMutex);
#endif
      }

      lastCommandTime = millis();  // Keep watchdog alive in throttle mode
    }

    // ── Serial commands ────────────────────────────────────────────────────────
    if (Serial.available() > 0) {
      char command = Serial.read();

      switch (command) {

        // ── Enable serial control ──────────────────────────────────────────────
        case 'M':
        case 'm':
          serialControlEnabled = true;
          lastCommandTime = millis();
          Serial.println("Serial control enabled — throttle overridden");
          break;

        // ── Return to throttle control ─────────────────────────────────────────
        case 'T':
        case 't':
          xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
          targetDutyCycle = 0.0f;  // Ramp down before handing back to throttle
          xSemaphoreGive(dutyCycleMutex);
          serialControlEnabled = false;
          lastCommandTime = millis();
          Serial.println("Throttle control enabled — ramping to 0, then following throttle");
          break;

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

        // ── Numeric duty cycle (0–100) — serial mode only ─────────────────────
        default:
          if (command >= '0' && command <= '9') {
            if (!serialControlEnabled) {
              // Drain the rest of the number to avoid stale bytes
              delay(10);
              while (Serial.available() > 0) Serial.read();
              Serial.println("Serial control disabled — send 'M' to enable");
              break;
            }
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
