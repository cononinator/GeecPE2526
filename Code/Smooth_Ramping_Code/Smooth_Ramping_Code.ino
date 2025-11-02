// VARIABLE INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
const int AcclPin = A1;                            // Accelerator potentiometer on pin A1
const int PWMPin = 11;                             // Motor PWM output on PWM pin 11 (Associated with TCCR2B timer)

const int minAcclSig = 225;                          // Minimum accelerator signal, starting from 0 for full range utilization
const int maxAcclSig = 1023;                       // Maximum accelerator signal, utilizing full range of the potentiometer

const int rampUpTime = 10000;                      // Time (in milliseconds) for full ramp up from 0 to max speed
const float rampUpStep = 255.0 / rampUpTime;       // How much to ramp up PWM value per millisecond

const int rampDownTime = 5000;                     // Time (in milliseconds) for full ramp down from max speed to 0
const float rampDownStep = 255.0 / rampDownTime;   // How much to ramp down PWM value per millisecond

const float filterConst = 0.05;                    // Filter constant for input smoothing

const int minMotorPWM = 0;                         // Minimum motor PWM output, for lowest power/speed
const int maxMotorPWM = 255;                       // Maximum motor PWM output, for highest power/speed

const int SoftStartPin = 2;                        // SoftStartPin is a digital output on pin 2
const int softStartDelay = 200;                    // Delay in milliseconds between Arduino startup and soft start disable

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
}

// MAIN LOOP
//---------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  int acclSig = readAccl(); // Read and process the accelerator signal
  boolean acclApplied = acclSig > 0; // Check if accelerator is applied
  int motorPWM = rampMotorPWM(acclSig, acclApplied); // Smoothly ramp PWM based on accelerator signal
  motorCtrl(motorPWM); // Control the motor with the ramped PWM signal
  Serial.println(String(acclSig) + "," + String(motorPWM)); // Debugging output
  delay(10); // Short delay for stability and serial output readability
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

