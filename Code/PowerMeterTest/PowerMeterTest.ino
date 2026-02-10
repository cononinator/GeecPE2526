#include <Wire.h>
#include "INA780.h"

INA780 powerMonitor(0x40); 

void setup() {
    Serial.begin(9600);
    Wire.begin(A5, A4);
    // Wire.setWireTimeout(3000, true);
    
    if (powerMonitor.isConnected()) {
        Serial.println("INA780 found and connected!");
    } else {
        Serial.println("INA780 not found!");
    }
  powerMonitor.configureADC(INA780::AVG_64, INA780::CONV_1052us, INA780::CONV_1052us);
}

void loop() {
    // --- New Reset Logic ---
    if (Serial.available() > 0) {
        char incomingByte = Serial.read();
        if (incomingByte == 'R' || incomingByte == 'r') {
            powerMonitor.reset();
            Serial.println("!!! Device Reset Triggered via Serial !!!");
            powerMonitor.configureADC(INA780::AVG_64, INA780::CONV_1052us, INA780::CONV_1052us);
            delay(100); // Short delay to allow the chip to reboot
        }
    }
    // -----------------------

    if (powerMonitor.isConnected()) {
        float busVoltage = powerMonitor.getBusVoltage();
        float current = powerMonitor.getCurrent();
        double power = powerMonitor.getPower();
        double energy = powerMonitor.getEnergy();
        float temperature = powerMonitor.getTemperature();
        double charge = powerMonitor.getCharge();
        uint16_t diagAlert = powerMonitor.getDiagAlert();

        Serial.print("Bus Voltage: ");
        Serial.print(busVoltage);
        Serial.println(" V");
        Serial.print("Current: ");
        Serial.print(current, 4);
        Serial.println(" A");
        Serial.print("Power: ");
        Serial.print(power, 4);
        Serial.println(" W");
        Serial.print("Energy: ");
        Serial.print(energy, 4);
        Serial.println(" J");
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");
        Serial.print("Charge: ");
        Serial.print(charge);
        Serial.println(" As");
        Serial.print("Diag Alert: ");
        Serial.println(diagAlert, BIN);
        Serial.println("--------------------");
    } else {
        Serial.println("INA780 not found!");
    }

    delay(2000);
}