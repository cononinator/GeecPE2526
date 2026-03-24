// ─── LTC2631-HZ12 DAC Driver ──────────────────────────────────────────────────
// The DAC sets the comparator reference voltage used for hardware current limiting.
// The comparator compares the DAC output against the current sensor output.
//
// Current sensor transfer function:
//   V_sensor = SENSOR_SLOPE * I_amps + SENSOR_OFFSET
//            = 0.1402 * I + 0.0473   (V, A)
//
// LTC2631-HZ12 specs:
//   I2C address : 0x73 (global address)
//   Full scale  : 4.096 V (internal reference)
//   Resolution  : 12-bit  (codes 0–4095)
//   V_out formula: V = (code / 4096) * 4.096  →  V = code * 0.001 V/LSB
//
// Commands (Table 3 of LTC2631 datasheet):
//   0x30 — Write to Input Register and Update DAC Register
//   0x60 — Select Internal Reference (HZ variant defaults to internal ref on POR,
//           but we send it explicitly during initDAC() to be safe)

static const byte LTC2631_CMD_WRITE_UPDATE = 0x30;
static const byte LTC2631_CMD_INTERNAL_REF = 0x60;

// ─── initDAC ──────────────────────────────────────────────────────────────────
// Call once in setup() after Wire.begin().
// Selects the internal 4.096 V reference and zeroes the DAC output.
void initDAC() {
  dacSendCommand(LTC2631_CMD_INTERNAL_REF, 0);
  dacSendCommand(LTC2631_CMD_WRITE_UPDATE, 0);  // Start at 0 V
}

// ─── setCurrentLimitDAC ───────────────────────────────────────────────────────
// Converts a desired current limit (A) to the corresponding DAC code and writes
// it to the LTC2631, updating the comparator reference immediately.
//
// Derivation:
//   V_setpoint = SENSOR_SLOPE * amps + SENSOR_OFFSET
//   code = round(V_setpoint / 4.096 * 4095)   clamped to [0, 4095]
void setCurrentLimitDAC(float amps) {
  float vSetpoint = SENSOR_SLOPE * amps + SENSOR_OFFSET;

  // Clamp voltage to DAC full-scale range [0 V, 4.096 V]
  if (vSetpoint < 0.0f)      vSetpoint = 0.0f;
  if (vSetpoint > 4.095f)    vSetpoint = 4.095f;

  uint16_t code = (uint16_t)(vSetpoint / 4.096f * 4095.0f + 0.5f);
  if (code > 4095) code = 4095;

  dacSendCommand(LTC2631_CMD_WRITE_UPDATE, code);

  Serial.print("[DAC] Current limit: ");
  Serial.print(amps, 2);
  Serial.print(" A  →  V_ref: ");
  Serial.print(vSetpoint, 4);
  Serial.print(" V  →  code: ");
  Serial.println(code);
}

// ─── dacSendCommand ───────────────────────────────────────────────────────────
// Low-level 3-byte I2C write to the LTC2631.
// Byte layout per datasheet:
//   Byte 1 : Command nibble (upper 4 bits) | don't-care (lower 4 bits)
//   Byte 2 : D[11:4]   — upper 8 bits of the 12-bit code
//   Byte 3 : D[3:0]<<4 — lower 4 bits in the upper nibble; lower nibble = don't-care
void dacSendCommand(byte command, uint16_t data) {
  uint16_t dataToSend = data << 4;  // Align 12-bit value to upper bits of 16-bit word

  Wire.beginTransmission(DAC_I2C_ADDRESS);
  Wire.write(command);               // Byte 1: command
  Wire.write(highByte(dataToSend)); // Byte 2: D[11:4]
  Wire.write(lowByte(dataToSend));  // Byte 3: D[3:0] in upper nibble
  byte err = Wire.endTransmission();

  if (err != 0) {
    Serial.print("[DAC] I2C error: ");
    Serial.println(err);
  }
}
