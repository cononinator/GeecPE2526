// ─── CAN Task ─────────────────────────────────────────────────────────────────
// Runs every 5 ms on Core 0.
//
// Transmit (every 100 ms): sends all PE_* CAN messages using current sensor
//   and control state read from shared variables.
//
// Receive (every loop): processes incoming DAQ_* and SCREEN_* messages.
//   DAQ_STATUS    (0x100)  1-byte boolean
//   DAQ_SPEED     (0x101)  4-byte float (m/s or km/h — as sent by DAQ)
//   SCREEN_STATUS (0x200)  1-byte boolean
//   SCREEN_LIMIT  (0x201)  4-byte float
//   SCREEN_LAP_NUMBER (0x202) 4-byte int32

#define CAN_TX_INTERVAL_MS  100

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void canSendFloat(uint32_t id, float value) {
  twai_message_t tx = {};
  tx.identifier       = id;
  tx.extd             = 0;
  tx.rtr              = 0;
  tx.data_length_code = 4;
  memcpy(tx.data, &value, 4);
  twai_transmit(&tx, pdMS_TO_TICKS(10));
}

static void canSendBool(uint32_t id, bool value) {
  twai_message_t tx = {};
  tx.identifier       = id;
  tx.extd             = 0;
  tx.rtr              = 0;
  tx.data_length_code = 1;
  tx.data[0]          = value ? 0x01 : 0x00;
  twai_transmit(&tx, pdMS_TO_TICKS(10));
}

// ─── Task ─────────────────────────────────────────────────────────────────────

void canTask(void *parameter) {
  unsigned long lastTx = 0;

  while (1) {
    // ── Receive: drain all queued frames ──────────────────────────────────────
    twai_message_t rx;
    while (twai_receive(&rx, pdMS_TO_TICKS(0)) == ESP_OK) {
      xSemaphoreTake(canRxMutex, portMAX_DELAY);

      switch (rx.identifier) {

        case DAQ_STATUS:
          if (rx.data_length_code >= 1)
            canDaqStatus = (rx.data[0] != 0);
          break;

        case DAQ_SPEED:
          if (rx.data_length_code >= 4) {
            float f;
            memcpy(&f, rx.data, 4);
            canSpeed = f;
          }
          break;

        case SCREEN_STATUS:
          if (rx.data_length_code >= 1)
            canScreenStatus = (rx.data[0] != 0);
          break;

        case SCREEN_LIMIT:
          if (rx.data_length_code >= 4) {
            float f;
            memcpy(&f, rx.data, 4);
            canScreenLimit = f;
          }
          break;

        case SCREEN_LAP_NUMBER:
          if (rx.data_length_code >= 4) {
            int32_t lap;
            memcpy(&lap, rx.data, 4);
            canScreenLapNumber = (int)lap;
          }
          break;

        default:
          break;
      }

      xSemaphoreGive(canRxMutex);
    }

    // ── Transmit PE messages every 100 ms ─────────────────────────────────────
    unsigned long now = millis();
    if (now - lastTx >= CAN_TX_INTERVAL_MS) {
      lastTx = now;

      // Read duty cycle and current limit state
      xSemaphoreTake(dutyCycleMutex, portMAX_DELAY);
      float localDuty = currentDutyCycle;
      xSemaphoreGive(dutyCycleMutex);

      xSemaphoreTake(currentLimitMutex, portMAX_DELAY);
      float localCurrent = measuredCurrent;
      float localLimit   = currentLimit;
      xSemaphoreGive(currentLimitMutex);

      // Read sensor snapshot (written by SensorTask every 100 ms)
      xSemaphoreTake(sensorDataMutex, portMAX_DELAY);
      float localVoltage      = sensBusVoltage;
      float localPower        = sensPower;
      float localEnergy       = sensEnergy;
      float localTemperature  = sensTemperature;
      float localMotorVoltage = sensMotorVoltage;
      float localMotorCurrent = sensMotorCurrent;
      xSemaphoreGive(sensorDataMutex);

      // PE_STATUS: true = running (duty > 0)
      canSendBool(PE_STATUS, localDuty > 0.0f);

      // Motor measurements
      canSendFloat(PE_MOTOR_CURRENT, localMotorCurrent);
      canSendFloat(PE_MOTOR_VOLTAGE, localMotorVoltage);

      // Battery measurements (from INA780)
      canSendFloat(PE_BATTERY_VOLTAGE, localVoltage);
      canSendFloat(PE_BATTERY_CURRENT, localCurrent);

      // Thermal / energy
      canSendFloat(PE_TEMPERATURE, localTemperature);
      canSendFloat(PE_ENERGY,      localEnergy);
      canSendFloat(PE_POWER,       localPower);

      // Control state
      canSendFloat(PE_DUTY_CYCLE,    localDuty);
      canSendFloat(PE_CURRENT_LIMIT, localLimit);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
