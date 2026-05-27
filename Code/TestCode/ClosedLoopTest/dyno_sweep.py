#!/usr/bin/env python3
"""
dyno_sweep.py — Closed-loop dyno sweep test

Accelerates vehicle to maximum speed using a current limit, then coasts down.
When dyno speed drops below minimum speed, the cycle repeats. Runs 5 repeats
per parameter set, then prompts for new parameters.

Usage:
    python dyno_sweep.py --port COM3
    python dyno_sweep.py --port COM3 --dyno-port COM4
"""

import serial
import time
import pandas as pd
import argparse
import sys


def parse_sensor_line(line):
    """Parse sensor data line from firmware.
    Expected format: DATA,Voltage,Current,Power,Energy,Temperature,MotorCurrent,MeasuredDutyCycle,TargetDutyCycle
    """
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
    """Parse dyno data line.
    Expected format: $<timestamp_ms> <dyno_rpm> <car_rpm> <torque_Nm> <power_W>;
    Converts RPM to km/h using wheel radius 235mm (from Dyno.ino carWheelDiameter=470mm).
    """
    try:
        line = line.strip()
        if line.startswith("$") and line.endswith(";"):
            parts = line[1:-1].split()
            if len(parts) == 5:
                # Convert RPM to km/h: speed_kmh = rpm * (2*PI*radius) / 60 * 3.6
                # With radius=0.235m: speed_kmh = rpm * 0.0886
                rpm_to_kmh = 0.0886
                dyno_rpm = float(parts[1])
                car_rpm = float(parts[2])
                return {
                    "Dyno_Timestamp_ms": float(parts[0]),
                    "Dyno_Speed_kmh":    dyno_rpm * rpm_to_kmh,
                    "Car_Speed_kmh":     car_rpm * rpm_to_kmh,
                    "Dyno_Torque_Nm":    float(parts[3]),
                    "Dyno_Power_W":      float(parts[4]),
                }
    except ValueError:
        pass
    return None


def mktimestamp():
    return time.strftime("%Y%m%d-%H%M%S")


def prompt_parameters():
    """Prompt user for current limit, min speed, and max speed."""
    while True:
        try:
            print("\n" + "=" * 60)
            print("DYNO SWEEP TEST PARAMETERS")
            print("=" * 60)

            current_limit = float(input("Enter current limit (A): "))
            if current_limit <= 0:
                print("Error: Current limit must be positive.")
                continue

            min_speed = float(input("Enter minimum speed (km/h) [default: 10]: ") or "10")
            if min_speed < 0:
                print("Error: Minimum speed cannot be negative.")
                continue

            max_speed = float(input("Enter maximum speed (km/h) [default: 20]: ") or "20")
            if max_speed <= min_speed:
                print("Error: Maximum speed must be greater than minimum speed.")
                continue

            # Confirm
            print("\nParameters:")
            print(f"  Current Limit: {current_limit:.1f} A")
            print(f"  Min Speed:     {min_speed:.1f} km/h")
            print(f"  Max Speed:     {max_speed:.1f} km/h")
            proceed = input("Proceed? (y/n): ").strip().lower()

            if proceed == 'y':
                return current_limit, min_speed, max_speed

        except ValueError:
            print("Error: Please enter numeric values.")


def run_dyno_sweep(port, baudrate=115200, output_file="dyno_sweep_results.csv",
                   dyno_port=None, dyno_baudrate=9600):
    """Run the closed-loop dyno sweep test."""

    # Open main serial port
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud.")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    # Open dyno serial port (if provided)
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
    ser.reset_input_buffer()
    if dyno_ser:
        dyno_ser.reset_input_buffer()

    all_results = []
    cycle_count = 0

    try:
        while True:
            cycle_count += 1
            current_limit, min_speed, max_speed = prompt_parameters()

            print(f"\n[Cycle {cycle_count}] Starting 5 repeats...")
            print("-" * 60)
            print(f"{'Repeat':<10} | {'Phase':<12} | {'Max Speed':<12} | {'Avg Power':<12}")
            print("-" * 60)

            # Set current limit
            ser.write(f"C {current_limit}\n".encode())
            time.sleep(0.2)

            cycle_samples = []
            dyno_buf = ""  # Buffer for partial dyno lines (timeout=0)
            test_start_time = time.time()  # Track test start time for entire cycle

            for repeat in range(5):
                # Reset power meter energy at start of repeat
                ser.write(b"R\n")
                time.sleep(0.1)

                # Phase 1: Accelerate
                ser.write(b"100\n")  # Full duty cycle
                accel_start = time.time()
                accel_samples = []
                accel_dyno = []
                max_speed_reached = False

                while (time.time() - accel_start) < 60.0:  # Max 60 seconds
                    if ser.in_waiting:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        data = parse_sensor_line(line)
                        if data:
                            data["Phase"] = "Accelerating"
                            data["Repeat"] = repeat + 1
                            data["CurrentLimit_A"] = current_limit
                            data["Timestamp_s"] = time.time() - test_start_time
                            accel_samples.append(data)

                    # Read dyno data with buffering
                    if dyno_ser and dyno_ser.in_waiting:
                        dyno_buf += dyno_ser.read(dyno_ser.in_waiting).decode('utf-8', errors='ignore')
                        while '\n' in dyno_buf:
                            line, dyno_buf = dyno_buf.split('\n', 1)
                            data = parse_dyno_line(line.strip())
                            if data:
                                data["Timestamp_s"] = time.time() - test_start_time
                                accel_dyno.append(data)
                                if data["Car_Speed_kmh"] >= max_speed and not max_speed_reached:
                                    max_speed_reached = True
                                    ser.write(b"0\n")  # Stop motor immediately

                    if max_speed_reached:
                        break

                    time.sleep(0.01)

                # Phase 2: Coast down
                ser.write(b"S\n")  # Stop
                coast_start = time.time()
                coast_samples = []
                coast_dyno = []
                current_speed = None

                while (time.time() - coast_start) < 120.0:  # Max 120 seconds
                    if ser.in_waiting:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        data = parse_sensor_line(line)
                        if data:
                            data["Phase"] = "Coasting"
                            data["Repeat"] = repeat + 1
                            data["CurrentLimit_A"] = current_limit
                            data["Timestamp_s"] = time.time() - test_start_time
                            coast_samples.append(data)

                    # Read dyno data with buffering
                    if dyno_ser and dyno_ser.in_waiting:
                        dyno_buf += dyno_ser.read(dyno_ser.in_waiting).decode('utf-8', errors='ignore')
                        while '\n' in dyno_buf:
                            line, dyno_buf = dyno_buf.split('\n', 1)
                            data = parse_dyno_line(line.strip())
                            if data:
                                current_speed = data["Car_Speed_kmh"]
                                data["Timestamp_s"] = time.time() - test_start_time
                                coast_dyno.append(data)

                            # Exit when speed drops below minimum
                            if current_speed is not None and current_speed < min_speed:
                                break

                    if current_speed is not None and current_speed < min_speed:
                        break

                    time.sleep(0.01)

                # Collect all samples for this repeat
                repeat_samples = accel_samples + coast_samples + accel_dyno + coast_dyno
                cycle_samples.extend(repeat_samples)

                # Summary for this repeat
                if accel_samples:
                    avg_power = sum(d['Power'] for d in accel_samples) / len(accel_samples)
                    max_accel_speed = max([d.get("Car_Speed_kmh", 0) for d in accel_dyno]) if accel_dyno else 0
                    print(f"{repeat + 1:<10} | {'Accel+Coast':<12} | {max_accel_speed:<12.1f} | {avg_power:<12.1f}")

            # Save cycle results
            if cycle_samples:
                df = pd.DataFrame(cycle_samples)
                filename = f"{output_file.split('.')[0]}_{mktimestamp()}_cycle{cycle_count}.csv"
                df.to_csv(filename, index=False)
                print(f"\nCycle {cycle_count} saved to {filename}")
                all_results.append(df)

            # Ask to continue
            print("\n" + "-" * 60)
            again = input("Run another cycle with different parameters? (y/n): ").strip().lower()
            if again != 'y':
                break

        # Save combined results
        if all_results:
            combined_df = pd.concat(all_results, ignore_index=True)
            combined_filename = f"{output_file.split('.')[0]}_{mktimestamp()}_all_cycles.csv"
            combined_df.to_csv(combined_filename, index=False)
            print(f"\nAll cycles saved to {combined_filename}")

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user.")

    finally:
        # Stop the system
        print("\nStopping system...")
        ser.write(b"S\n")
        time.sleep(0.5)
        ser.close()

        if dyno_ser and dyno_ser.is_open:
            dyno_ser.close()
            print("Dyno port closed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Dyno Sweep Closed-Loop Test')
    parser.add_argument('--port', type=str, required=True, help='COM port for firmware (e.g., COM3)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate for firmware')
    parser.add_argument('--output', type=str, default='dyno_sweep_results.csv', help='Base output filename')
    parser.add_argument('--dyno-port', type=str, default=None, help='COM port for dyno (optional)')
    parser.add_argument('--dyno-baud', type=int, default=9600, help='Baud rate for dyno')

    args = parser.parse_args()

    run_dyno_sweep(args.port, args.baud, args.output, args.dyno_port, args.dyno_baud)
