#include "HX711.h"

// ========================================
// DYNO SYSTEM - PRODUCTION CODE
// ========================================
// Measures motor RPM, torque, and power
// Uses hall effect sensor (20 magnets) on dyno wheel
// Compensates for car wheel diameter difference
// ========================================

// -----------------------------
// PIN DEFINITIONS
// -----------------------------
const int hallEffectPin = 18;
const int CLK = 2;
const int DOUT = 3;

// -----------------------------
// PHYSICAL CONSTANTS
// -----------------------------
const int numMagnets = 20;
const float dynoWheelDiameter = 500.0;    // mm - where hall sensor measures
const float carWheelDiameter = 470.0;     // mm - actual car wheel size
const float dynoWheelRadius = carWheelDiameter / 2;      // meters
const float torqueDistance = 0.199;       // meters - load cell moment arm
const float calibration_factor = -144.5526;

// -----------------------------
// TIMING CONSTANTS
// -----------------------------
const int rpmSampleTime = 100;      // ms - how often to calculate RPM
const int loadSampleInterval = 1;    // Read load every Nth RPM sample

// -----------------------------
// HARDWARE
// -----------------------------
HX711 loadcell;

// -----------------------------
// RPM MEASUREMENT
// -----------------------------
volatile unsigned long pulseCount = 0;
unsigned long lastRpmSampleTime = 0;

// -----------------------------
// LOAD CELL MANAGEMENT
// -----------------------------
int loadSampleCounter = 0;
double currentLoad = 0;

// -----------------------------
// TIMING
// -----------------------------
long startTime;
long prevTime;

// -----------------------------
// SETUP
// -----------------------------
void setup() {
  // Initialize serial
  Serial.begin(9600);
 
  // Initialize load cell
  loadcell.begin(DOUT, CLK);
  loadcell.set_scale(calibration_factor);
  loadcell.tare();
 
  // Initialize hall effect sensor
  pinMode(hallEffectPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(hallEffectPin), pulseInterrupt, RISING);
 
  // Initialize timing
  startTime = millis();
  prevTime = startTime;
  lastRpmSampleTime = micros();
 
  // Print header
  Serial.println("=== DYNO SYSTEM INITIALIZED ===");
  Serial.println("time_ms,dyno_rpm,car_rpm,torque_Nm,power_W,speed_kmh");
 
  delay(1000);  // Allow system to stabilize
}

// -----------------------------
// MAIN LOOP
// -----------------------------
void loop() {
  if (millis() - prevTime >= rpmSampleTime) {
   
    // Calculate RPM
    double dynoRPM = calculateRPM();
    double carRPM = dynoRPM * (dynoWheelDiameter / carWheelDiameter);

   
    // Sample load cell (only every Nth iteration to minimize interrupt blocking)
    loadSampleCounter++;
    if (loadSampleCounter >= loadSampleInterval) {
      currentLoad = loadcell.get_units(10);
      loadSampleCounter = 0;
    }
   
    // Calculate derived values
    double torque = calculateTorque(currentLoad);
    double power = calculatePower(dynoRPM, torque);
    double speed = calculateSpeed(carRPM);
   
    // Get timestamp
    long timestamp = millis() - startTime;
   

Serial.print("$");
Serial.print(timestamp); Serial.print(" ");
Serial.print(dynoRPM, 2); Serial.print(" ");
Serial.print(carRPM, 2); Serial.print(" ");
Serial.print(torque, 3); Serial.print(" ");
Serial.print(power, 2);
Serial.println(";");
   
    prevTime = millis();
  }
}

// -----------------------------
// INTERRUPT - COUNT PULSES
// -----------------------------
void pulseInterrupt() {
  pulseCount++;
}

// -----------------------------
// CALCULATE RPM FROM PULSES
// -----------------------------
double calculateRPM() {
  unsigned long now = micros();
 
  // Safely read and reset pulse counter
  noInterrupts();
  unsigned long pulses = pulseCount;
  pulseCount = 0;
  interrupts();
 
  // Calculate elapsed time
  unsigned long elapsed = now - lastRpmSampleTime;
  lastRpmSampleTime = now;
 
  // Handle edge cases
  if (elapsed == 0 || pulses == 0) return 0;
 
  // Convert pulses to RPM
  double seconds = elapsed / 1e6;
  double revolutions = pulses / (double)numMagnets;
  double rpm = (revolutions / seconds) * 60.0;
 
  return rpm;
}

// -----------------------------
// CALCULATE TORQUE FROM LOAD
// -----------------------------
double calculateTorque(double loadGrams) {
  // Convert grams to Newtons, then calculate torque
  double forceNewtons = (loadGrams / 1000.0) * 9.81;
  double torque = torqueDistance * forceNewtons;
  return torque;
}

// -----------------------------
// CALCULATE MECHANICAL POWER
// -----------------------------
double calculatePower(double rpm, double torque) {
  // Power (W) = Torque (Nm) × Angular velocity (rad/s)
  double angularVelocity = (rpm * 2.0 * PI) / 60.0;
  double power = torque * angularVelocity;
  return power;
}

// -----------------------------
// CALCULATE VEHICLE SPEED
// -----------------------------
double calculateSpeed(double rpm) {
  // Convert RPM to km/h using wheel circumference
  double metersPerSecond = (rpm * (2.0 * PI * dynoWheelRadius)) / 60.0;
  double kmPerHour = metersPerSecond * 3.6;
  return kmPerHour;
}