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
    # Expected format: DATA,Voltage,Current,Power,Energy,Temperature,DutyCycle,TargetDutyCycle
    try:
        parts = line.strip().split(',')
        if len(parts) >= 8 and parts[0] == "DATA":
            return {
                "Voltage": float(parts[1]),
                "Current": float(parts[2]),
                "Power": float(parts[3]),
                "Energy": float(parts[4]),
                "Temperature": float(parts[5]),
                "MeasuredDutyCycle": float(parts[6]),
                "TargetDutyCycle": float(parts[7])
            }
    except ValueError:
        pass
    return None

def mktimestamp():
    return time.strftime("%Y%m%d-%H%M%S")

def run_sweep(port, baudrate=115200, output_file="duty_sweep_results.csv"):
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud.")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    # Wait for connection to stabilize
    time.sleep(2)
    
    # Clear buffers
    ser.reset_input_buffer()
    
    duty_cycles = generate_duty_cycles()
    results = []
    
    print(f"Starting sweep with {len(duty_cycles)} points.")
    print("-" * 50)
    print(f"{'Target DC (%)':<15} | {'Voltage (V)':<12} | {'Current (A)':<12} | {'Power (W)':<12}")
    print("-" * 50)

    try:
        for target_dc in duty_cycles:
            # Send duty cycle command
            command = f"{target_dc}\n"
            ser.write(command.encode())
            
            # Wait for 5 seconds total
            # We'll collect data continuously, but only use the data from the last 2 seconds for averaging
            start_time = time.time()
            samples = []
            
            while (time.time() - start_time) < 5.0:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    data = parse_sensor_line(line)
                    
                    if data:
                        # Only keep samples from the last 2 seconds
                        if (time.time() - start_time) > 3.0:
                            samples.append(data)
                
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
                
                print(f"{target_dc:<15.1f} | {avg_voltage:<12.4f} | {avg_current:<12.4f} | {avg_power:<12.4f}")
                
                results.append({
                    "SetDutyCycle": target_dc,
                    "MeasuredDutyCycle": avg_measured_dc,
                    "Voltage_V": avg_voltage,
                    "Current_A": avg_current,
                    "Power_W": avg_power,
                    "Energy_J": avg_energy,
                    "Temperature_C": avg_temp,
                    "SamplesCount": len(samples)
                })
            else:
                print(f"Warning: No valid data received for {target_dc}% duty cycle")

    except KeyboardInterrupt:
        print("\nSweep interrupted by user.")
        
    finally:
        # Stop the system
        print("\nStopping system (Setting 0% Duty Cycle)...")
        ser.write(b"S")
        ser.close()
        
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
    parser.add_argument('--port', type=str, required=True, help='COM port (e.g., COM3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--output', type=str, default='duty_sweep_results.csv', help='Base output filename')
    
    args = parser.parse_args()
    
    run_sweep(args.port, args.baud, args.output)
