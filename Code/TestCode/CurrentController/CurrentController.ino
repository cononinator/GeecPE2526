// VARIABLE INITIALIZATION
#include <Arduino.h>
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const int AcclPin = A1;                            // Accelerator potentiometer on pin A1
const int PWMPin = 11;                             // Motor PWM output on PWM pin 11 (Associated with TCCR2B timer)
const int CurrentPin = A0;                         // Current sensor input pin

const int minAcclSig = 225;                          // Minimum accelerator signal, starting from 0 for full range utilization
const int maxAcclSig = 1023;                       // Maximum accelerator signal, utilizing full range of the potentiometer

const int rampUpTime = 10000;                      // Time (in milliseconds) for full ramp up from 0 to max speed
const float rampUpStep = 255.0 / rampUpTime;       // How much to ramp up PWM value per millisecond

const int rampDownTime = 5000;                     // Time (in milliseconds) for full ramp down from max speed to 0
const float rampDownStep = 255.0 / rampDownTime;   // How much to ramp down PWM value per millisecond

const float filterConst = 0.05;                    // Filter constant for input smoothing

const int minMotorPWM = 0;                         // Minimum motor PWM output, for lowest power/speed
const int maxMotorPWM = 253;                       // Maximum motor PWM output, for highest power/speed

const int SoftStartPin = 2;                        // SoftStartPin is a digital output on pin 2
const int softStartDelay = 200;                    // Delay in milliseconds between Arduino startup and soft start disable

float Kp = 20.0f;                                  // Proportional gain for current PID controller
float Ki = 0.0f;                                  // Integral gain for current PID controller
float Kd = 0.0f;                                  // Derivative gain for current PID controller
float target_current = 0.0f;
float max_current = 15.0f;                        // Maximum current limit (15A)
float pwm_accumulator = 0.0f;                      // Accumulator for PWM adjustments from PID controller

// SETUP
//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void setup() {
  TCCR2B = TCCR2B & 0b11111000 | 0x01; // Set PWM frequency for pins 3 and 11 to 31372.55Hz for smoother motor control
  Serial.begin(9600);
  pinMode(AcclPin, INPUT);
  
  pinMode(PWMPin, OUTPUT);
  pinMode(SoftStartPin, OUTPUT);
  delay(softStartDelay);
  digitalWrite(SoftStartPin, HIGH); // Engage soft start

  pinMode(CurrentPin, INPUT); // Current sensor input pin
}

// MAIN LOOP
//---------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  int acclSig = readAccl(); // Read and process the accelerator signal

  // // Practical Accelerator Code
  // if (acclSig > 25 ) {
  //   float target_current = map(acclSig, 0, 255, 0, 15); // Map accelerator signal to target current (0-15A)
  //   int pid_pwm = current_PID_realize(target_current, Kp, Ki, Kd); // Get PWM from PID controller
  //   motorCtrl(pid_pwm); // Control motor with PID output
  //   Serial.print("Accl: ");
  //   Serial.print(acclSig);
  //   Serial.print(", Target Current: ");
  //   Serial.print(target_current, 2);
  //   Serial.print(" A, PID PWM: ");
  //   Serial.println(pid_pwm);
  // } else {
  //   motorCtrl(0); // No accelerator input, stop the motor
  //   Serial.printf("Accl: %d, Motor Stopped\n", acclSig);
  // }

  // Setpoint Accelerator Code (check every 30ms, print every 300ms)
  static unsigned long lastCheckTime = 0;
  static unsigned long lastPrintTime = 0;
  unsigned long now = millis();
  if (now - lastCheckTime >= 30) {
    lastCheckTime = now;
    int pid_pwm = 0;
    if (acclSig > 25 && acclSig < 225) {
      pid_pwm = current_PID_realize(target_current, Kp, Ki, Kd); // Get PWM from PID controller
      motorCtrl(pid_pwm); // Control motor with PID output
      if (now - lastPrintTime >= 300) {
        lastPrintTime = now;
        Serial.println("Accl: " + String(acclSig) + ", Target Current: " + String(target_current, 2) + " A, PID PWM: " + String(pid_pwm));
      }
    } else {
      motorCtrl(0); // No accelerator input, stop the motor
      if (now - lastPrintTime >= 300) {
        lastPrintTime = now;
        Serial.printf("Accl: %d, Motor Stopped\n", acclSig);
      }
    }
  }
  // boolean acclApplied = acclSig > 0; // Check if accelerator is applied
  // int motorPWM = rampMotorPWM(acclSig, acclApplied); // Smoothly ramp PWM based on accelerator signal
  // motorCtrl(motorPWM); // Control the motor with the ramped PWM signal
  // Serial.println(String(acclSig) + "," + String(motorPWM)); // Debugging output


  // Check for serial input and process command
  static String serialBuffer = "";
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    if (inChar == '*') {
      serialBuffer = "";
    }
    serialBuffer += inChar;
    if (inChar == '@') {
      // Parse command
      if (serialBuffer.length() > 3 && serialBuffer.charAt(0) == '*' && serialBuffer.charAt(serialBuffer.length() - 1) == '@') {
        int firstComma = serialBuffer.indexOf(',');
        int secondComma = serialBuffer.indexOf(',', firstComma + 1);
        if (firstComma > 0 && secondComma > firstComma) {
          char cmd = serialBuffer.charAt(1);
          String valueStr = serialBuffer.substring(firstComma + 1, secondComma > 0 ? secondComma : serialBuffer.length() - 1);
          float value = valueStr.toFloat();
          if (cmd == 'P') {  //*P,45.21,@
            Kp = value;
            Serial.print("Kp set to: ");
            Serial.println(Kp);
          }
          if (cmd == 'I') { //*I,1.21,@
            Ki = value;
            Serial.print("Ki set to: ");
            Serial.println(Ki);
          }
          if (cmd == 'D') { //*D,0.01,@ 
            Kd = value;
            Serial.print("Kd set to: ");
            Serial.println(Kd);
          }
          if (cmd == 'C') {
            // Set current target for PID controller
            target_current = value; // Target current in Amperes
            Serial.print("Target Current: ");
            Serial.print(target_current);
            
          }
        }
      }
      // Print Kp, Ki, Kd if command is *S,@
      if (serialBuffer == "*S,@") {
        Serial.println("Kp: " + String(Kp) + ", Ki: " + String(Ki) + ", Kd: " + String(Kd) + ", Target Current: " + String(target_current) + " A");
      }
      // Print help if command is *H,@
      if (serialBuffer == "*H,@") {
        Serial.println("Available Commands:");
        Serial.println("*P,<value>,@ - Set Proportional gain (Kp)");
        Serial.println("*I,<value>,@ - Set Integral gain (Ki)");
        Serial.println("*D,<value>,@ - Set Derivative gain (Kd)");
        Serial.println("*C,<value>,@ - Set Target Current (Amperes)");
        Serial.println("*S,@ - Show current PID parameters");
        Serial.println("*H,@ - Show this help message");
      }
      serialBuffer = "";
    }
  }

  delay(1); // Short delay for stability and serial output readability
}

// FUNCTIONS
//---------------------------------------------------------------------------------------------------------------------------------------------------
int readAccl() {
  static int filterAcclSig;
  int rawAcclSig = analogRead(AcclPin);
  filterAcclSig += (rawAcclSig - filterAcclSig) * filterConst; // Apply simple low-pass filter for noise reduction
  int mappedAcclSig = map(filterAcclSig, minAcclSig, maxAcclSig, 0, 255); // Map signal from full range to PWM range
  return mappedAcclSig;
}

// Takes in the target current and PID coefficients, returns the PWM value to apply to the motor
int current_PID_realize(float target_current, float Kp, float Ki, float Kd) {
  static float current_sum_error = 0;
  static float current_error_last = 0;

  float PID_out;
  float actual_current = 0;
  int SUM_ADC = 0;
  // Read current sensor from A0, average 30 samples for noise reduction
  for (int i = 0; i < 30; i++) {
    SUM_ADC += analogRead(A0);
  }
  float PRE_ADC = SUM_ADC / 30.0f;
  // Convert ADC value to real current 
  actual_current = (PRE_ADC / 1023.0f) * 0.080 * 5.0f; // 0-5V range, 0.080 V/A sensitivity for ACS770-50U, 2^10 - 1 
  float current_error = target_current - actual_current;
  // Limiting closed-loop deadband
  if (abs(current_error / target_current) < 0.1f && abs(current_error) < 0.1f) {
    current_error = 0;
  }
  current_sum_error += current_error;  // error integrate
  // Preventing integral saturation
  if (current_sum_error > 10)
    current_sum_error = 10;
  else if (current_sum_error < -10)
    current_sum_error = -10;
  // PID calculation - outputs correction factor
  PID_out = Kp * current_error + Ki * current_sum_error + Kd * (current_error - current_error_last);
  current_error_last = current_error; // Error propagation
  
  // Normalize PID output to range -10 to +10 (adjust scaling as needed)
  PID_out = constrain(PID_out, -255, 255) / 25.50f;
  
  // Add correction to PWM accumulator
  pwm_accumulator += PID_out; // Scale factor for sensitivity adjustment
  
  // Constrain accumulator to valid PWM range
  pwm_accumulator = constrain(pwm_accumulator, minMotorPWM, maxMotorPWM);
  
  return static_cast<int>(pwm_accumulator);
}

int rampMotorPWM(int acclSig, boolean acclApplied) {
  static float currentPWM = 0;
  static unsigned long prevUpdateTime = millis();
  unsigned long currentTime = millis();
  unsigned long timeDiff = currentTime - prevUpdateTime;
  prevUpdateTime = currentTime;
  
  float targetPWM = map(acclSig, 0, 1023, 0, 255); // Direct mapping for linear response
  float maxChangeUp = rampUpStep * timeDiff;
  float maxChangeDown = rampDownStep * timeDiff;

  // Smoothly adjust currentPWM towards targetPWM within ramp constraints
  if (currentPWM < targetPWM) {
    currentPWM += min(maxChangeUp, targetPWM - currentPWM);
  } else if (currentPWM > targetPWM) {
    currentPWM -= min(maxChangeDown, currentPWM - targetPWM);
  }
  
  currentPWM = constrain(currentPWM, 0, 255); // Ensure PWM stays within bounds
  return map(static_cast<int>(currentPWM), 0, 255, minMotorPWM, maxMotorPWM); // Scale to motor's range
}

void motorCtrl(int motorPWM) {
  analogWrite(PWMPin, motorPWM); // Write the PWM signal to the motor control pin
}

