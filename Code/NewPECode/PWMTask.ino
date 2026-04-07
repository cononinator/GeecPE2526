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

    xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);

    if (currentDutyCycle < localTarget) {
      float step = fabs(localTarget - currentDutyCycle);
      currentDutyCycle += (step < RAMP_UP_STEP) ? step : RAMP_UP_STEP;   
    } else if (currentDutyCycle > localTarget) {
      // Ramp DOWN — never blocked by current
      float step = fabs(currentDutyCycle - localTarget);
      currentDutyCycle -= (step < RAMP_DOWN_STEP) ? step : RAMP_DOWN_STEP;
      throttleMessageSent = false;
    }

    float localDutyCycle = currentDutyCycle;
    xSemaphoreGive(dutyCycleMutex);

    // ── Apply to genA only (cmpB / genB are fixed at 1 µs — never touched) ──
    // Convert duty percentage -> comparator ticks.
    // Compare value latches at timer zero (update_cmp_on_tez = true) or next period start
    // Cap at 99.8% — 100% duty must never be sent.
    if (localDutyCycle > 99.8f) localDutyCycle = 99.8f;
    uint32_t ticks = (uint32_t)(localDutyCycle / 100.0f * (float)PERIOD_TICKS);
    mcpwm_comparator_set_compare_value(cmpA, ticks);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
