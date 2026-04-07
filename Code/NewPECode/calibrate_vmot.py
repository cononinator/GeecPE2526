#!/usr/bin/env python3
"""
calibrate_vmot.py - Motor voltage sensor (VmotLP) calibration for NewPECode.

Sends PWM duty cycle commands to the ESP32, then at each operating point
compares three voltage sources:
  V_calc    = avg(V_bus) x duty/100   [theoretical, from INA780 bus voltage]
  V_dmm     = multimeter reading      [optional, overrides V_calc as truth]
  V_reported = firmware motorVoltage  [using current SLOPE/OFFSET constants]

Calibration fit:
  Vadc = new_slope x Vmot_true + new_offset
  -> motorVoltage = (motorVoltageADC - new_offset) / new_slope

Usage:
    python calibrate_vmot.py --port /dev/ttyACM0
    python calibrate_vmot.py --port /dev/ttyACM0 --duties 0 10 20 40 50 60 70 80 90
    python calibrate_vmot.py --port /dev/ttyACM0 --auto --duties 10 20 30 40 50
"""

import argparse
import sys
import threading
import time
from collections import deque

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run:  pip install pyserial")
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy not installed.  Run:  pip install numpy")
    sys.exit(1)

# ---- Existing firmware constants (SensorTask.ino) ----------------------------
# Used to back-calculate Vadc from the reported motorVoltage.
CURRENT_SLOPE  = 0.054951   # V_adc per V_motor
CURRENT_OFFSET = 0.454319   # V

# DATA CSV absolute column indices after split (parts[0] == "DATA")
COL_BUS_VOLTAGE   = 1   # INA780 bus voltage
COL_MOTOR_VOLTAGE = 6   # sensMotorVoltage (calibrated)
COL_DUTY_CYCLE    = 8   # currentDutyCycle


# ---- ANSI colour helpers ------------------------------------------------------
def _c(code, text):
    return f"\033[{code}m{text}\033[0m"


def RED(t):    return _c("31", t)
def YELLOW(t): return _c("33", t)
def CYAN(t):   return _c("36", t)
def GREEN(t):  return _c("32", t)
def BOLD(t):   return _c("1",  t)
def DIM(t):    return _c("2",  t)


# ---- Serial reader ------------------------------------------------------------
class SerialReader(threading.Thread):
    """Background thread: reads DATA CSV lines, maintains rolling buffers."""

    def __init__(self, port, window=100):
        super().__init__(daemon=True)
        self._port     = port
        self._stop     = threading.Event()
        self._lock     = threading.Lock()
        self._buf_vbus = deque(maxlen=window)
        self._buf_vrep = deque(maxlen=window)
        self._buf_vadc = deque(maxlen=window)
        self._buf_duty = deque(maxlen=window)

    def stop(self):
        self._stop.set()

    def clear(self):
        with self._lock:
            self._buf_vbus.clear()
            self._buf_vrep.clear()
            self._buf_vadc.clear()
            self._buf_duty.clear()

    def snapshot(self):
        """Return dict of means/stds/n, or None if empty."""
        with self._lock:
            vbus = list(self._buf_vbus)
            vrep = list(self._buf_vrep)
            vadc = list(self._buf_vadc)
            duty = list(self._buf_duty)
        if not vadc:
            return None

        def ms(arr):
            a = np.array(arr, dtype=float)
            return float(a.mean()), float(a.std())

        bm, bs = ms(vbus)
        rm, rs = ms(vrep)
        am, as_ = ms(vadc)
        dm, ds = ms(duty)
        return {
            "vbus_mean": bm, "vbus_std": bs,
            "vrep_mean": rm, "vrep_std": rs,
            "vadc_mean": am, "vadc_std": as_,
            "duty_mean": dm, "duty_std": ds,
            "n": len(vadc),
        }

    def run(self):
        while not self._stop.is_set():
            try:
                if self._port.in_waiting:
                    line = self._port.readline().decode("utf-8", errors="replace").strip()
                    self._parse(line)
                else:
                    time.sleep(0.005)
            except serial.SerialException:
                break
            except Exception:
                pass

    def _parse(self, line):
        if not line.startswith("DATA,"):
            return
        parts = line.split(",")
        if len(parts) <= COL_DUTY_CYCLE:
            return
        try:
            v_bus = float(parts[COL_BUS_VOLTAGE])
            v_rep = float(parts[COL_MOTOR_VOLTAGE])
            duty  = float(parts[COL_DUTY_CYCLE])
            vadc  = v_rep * CURRENT_SLOPE + CURRENT_OFFSET
            with self._lock:
                self._buf_vbus.append(v_bus)
                self._buf_vrep.append(v_rep)
                self._buf_vadc.append(vadc)
                self._buf_duty.append(duty)
        except (ValueError, IndexError):
            pass


# ---- Helpers ------------------------------------------------------------------
def send_cmd(ser, cmd):
    ser.write((cmd.strip() + "\n").encode("utf-8"))


def settle_and_capture(reader, settle, avg):
    """Settle, discard transients, average, return snapshot dict or None."""
    reader.clear()
    for i in range(int(settle), 0, -1):
        print(f"\r  Settling...  {i} s   ", end="", flush=True)
        time.sleep(1)
    reader.clear()
    for i in range(int(avg), 0, -1):
        print(f"\r  Averaging... {i} s   ", end="", flush=True)
        time.sleep(1)
    print("\r" + " " * 30 + "\r", end="", flush=True)
    return reader.snapshot()


def linear_regression(x, y):
    """Fit y = slope*x + offset. Returns (slope, offset, R2)."""
    x = np.array(x, dtype=float)
    y = np.array(y, dtype=float)
    A = np.column_stack([x, np.ones_like(x)])
    coeffs, _, _, _ = np.linalg.lstsq(A, y, rcond=None)
    slope, offset = float(coeffs[0]), float(coeffs[1])
    y_pred = slope * x + offset
    ss_res = float(np.sum((y - y_pred) ** 2))
    ss_tot = float(np.sum((y - y.mean()) ** 2))
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 1.0
    return slope, offset, r2


def print_points(points):
    if not points:
        print("  No points collected yet.")
        return
    print(f"\n  {'#':>3}  {'Duty%':>6}  {'V_bus':>7}  {'V_calc':>8}"
          f"  {'V_dmm':>8}  {'V_rep':>8}  {'Truth':>8}  {'Src':>4}")
    print("  " + "-" * 68)
    for i, p in enumerate(points):
        dmm = f"{p['v_dmm']:>8.3f}" if p["v_dmm"] is not None else f"{'---':>8}"
        src = "DMM" if p["v_dmm"] is not None else "calc"
        print(f"  {i+1:>3}  {p['duty']:>6.1f}  {p['v_bus']:>7.3f}"
              f"  {p['v_calc']:>8.3f}  {dmm}  {p['v_rep']:>8.3f}"
              f"  {p['v_truth']:>8.3f}  {src:>4}")
    print()


# ---- Main --------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Calibrate VmotLP sensor in NewPECode via PWM sweep."
    )
    parser.add_argument("--port",   required=True,
                        help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    parser.add_argument("--baud",   type=int, default=115200)
    parser.add_argument("--settle", type=float, default=4.0,
                        help="Settle time per point in seconds (default: 4)")
    parser.add_argument("--avg",    type=float, default=3.0,
                        help="Averaging window per point in seconds (default: 3)")
    parser.add_argument("--duties", type=float, nargs="+",
                        default=[10, 20, 30, 40, 50],
                        help="Duty cycle points in %% (default: 10 20 30 40 50)")
    parser.add_argument("--auto",   action="store_true",
                        help="Auto-sweep without per-point prompts")
    args = parser.parse_args()

    duties = sorted(set(max(0.0, min(100.0, d)) for d in args.duties))

    print(BOLD("\n=== VmotLP Calibration Tool (PWM sweep) ==="))
    print(f"Port    : {args.port}  @{args.baud} baud")
    print(f"Slope   : {CURRENT_SLOPE}   Offset: {CURRENT_OFFSET}")
    print(f"Duties  : {duties}%")
    print(f"Mode    : {'AUTO' if args.auto else 'interactive'}\n")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(RED(f"ERROR: {e}"))
        sys.exit(1)

    time.sleep(2)
    print(GREEN("Connected.\n"))

    reader = SerialReader(ser)
    reader.start()

    time.sleep(1.5)
    snap = reader.snapshot()
    if snap is None:
        print(YELLOW("WARNING: No DATA lines received yet."))
    else:
        print(f"Receiving data  (V_bus={snap['vbus_mean']:.2f} V  "
              f"V_rep={snap['vrep_mean']:.3f} V  duty={snap['duty_mean']:.1f}%)\n")

    print("Enabling serial control (sending 'M')...")
    send_cmd(ser, "M")
    time.sleep(0.3)

    print(CYAN("\n---- At each duty cycle point: -----------------------------------"))
    print("  V_calc    = avg(V_bus) x duty/100  [INA780, theoretical]")
    print("  V_reported = firmware motorVoltage  [current calibration]")
    print("  V_dmm     = your multimeter         [optional truth override]")
    if not args.auto:
        print("\n  Commands: ENTER=record  skip  done  list  undo")
    print(CYAN("------------------------------------------------------------------\n"))

    points = []

    try:
        i = 0
        while i < len(duties):
            duty_set = duties[i]
            duty_int = int(round(duty_set))

            print(BOLD(f"\n-- Duty: {duty_set:.1f}% -----------------------------------------------"))
            print(f"  Sending '{duty_int}'...")
            send_cmd(ser, str(duty_int))

            snap = settle_and_capture(reader, args.settle, args.avg)
            if snap is None or snap["n"] < 3:
                print(RED("  Not enough samples - skipping."))
                i += 1
                continue

            v_bus  = snap["vbus_mean"]
            v_rep  = snap["vrep_mean"]
            vadc   = snap["vadc_mean"]
            duty_r = snap["duty_mean"]
            v_calc = v_bus * (duty_r / 100.0)
            diff   = (v_rep - v_calc) * 1000

            print(f"  V_bus      = {v_bus:.3f} V  (+-{snap['vbus_std']*1000:.1f} mV,  n={snap['n']})")
            print(f"  Duty (fw)  = {duty_r:.2f}%")
            print(f"  V_calc     = {v_calc:.3f} V   [V_bus x duty/100]")
            print(f"  V_reported = {v_rep:.3f} V   [current calibration]")
            diff_col = RED if abs(diff) > 500 else YELLOW if abs(diff) > 100 else GREEN
            print(f"  Difference = {diff_col(f'{diff:+.1f} mV')}  (reported - calc)")

            v_dmm = None
            if not args.auto:
                dmm_str = input("\n  DMM reading (V), or ENTER to use V_calc: ").strip()
                if dmm_str:
                    try:
                        v_dmm = float(dmm_str)
                        dmm_diff = (v_rep - v_dmm) * 1000
                        print(f"  V_dmm      = {v_dmm:.3f} V  (reported error: {dmm_diff:+.1f} mV)")
                    except ValueError:
                        print(YELLOW("  Invalid - using V_calc."))

                action = input("  [ENTER=record  skip  done  list  undo]: ").strip().lower()
                if action == "skip":
                    print("  Skipped.")
                    i += 1
                    continue
                if action == "done":
                    break
                if action == "undo":
                    if points:
                        p = points.pop()
                        print(f"  Removed: duty={p['duty']:.1f}%  V_truth={p['v_truth']:.3f} V")
                    else:
                        print("  Nothing to undo.")
                    # stay on same duty point so user can re-record
                    continue
                if action == "list":
                    print_points(points)
                    continue

            v_truth = v_dmm if v_dmm is not None else v_calc
            src     = "DMM" if v_dmm is not None else "calc"
            points.append({
                "duty":    duty_r,
                "v_bus":   v_bus,
                "v_calc":  v_calc,
                "v_dmm":   v_dmm,
                "v_rep":   v_rep,
                "v_truth": v_truth,
                "vadc":    vadc,
            })
            print(GREEN(f"  Recorded {len(points)}: duty={duty_r:.1f}%  "
                        f"V_truth={v_truth:.3f} V  (src: {src})"))
            i += 1

    except KeyboardInterrupt:
        print("\nInterrupted.")

    finally:
        print("\nSending stop ('S') and returning to throttle ('T')...")
        try:
            send_cmd(ser, "S")
            time.sleep(0.3)
            send_cmd(ser, "T")
        except Exception:
            pass
        reader.stop()
        ser.close()

    # ---- Fit -----------------------------------------------------------------
    print()
    if len(points) < 2:
        print(YELLOW("Need at least 2 points to fit - no results produced."))
        return

    vtrue_list = [p["v_truth"] for p in points]
    vadc_list  = [p["vadc"]   for p in points]

    new_slope, new_offset, r2 = linear_regression(vtrue_list, vadc_list)

    print(BOLD(CYAN("\n=== Calibration Results ============================================")))
    print(f"  Points : {len(points)}")
    r2_lbl = "(excellent)" if r2 > 0.9999 else "(good)" if r2 > 0.999 else "(check for noise)"
    print(f"  R2     : {r2:.6f}  {r2_lbl}")
    print(f"\n  Fit:  Vadc = {new_slope:.6f} x Vmot_true + {new_offset:.6f}")

    print(BOLD("\n  Comparison table:"))
    print(f"\n  {'#':>3}  {'Duty%':>6}  {'V_calc':>8}  {'V_dmm':>8}  {'V_rep':>8}  "
          f"{'V_truth':>8}  {'err_now(mV)':>12}  {'err_new(mV)':>12}")
    print("  " + "-" * 80)
    for i, p in enumerate(points):
        vmot_new  = (p["vadc"] - new_offset) / new_slope
        err_now   = (p["v_rep"]  - p["v_truth"]) * 1000
        err_new   = (vmot_new    - p["v_truth"]) * 1000
        dmm_str   = f"{p['v_dmm']:.3f}" if p["v_dmm"] is not None else "  ---"
        now_col   = RED if abs(err_now) > 500 else YELLOW if abs(err_now) > 100 else GREEN
        new_col   = RED if abs(err_new) > 100 else YELLOW if abs(err_new) > 20  else GREEN
        print(f"  {i+1:>3}  {p['duty']:>6.1f}  {p['v_calc']:>8.3f}  {dmm_str:>8}  "
              f"{p['v_rep']:>8.3f}  {p['v_truth']:>8.3f}  "
              f"{now_col(f'{err_now:>+10.1f}')}  {new_col(f'{err_new:>+10.2f}')}")

    print(BOLD("\n  New firmware constants - update SensorTask.ino lines 16-18:\n"))
    print(f"    // A0: motor voltage  Vadc = {new_slope:.6f}*Vmot + {new_offset:.6f}")
    print(f"    float motorVoltageADC = analogRead(A0) * (3.3f / 4095.0f);")
    print(f"    float motorVoltage    = (motorVoltageADC - {new_offset:.6f}f) / {new_slope:.6f}f;\n")
    print(f"    Previous:  SLOPE={CURRENT_SLOPE}  OFFSET={CURRENT_OFFSET}")
    print(f"    New:       SLOPE={new_slope:.6f}  OFFSET={new_offset:.6f}")
    print(CYAN("\n===================================================================="))


if __name__ == "__main__":
    if sys.platform == "win32":
        import os
        os.system("")
    main()
