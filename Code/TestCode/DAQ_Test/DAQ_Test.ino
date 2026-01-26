#include <Wire.h>

/* * LTC2631-HZ12 Configuration
 * I2C Address: 0x73 (Global Address) 
 * Full Scale Voltage: 4.096V (Internal Reference) [cite: 136]
 * Resolution: 12-bit (0 - 4095)
 */

const byte DAC_ADDR = 0x10; 

// Commands from Table 3 of the Datasheet [cite: 327]
const byte CMD_WRITE_UPDATE = 0x30; // Write to and Update DAC Register
const byte CMD_INTERNAL_REF = 0x60; // Select Internal Reference

// Variables
int currentVoltageStep = 0; // 0 = 0V, 1 = 1V, 2 = 2V, etc.

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Initialize I2C (SDA = A4, SCL = A5 on Nano ESP32 by default)

  // Wait a moment for power to stabilize
  delay(100);

  Serial.println("Initializing LTC2631-HZ12...");

  // 1. Set DAC to Internal Reference Mode
  // The HZ model defaults to Internal Ref on power reset[cite: 331], 
  // but we send the command explicitly to be sure.
  sendI2CCommand(CMD_INTERNAL_REF, 0); 
}

void loop() {
  // 2. Calculate DAC Code
  // V_OUT = (Code / 4096) * 4.096V
  // Therefore, 1000 Code = 1.0V exactly.
  int dacCode = currentVoltageStep * 1000;

  // 3. Loop back if voltage is too high
  // Max code is 4095 (4.095V). 4000 code is 4.0V.
  // If we try to go to 5V (5000 code), we reset.
  if (dacCode > 4095) {
    Serial.println("Max voltage reached. Resetting to 0V.");
    currentVoltageStep = 0;
    dacCode = 0;
  }

  // 4. Send Voltage Command to DAC
  Serial.print("Setting Output to: ");
  Serial.print(currentVoltageStep);
  Serial.print("V (Code: ");
  Serial.print(dacCode);
  Serial.println(")");
  
  sendI2CCommand(CMD_WRITE_UPDATE, dacCode);

  // 5. Increment and Wait
  currentVoltageStep++;
  delay(5000); // Wait 5 seconds
}

void sendI2CCommand(byte command, uint16_t data) {
  // The LTC2631 expects a 3-byte sequence:
  // Byte 1: Command (4 bits) + Don't Care (4 bits)
  // Byte 2: MSB of Data (8 bits)
  // Byte 3: LSB of Data (4 bits) + Don't Care (4 bits)

  // Shift data left by 4 to align the 12-bit value into the upper positions
  uint16_t dataToSend = data << 4;

  Wire.beginTransmission(DAC_ADDR);
  Wire.write(command);              // Byte 1: Command
  Wire.write(highByte(dataToSend)); // Byte 2: Upper 8 bits
  Wire.write(lowByte(dataToSend));  // Byte 3: Lower 8 bits
  Wire.endTransmission();
}