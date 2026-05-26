import serial
import time
import pandas as pd
import argparse
import sys

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QGridLayout, QLabel, QVBoxLayout
)
from PyQt5.QtCore import QThread, pyqtSignal, Qt
import pyqtgraph as pg
import pyqtgraph.exporters

# ─────────────────────────────────────────────
# Duty cycle sequence
# ─────────────────────────────────────────────

def generate_duty_cycles():
    duty_cycles = []
    for i in range(0, 16):          # 0–15 % at 1 % steps
        duty_cycles.append(float(i))
    for i in range(20, 90, 5):      # 15–85 % at 5 % steps
        duty_cycles.append(float(i))
    for i in range(86, 100, 3):        # 85–99 % at 1 % steps
        duty_cycles.append(float(i))
    duty_cycles.append(99.8)        # ~100 %
    return duty_cycles

# ─────────────────────────────────────────────
# Serial parsers
# ─────────────────────────────────────────────

def parse_sensor_line(line):
    # DATA,Timestamp_ms,Voltage,Current,Power,Energy,Temperature,MotorCurrent,DutyCycle,TargetDutyCycle
    try:
        parts = line.strip().split(',')
        if len(parts) >= 10 and parts[0] == "DATA":
            return {
                "Timestamp_ms":      float(parts[1]),
                "Voltage":           float(parts[2]),
                "Current":           float(parts[3]),
                "Power":             float(parts[4]),
                "Energy":            float(parts[5]),
                "Temperature":       float(parts[6]),
                "MotorCurrent":      float(parts[7]),
                "MeasuredDutyCycle": float(parts[8]),
                "TargetDutyCycle":   float(parts[9]),
            }
    except ValueError:
        pass
    return None

def parse_dyno_line(line):
    # $<timestamp_ms> <dynoRPM> <carRPM> <torque_Nm> <power_W>;
    try:
        line = line.strip()
        if line.startswith("$") and line.endswith(";"):
            parts = line[1:-1].split()
            if len(parts) == 5:
                return {
                    "Dyno_Timestamp_ms": float(parts[0]),
                    "Dyno_RPM":          float(parts[1]),
                    "Car_RPM":           float(parts[2]),
                    "Dyno_Torque_Nm":    float(parts[3]),
                    "Dyno_Power_W":      float(parts[4]),
                }
    except ValueError:
        pass
    return None

def mktimestamp():
    return time.strftime("%Y%m%d-%H%M%S")

# ─────────────────────────────────────────────
# Sweep worker (runs in a QThread)
# ─────────────────────────────────────────────

class SweepWorker(QThread):
    """Runs the duty-cycle sweep and emits a signal for each completed step."""

    # Emitted after each step with the averaged result dict
    step_complete = pyqtSignal(dict)
    # Emitted when the sweep finishes (passes full results list and timestamp string)
    sweep_done = pyqtSignal(list, str)
    # Emitted for status messages
    status = pyqtSignal(str)

    def __init__(self, port, baudrate, output_file, dyno_port, dyno_baudrate):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.output_file = output_file
        self.dyno_port = dyno_port
        self.dyno_baudrate = dyno_baudrate

    def run(self):
        # ── Open ports ──────────────────────────────
        try:
            ser = serial.Serial(self.port, self.baudrate, timeout=1)
            self.status.emit(f"Connected to {self.port} at {self.baudrate} baud.")
        except serial.SerialException as e:
            self.status.emit(f"ERROR: {e}")
            return

        dyno_ser = None
        if self.dyno_port:
            try:
                dyno_ser = serial.Serial(self.dyno_port, self.dyno_baudrate, timeout=0)
                self.status.emit(f"Dyno connected to {self.dyno_port} at {self.dyno_baudrate} baud.")
            except serial.SerialException as e:
                self.status.emit(f"ERROR (dyno): {e}")
                ser.close()
                return

        time.sleep(2)
        ser.reset_input_buffer()
        if dyno_ser:
            dyno_ser.reset_input_buffer()

        duty_cycles = generate_duty_cycles()
        results = []

        self.status.emit(f"Starting sweep with {len(duty_cycles)} points.")

        all_raw_samples = []
        dyno_buf = ""  # accumulation buffer for partial dyno lines

        try:
            for target_dc in duty_cycles:
                # Send duty cycle then immediately reset energy register
                ser.write(f"{target_dc}\n".encode())
                time.sleep(0.05)   # brief pause so Arduino processes the DC command first
                ser.write(b"R")    # reset INA780 energy register

                start_time = time.time()
                samples = []
                dyno_samples = []

                while (time.time() - start_time) < 5.0:
                    elapsed = time.time() - start_time
                    in_averaging_window = elapsed > 3.0

                    if ser.in_waiting:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        data = parse_sensor_line(line)
                        if data:
                            host_ts = time.time()
                            data["Host_Timestamp"] = host_ts
                            data["SetDutyCycle"] = target_dc
                            data["InAveragingWindow"] = in_averaging_window
                            all_raw_samples.append(data)
                            if in_averaging_window:
                                samples.append(data)

                    if dyno_ser and dyno_ser.in_waiting:
                        # Read all available bytes and append to buffer; only
                        # process complete newline-terminated lines to avoid
                        # dropping partial frames when timeout=0.
                        dyno_buf += dyno_ser.read(dyno_ser.in_waiting).decode('utf-8', errors='ignore')
                        while '\n' in dyno_buf:
                            line, dyno_buf = dyno_buf.split('\n', 1)
                            data = parse_dyno_line(line.strip())
                            if data and in_averaging_window:
                                dyno_samples.append(data)

                    time.sleep(0.01)

                if not samples:
                    self.status.emit(f"Warning: no data for {target_dc}% duty cycle")
                    continue

                # ── Average ──────────────────────────────────────────────
                def avg(key, src=samples):
                    return sum(d[key] for d in src) / len(src)

                row = {
                    "SetDutyCycle":      target_dc,
                    "MeasuredDutyCycle": avg("MeasuredDutyCycle"),
                    "Voltage_V":         avg("Voltage"),
                    "Current_A":         avg("Current"),
                    "Power_W":           avg("Power"),
                    "Energy_J":          samples[-1]["Energy"],
                    "Temperature_C":     avg("Temperature"),
                    "MotorCurrent_A":    avg("MotorCurrent"),
                    "SamplesCount":      len(samples),
                }

                if dyno_samples:
                    def davg(key):
                        return sum(d[key] for d in dyno_samples) / len(dyno_samples)
                    row["Dyno_RPM"]         = davg("Dyno_RPM")
                    row["Car_RPM"]          = davg("Car_RPM")
                    row["Dyno_Torque_Nm"]   = davg("Dyno_Torque_Nm")
                    row["Dyno_Power_W"]     = davg("Dyno_Power_W")
                    row["DynoSamplesCount"] = len(dyno_samples)
                elif dyno_ser:
                    self.status.emit(f"Warning: no dyno data for {target_dc}%")
                    row.update({"Dyno_RPM": None, "Car_RPM": None,
                                "Dyno_Torque_Nm": None, "Dyno_Power_W": None,
                                "DynoSamplesCount": 0})

                results.append(row)
                self.step_complete.emit(row)
                self.status.emit(
                    f"{target_dc:5.1f}%  V={row['Voltage_V']:.3f} V  "
                    f"I={row['Current_A']:.3f} A  P={row['Power_W']:.2f} W"
                )

        except Exception as e:
            self.status.emit(f"Sweep error: {e}")

        finally:
            self.status.emit("Stopping system (setting 0% duty cycle)...")
            ser.write(b"S")
            ser.close()
            if dyno_ser and dyno_ser.is_open:
                dyno_ser.close()

            ts = mktimestamp()
            base = self.output_file.split('.')[0]
            if results:
                df = pd.DataFrame(results)
                filename = f"{base}_{ts}.csv"
                df.to_csv(filename, index=False)
                self.status.emit(f"Results saved to {filename}")
            else:
                self.status.emit("No results collected.")

            if all_raw_samples:
                raw_filename = f"{base}_{ts}_raw.csv"
                df_raw = pd.DataFrame(all_raw_samples)
                # Reorder columns for readability
                col_order = ["Host_Timestamp", "Timestamp_ms", "SetDutyCycle",
                             "InAveragingWindow", "MeasuredDutyCycle", "TargetDutyCycle",
                             "Voltage", "Current", "Power", "Energy",
                             "Temperature", "MotorCurrent"]
                col_order = [c for c in col_order if c in df_raw.columns]
                df_raw = df_raw[col_order]
                df_raw.to_csv(raw_filename, index=False)
                self.status.emit(f"Raw samples saved to {raw_filename} ({len(all_raw_samples)} rows)")
            else:
                self.status.emit("No raw samples collected.")

            self.sweep_done.emit(results, ts)

# ─────────────────────────────────────────────
# Main window
# ─────────────────────────────────────────────

class SweepWindow(QMainWindow):
    def __init__(self, worker: SweepWorker, dyno: bool, args_base: str = "duty_sweep_results"):
        super().__init__()
        self.setWindowTitle("Duty Cycle Sweep")
        self.resize(1200, 800 if not dyno else 1100)

        pg.setConfigOption("background", "w")
        pg.setConfigOption("foreground", "k")

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        self.status_label = QLabel("Initialising…")
        self.status_label.setAlignment(Qt.AlignLeft)
        layout.addWidget(self.status_label)

        grid = QGridLayout()
        layout.addLayout(grid)

        # {slug: (PlotWidget, curve)}
        self._plots: dict = {}

        def make_plot(slug, title, ylabel, row, col, color):
            pw = pg.PlotWidget(title=title)
            pw.setLabel("bottom", "Duty Cycle", units="%")
            pw.setLabel("left", ylabel)
            pw.showGrid(x=True, y=True, alpha=0.3)
            pen = pg.mkPen(color, width=2)
            curve = pw.plot([], [], pen=pen, symbol="o", symbolSize=5,
                            symbolBrush=color)
            grid.addWidget(pw, row, col)
            self._plots[slug] = pw
            return curve

        self.c_voltage  = make_plot("bus_voltage",      "Bus Voltage",      "V (V)",  0, 0, "#1f77b4")
        self.c_current  = make_plot("ina_current",      "INA Current",      "A",      0, 1, "#ff7f0e")
        self.c_power    = make_plot("elec_power",       "Electrical Power", "W",      1, 0, "#2ca02c")
        self.c_motorcur = make_plot("motor_current",    "Motor Current",    "A",      1, 1, "#d62728")

        if dyno:
            self.c_rpm    = make_plot("car_rpm",   "Car RPM",  "RPM",  2, 0, "#9467bd")
            self.c_torque = make_plot("torque",    "Torque",   "N·m",  2, 1, "#8c564b")
        else:
            self.c_rpm    = None
            self.c_torque = None

        self._base = args_base

        # Data buffers
        self._dc       = []
        self._voltage  = []
        self._current  = []
        self._power    = []
        self._motorcur = []
        self._rpm      = []
        self._torque   = []

        # Connect worker signals
        worker.step_complete.connect(self._on_step)
        worker.sweep_done.connect(self._on_done)
        worker.status.connect(self._on_status)

    def _on_step(self, row: dict):
        self._dc.append(row["SetDutyCycle"])
        self._voltage.append(row["Voltage_V"])
        self._current.append(row["Current_A"])
        self._power.append(row["Power_W"])
        self._motorcur.append(row["MotorCurrent_A"])

        self.c_voltage.setData(self._dc, self._voltage)
        self.c_current.setData(self._dc, self._current)
        self.c_power.setData(self._dc, self._power)
        self.c_motorcur.setData(self._dc, self._motorcur)

        if self.c_rpm and row.get("Car_RPM") is not None:
            self._rpm.append(row["Car_RPM"])
            self._torque.append(row["Dyno_Torque_Nm"])
            self.c_rpm.setData(self._dc, self._rpm)
            self.c_torque.setData(self._dc, self._torque)

    def _on_status(self, msg: str):
        self.status_label.setText(msg)
        print(msg)

    def _on_done(self, results: list, ts: str):
        self.status_label.setText(
            f"Sweep complete — {len(results)} points. Saving figures…"
        )
        QApplication.processEvents()  # repaint before grabbing screenshots
        self._save_figures(ts)
        self.status_label.setText(
            f"Sweep complete — {len(results)} points. Close window to exit."
        )

    def _save_figures(self, ts: str):
        import os
        for slug, pw in self._plots.items():
            filename = f"{self._base}_{ts}_{slug}.png"
            exporter = pg.exporters.ImageExporter(pw.plotItem)
            exporter.parameters()["width"] = 1200
            exporter.export(filename)
            print(f"Figure saved: {filename}")

# ─────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Duty Cycle Sweep Test (GUI)")
    parser.add_argument("--port",      type=str, required=True,                    help="COM port for sweep/sensor Arduino (e.g. COM3)")
    parser.add_argument("--baud",      type=int, default=115200,                   help="Baud rate for sweep Arduino")
    parser.add_argument("--output",    type=str, default="duty_sweep_results.csv", help="Base output filename")
    parser.add_argument("--dyno-port", type=str, default=None,                     help="COM port for dyno Arduino (optional)")
    parser.add_argument("--dyno-baud", type=int, default=9600,                     help="Baud rate for dyno Arduino (default: 9600)")
    args = parser.parse_args()

    app = QApplication(sys.argv)

    worker = SweepWorker(
        args.port, args.baud, args.output,
        args.dyno_port, args.dyno_baud,
    )

    base = args.output.split('.')[0]
    window = SweepWindow(worker, dyno=args.dyno_port is not None, args_base=base)
    window.show()

    worker.start()

    sys.exit(app.exec_())
