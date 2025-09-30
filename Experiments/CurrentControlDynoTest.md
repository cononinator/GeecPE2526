# Test Procedure for Current Control Verification:

## Experimental Steps

1. Set up the car according to the dyno configuration.
2. Connect the oscilloscope or picoscope to the output current sensor (after the low-pass filter).
3. Operate the car with the original firmware and no load (elevated). Observe and verify the current output on the oscilloscope.
4. Run the car on the dyno with 3 Nm torque applied to the rear wheel. Confirm current output on the oscilloscope.
5. Replace the Arduino with one running the current control firmware using these parameters:
    - Update Frequency: 30 ms (100 × f_pwm)
    - K_p: 20
    - K_i: 5 (consider testing with 0 for comparison)
    - K_d: 0
    - target_current: 1 A
6. Run the car with no load and observe current rise.
7. Set target current to 3 A and run on dyno with 3 Nm torque.
8. Record the torque response curve.
9. Repeat the experiment, adjusting K_p to achieve a response rate of approximately 300–500 ms.
10. Adjust K_i to improve accuracy.
11. Add the differential term (K_d) if necessary.
12. Vary torque and current setpoints to generate response curves. Capture screenshots from picoscope.

## Message Protocol
Messages follow the format:
`*I,40@` - Where `*` indicates the start `@` indicates the end, `I` is the identifier, `40` is the value to be assigned for that value.
### Commands:
**Gains:**
 - `*P,45.21,@`: Sets ``Kp`` proportional gain
 - `*I,1.21,@`: Sets `Ki` integral gain
 - ``*D,0.01,@ ``: Sets `Kd` derivative gain

**Setpoint**
 - `*C,5.0,@ `: Sets `target_current`

**Print:**
- `"*S,@"`: Prints out current gains and setpoint current
- `*H,@`: Prints a help message with the following:
    ```
    Available Commands:
    *P,<value>,@ - Set Proportional gain (Kp)
    *I,<value>,@ - Set Integral gain (Ki)
    *D,<value>,@ - Set Derivative gain (Kd)
    *C,<value>,@ - Set Target Current (Amperes)
    *S,@ - Show current PID parameters
    *H,@ - Show this help message 
    ```

## Results Table

| Constant | Final Value |
|----------|-------------|
| K_p      |             |
| K_i      |             |
| K_d      |             |