// ─── DebugTest.ino ────────────────────────────────────────────────────────────
// Minimal debug firmware:
//   - PWM A: 50% duty cycle at 30 kHz  (PWM_PIN_A = 6)
//   - PWM B: 1 µs fixed pulse at 30 kHz (D2)
//   - Current limit: 10 A via LTC2631-HZ12 DAC (I2C 0x73)
// ─────────────────────────────────────────────────────────────────────────────

#include "driver/mcpwm_prelude.h"
#include <Wire.h>

// ─── Pin Definitions ──────────────────────────────────────────────────────────
#define PWM_PIN_A       D3     // MCPWM genA output — 50% duty
#define PWM_PIN_B       D2    // MCPWM genB output — 1 µs fixed pulse
#define SOFT_START_PIN  D5    // Soft-start enable
#define ENABLE_PIN      D4    // Gate driver enable

// ─── I2C ──────────────────────────────────────────────────────────────────────
#define DAC_I2C_ADDRESS 0x73  // LTC2631-HZ12 (global address)

// ─── Current sensor calibration (used to convert amps → DAC voltage) ──────────
#define SENSOR_SLOPE  0.1402f
#define SENSOR_OFFSET 0.0473f

// ─── MCPWM ────────────────────────────────────────────────────────────────────
#define PWM_FREQUENCY        30000UL
#define MCPWM_RESOLUTION_HZ  80000000UL
#define PERIOD_TICKS         (MCPWM_RESOLUTION_HZ / PWM_FREQUENCY)  // 2667 ticks
#define PWM_A_DUTY_PCT       50.0f
#define PWM_A_TICKS          ((uint32_t)(PWM_A_DUTY_PCT / 100.0f * PERIOD_TICKS + 0.5f))  // 1334
#define PWM_B_TICKS          80UL   // 1 µs × 80 MHz = 80 ticks

// ─── LTC2631 commands ─────────────────────────────────────────────────────────
#define LTC2631_CMD_WRITE_UPDATE 0x30
#define LTC2631_CMD_INTERNAL_REF 0x60

// ─── MCPWM handles ────────────────────────────────────────────────────────────
static mcpwm_timer_handle_t mcpwmTimer = NULL;
static mcpwm_oper_handle_t  mcpwmOper  = NULL;
static mcpwm_cmpr_handle_t  cmpA       = NULL;
static mcpwm_cmpr_handle_t  cmpB       = NULL;
static mcpwm_gen_handle_t   genA       = NULL;
static mcpwm_gen_handle_t   genB       = NULL;

// ─── DAC helpers ──────────────────────────────────────────────────────────────
static void dacSendCommand(uint8_t command, uint16_t data) {
  uint16_t dataToSend = data << 4;
  Wire.beginTransmission(DAC_I2C_ADDRESS);
  Wire.write(command);
  Wire.write(highByte(dataToSend));
  Wire.write(lowByte(dataToSend));
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.print("[DAC] I2C error: ");
    Serial.println(err);
  }
}

static void setCurrentLimit(float amps) {
  float vSetpoint = SENSOR_SLOPE * amps + SENSOR_OFFSET;
  if (vSetpoint < 0.0f)   vSetpoint = 0.0f;
  if (vSetpoint > 4.095f) vSetpoint = 4.095f;
  uint16_t code = (uint16_t)(vSetpoint / 4.096f * 4095.0f + 0.5f);
  dacSendCommand(LTC2631_CMD_WRITE_UPDATE, code);
  Serial.print("[DAC] Current limit set: ");
  Serial.print(amps, 1);
  Serial.print(" A  →  V_ref = ");
  Serial.print(vSetpoint, 4);
  Serial.print(" V  →  code = ");
  Serial.println(code);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== DebugTest Starting ===");

  // ── I2C + DAC ──
  Wire.begin();
  dacSendCommand(LTC2631_CMD_INTERNAL_REF, 0);  // Select internal 4.096 V ref
  dacSendCommand(LTC2631_CMD_WRITE_UPDATE,  0);  // Zero output initially
  setCurrentLimit(10.0f);

  // ── Gate driver ──
  pinMode(SOFT_START_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(SOFT_START_PIN, LOW);
  digitalWrite(ENABLE_PIN, LOW);
  delay(200);
  digitalWrite(SOFT_START_PIN, HIGH);
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("Gate driver enabled");

  // ── MCPWM ──
  // Timer
  mcpwm_timer_config_t timerCfg = {
    .group_id      = 0,
    .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = MCPWM_RESOLUTION_HZ,
    .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
    .period_ticks  = PERIOD_TICKS,
  };
  ESP_ERROR_CHECK(mcpwm_new_timer(&timerCfg, &mcpwmTimer));

  // Operator
  mcpwm_operator_config_t operCfg = { .group_id = 0 };
  ESP_ERROR_CHECK(mcpwm_new_operator(&operCfg, &mcpwmOper));
  ESP_ERROR_CHECK(mcpwm_operator_connect_timer(mcpwmOper, mcpwmTimer));

  // Comparators
  mcpwm_comparator_config_t cmpCfg = {};
  cmpCfg.flags.update_cmp_on_tez = true;
  ESP_ERROR_CHECK(mcpwm_new_comparator(mcpwmOper, &cmpCfg, &cmpA));
  ESP_ERROR_CHECK(mcpwm_new_comparator(mcpwmOper, &cmpCfg, &cmpB));
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpA, PWM_A_TICKS));  // 50%
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpB, PWM_B_TICKS));  // 1 µs

  // Generators
  mcpwm_generator_config_t genCfgA = { .gen_gpio_num = PWM_PIN_A };
  mcpwm_generator_config_t genCfgB = { .gen_gpio_num = (int)PWM_PIN_B };
  ESP_ERROR_CHECK(mcpwm_new_generator(mcpwmOper, &genCfgA, &genA));
  ESP_ERROR_CHECK(mcpwm_new_generator(mcpwmOper, &genCfgB, &genB));

  // genA: HIGH at timer zero, LOW at cmpA (50% duty)
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_timer_event(genA,
    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
    MCPWM_GEN_TIMER_EVENT_ACTION_END()));
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_compare_event(genA,
    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpA, MCPWM_GEN_ACTION_LOW),
    MCPWM_GEN_COMPARE_EVENT_ACTION_END()));

  // genB: HIGH at timer zero, LOW at cmpB (1 µs)
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_timer_event(genB,
    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
    MCPWM_GEN_TIMER_EVENT_ACTION_END()));
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_compare_event(genB,
    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpB, MCPWM_GEN_ACTION_LOW),
    MCPWM_GEN_COMPARE_EVENT_ACTION_END()));

  gpio_pulldown_en((gpio_num_t)PWM_PIN_A);
  gpio_pulldown_en((gpio_num_t)PWM_PIN_B);

  ESP_ERROR_CHECK(mcpwm_timer_enable(mcpwmTimer));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(mcpwmTimer, MCPWM_TIMER_START_NO_STOP));

  Serial.println("=== DebugTest Running ===");
  Serial.print("  PWM A: ");
  Serial.print(PWM_A_DUTY_PCT, 0);
  Serial.print("% duty  (");
  Serial.print(PWM_A_TICKS);
  Serial.println(" ticks)");
  Serial.print("  PWM B: 1 µs fixed pulse  (");
  Serial.print(PWM_B_TICKS);
  Serial.println(" ticks)");
  Serial.print("  Frequency: ");
  Serial.print(PWM_FREQUENCY);
  Serial.println(" Hz");
  Serial.println("  Current limit: 10 A");
}

void loop() {
  // Nothing to do — fixed outputs, no Serial commands
  delay(5000);
  Serial.println("[alive]");
}
