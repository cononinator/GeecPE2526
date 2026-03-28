// ─── PWM Control Task ─────────────────────────────────────────────────────────
// Runs every 10 ms on Core 1.
//
// Ramps currentDutyCycle toward targetDutyCycle using asymmetric slopes:
//   Ramp up  : RAMP_UP_STEP   % per 10 ms  (0→100% in 2 s)
//   Ramp down: RAMP_DOWN_STEP % per 10 ms  (100→0% in 0.5 s)
//
// Duty cycle is applied by writing a comparator tick count to cmpA:
//   ticks = round(duty% / 100 × PERIOD_TICKS)
// cmpA latches the new value at timer zero (glitch-free, no extra API call needed).
// cmpB / genB are NEVER modified here — they hold the fixed 1 µs bootstrap pulse.
//
// Hardware peak current limiting via DAC prevents overcurrent; no software
// cycle-by-cycle suppression needed.

void pwmTask(void *parameter) {
  static bool throttleMessageSent = false;
  static unsigned long lastThrottleMessage = 0;

  while (1) {
    // ── Read shared state ──
    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
    float localTarget = targetDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    xSemaphoreTake(currentLimitMutex, portMAX_DELAY);
    float localMeasured = measuredCurrent;
    float localLimit    = currentLimit;
    xSemaphoreGive(currentLimitMutex);

    // ── Ramp logic ──
    bool currentThrottled = false;

    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);

    if (currentDutyCycle < localTarget) {
      // Ramp UP — check current before stepping
      bool overCurrent = (localMeasured >= localLimit * 0.95f);

      if (overCurrent) {
        currentThrottled = true;
        // Hold duty flat — do not increment
      } else {
        float step = fabs(localTarget - currentDutyCycle);
        currentDutyCycle += (step < RAMP_UP_STEP) ? step : RAMP_UP_STEP;
        throttleMessageSent = false;  // Reset message flag when not throttled
      }

    } else if (currentDutyCycle > localTarget) {
      // Ramp DOWN — never blocked by current
      float step = fabs(currentDutyCycle - localTarget);
      currentDutyCycle -= (step < RAMP_DOWN_STEP) ? step : RAMP_DOWN_STEP;
      throttleMessageSent = false;
    }

    float localDutyCycle = currentDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    // ── Throttle message (rate-limited to 1/s) ──
    if (currentThrottled && !throttleMessageSent) {
      unsigned long now = millis();
      if (now - lastThrottleMessage >= 1000) {
        Serial.print("[PWM] Ramp paused — current ");
        Serial.print(localMeasured, 2);
        Serial.print(" A >= ");
        Serial.print(localLimit * 0.95f, 2);
        Serial.println(" A threshold");
        lastThrottleMessage = now;
        throttleMessageSent = true;
      }
    }

    // ── Apply to genA only (cmpB / genB are fixed at 1 µs — never touched) ──
    // Convert duty percentage → comparator ticks.
    // Compare value latches at timer zero (update_cmp_on_tez = true) so updates
    // are glitch-free and take effect at the very next period start.
    // Cap at 99.8% — 100% duty must never be sent.
    if (localDutyCycle > 99.8f) localDutyCycle = 99.8f;
    uint32_t ticks = (uint32_t)(localDutyCycle / 100.0f * (float)PERIOD_TICKS);
    mcpwm_comparator_set_compare_value(cmpA, ticks);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
