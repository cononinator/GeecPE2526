// Minimal equivalence test for ClosedLoopTest D2 behavior:
// - Push-pull MCPWM output on D2
// - 30 kHz period
// - 1 us HIGH pulse each cycle
// Requires ESP32 Arduino core 3.x (IDF 5.x MCPWM prelude API).

#include "driver/mcpwm_prelude.h"

#define PWM_PIN_B             D2
#define PWM_FREQUENCY_HZ      30000UL
#define MCPWM_RESOLUTION_HZ   80000000UL
#define PERIOD_TICKS          (MCPWM_RESOLUTION_HZ / PWM_FREQUENCY_HZ)
#define PULSE_WIDTH_US        1UL
#define PWM_B_TICKS           (PULSE_WIDTH_US * (MCPWM_RESOLUTION_HZ / 1000000UL))

mcpwm_timer_handle_t timerB = NULL;
mcpwm_oper_handle_t operB = NULL;
mcpwm_cmpr_handle_t cmpB = NULL;
mcpwm_gen_handle_t genB = NULL;

void setup() {
  Serial.begin(115200);
  delay(500);
  while (!Serial) {
    ;
  }

  mcpwm_timer_config_t timerCfg = {
    .group_id = 0,
    .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = MCPWM_RESOLUTION_HZ,
    .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    .period_ticks = PERIOD_TICKS,
  };
  ESP_ERROR_CHECK(mcpwm_new_timer(&timerCfg, &timerB));

  mcpwm_operator_config_t operCfg = { .group_id = 0 };
  ESP_ERROR_CHECK(mcpwm_new_operator(&operCfg, &operB));
  ESP_ERROR_CHECK(mcpwm_operator_connect_timer(operB, timerB));

  mcpwm_comparator_config_t cmpCfg = {};
  cmpCfg.flags.update_cmp_on_tez = true;
  ESP_ERROR_CHECK(mcpwm_new_comparator(operB, &cmpCfg, &cmpB));
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpB, PWM_B_TICKS));

  mcpwm_generator_config_t genCfg = { .gen_gpio_num = (int)PWM_PIN_B };
  ESP_ERROR_CHECK(mcpwm_new_generator(operB, &genCfg, &genB));

  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_timer_event(
      genB,
      MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
      MCPWM_GEN_TIMER_EVENT_ACTION_END()));
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_compare_event(
      genB,
      MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpB, MCPWM_GEN_ACTION_LOW),
      MCPWM_GEN_COMPARE_EVENT_ACTION_END()));

  ESP_ERROR_CHECK(mcpwm_timer_enable(timerB));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(timerB, MCPWM_TIMER_START_NO_STOP));

  Serial.println("MCPWM D2 test running");
  Serial.print("Frequency: ");
  Serial.print(PWM_FREQUENCY_HZ);
  Serial.println(" Hz");
  Serial.print("Pulse width: ");
  Serial.print(PULSE_WIDTH_US);
  Serial.println(" us");
  Serial.print("Expected DMM average at 3.3V IO: ~");
  Serial.print(3.3f * ((float)PWM_B_TICKS / (float)PERIOD_TICKS), 3);
  Serial.println(" V");
}

void loop() {
  delay(1000);
}
