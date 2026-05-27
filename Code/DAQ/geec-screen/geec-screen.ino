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

// ========== RGB LCD CONFIGURATION ==========
#define TEXT_ADDR 0x3E  // LCD text address
#define RGB_ADDR  0x30  // LCD RGB backlight address
rgb_lcd lcd;

// ========== CAN BUS CONFIGURATION ==========
#define CAN_I2C_ADDRESS 0x25  // Default I2C address for Seeed CAN module
I2C_CAN CAN(CAN_I2C_ADDRESS);  // Initialize CAN object

// CAN Message IDs (matching transmitter)
#define CAN_ID_SPEED   0x100  // Speed data (wheel + GPS)

// ========== BUTTON CONFIGURATION ==========
#define LAP_BUTTON_PIN 5  // Button pin for lap timer reset
#define DEBOUNCE_DELAY 250  // Debounce delay in ms

// ========== DATA VARIABLES ==========
// Speed data
float wheelSpeedKPH = 0.0;  // Wheel speed in km/h

// CAN status
bool canInitialized = false;
unsigned long lastCanReceiveTime = 0;

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
      if (canId == CAN_ID_SPEED && len >= 8) {
        // Speed data packet (ID: 0x100)
        // Bytes 0-3: Wheel speed (float, km/h)
        
        // Extract wheel speed (float)
        memcpy(&wheelSpeedKPH, &buf[0], 4);
        
        // Debug output
        Serial.print("📊 CAN Speed: ");
        Serial.print(wheelSpeedKPH, 1);
        Serial.println(" km/h");
      }
    }
  }
  
  // If no CAN message received for 3 seconds, show timeout indicator
  if (millis() - lastCanReceiveTime > 3000 && lastCanReceiveTime > 0) {
    // Only print timeout message once
    static bool timeoutPrinted = false;
    if (!timeoutPrinted) {
      Serial.println("⚠️ CAN timeout - No data received for 3 seconds");
      timeoutPrinted = true;
    }
    // Set speed to -1 to indicate no data
    wheelSpeedKPH = -1;
  } else {
    // Reset timeout flag when data is received
    static bool timeoutPrinted = false;
    if (timeoutPrinted && (millis() - lastCanReceiveTime) < 3000) {
      timeoutPrinted = false;
    }
  }
}

void updateDisplay() {
  // Format lap timer as MM:SS (top left)
  char lapTimeStr[6];
  sprintf(lapTimeStr, "%02d:%02d", lapMinutes, lapSeconds);
  
  // Format total time counter as MM:SS (top right)
  char totalTimeStr[6];
  sprintf(totalTimeStr, "%02d:%02d", totalMinutes, totalSeconds);
  
  // Format speed (bottom left)
  char speedStr[8];
  if (wheelSpeedKPH >= 0) {
    // Show speed with one decimal place
    sprintf(speedStr, "%4.1fkmh", wheelSpeedKPH);  // "xx.xkmh" format
  } else {
    sprintf(speedStr, " --.-kmh");
  }
  
  // Format lap counter as L:xx (bottom right)
  char lapStr[5];
  sprintf(lapStr, "L:%02d", lapCount);
  
  // Display on LCD - Row 0 (top line)
  lcd.setCursor(0, 0);
  lcd.print(" ");              // 1 space
  lcd.print(lapTimeStr);      // "00:00" (5 chars)
  lcd.print("     ");              // 1 space
  lcd.print(totalTimeStr);    // "00:00" (5 chars)
  
  // Fill remaining space with spaces (for clean display)
  // int remaining = 16 - (5 + 2 + 5);
  // for (int i = 0; i < remaining; i++) {
  //   lcd.print("      ");
  // }
  
  // Display on LCD - Row 1 (bottom line)
  lcd.setCursor(0, 1);
  
  // Show speed
  if (wheelSpeedKPH >= 0) {
    lcd.print(speedStr);      // "xx.xkmh" (6 chars)
  } else {
    lcd.print("--.-kmh");     // "--.-kmh" (7 chars)
  }
  
  // Add spacing to center the lap counter on the right side
  lcd.print("     ");         // 5 spaces
  
  // Show lap counter (L:xx)
  lcd.print(lapStr);          // "L:xx" (4 chars)
  
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