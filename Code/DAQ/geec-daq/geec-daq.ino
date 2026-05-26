/**
 * ESP32 GPS + SD Card Logger with Error Blink Codes, Wheel Speed Sensor, and Voltage/Current Monitoring
 * CAN Bus using Seeed Studio CAN Bus Module (I2C Mode)
 * 
 * GPS Connections:
 * - GPS TX -> GPIO44 (ESP32 receives from GPS)
 * - GPS RX -> GPIO43 (ESP32 sends to GPS)
 * - GPS VIN -> 3.3V
 * - GPS GND -> GND
 * 
 * SD Card Module Connections:
 * - CS  -> GPIO21
 * - SCK -> GPIO48
 * - MOSI -> GPIO38
 * - MISO -> GPIO47
 * - VIN -> 5V (if module requires 5V) or 3.3V
 * - GND -> GND
 * 
 * Indicator LEDs:
 * - Blink LED 1 -> GPIO10 (General status/activity)
 * - Blink LED 2 -> GPIO9 (Error codes)
 * 
 * Error Codes (LED on GPIO9):
 * - 2 blinks, pause: SD Card error
 * - 3 blinks, pause: GPS error/no fix timeout
 * - 4 blinks, pause: CAN bus error
 * - 5 blinks, pause: Sensor error
 * 
 * Wheel Speed Sensor:
 * - Sensor Signal -> GPIO5 (pulses on each spoke detection)
 * - 6 spokes per wheel = 60° per pulse
 * 
 * Analog Sensors:
 * - Voltage Sensor -> GPIO6 (A6) - 0-30V range scaled for 5V logic
 * - Current Sensor -> GPIO7 (A7) - 0-30A range scaled for 5V logic
 * 
 * CAN Bus Module (Seeed Studio I2C Mode):
 * - Connect using standard I2C pins:
 * - SDA -> GPIO21
 * - SCL -> GPIO22
 * - VCC -> 5V
 * - GND -> GND
 */

#include <Adafruit_GPS.h>
#include <SD.h>
#include <Wire.h>
#include "I2C_CAN.h"  // Working CAN library

// ========== GPS PIN CONFIGURATION ==========
#define GPS_RX_PIN 44   // ESP32 RX from GPS TX
#define GPS_TX_PIN 43   // ESP32 TX to GPS RX
// ============================================

// ========== SD CARD PIN CONFIGURATION ==========
#define SD_CS    21      // Chip Select
#define SD_SCK   48      // SPI Clock
#define SD_MOSI  38      // SPI MOSI
#define SD_MISO  47      // SPI MISO
// ================================================

// ========== INDICATOR LEDS ==========
#define ACTIVITY_LED 10     // Blinking LED for general activity
#define ERROR_LED    9      // Blinking LED for error codes
// ====================================

// ========== WHEEL SPEED SENSOR ==========
#define WHEEL_SENSOR_PIN 5   // GPIO5 - wheel speed sensor input
#define WHEEL_DIAMETER 0.5    // Wheel diameter in meters
#define SPOKES_PER_REV 6      // Number of spokes = pulses per revolution
// ========================================

// ========== ANALOG SENSORS ==========
#define VOLTAGE_PIN 6      // GPIO6 (A6) - Voltage sensor input
#define CURRENT_PIN 7      // GPIO7 (A7) - Current sensor input
#define ADC_MAX 4095       // 12-bit ADC max value
#define ADC_REF_VOLTAGE 3.3 // ESP32 ADC reference voltage
#define VOLTAGE_DIVIDER_RATIO 11.0  // Voltage divider ratio for 0-30V range
#define CURRENT_SENSOR_SCALE 9.09   // (30A / 3.3V) = 9.09 A/V
// ====================================

// ========== CAN BUS CONFIGURATION ==========
#define CAN_I2C_ADDRESS 0x25  // Default I2C address for Seeed CAN module
// ============================================

// ========== ERROR CODES ==========
#define ERR_NONE        0
#define ERR_SD_CARD     2   // 2 blinks
#define ERR_GPS         3   // 3 blinks
#define ERR_CAN         4   // 4 blinks
#define ERR_SENSOR      5   // 5 blinks
// ================================

#define GPS_BAUD 9600
#define GPS_FIX_TIMEOUT 300000  // 5 minutes without fix = error

// Connect to the GPS on hardware serial port (UART2)
HardwareSerial gpsSerial(2);
Adafruit_GPS GPS(&gpsSerial);

// Set to true to see raw NMEA sentences
#define GPSECHO false

// File object for SD card
File dataFile;
bool sdCardAvailable = false;

// SPI pins for SD card
SPIClass spiSD(HSPI);

// Filename for logging
char filename[32] = "";

// Wheel speed sensor variables
volatile unsigned long pulseCount = 0;
unsigned long lastPulseCount = 0;
unsigned long lastSpeedCalcTime = 0;
float wheelSpeedMPS = 0.0;
float wheelSpeedKPH = 0.0;
bool wheelSensorDetected = false;

// Analog sensor variables
float voltageValue = 0.0;
float currentValue = 0.0;
float powerValue = 0.0;

// CAN Bus variables
I2C_CAN CAN(CAN_I2C_ADDRESS);  // Initialize CAN object with I2C address
bool canInitialized = false;
unsigned long lastCanSendTime = 0;
const unsigned long CAN_SEND_INTERVAL = 100;  // Send CAN messages every 100ms

// GPS tracking
bool gpsFixStatus = false;
unsigned long gpsFixStartTime = 0;
unsigned long lastFixTime = 0;

// Error handling
uint8_t currentError = ERR_NONE;
unsigned long lastErrorBlinkTime = 0;
int errorBlinkCount = 0;
bool errorBlinkState = false;

// Timer for GPS updates
uint32_t timer = millis();

// Interrupt service routine for wheel sensor
void IRAM_ATTR wheelSensorISR() {
  pulseCount++;
}

// ========== ERROR HANDLING FUNCTIONS ==========
void setError(uint8_t errorCode) {
  if (currentError != errorCode) {
    currentError = errorCode;
    errorBlinkCount = 0;
    lastErrorBlinkTime = millis();
    errorBlinkState = false;
    
    Serial.print("❌ Error set: ");
    switch(errorCode) {
      case ERR_SD_CARD: Serial.println("SD Card Error"); break;
      case ERR_GPS: Serial.println("GPS Error"); break;
      case ERR_CAN: Serial.println("CAN Bus Error"); break;
      case ERR_SENSOR: Serial.println("Sensor Error"); break;
      default: Serial.println("Unknown Error"); break;
    }
  }
}

void clearError() {
  if (currentError != ERR_NONE) {
    currentError = ERR_NONE;
    digitalWrite(ERROR_LED, LOW);
    Serial.println("✅ Errors cleared");
  }
}

void handleErrorBlinking() {
  if (currentError == ERR_NONE) {
    digitalWrite(ERROR_LED, LOW);
    return;
  }
  
  unsigned long now = millis();
  
  if (errorBlinkCount < currentError) {
    // Still need to blink
    if (!errorBlinkState) {
      // Turn LED on
      digitalWrite(ERROR_LED, HIGH);
      errorBlinkState = true;
      lastErrorBlinkTime = now;
    } else if (now - lastErrorBlinkTime > 200) {
      // Turn LED off after 200ms
      digitalWrite(ERROR_LED, LOW);
      errorBlinkState = false;
      errorBlinkCount++;
      lastErrorBlinkTime = now;
    }
  } else if (now - lastErrorBlinkTime > 1000) {
    // Pause for 1 second, then repeat
    errorBlinkCount = 0;
    lastErrorBlinkTime = now;
  }
}

// ========== HELPER FUNCTIONS ==========
String formatNumber(int num, int digits) {
  String result = String(num);
  while (result.length() < digits) {
    result = "0" + result;
  }
  return result;
}

float readVoltage(int pin) {
  int adcValue = analogRead(pin);
  float voltageAtPin = (adcValue * ADC_REF_VOLTAGE) / ADC_MAX;
  float actualVoltage = voltageAtPin * VOLTAGE_DIVIDER_RATIO;
  
  // Sanity check
  if (actualVoltage < 0 || actualVoltage > 35) {
    setError(ERR_SENSOR);
  }
  
  return actualVoltage;
}

float readCurrent(int pin) {
  int adcValue = analogRead(pin);
  float voltageAtPin = (adcValue * ADC_REF_VOLTAGE) / ADC_MAX;
  float current = voltageAtPin * CURRENT_SENSOR_SCALE;
  
  if (current < 0) current = 0;
  if (current > 30) current = 30;
  
  // Sanity check
  if (current < -5 || current > 35) {
    setError(ERR_SENSOR);
  }
  
  return current;
}

// ========== CAN BUS FUNCTIONS (Using Working Library) ==========
void sendCanData() {
  if (!canInitialized) {
    setError(ERR_CAN);
    return;
  }
  
  unsigned char canData[8];
  
  // Prepare speed data to send
  // Convert float to bytes (4 bytes for wheel speed KPH)
  // Using union for easy float to byte conversion
  union {
    float f;
    unsigned char bytes[4];
  } speedUnion;
  
  speedUnion.f = wheelSpeedKPH;
  canData[0] = speedUnion.bytes[0];
  canData[1] = speedUnion.bytes[1];
  canData[2] = speedUnion.bytes[2];
  canData[3] = speedUnion.bytes[3];
  
  // Optionally add GPS speed for comparison (scaled to avoid float)
  uint16_t gpsSpeedScaled = (uint16_t)(GPS.speed * 1.852 * 100);
  canData[4] = gpsSpeedScaled & 0xFF;
  canData[5] = (gpsSpeedScaled >> 8) & 0xFF;
  canData[6] = 0;
  canData[7] = 0;
  
  // Send CAN message with ID 0x100 (speed data)
  if (CAN.sendMsgBuf(0x100, 0, 8, canData) != CAN_OK) {
    setError(ERR_CAN);
  } else {
    // Clear CAN error if we successfully sent
    if (currentError == ERR_CAN) {
      clearError();
    }
  }
  
  // Also send sensor data on a different ID if desired
  unsigned char sensorData[8];
  uint16_t voltageScaled = (uint16_t)(voltageValue * 100);
  uint16_t currentScaled = (uint16_t)(currentValue * 100);
  uint16_t powerScaled = (uint16_t)(powerValue * 100);
  
  sensorData[0] = voltageScaled & 0xFF;
  sensorData[1] = (voltageScaled >> 8) & 0xFF;
  sensorData[2] = currentScaled & 0xFF;
  sensorData[3] = (currentScaled >> 8) & 0xFF;
  sensorData[4] = powerScaled & 0xFF;
  sensorData[5] = (powerScaled >> 8) & 0xFF;
  sensorData[6] = 0;
  sensorData[7] = 0;
  
  // Send sensor data with ID 0x101
  CAN.sendMsgBuf(0x101, 0, 8, sensorData);
}

void activityBlink() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  
  if (millis() - lastBlink > 500) {  // Blink every 500ms
    ledState = !ledState;
    digitalWrite(ACTIVITY_LED, ledState);
    lastBlink = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("================================================");
  Serial.println("ESP32 GPS + SD Card Logger with Error Codes");
  Serial.println("================================================");
  
  // Initialize LEDs
  pinMode(ACTIVITY_LED, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ACTIVITY_LED, LOW);
  digitalWrite(ERROR_LED, LOW);
  
  // Initialize wheel sensor
  pinMode(WHEEL_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WHEEL_SENSOR_PIN), wheelSensorISR, RISING);
  
  // Initialize analog pins
  pinMode(VOLTAGE_PIN, INPUT);
  pinMode(CURRENT_PIN, INPUT);
  analogReadResolution(12);
  
  // Initialize I2C
  Wire.begin();
  Wire.setClock(100000);
  
  // Initialize CAN using working library
  Serial.println("\n📌 Initializing CAN Bus...");
  if (CAN.begin(CAN_500KBPS) == CAN_OK) {
    canInitialized = true;
    Serial.println("   ✅ CAN Bus initialized!");
  } else {
    Serial.println("   ❌ CAN Bus initialization failed!");
    setError(ERR_CAN);
  }
  
  // Initialize SD card
  Serial.print("\nInitializing SD card...");
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  int retryCount = 0;
  bool sdInitSuccess = false;
  
  while (retryCount < 3 && !sdInitSuccess) {
    if (retryCount > 0) {
      Serial.print("\n   Retry " + String(retryCount) + "...");
      delay(500);
    }
    if (SD.begin(SD_CS, spiSD)) {
      sdInitSuccess = true;
    }
    retryCount++;
  }
  
  if (!sdInitSuccess) {
    Serial.println(" ❌ SD Card failed!");
    sdCardAvailable = false;
    setError(ERR_SD_CARD);
  } else {
    Serial.println(" ✅ SD Card initialized!");
    sdCardAvailable = true;
    clearError();  // Clear SD error if it was set
    
    // Find next available filename
    int lapNumber = 1;
    bool fileExists = true;
    
    while (fileExists) {
      sprintf(filename, "/lap%d.csv", lapNumber);
      if (SD.exists(filename)) {
        lapNumber++;
      } else {
        fileExists = false;
      }
    }
    
    // Create file with header
    dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.println("UTC Time,Date,Latitude,Longitude,Altitude(m),GPS Speed(km/h),Heading(°),Satellites,Fix Quality,Wheel Speed(m/s),Wheel Speed(km/h),Pulses,Voltage(V),Current(A),Power(W)");
      dataFile.flush();
      dataFile.close();
      Serial.println("   File created: " + String(filename));
    }
    
    dataFile = SD.open(filename, FILE_APPEND);
    if (dataFile) {
      Serial.println("   ✅ File ready for logging");
    }
  }
  
  // Initialize GPS
  Serial.println("\nInitializing GPS...");
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  GPS.begin(GPS_BAUD);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  delay(1000);
  
  Serial.println("✅ GPS initialized!");
  gpsFixStartTime = millis();
  
  // Test sensors
  delay(100);
  if (pulseCount > 0) {
    wheelSensorDetected = true;
    Serial.println("✅ Wheel sensor detected!");
  }
  
  float testVoltage = readVoltage(VOLTAGE_PIN);
  float testCurrent = readCurrent(CURRENT_PIN);
  Serial.print("✅ Analog readings - Voltage: ");
  Serial.print(testVoltage, 2);
  Serial.print("V, Current: ");
  Serial.print(testCurrent, 2);
  Serial.println("A");
  
  Serial.println("\n⏱️  Waiting for GPS fix...\n");
  
  lastSpeedCalcTime = millis();
  lastPulseCount = pulseCount;
  lastCanSendTime = millis();
  lastFixTime = millis();
}

void loop() {
  // Handle error blinking
  handleErrorBlinking();
  
  // Activity LED blink
  activityBlink();
  
  // Read GPS
  char c = GPS.read();
  if (GPSECHO && c) {
    Serial.print(c);
  }
  
  if (GPS.newNMEAreceived()) {
    if (!GPS.parse(GPS.lastNMEA())) {
      return;
    }
  }
  
  // Check GPS fix status and timeout
  if (GPS.fix) {
    gpsFixStatus = true;
    lastFixTime = millis();
    clearError();  // Clear GPS error if we have a fix
  } else {
    gpsFixStatus = false;
    // Check for GPS timeout
    if (millis() - lastFixTime > GPS_FIX_TIMEOUT) {
      setError(ERR_GPS);
    }
  }
  
  // Read analog sensors
  voltageValue = readVoltage(VOLTAGE_PIN);
  currentValue = readCurrent(CURRENT_PIN);
  powerValue = voltageValue * currentValue;
  
  // Send CAN data at regular intervals
  if (canInitialized && (millis() - lastCanSendTime > CAN_SEND_INTERVAL)) {
    sendCanData();
    lastCanSendTime = millis();
  }
  
  // Log data every 2 seconds
  if (millis() - timer > 2000) {
    timer = millis();
    
    // Calculate wheel speed
    unsigned long currentTime = millis();
    unsigned long timeDelta = currentTime - lastSpeedCalcTime;
    
    noInterrupts();
    unsigned long currentPulseCount = pulseCount;
    interrupts();
    
    unsigned long pulseDelta = currentPulseCount - lastPulseCount;
    
    if (timeDelta > 0 && pulseDelta > 0) {
      float distancePerPulse = (3.14159 * WHEEL_DIAMETER) / SPOKES_PER_REV;
      float distance = pulseDelta * distancePerPulse;
      wheelSpeedMPS = (distance * 1000.0) / timeDelta;
      wheelSpeedKPH = wheelSpeedMPS * 3.6;
      wheelSensorDetected = true;
    } else {
      wheelSpeedMPS = 0;
      wheelSpeedKPH = 0;
    }
    
    // Create log data
    String logData = "";
    String utcTime = formatNumber(GPS.hour, 2) + ":" + 
                     formatNumber(GPS.minute, 2) + ":" + 
                     formatNumber(GPS.seconds, 2);
    String date = formatNumber(GPS.day, 2) + "/" + 
                  formatNumber(GPS.month, 2) + "/20" + 
                  formatNumber(GPS.year, 2);
    
    logData += utcTime + "," + date + ",";
    
    if (GPS.fix) {
      logData += String(GPS.latitudeDegrees, 6) + ",";
      logData += String(GPS.longitudeDegrees, 6) + ",";
      logData += String(GPS.altitude) + ",";
      logData += String(GPS.speed * 1.852) + ",";
      logData += String(GPS.angle) + ",";
      logData += String(GPS.satellites) + ",";
      if (GPS.fixquality == 1) logData += "GPS";
      else if (GPS.fixquality == 2) logData += "DGPS";
      else logData += "Unknown";
    } else {
      logData += "0,0,0,0,0,0,No Fix";
    }
    
    logData += "," + String(wheelSpeedMPS, 2);
    logData += "," + String(wheelSpeedKPH, 2);
    logData += "," + String(pulseDelta);
    logData += "," + String(voltageValue, 2);
    logData += "," + String(currentValue, 2);
    logData += "," + String(powerValue, 2);
    
    // Write to SD card
    if (sdCardAvailable) {
      if (!dataFile) {
        dataFile = SD.open(filename, FILE_APPEND);
      }
      
      if (dataFile) {
        dataFile.println(logData);
        dataFile.flush();
        
        // Extra activity blink on data write
        digitalWrite(ACTIVITY_LED, HIGH);
        delay(50);
        digitalWrite(ACTIVITY_LED, LOW);
        
        Serial.println("✅ Data logged");
        clearError();  // Clear SD error if successful
      } else {
        setError(ERR_SD_CARD);
      }
    }
    
    // Print to Serial (minimal output to avoid clutter)
    Serial.println("\n📊 --- Status ---");
    Serial.print("🕐 " + utcTime);
    Serial.print(" | GPS: ");
    Serial.print(GPS.fix ? "YES" : "NO");
    Serial.print(" | Wheel: ");
    Serial.print(wheelSpeedKPH, 1);
    Serial.print(" km/h");
    Serial.print(" | " + String(voltageValue, 1) + "V");
    Serial.print(" | " + String(currentValue, 1) + "A");
    
    if (currentError != ERR_NONE) {
      Serial.print(" | ERR:");
      switch(currentError) {
        case ERR_SD_CARD: Serial.print("SD"); break;
        case ERR_GPS: Serial.print("GPS"); break;
        case ERR_CAN: Serial.print("CAN"); break;
        case ERR_SENSOR: Serial.print("SENS"); break;
      }
    }
    Serial.println();
    
    // Update last values
    lastPulseCount = currentPulseCount;
    lastSpeedCalcTime = currentTime;
  }
}