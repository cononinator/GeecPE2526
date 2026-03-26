#!/usr/bin/env python3
"""
run_interactive.py — Interactive terminal for ClosedLoopTest firmware.

A background thread continuously reads sensor lines from the ESP32 and
prints them in a formatted table.  The main thread reads stdin and
forwards typed commands directly to the serial port.

Usage:
    python run_interactive.py --port COM3
    python run_interactive.py --port /dev/ttyUSB0 --baud 115200

Commands (sent directly to firmware):
    M          Enable serial control (overrides throttle input)
    T          Return to throttle control (default on power-on)
    0-100      Set target duty cycle (%) [serial mode only]
    S          Stop (ramp to 0%)
    R          Reset power meter
    C <amps>   Set current limit (e.g.  C 15.5)
    H          Firmware help text
    q / quit   Exit this script (sends S first)
"""

import argparse
import sys
import threading
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run:  pip install pyserial")
    sys.exit(1)


# ─── ANSI colour helpers ──────────────────────────────────────────────────────
def _c(code: str, text: str) -> str:
    """Wrap text in an ANSI colour code (no-op on Windows if colorama absent)."""
    return f"\033[{code}m{text}\033[0m"

RED    = lambda t: _c("31", t)
YELLOW = lambda t: _c("33", t)
CYAN   = lambda t: _c("36", t)
GREEN  = lambda t: _c("32", t)
DIM    = lambda t: _c("2",  t)


# ─── Serial reader thread ─────────────────────────────────────────────────────
class SerialReader(threading.Thread):
    """Reads lines from the serial port and prints them to stdout."""

    def __init__(self, port: serial.Serial):
        super().__init__(daemon=True)
        self._port = port
        self._stop_event = threading.Event()

    def stop(self):
        self._stop_event.set()

    def run(self):
        while not self._stop_event.is_set():
            try:
                if self._port.in_waiting:
                    raw = self._port.readline()
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    self._handle_line(line)
                else:
                    time.sleep(0.005)
            except serial.SerialException:
                break
            except Exception:
                pass

    def _handle_line(self, line: str):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]

        # Highlight important lines
        if "[!]" in line or "FAULT" in line or "WARNING" in line:
            print(f"  {DIM(ts)}  {RED(line)}")
        elif "WATCHDOG" in line:
            print(f"  {DIM(ts)}  {YELLOW(line)}")
        elif "===== Sensor" in line:
            print(f"\n  {DIM(ts)}  {CYAN(line)}")
        elif "===========================" in line:
            print(f"  {DIM(ts)}  {CYAN(line)}\n", end="")
        elif "Current limit" in line and "DAC" in line:
            print(f"  {DIM(ts)}  {GREEN(line)}")
        elif "Serial control enabled" in line or "Throttle control enabled" in line:
            print(f"  {DIM(ts)}  {GREEN(line)}")
        elif line.strip():
            print(f"  {DIM(ts)}  {line}")


# ─── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Interactive terminal for ClosedLoopTest firmware."
    )
    parser.add_argument("--port",  required=True, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud",  type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    print(f"Connecting to {args.port} at {args.baud} baud…")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"ERROR: {e}")
        sys.exit(1)

    time.sleep(2)  # Allow ESP32 to boot / reset after DTR assertion
    print(f"Connected.  Type commands, or 'q' to quit.\n")

    reader = SerialReader(ser)
    reader.start()

    try:
        while True:
            try:
                cmd = input()
            except EOFError:
                break

            stripped = cmd.strip()
            if not stripped:
                continue

            if stripped.lower() in ("q", "quit", "exit"):
                print("Sending stop command…")
                ser.write(b"S\n")
                time.sleep(0.5)
                break

            # Forward command to firmware
            ser.write((stripped + "\n").encode("utf-8"))

    except KeyboardInterrupt:
        print("\nInterrupted — sending stop command…")
        try:
            ser.write(b"S\n")
            time.sleep(0.5)
        except Exception:
            pass

    finally:
        reader.stop()
        ser.close()
        print("Port closed.")


if __name__ == "__main__":
    # Enable ANSI escape codes on Windows
    if sys.platform == "win32":
        import os
        os.system("")  # Activates VT100 processing in Windows console

    main()
