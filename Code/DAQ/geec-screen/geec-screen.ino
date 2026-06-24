/**
 * ESP32 CAN Bus Receiver with RGB LCD Display
 * Receives speed data from CAN bus and displays on RGB LCD
 * 
 * CAN Bus Module (Seeed Studio I2C Mode):
 * - Connect using standard I2C pins:
 * - SDA -> GPIO21
 * - SCL -> GPIO22
 * - VCC -> 5V
 * - GND -> GND
 * 
 * RGB LCD Display (I2C):
 * - SDA -> GPIO21 (shared with CAN)
 * - SCL -> GPIO22 (shared with CAN)
 * - VCC -> 5V
 * - GND -> GND
 * 
 * Button Input:
 * - Lap Button -> GPIO5 (resets lap timer)
 */

#include <Wire.h>
#include "rgb_lcd.h"
#include "I2C_CAN.h"
#include "CANid.h"

// ========== RGB LCD CONFIGURATION ==========
#define TEXT_ADDR 0x3E  // LCD text address
#define RGB_ADDR  0x30  // LCD RGB backlight address
rgb_lcd lcd;

// ========== CAN BUS CONFIGURATION ==========
#define CAN_I2C_ADDRESS 0x25  // Default I2C address for Seeed CAN module
I2C_CAN CAN(CAN_I2C_ADDRESS);  // Initialize CAN object

// ========== BUTTON CONFIGURATION ==========
#define LAP_BUTTON_PIN 5  // Button pin for lap timer reset
#define DEBOUNCE_DELAY 250  // Debounce delay in ms

// ========== DATA VARIABLES ==========
// Speed data
float wheelSpeedKPH = 0.0;  // Wheel speed in km/h

// PE data received from CANTask
bool peStatus = false;
float peMotorCurrent = 0.0;
float peMotorVoltage = 0.0;
float peBatteryVoltage = 0.0;
float peBatteryCurrent = 0.0;
float peTemperature = 0.0;
float peEnergy = 0.0;
float pePower = 0.0;
float peDutyCycle = 0.0;
float peCurrentLimit = 0.0;

// CAN status
bool canInitialized = false;
unsigned long lastCanReceiveTime = 0;
unsigned long lastPeReceiveTime = 0;
static bool timeoutPrinted = false;

// Timer variables (lap timer - resets on lap button)
unsigned long lapStartTime = 0;
unsigned long lapElapsedSeconds = 0;
int lapMinutes = 0;
int lapSeconds = 0;

// Total time counter (never resets - counts total seconds since start)
unsigned long totalStartTime = 0;
unsigned long totalElapsedSeconds = 0;
int totalMinutes = 0;
int totalSeconds = 0;

// Lap counter
int lapCount = 0;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Display update
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 200;  // Update display every 200ms

// ========== FUNCTION PROTOTYPES ==========
void updateTimers();
void checkLapButton();
void readCAN();
void updateDisplay();
void setBacklightWhite();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("================================================");
  Serial.println("ESP32 CAN Bus Receiver with RGB LCD");
  Serial.println("================================================");
  
  // Initialize I2C
  Wire.begin();
  Wire.setClock(100000);
  
  // Initialize RGB LCD
  Serial.println("\n📌 Initializing RGB LCD...");
  lcd.begin(16, 2);
  setBacklightWhite();  // Set backlight to white and keep it white
  lcd.print("CAN Receiver");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  
  // Initialize button
  pinMode(LAP_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize CAN bus using working library
  Serial.println("\n📌 Initializing CAN Bus...");
  if (CAN.begin(CAN_500KBPS) == CAN_OK) {
    canInitialized = true;
    Serial.println("   ✅ CAN Bus initialized!");
    lcd.clear();
    lcd.print("CAN OK");
    delay(1000);
  } else {
    Serial.println("   ❌ CAN Bus initialization failed!");
    lcd.clear();
    lcd.print("CAN ERROR!");
    delay(2000);
  }
  
  // Clear LCD and display initial message
  lcd.clear();
  lcd.print("Waiting for data");
  lcd.setCursor(0, 1);
  lcd.print("L:0  Time:00:00");
  
  // Start timers
  lapStartTime = millis();
  totalStartTime = millis();
  lastCanReceiveTime = millis();
  
  Serial.println("\n✅ System ready!");
  Serial.println("Waiting for CAN data...\n");
}

void loop() {
  // Update timers
  updateTimers();
  
  // Check for lap button press
  checkLapButton();
  
  // Read CAN bus for data
  readCAN();
  
  // Update display at regular intervals
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}

void updateTimers() {
  // Update lap timer (resets on button press)
  lapElapsedSeconds = (millis() - lapStartTime) / 1000;
  lapMinutes = lapElapsedSeconds / 60;
  lapSeconds = lapElapsedSeconds % 60;
  
  // Cap lap timer at 99:59
  if (lapMinutes > 99) {
    lapMinutes = 99;
    lapSeconds = 59;
  }
  
  // Update total time counter (never resets - counts total seconds)
  totalElapsedSeconds = (millis() - totalStartTime) / 1000;
  totalMinutes = totalElapsedSeconds / 60;
  totalSeconds = totalElapsedSeconds % 60;
  
  // Cap total minutes at 99 for display
  if (totalMinutes > 99) {
    totalMinutes = 99;
    totalSeconds = 59;
  }
}

void checkLapButton() {
  int reading = digitalRead(LAP_BUTTON_PIN);
  
  // Check if button is pressed (LOW with PULLUP) with debounce
  if (reading == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // Button pressed - reset lap timer and increment lap counter
    lapStartTime = millis();  // Reset lap timer
    lapCount++;               // Increment lap counter
    
    // Print lap info to serial
    Serial.print("🏁 Lap ");
    Serial.print(lapCount);
    Serial.print(" - Lap time: ");
    Serial.print(lapMinutes);
    Serial.print(":");
    if (lapSeconds < 10) Serial.print("0");
    Serial.print(lapSeconds);
    Serial.print(" | Total time: ");
    Serial.print(totalMinutes);
    Serial.print(":");
    if (totalSeconds < 10) Serial.print("0");
    Serial.println(totalSeconds);
    
    lastDebounceTime = millis();
  }
  
  lastButtonState = reading;
}

void readCAN() {
  // Check if CAN message is available
  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    unsigned char len = 0;
    unsigned char buf[8];
    
    // Read the message
    CAN.readMsgBuf(&len, buf);
    
    if (len > 0) {
      unsigned long canId = CAN.getCanId();
      lastCanReceiveTime = millis();
      
      // Parse based on CAN ID
      if (canId == DAQ_SPEED && len >= 4) {
        // Speed data packet (ID: 0x100)
        memcpy(&wheelSpeedKPH, &buf[0], 4);

        Serial.print("CAN Speed: ");
        Serial.print(wheelSpeedKPH, 1);
        Serial.println(" km/h");
      } else if (canId == PE_STATUS && len >= 1) {
        peStatus = (buf[0] != 0);
        lastPeReceiveTime = millis();
      } else if (canId == PE_MOTOR_CURRENT && len >= 4) {
        memcpy(&peMotorCurrent, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_MOTOR_VOLTAGE && len >= 4) {
        memcpy(&peMotorVoltage, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_BATTERY_VOLTAGE && len >= 4) {
        memcpy(&peBatteryVoltage, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_BATTERY_CURRENT && len >= 4) {
        memcpy(&peBatteryCurrent, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_TEMPERATURE && len >= 4) {
        memcpy(&peTemperature, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_ENERGY && len >= 4) {
        memcpy(&peEnergy, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_POWER && len >= 4) {
        memcpy(&pePower, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_DUTY_CYCLE && len >= 4) {
        memcpy(&peDutyCycle, &buf[0], 4);
        lastPeReceiveTime = millis();
      } else if (canId == PE_CURRENT_LIMIT && len >= 4) {
        memcpy(&peCurrentLimit, &buf[0], 4);
        lastPeReceiveTime = millis();
      }
    }
  }
  
  // If no CAN message received for 3 seconds, show timeout indicator
  if (millis() - lastCanReceiveTime > 3000 && lastCanReceiveTime > 0) {
    // Only print timeout message once
    if (!timeoutPrinted) {
      Serial.println("CAN timeout - No data received for 3 seconds");
      timeoutPrinted = true;
    }
    // Set speed to -1 to indicate no data
    wheelSpeedKPH = -1;
  } else {
    // Reset timeout flag when data is received
    if (timeoutPrinted && (millis() - lastCanReceiveTime) < 3000) {
      timeoutPrinted = false;
    }
  }
}

void updateDisplay() {
  // Format lap timer as MM:SS (top left)
  char lapTimeStr[6];
  sprintf(lapTimeStr, "%02d:%02d", lapMinutes, lapSeconds);

  // Alternate the top-right field between total time and temperature
  bool showTemperatureRow = ((millis() / 3000UL) % 2) == 1;

  // Format total time counter as MM:SS
  char totalTimeStr[6];
  sprintf(totalTimeStr, "%02d:%02d", totalMinutes, totalSeconds);

  // Format temperature and speed values for display
  char tempStr[8];
  char speedStr[8];
  if (millis() - lastPeReceiveTime < 3000) {
    sprintf(tempStr, "%5.0fC", peTemperature);
  } else {
    sprintf(tempStr, " ---C");
  }

  if (wheelSpeedKPH >= 0) {
    sprintf(speedStr, "%4.1fkmh", wheelSpeedKPH);  // "xx.xkmh" format
  } else {
    sprintf(speedStr, " --.-kmh");
  }

  // Format lap counter as L:xx (bottom right)
  char lapStr[5];
  sprintf(lapStr, "L:%02d", lapCount);

  // Display on LCD - Row 0 (top line): original layout with temperature alternating in the right slot
  lcd.setCursor(0, 0);
  lcd.print(" ");
  lcd.print(lapTimeStr);
  if (showTemperatureRow) {
    lcd.print("  T:");
    lcd.print(tempStr);
    lcd.print(" ");
  } else {
    lcd.print("     ");
    lcd.print(totalTimeStr);
  }

  // Display on LCD - Row 1 (bottom line): speed + lap counter
  lcd.setCursor(0, 1);
  lcd.print(speedStr);
  lcd.print("     ");
  lcd.print(lapStr);
  lcd.print(" ");

  // Ensure backlight stays white
 // setBacklightWhite();
}

void setBacklightWhite() {
  // Set RGB backlight to white
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x04);  // Red register
  Wire.write(255);
  Wire.endTransmission();
  
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x03);  // Green register
  Wire.write(255);
  Wire.endTransmission();
  
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x02);  // Blue register
  Wire.write(255);
  Wire.endTransmission();
}