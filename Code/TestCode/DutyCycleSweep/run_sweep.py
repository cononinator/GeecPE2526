import serial
import time
import pandas as pd
import argparse
import sys

def generate_duty_cycles():
    duty_cycles = []
    
    # 0-15% with 1% resolution
    for i in range(0, 16):
        duty_cycles.append(float(i))
        
    # 15-85% with 5% resolution
    # Start from 20 because 15 is already included
    for i in range(20, 90, 5):
        duty_cycles.append(float(i))
        
    # 85-99.8% with 1% resolution
    # Start from 86 because 85 is already included
    for i in range(86, 100):
        duty_cycles.append(float(i))
        
    # Final point at 99.8% (treated as 100%)
    duty_cycles.append(99.8)
    
    return duty_cycles

def parse_sensor_line(line):
    # Expected format: DATA,Voltage,Current,Power,Energy,Temperature,MotorCurrent,DutyCycle,TargetDutyCycle
    try:
        parts = line.strip().split(',')
        if len(parts) >= 9 and parts[0] == "DATA":
            return {
                "Voltage": float(parts[1]),
                "Current": float(parts[2]),
                "Power": float(parts[3]),
                "Energy": float(parts[4]),
                "Temperature": float(parts[5]),
                "MotorCurrent": float(parts[6]),
                "MeasuredDutyCycle": float(parts[7]),
                "TargetDutyCycle": float(parts[8])
            }
    except ValueError:
        pass
    return None

def parse_dyno_line(line):
    # Expected format: $<timestamp_ms> <dynoRPM> <carRPM> <torque_Nm> <power_W>;
    try:
        line = line.strip()
        if line.startswith("$") and line.endswith(";"):
            parts = line[1:-1].split()
            if len(parts) == 5:
                return {
                    "Dyno_Timestamp_ms": float(parts[0]),
                    "Dyno_RPM": float(parts[1]),
                    "Car_RPM": float(parts[2]),
                    "Dyno_Torque_Nm": float(parts[3]),
                    "Dyno_Power_W": float(parts[4]),
                }
    except ValueError:
        pass
    return None

def mktimestamp():
    return time.strftime("%Y%m%d-%H%M%S")




def run_sweep(port, baudrate=115200, output_file="duty_sweep_results.csv", dyno_port=None, dyno_baudrate=9600):
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud.")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    # Open dyno serial port if provided
    dyno_ser = None
    if dyno_port:
        try:
            dyno_ser = serial.Serial(dyno_port, dyno_baudrate, timeout=0)
            print(f"Dyno connected to {dyno_port} at {dyno_baudrate} baud.")
        except serial.SerialException as e:
            print(f"Error opening dyno serial port: {e}")
            sys.exit(1)

    # Wait for connection to stabilize
    time.sleep(2)
    
    # Clear buffers
    ser.reset_input_buffer()
    if dyno_ser:
        dyno_ser.reset_input_buffer()
    
    duty_cycles = generate_duty_cycles()
    results = []
    
    print(f"Starting sweep with {len(duty_cycles)} points.")
    print("-" * 50)
    print(f"{'Target DC (%)':<15} | {'Voltage (V)':<12} | {'Current (A)':<12} | {'Power (W)':<12}")
    print("-" * 50)

    try:
        for target_dc in duty_cycles:
            # Send duty cycle command then reset energy register
            ser.write(f"{target_dc}\n".encode())
            time.sleep(0.05)   # brief pause so Arduino processes the DC command first
            ser.write(b"R")    # reset INA780 energy register

            # Wait for 5 seconds total
            # We'll collect data continuously, but only use the data from the last 2 seconds for averaging
            start_time = time.time()
            samples = []
            dyno_samples = []
            
            while (time.time() - start_time) < 5.0:
                elapsed = time.time() - start_time
                in_averaging_window = elapsed > 2.0

                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    data = parse_sensor_line(line)
                    if data and in_averaging_window:
                        samples.append(data)

                if dyno_ser and dyno_ser.in_waiting:
                    line = dyno_ser.readline().decode('utf-8', errors='ignore').strip()
                    data = parse_dyno_line(line)
                    if data and in_averaging_window:
                        dyno_samples.append(data)
                
                time.sleep(0.01) # Small sleep to prevent tight loop
            
            # Process samples for this step
            if samples:
                # Calculate averages
                avg_voltage = sum(d['Voltage'] for d in samples) / len(samples)
                avg_current = sum(d['Current'] for d in samples) / len(samples)
                avg_power = sum(d['Power'] for d in samples) / len(samples)
                avg_energy = samples[-1]['Energy'] # Energy is cumulative, take the last one
                avg_temp = sum(d['Temperature'] for d in samples) / len(samples)
                avg_measured_dc = sum(d['MeasuredDutyCycle'] for d in samples) / len(samples)
                avg_motor_current = sum(d['MotorCurrent'] for d in samples) / len(samples)
                
                print(f"{target_dc:<15.1f} | {avg_voltage:<12.4f} | {avg_current:<12.4f} | {avg_power:<12.4f}")
                
                row = {
                    "SetDutyCycle": target_dc,
                    "MeasuredDutyCycle": avg_measured_dc,
                    "Voltage_V": avg_voltage,
                    "Current_A": avg_current,
                    "Power_W": avg_power,
                    "Energy_J": avg_energy,
                    "Temperature_C": avg_temp,
                    "MotorCurrent_A": avg_motor_current,
                    "SamplesCount": len(samples),
                }

                # Merge averaged dyno data if available
                if dyno_samples:
                    row["Dyno_RPM"] = sum(d['Dyno_RPM'] for d in dyno_samples) / len(dyno_samples)
                    row["Car_RPM"] = sum(d['Car_RPM'] for d in dyno_samples) / len(dyno_samples)
                    row["Dyno_Torque_Nm"] = sum(d['Dyno_Torque_Nm'] for d in dyno_samples) / len(dyno_samples)
                    row["Dyno_Power_W"] = sum(d['Dyno_Power_W'] for d in dyno_samples) / len(dyno_samples)
                    row["DynoSamplesCount"] = len(dyno_samples)
                elif dyno_ser:
                    print(f"  Warning: No dyno data received for {target_dc}% duty cycle")
                    row["Dyno_RPM"] = None
                    row["Car_RPM"] = None
                    row["Dyno_Torque_Nm"] = None
                    row["Dyno_Power_W"] = None
                    row["DynoSamplesCount"] = 0

                results.append(row)
            else:
                print(f"Warning: No valid data received for {target_dc}% duty cycle")

    except KeyboardInterrupt:
        print("\nSweep interrupted by user.")
        
    finally:
        # Stop the system
        print("\nStopping system (Setting 0% Duty Cycle)...")
        ser.write(b"S")
        ser.close()

        # Close dyno port if open
        if dyno_ser and dyno_ser.is_open:
            dyno_ser.close()
            print("Dyno port closed.")
        
        # Save results
        if results:
            df = pd.DataFrame(results)
            # Add timestamp to filename
            filename = f"{output_file.split('.')[0]}_{mktimestamp()}.csv"
            df.to_csv(filename, index=False)
            print(f"\nResults saved to {filename}")
        else:
            print("\nNo results collected.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Duty Cycle Sweep Test')
    parser.add_argument('--port', type=str, required=True, help='COM port for the sweep/sensor Arduino (e.g., COM3)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate for sweep Arduino')
    parser.add_argument('--output', type=str, default='duty_sweep_results.csv', help='Base output filename')
    parser.add_argument('--dyno-port', type=str, default=None, help='COM port for the dyno Arduino (e.g., COM4). Optional.')
    parser.add_argument('--dyno-baud', type=int, default=9600, help='Baud rate for dyno Arduino (default: 9600)')
    
    args = parser.parse_args()
    
    run_sweep(args.port, args.baud, args.output, args.dyno_port, args.dyno_baud)
