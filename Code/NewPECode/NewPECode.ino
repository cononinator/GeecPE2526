// ─── MCPWM API: requires ESP32 Arduino Core 3.x (ESP-IDF 5.x) ────────────────
// Install via Boards Manager: "esp32 by Espressif Systems" version 3.0.0 or later.
#include "driver/mcpwm_prelude.h"
#include "driver/twai.h"
#include "INA780.h"
#include "CANid.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ─── Pin Definitions ──────────────────────────────────────────────────────────
#define CAN_TX_PIN      D9   // TWAI TX → CAN transceiver
#define CAN_RX_PIN      D8   // TWAI RX ← CAN transceiver

#define PWM_PIN_A       6    // MCPWM0A output (controlled by duty cycle)
#define PWM_PIN_B       D2   // MCPWM0B output (fixed 1 µs pulse, bootstrap/sync)
#define SOFT_START_PIN  D5   // Soft-start enable
#define ENABLE_PIN      D4   // Gate driver enable
#define FAULT_PIN       D7   // Current comparator output: LOW = overcurrent detected

// ─── I2C Device Addresses ─────────────────────────────────────────────────────
#define INA780_ADDRESS  0x40  // INA780 power meter
#define DAC_I2C_ADDRESS 0x73  // LTC2631-HZ12 current-limit DAC (global address)

// ─── Current Sensor Calibration ───────────────────────────────────────────────
// Sensor output voltage vs. current:  V_sensor = 0.983 * I_amps + 0.17
// Used to convert a desired current limit (A) into a DAC voltage setpoint.
#define SENSOR_SLOPE    0.983f
#define SENSOR_OFFSET   0.17f

// ─── MCPWM Constants ──────────────────────────────────────────────────────────
#define PWM_FREQUENCY    30000       // Hz
// Timer resolution: 80 MHz.  Period ticks = 80,000,000 / 30,000 = 2667 → 29,996 Hz (≈30 kHz)
#define MCPWM_RESOLUTION_HZ  80000000UL
#define PERIOD_TICKS         (MCPWM_RESOLUTION_HZ / PWM_FREQUENCY)  // 2667
// Fixed 1 µs on-time for generator B:  1 µs × 80 MHz = 80 ticks
#define PWM_B_TICKS          80UL

// ─── Throttle ─────────────────────────────────────────────────────────────────
#define THROTTLE_PIN              A1
#define THROTTLE_OFF_V            1.4f    // Below this voltage → output off (safety)
#define THROTTLE_MAX_V            3.3f    // Full throttle voltage = 100%

// Set to true to have throttle control the current limit (via DAC) instead of duty cycle.
// When true: throttle scales 0–100% of the value set by the 'C' command; duty holds at 100%.
#define THROTTLE_CONTROLS_CURRENT false

// ─── Current Limiting ─────────────────────────────────────────────────────
// Hardware peak current limiting via LTC2631 DAC (sets comparator reference).
// No software fault handling needed.

// ─── Ramp Constants ───────────────────────────────────────────────────────────
const float RAMP_UP_STEP   = 1.0f;  // %/10 ms  →  0→100% in 2 s
const float RAMP_DOWN_STEP = 2.0f;  // %/10 ms  →  100→0% in 0.5 s

// ─── Shared State ─────────────────────────────────────────────────────────────
INA780 powerMeter(INA780_ADDRESS);

volatile float currentDutyCycle  = 0.0f;
volatile float targetDutyCycle   = 0.0f;
volatile float currentLimit      = 10.0f;   // Amperes — set via 'C' command
volatile float measuredCurrent   = 0.0f;    // Written by SensorTask, read by PWMTask

volatile bool serialControlEnabled = false;   // false = throttle mode (default), true = serial mode
volatile unsigned long lastCommandTime = 0;

// ─── Shared Sensor Readings (written by SensorTask, read by CANTask) ──────────
volatile float sensBusVoltage   = 0.0f;
volatile float sensPower        = 0.0f;
volatile float sensEnergy       = 0.0f;
volatile float sensTemperature  = 0.0f;
volatile float sensMotorVoltage = 0.0f;
volatile float sensMotorCurrent = 0.0f;

// ─── CAN-Received State (written by CANTask, available for other tasks) ────────
volatile bool  canDaqStatus      = false;
volatile float canSpeed          = 0.0f;
volatile bool  canScreenStatus   = false;
volatile float canScreenLimit    = 0.0f;
volatile int   canScreenLapNumber = 0;

// ─── FreeRTOS Handles ─────────────────────────────────────────────────────────
SemaphoreHandle_t dutyCycleMutex;       // Protects currentDutyCycle / targetDutyCycle
SemaphoreHandle_t currentLimitMutex;   // Protects measuredCurrent / currentLimit
SemaphoreHandle_t sensorDataMutex;     // Protects sens* variables
SemaphoreHandle_t canRxMutex;          // Protects canDaqStatus, canSpeed, etc.

TaskHandle_t commandTaskHandle   = NULL;
TaskHandle_t pwmTaskHandle       = NULL;
TaskHandle_t sensorTaskHandle    = NULL;
TaskHandle_t watchdogTaskHandle  = NULL;
TaskHandle_t canTaskHandle       = NULL;

// ─── MCPWM Object Handles ─────────────────────────────────────────────────────
// One timer, one operator, two comparators (A = variable duty, B = fixed 1 µs),
// two generators (one per output pin).
mcpwm_timer_handle_t    mcpwmTimer  = NULL;
mcpwm_oper_handle_t     mcpwmOper   = NULL;
mcpwm_cmpr_handle_t     cmpA        = NULL;  // Controls generator A duty cycle
mcpwm_cmpr_handle_t     cmpB        = NULL;  // Fixed 1 µs on-time for generator B
mcpwm_gen_handle_t      genA        = NULL;  // PWM_PIN_A output
mcpwm_gen_handle_t      genB        = NULL;  // PWM_PIN_B output (D2)

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  // while (!Serial);
  Serial.println("=== ClosedLoopTest Starting ===");

  lastCommandTime = millis();

  // ── I2C + INA780 ──
  Wire.begin();
  if (powerMeter.isConnected()) {
    Serial.println("INA780 Connected");
    powerMeter.setMode(INA780::MODE_CONTINUOUS_SHUNT_BUS);
    powerMeter.configureADC(INA780::AVG_16, INA780::CONV_1052us, INA780::CONV_1052us);
  } else {
    Serial.println("WARNING: INA780 Not Found!");
  }

  // ── LTC2631 DAC — set initial current limit ──
  initDAC();
  setCurrentLimitDAC(currentLimit);
  Serial.print("Current limit DAC initialised: ");
  Serial.print(currentLimit, 2);
  Serial.println(" A");

  // ── GPIO ──
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  pinMode(SOFT_START_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  digitalWrite(SOFT_START_PIN, LOW);
  digitalWrite(ENABLE_PIN, LOW);
  delay(500);
  digitalWrite(SOFT_START_PIN, HIGH);
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("Gate driver enabled");

  // ── MCPWM (IDF v5 API) ────────────────────────────────────────────────────
  //
  // Topology: one timer → one operator → two comparators → two generators
  //
  //   cmpA / genA (PWM_PIN_A) — variable duty, controlled by PWMTask
  //   cmpB / genB (D2)        — fixed 1 µs on-time, set once and never changed
  //
  // Both generators use active-high PWM:
  //   HIGH at timer empty (counter = 0, start of period)
  //   LOW  at comparator match
  //
  // Duty for A: compare value = round(duty% / 100 × PERIOD_TICKS)
  // Duty for B: compare value = PWM_B_TICKS (80 ticks = 1 µs at 80 MHz)

  // ── 1. Timer ──
  mcpwm_timer_config_t timerCfg = {
    .group_id      = 0,
    .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = MCPWM_RESOLUTION_HZ,
    .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
    .period_ticks  = PERIOD_TICKS,
  };
  ESP_ERROR_CHECK(mcpwm_new_timer(&timerCfg, &mcpwmTimer));

  // ── 2. Operator (connects to the timer) ──
  mcpwm_operator_config_t operCfg = { .group_id = 0 };
  ESP_ERROR_CHECK(mcpwm_new_operator(&operCfg, &mcpwmOper));
  ESP_ERROR_CHECK(mcpwm_operator_connect_timer(mcpwmOper, mcpwmTimer));

  // ── 3. Comparators ──
  // update_cmp_on_tez = true → compare value latches at timer zero (glitch-free)
  mcpwm_comparator_config_t cmpCfg = {};
  cmpCfg.flags.update_cmp_on_tez = true;
  ESP_ERROR_CHECK(mcpwm_new_comparator(mcpwmOper, &cmpCfg, &cmpA));
  ESP_ERROR_CHECK(mcpwm_new_comparator(mcpwmOper, &cmpCfg, &cmpB));

  // Initialise compare values: A = 0% duty, B = 1 µs fixed
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpA, 0));
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpB, PWM_B_TICKS));

  // ── 4. Generators ──
  mcpwm_generator_config_t genCfgA = { .gen_gpio_num = PWM_PIN_A };
  mcpwm_generator_config_t genCfgB = { .gen_gpio_num = (int)D2 };
  ESP_ERROR_CHECK(mcpwm_new_generator(mcpwmOper, &genCfgA, &genA));
  ESP_ERROR_CHECK(mcpwm_new_generator(mcpwmOper, &genCfgB, &genB));

  // Generator A actions: HIGH at timer empty, LOW at cmpA
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_timer_event(genA,
    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
    MCPWM_GEN_TIMER_EVENT_ACTION_END()));
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_compare_event(genA,
    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpA, MCPWM_GEN_ACTION_LOW),
    MCPWM_GEN_COMPARE_EVENT_ACTION_END()));

  // Generator B actions: HIGH at timer empty, LOW at cmpB (fixed 1 µs)
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_timer_event(genB,
    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
    MCPWM_GEN_TIMER_EVENT_ACTION_END()));
  ESP_ERROR_CHECK(mcpwm_generator_set_actions_on_compare_event(genB,
    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmpB, MCPWM_GEN_ACTION_LOW),
    MCPWM_GEN_COMPARE_EVENT_ACTION_END()));

  // Pull-down on both output pins (ensures defined low state at startup)
  gpio_pulldown_en((gpio_num_t)PWM_PIN_A);
  gpio_pulldown_en((gpio_num_t)D2);

  // ── 5. Enable and start timer ──
  ESP_ERROR_CHECK(mcpwm_timer_enable(mcpwmTimer));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(mcpwmTimer, MCPWM_TIMER_START_NO_STOP));

  Serial.print("MCPWM Initialised — ");
  Serial.print(PWM_FREQUENCY);
  Serial.print(" Hz, period = ");
  Serial.print(PERIOD_TICKS);
  Serial.println(" ticks (80 MHz)");
  Serial.println("  genA: variable duty (PWM_PIN_A)");
  Serial.println("  genB: 1 µs fixed pulse (D2)");

  // ── TWAI (CAN) ──
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("ERROR: TWAI driver install failed");
  } else if (twai_start() != ESP_OK) {
    Serial.println("ERROR: TWAI start failed");
  } else {
    Serial.println("CAN (TWAI) started — 500 kbit/s");
  }

  // ── FreeRTOS ──
  dutyCycleMutex    = xSemaphoreCreateMutex();
  currentLimitMutex = xSemaphoreCreateMutex();
  sensorDataMutex   = xSemaphoreCreateMutex();
  canRxMutex        = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(commandTask,  "CommandTask",  4096, NULL, 2, &commandTaskHandle,  1);
  xTaskCreatePinnedToCore(pwmTask,      "PWMTask",      2048, NULL, 1, &pwmTaskHandle,      1);
  xTaskCreatePinnedToCore(sensorTask,   "SensorTask",   4096, NULL, 1, &sensorTaskHandle,   0);
  xTaskCreatePinnedToCore(watchdogTask, "WatchdogTask", 2048, NULL, 2, &watchdogTaskHandle, 0);
  xTaskCreatePinnedToCore(canTask,      "CANTask",      4096, NULL, 1, &canTaskHandle,      0);

  Serial.println("FreeRTOS tasks created");
  printHelp();
  Serial.println("System Ready!");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ─── Help text (also called from CommandTask) ─────────────────────────────────
void printHelp() {
  Serial.println("\n===== Commands =====");
  Serial.println("  M          : Enable serial control (overrides throttle)");
  Serial.println("  T          : Return to throttle control (default)");
  Serial.println("  0-100      : Set target duty cycle (%) [serial mode only]");
  Serial.println("  S          : Stop (ramp to 0%)");
  Serial.println("  R          : Reset power meter energy/charge");
  Serial.println("  C <amps>   : Set current limit (e.g. 'C 15.5')");
  Serial.println("  H          : Show this help");
  Serial.println("====================\n");
}
