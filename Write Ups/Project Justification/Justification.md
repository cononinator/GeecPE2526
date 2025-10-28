# Justification for the new Design of Power Electronics

This document is a summary of reasons why the power electronics should be redesigned and a comparison with a new power electronics board
It is broken down into the following sections:
1. Closed Loop Control
   - A closed loop controller would require an accurate current sensor and either a speed sensor or an analogue for speed sensing
   - The current sensor on the power electronics is low bandwidth and placed at the battery terminal, resulting in a phase difference between the motor current and battery current due to the large Capacitors
2. Simplify the high current path through the circuit
   - A small PCB with only the required components, and a common IO platform that a DAQ/Power Controller can be designed around
3. Switching Losses
   - GaN FETs have smaller gates losses compared with either the originally designed MOSFETs or the current FETs
4. Modularity and common supply voltage
   - All additional devices and board are supplied directly from the battery along the main current path. This cause the major issue that the high current loop goes through each section individually

## Summary of Issues and Proposed Fixes

1. The Opto Isolators on the Power Electronics were designed with now obsolete ICs. The current fix is different opto isolators (not reccomended for new devices) that have been modified with two layers of DIP holders to change the pinout.
   **Proposal: Investigate the need for fully isolated grounding vs single connection AGND and DGND**
2. Closed Loop Control Capabilities. This requires at minimum, accurate current sensing for the motor. 
   **Proposal: AD8411A or INA241 Current sense amplifiers capable of 1-2 MHz bandwidth, this could then be controlled in hardware or software depending on timing requirements (PID, Peak Current, Current Limiting). The Arduino Nano PI Connect includes a built in IMU which could be used for speed verification alongside GPS and or wheelspeed sensor.**
3. Supply Voltages and Main Battery path. The battery is connected directly though most components in the system before reaching the motor, additionally it is directly connected to the steering wheel display. This means all boards need to deal directly with instability in the main battery. 
   **Proposal: Create a primary transmission bus voltage on the power electronics board. 12V 1A which can be stably transmitted across the system without worrying about battery instability.**
4. Modularity and simplification, the DAQ and the power electronics boards are large boards with many functions but are difficult to modify without respinning the device. There are a large number of connectors in series with the high current path.
   **Proposal: Create dedicated boards with limited functionality, that can allow for more versatility overall. Removal of DAQ and repositioning of WAGOs/replacing to keep connections in the high current path minimal.**
5. Power FETs, the original power FETs that Oisin Anderson designed the board around have decent specifications but high switching losses. David Kong replaced them with new FETs in 2023 that have very good specifications but are only rated for 30V. The use of TVS diodes can mitigate this and can clamp the voltage to the specification but the proximity to the rated voltage is cause for concern.
   **Proposal: Investigate new generations of power FETs including GaN FETs to find similar efficiencies but with voltage ratings of at least 40V ideally 60V+.**
6. Device Idle, Based on data from Poland the car was idling for 40% of the race, This works out at about 6000J (2.7%) over the course of the race
   **Proposal: Integrate sleep mode to the electrical system which can sleep while the throttle isn't active or above a certain state. To reduce idle currents.**
7. Power Monitoring, The DAQ currently monitors voltage and current independently and the trapezoidal rule is used to calculate power consumption, this provides results very close to the SEM result (1~2% accurate). This requires about 15 discrete components to make work.
   **Propsal: INA780B is an integrated joulemeter with temperature sensing, it requires I2C communication and can monitor bus voltage, current, power etc. with automatic averaging to impreove accuracy of the built in 16-bit ADC.**

Below is more detailed investigations into the critical sections, with further justifications.

## Closed Loop Control
![](Fixed.drawio.png)
Above is a diagram showing the control methodolgy for the current power electronics, it is an open loop controller i.e. no feedback.
The Linear mapping update frequency and max change in duty cycle can be modified to a more conservative requirements, which will should prevent massive current spikes. 
Additionally the linear mapping could be changed to a logarthmic mapping to allow for finer control at higher duty cycles. 
![alt text](CurrentControl.drawio.png)

The requirements for a control system described above are:
1. Accurate Motor current
2. Reliable Motor Speed value

The 2 requirements are present on the Geec at the moment however they exist on the DAQ without an easy method of communicating between the two boards. Communicating with the DAQ also adds latency issues which would need to be controlled for. 
There is bandwidth issues too, the 2 current sensors (ACS712, ACS723) in the Geec have bandwidths of 80kHz. Shunt based sensors can have much higher bandwidths e.g. INA241 with a bandwidth of 1.1MHz. This can allow much better knowledge of current transients and a better understanding of the vehicle.
Below is a curve showing the Battery current and the motor current as reported by the DAQ current sensor
![alt text](image.png)
A new Power Electronics board could contain this shunt sensor to monitor the output current accurately.

## High Current Path

![alr text](Electrical_Diagram.drawio.png)

The diagram above details the main electrical current path which every amp must travel to reach the motor. 
Here are some fixes which would improve the series resistance and the total return path length of the system:
1. Swap the horn and main electrical relays. This would mean only pair set of Wago's would need to be connected through lowering some contact resistance.
2. Removal of the DAQ would not only remove 2 XT90's but also the series resistance from the DAQ.
3. Removal of the XT90's from the circuit after the Joulemeter. 

![alt text](image-1.png)
Above is a press fit M4 connector rated for up to 130A, this would be combined with a ring crimp connector to achieve very low resistances.


## Switching Losses

I'm in the process of simulating the switching losses present in the current circuit, however I'm looking at GaN power FETs with integrated drivers and very low gate charges and moderately low Rdson.

The likely result is they will have at worst the equivalent characteristics of the power MOSFETs on the Power Electronics right now, except they will be rated for 60-100V instead of 30V. 
The current FETs likely have higher breakdown voltages than the datasheet states and there are TVS diodes which if rated for 30V will suppress any transients voltages greater than 30V, but I'm personally not comfortable with the gamble. 

## Modularity and Supply Voltages
An issue related to the one described in section 2 is that all devices currently use the main battery voltage as the main supply voltage. This creates issues with stability and noise propogation. A single step down supply, that does, would make design:
Vbat -> 12V
The supply wouldn't need to be massive either: 0.6-1A would be able to output 7-12W and well designed converters have minimum 90% efficiencies. All other boards would then require a 12V->5V or 12V->3.3V converter, which can be readily found with efficiencies from 75% up to 95% efficient. 

![alt text](image-2.png)

The modularity would be achieved with these connectors:

![alt text](image-3.png)

Which have a pair as shown:
![alt text](image-4.png)

The above connectors can easily be connected and disconnected but have very high gripping forces. A 12 pin connector could have the following pins:
1. 12V
2. GND
3. SDA (power meter)
4. SCL (power meter)
5. PWMH
6. PWML
7. Imot (analog)
8. Vmot (analog)
9. Sleep
10. 3.3V
11. GND
12. SPARE (5V?)

This allows for a number of options, 
1. The placement of the DAQ could be in series with the power controller board, reporting data from the device, before transmission to the power controller.
2. The method of PWM control could be altered with a board change:
   1. Software PID control
   2. Hardware peak current control, using discrete logic gates and comparators.
   3. Addition of DAQ sensors (GPS, SD card, CAN device, Speed Sensor) and integration into control
   4. More advanced control schemes or more basic controls.
3. Better design considerations. 
   1. The power electronics board has many requirements, high current paths, potentially noisy grounds, high di/dt and dv/dt nodes, heatsink requirements, etc. This could be designed with a tight 4 layer board with solid ground and power planes to allow for parrallel connections with small power loops.
   2. The power controller boards have different requirements, mainly high speed digital signals and some analog signal measurement. This could be designed with a 2 layer board and large amounts of IO outputs to keep the devices versatile


## Diagram of new power board design
![alt text](Circuit_Design.drawio.png)
| Device  | Purpose   |
|---|---|
|AD8411A   |  2.7 MHz bandwidth current sensor |
| WSLP3921L5000FEA | 500uOhm Shunt resistor  |
|EPC23101   | High side FET and integrated gate driver  |
| EPC2361 | Low side FET |
|INA780   | 500uOhm I2C joulemeter with integrated temp sensor  |
|Arduino Nano ESP32 (RP2040?)  | Microcontroller   |
| TPSM33615| Vbat > 12V 1.5A Buck Module|
|  TPS62933 | 12 > 3.3V Buck Module  |
|  Adafruit Ultimate GPS | 10 Hz GPS Module  |
|  WR-TBL Series 322  | Green Connectors above  |
|  WR-TBL Series 3093 | Green Connectors above  |
|   Adafruit CAN Pal | CAN module requires a uC with a CAN peripheral|
| 7461096 | REDCUBE PRESS-FIT|

