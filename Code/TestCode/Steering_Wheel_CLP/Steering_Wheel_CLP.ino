#include <Wire.h>
#include "Longan_I2C_CAN_Arduino.h" // communicates with CAN Bus module
#include "rgb_lcd.h"                 // LCD screen

// LCD Variables
rgb_lcd lcd; 
// adjust to change LCD backlight colour
const int colorR = 0;
const int colorG = 50;
const int colorB = 0;

// Lap button 
const byte interruptPin = 2; // lap button is on pin 2
unsigned int  lapNumber = 0;
const unsigned long debounceMs = 500;
unsigned long lastLapMs = 0;
bool running = false;
volatile bool pressed = false;
unsigned long lapStartMs = 0;     // when current lap started

// CAN
I2C_CAN        CAN(0x25);  // Set I2C Address
unsigned char  len = 0;
unsigned long  canId = 0;
uint8_t rxBuf[8];    // 8 bytes


// Telemetry 
float wheelSpeedKph = 0.0f;
unsigned long lastSpeedRxMs = 0;

// Schedulers 
const unsigned long LCD_PERIOD_MS = 100; // Update LDC at 10 Hz (every 100ms)
unsigned long lastLcdMs = 0; // Tracks last time LCD was updated
// Store previously printed text
char prevLine0[17] = "";
char prevLine1[17] = "";

// Prints only new text onto LCD screen
static void printLineIfChanged(uint8_t row, const char* line, char* cache) {
  // compare new line with previous line, returns non-zero value if different
  if (strncmp(line, cache, 16) != 0) { 
    lcd.setCursor(0, row);
    // write  16 chars (2x16 screen)
    for (uint8_t i = 0; i < 16; i++) {
      char c = line[i];
      lcd.print(c ? c : ' '); // print space if string ended
      if (!c) { 
        for (uint8_t j = i + 1; j < 16; j++) lcd.print(' ');
        break;
      }
    }
    strncpy(cache, line, 16);
    cache[16] = '\0';
  }
}

static float readFloatLE(const uint8_t* b) {
  float f;
  memcpy(&f, b, sizeof(float));
  return f;
}

void pressLapButton(){
  pressed = true; 
}


// SETUP
void setup() {
  delay(200);
  // Start I2C and increase speed to 400 kHz
  Wire.begin();
  Wire.setClock(400000);

  // Initialise LCD
  lcd.begin(16, 2);
  lcd.setRGB(colorR, colorG, colorB);
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("Starting...");

  Serial.begin(9600);

  while (CAN_OK != CAN.begin(CAN_500KBPS)) { // baud rate of 500k
    Serial.println("CAN BUS FAIL!");
    delay(200);
  }
  Serial.println("CAN BUS OK!");

  // Initialize lap button interrupt pin and interrupt
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), pressLapButton, FALLING);

  delay(500);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Press button");
  lcd.setCursor(0, 1); lcd.print("to start");
}

// Read speed only from CAN
void readSpeed() {
  const unsigned long now = millis();

  while (CAN_MSGAVAIL == CAN.checkReceive()) {
    CAN.readMsgBuf(&len, rxBuf);
    canId = CAN.getCanId();

    if (canId == 1 && len >= 4) {
      float sp = readFloatLE(rxBuf);
      if (!isfinite(sp) || sp < 0) sp = 0;
      if (sp > 999.9f) sp = 999.9f;
      wheelSpeedKph = sp;
      lastSpeedRxMs = now;
    }
  }
}

void writeToLCD() {
  char line0[17];
  char line1[17];

  if (!running) {
    snprintf(line0, sizeof(line0), "Press button");
    snprintf(line1, sizeof(line1), "to start");
  } else {
    int sp = (int)(wheelSpeedKph + 0.5f);  // round to whole number
    if (sp < 0) sp = 0;
    if (sp > 999) sp = 999;
    // Lap time elapsed
    unsigned long lapMs = millis() - lapStartMs;
    unsigned int lapSec = lapMs / 1000;
    unsigned int min = lapSec / 60;
    unsigned int sec = lapSec % 60;

    snprintf(line0, sizeof(line0), "SPD:%3d km/h", sp);
    snprintf(line1, sizeof(line1), "L%3u %2u:%02u", lapNumber, min, sec);
   }

  printLineIfChanged(0, line0, prevLine0);
  printLineIfChanged(1, line1, prevLine1);
}


// LOOP
void loop() {

  readSpeed();

  const unsigned long now = millis();
  
  if (pressed) {
    pressed = false;
    if (now - lastLapMs >= debounceMs) {
      lastLapMs = now;

      if (!running) {
        running = true;
        lapNumber = 1;
        lapStartMs = now;   // start lap timer
        prevLine0[0] = prevLine1[0] = '\0';   // write to screen
      } else {
        lapNumber++;
        lapStartMs = now;   // start new lap
      }
    }
  }

  // Update LCD at 10 Hz
  if (now - lastLcdMs >= LCD_PERIOD_MS) {
    lastLcdMs = now;
    writeToLCD();
  }
}

