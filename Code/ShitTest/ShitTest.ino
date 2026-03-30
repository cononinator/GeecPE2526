// I2C bus scanner:
// - Probes all 7-bit addresses (0x01 to 0x7E)
// - Prints every detected device address

#include <Wire.h>

static void scanI2CBus() {
  int foundCount = 0;

  Serial.println("Scanning I2C bus...");

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at 0x");
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
      foundCount++;
    } else if (error == 4) {
      Serial.print("Unknown error at 0x");
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }

  if (foundCount == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Scan complete. Devices found: ");
    Serial.println(foundCount);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin();

  Serial.println();
  Serial.println("I2C scanner started.");
}

void loop() {
  scanI2CBus();
  Serial.println();
  delay(2000);
}
