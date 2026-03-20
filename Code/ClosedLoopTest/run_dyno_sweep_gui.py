#!/usr/bin/env python3
"""
run_dyno_sweep_gui.py — GUI for closed-loop dyno sweep test with live plotting

Provides real-time data visualization and parameter input.
"""

import serial
import time
import pandas as pd
import argparse
import sys

try:
    from PyQt5.QtWidgets import (
        QApplication, QMainWindow, QWidget, QGridLayout, QLabel, QVBoxLayout,
        QHBoxLayout, QPushButton, QSpinBox, QDoubleSpinBox
    )
    from PyQt5.QtCore import QThread, pyqtSignal, Qt
    import pyqtgraph as pg
    import pyqtgraph.exporters
except ImportError as e:
    print(f"Missing dependencies: {e}")
    print("Install with: pip install PyQt5 pyqtgraph pandas pyserial")
    sys.exit(1)


def parse_sensor_line(line):
    """Parse sensor data line from firmware."""
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


# ─────────────────────────────────────────────
# Parameter dialog
# ─────────────────────────────────────────────

class ParameterDialog(QMainWindow):
    """Dialog to gather test parameters from user."""

    parameters_accepted = pyqtSignal(float, float, float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("DynoSweep Parameters")
        self.resize(400, 200)

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # Current limit
        hbox1 = QHBoxLayout()
        hbox1.addWidget(QLabel("Current Limit (A):"))
        self.spin_current = QDoubleSpinBox()
        self.spin_current.setMinimum(0.1)
        self.spin_current.setMaximum(100.0)
        self.spin_current.setValue(20.0)
        self.spin_current.setSingleStep(0.5)
        hbox1.addWidget(self.spin_current)
        hbox1.addStretch()
        layout.addLayout(hbox1)

        # Min speed
        hbox2 = QHBoxLayout()
        hbox2.addWidget(QLabel("Min Speed (km/h):"))
        self.spin_min = QDoubleSpinBox()
        self.spin_min.setMinimum(0)
        self.spin_min.setMaximum(200)
        self.spin_min.setValue(10)
        self.spin_min.setSingleStep(1)
        hbox2.addWidget(self.spin_min)
        hbox2.addStretch()
        layout.addLayout(hbox2)

        # Max speed
        hbox3 = QHBoxLayout()
        hbox3.addWidget(QLabel("Max Speed (km/h):"))
        self.spin_max = QDoubleSpinBox()
        self.spin_max.setMinimum(0)
        self.spin_max.setMaximum(200)
        self.spin_max.setValue(20)
        self.spin_max.setSingleStep(1)
        hbox3.addWidget(self.spin_max)
        hbox3.addStretch()
        layout.addLayout(hbox3)

        # Buttons
        hbox_buttons = QHBoxLayout()
        btn_start = QPushButton("Start Test")
        btn_cancel = QPushButton("Cancel")
        btn_start.clicked.connect(self._on_start)
        btn_cancel.clicked.connect(self.close)
        hbox_buttons.addWidget(btn_start)
        hbox_buttons.addWidget(btn_cancel)
        layout.addLayout(hbox_buttons)
        layout.addStretch()

    def _on_start(self):
        current = self.spin_current.value()
        min_sp = self.spin_min.value()
        max_sp = self.spin_max.value()

        if current <= 0 or max_sp <= min_sp:
            return

        self.parameters_accepted.emit(current, min_sp, max_sp)
        self.close()


# ─────────────────────────────────────────────
# Sweep worker (runs in a QThread)
# ─────────────────────────────────────────────

class DynoSweepWorker(QThread):
    """Runs the dyno sweep in a separate thread."""

    status = pyqtSignal(str)
    data_updated = pyqtSignal(dict)  # (time, speed, voltage, current, power, temp)
    sweep_done = pyqtSignal(list, str)  # (results, timestamp)

    def __init__(self, port, baudrate, dyno_port, dyno_baudrate,
                 current_limit, min_speed, max_speed, num_repeats=5):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.dyno_port = dyno_port
        self.dyno_baudrate = dyno_baudrate
        self.current_limit = current_limit
        self.min_speed = min_speed
        self.max_speed = max_speed
        self.num_repeats = num_repeats
        self._stop_flag = False

    def stop(self):
        self._stop_flag = True

    def run(self):
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

        # Set current limit
        self.status.emit(f"Setting current limit to {self.current_limit} A...")
        ser.write(f"C {self.current_limit}\n".encode())
        time.sleep(0.2)

        all_samples = []
        results = []
        test_start_time = time.time()  # Track test start time for entire cycle

        try:
            for repeat in range(self.num_repeats):
                if self._stop_flag:
                    break

                # Reset power meter energy at start of repeat
                ser.write(b"R\n")
                time.sleep(0.1)

                self.status.emit(f"[{repeat + 1}/{self.num_repeats}] Accelerating...")

                # Phase 1: Accelerate
                ser.write(b"100\n")
                accel_start = time.time()
                accel_samples = []
                accel_dyno = []
                max_speed_reached = False
                dyno_buf = ""

                while (time.time() - accel_start) < 60.0 and not self._stop_flag:
                    if ser.in_waiting:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        data = parse_sensor_line(line)
                        if data:
                            data["Phase"] = "Accelerating"
                            data["Repeat"] = repeat + 1
                            data["CurrentLimit_A"] = self.current_limit
                            data["Timestamp_s"] = time.time() - test_start_time
                            accel_samples.append(data)
                            all_samples.append(data)

                    if dyno_ser and dyno_ser.in_waiting:
                        dyno_buf += dyno_ser.read(dyno_ser.in_waiting).decode('utf-8', errors='ignore')
                        while '\n' in dyno_buf:
                            line, dyno_buf = dyno_buf.split('\n', 1)
                            data = parse_dyno_line(line.strip())
                            if data:
                                data["Timestamp_s"] = time.time() - test_start_time
                                accel_dyno.append(data)
                                all_samples.append(data)
                                self.data_updated.emit({
                                    "time": time.time() - accel_start,
                                    "speed": data["Car_Speed_kmh"],
                                    "voltage": accel_samples[-1]["Voltage"] if accel_samples else 0,
                                    "current": accel_samples[-1]["MotorCurrent"] if accel_samples else 0,
                                    "power": accel_samples[-1]["Power"] if accel_samples else 0,
                                    "temp": accel_samples[-1]["Temperature"] if accel_samples else 0,
                                })

                                if data["Car_Speed_kmh"] >= self.max_speed and not max_speed_reached:
                                    max_speed_reached = True
                                    ser.write(b"0\n")  # Stop motor immediately

                    if max_speed_reached:
                        break

                    time.sleep(0.01)

                # Phase 2: Coast down
                self.status.emit(f"[{repeat + 1}/{self.num_repeats}] Coasting...")
                ser.write(b"S\n")
                coast_start = time.time()
                coast_samples = []
                coast_dyno = []
                current_speed = None

                while (time.time() - coast_start) < 120.0 and not self._stop_flag:
                    if ser.in_waiting:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                        data = parse_sensor_line(line)
                        if data:
                            data["Phase"] = "Coasting"
                            data["Repeat"] = repeat + 1
                            data["CurrentLimit_A"] = self.current_limit
                            data["Timestamp_s"] = time.time() - test_start_time
                            coast_samples.append(data)
                            all_samples.append(data)

                    if dyno_ser and dyno_ser.in_waiting:
                        dyno_buf += dyno_ser.read(dyno_ser.in_waiting).decode('utf-8', errors='ignore')
                        while '\n' in dyno_buf:
                            line, dyno_buf = dyno_buf.split('\n', 1)
                            data = parse_dyno_line(line.strip())
                            if data:
                                current_speed = data["Car_Speed_kmh"]
                                data["Timestamp_s"] = time.time() - test_start_time
                                coast_dyno.append(data)
                                all_samples.append(data)
                                self.data_updated.emit({
                                    "time": (time.time() - accel_start) + 60.0,  # Offset from accel phase
                                    "speed": current_speed,
                                    "voltage": coast_samples[-1]["Voltage"] if coast_samples else 0,
                                    "current": coast_samples[-1]["MotorCurrent"] if coast_samples else 0,
                                    "power": coast_samples[-1]["Power"] if coast_samples else 0,
                                    "temp": coast_samples[-1]["Temperature"] if coast_samples else 0,
                                })

                            if current_speed < self.min_speed:
                                break

                    if current_speed is not None and current_speed < self.min_speed:
                        break

                    time.sleep(0.01)

                # Summarize repeat
                repeat_samples = accel_samples + coast_samples + accel_dyno + coast_dyno
                if repeat_samples:
                    results.append({
                        "Repeat": repeat + 1,
                        "CurrentLimit_A": self.current_limit,
                        "MinSpeed_kmh": self.min_speed,
                        "MaxSpeed_kmh": self.max_speed,
                        "SamplesCollected": len(repeat_samples),
                    })

        except Exception as e:
            self.status.emit(f"ERROR: {e}")

        finally:
            self.status.emit("Stopping system...")
            ser.write(b"S\n")
            time.sleep(0.5)
            ser.close()

            if dyno_ser and dyno_ser.is_open:
                dyno_ser.close()

            ts = mktimestamp()
            if all_samples:
                df = pd.DataFrame(all_samples)
                filename = f"dyno_sweep_{ts}.csv"
                df.to_csv(filename, index=False)
                self.status.emit(f"Results saved to {filename}")

            self.sweep_done.emit(results, ts)


# ─────────────────────────────────────────────
# Main window
# ─────────────────────────────────────────────

class DynoSweepWindow(QMainWindow):
    def __init__(self, port, baudrate, dyno_port, dyno_baudrate):
        super().__init__()
        self.setWindowTitle("DynoSweep Closed-Loop Test")
        self.resize(1200, 800)

        pg.setConfigOption("background", "w")
        pg.setConfigOption("foreground", "k")

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # Status label
        self.status_label = QLabel("Ready. Click 'Start Test' to begin.")
        layout.addWidget(self.status_label)

        # Buttons
        hbox_buttons = QHBoxLayout()
        self.btn_start = QPushButton("Start Test")
        self.btn_stop = QPushButton("Stop")
        self.btn_start.clicked.connect(self._on_start_test)
        self.btn_stop.clicked.connect(self._on_stop_test)
        self.btn_stop.setEnabled(False)
        hbox_buttons.addWidget(self.btn_start)
        hbox_buttons.addWidget(self.btn_stop)
        hbox_buttons.addStretch()
        layout.addLayout(hbox_buttons)

        # Plots grid
        grid = QGridLayout()

        # Create plots
        self.plot_speed = pg.PlotWidget(title="Dyno Speed")
        self.plot_speed.setLabel("bottom", "Time", units="s")
        self.plot_speed.setLabel("left", "Speed", units="km/h")
        self.plot_speed.showGrid(x=True, y=True, alpha=0.3)
        self.curve_speed = self.plot_speed.plot([], [], pen=pg.mkPen("#1f77b4", width=2), symbol="o", symbolSize=3)

        self.plot_power = pg.PlotWidget(title="Electrical Power")
        self.plot_power.setLabel("bottom", "Time", units="s")
        self.plot_power.setLabel("left", "Power", units="W")
        self.plot_power.showGrid(x=True, y=True, alpha=0.3)
        self.curve_power = self.plot_power.plot([], [], pen=pg.mkPen("#ff7f0e", width=2), symbol="o", symbolSize=3)

        self.plot_current = pg.PlotWidget(title="Motor Current")
        self.plot_current.setLabel("bottom", "Time", units="s")
        self.plot_current.setLabel("left", "Current", units="A")
        self.plot_current.showGrid(x=True, y=True, alpha=0.3)
        self.curve_current = self.plot_current.plot([], [], pen=pg.mkPen("#2ca02c", width=2), symbol="o", symbolSize=3)

        self.plot_temp = pg.PlotWidget(title="Temperature")
        self.plot_temp.setLabel("bottom", "Time", units="s")
        self.plot_temp.setLabel("left", "Temperature", units="°C")
        self.plot_temp.showGrid(x=True, y=True, alpha=0.3)
        self.curve_temp = self.plot_temp.plot([], [], pen=pg.mkPen("#d62728", width=2), symbol="o", symbolSize=3)

        grid.addWidget(self.plot_speed, 0, 0)
        grid.addWidget(self.plot_power, 0, 1)
        grid.addWidget(self.plot_current, 1, 0)
        grid.addWidget(self.plot_temp, 1, 1)
        layout.addLayout(grid)

        self.port = port
        self.baudrate = baudrate
        self.dyno_port = dyno_port
        self.dyno_baudrate = dyno_baudrate
        self.worker = None

        # Data buffers
        self.times = []
        self.speeds = []
        self.powers = []
        self.currents = []
        self.temps = []

    def _on_start_test(self):
        dialog = ParameterDialog(self)
        dialog.parameters_accepted.connect(self._start_sweep)
        dialog.show()

    def _on_stop_test(self):
        if self.worker and self.worker.isRunning():
            self.worker.stop()
            self.btn_stop.setEnabled(False)

    def _start_sweep(self, current_limit, min_speed, max_speed):
        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(True)

        # Reset buffers
        self.times = []
        self.speeds = []
        self.powers = []
        self.currents = []
        self.temps = []

        self.worker = DynoSweepWorker(
            self.port, self.baudrate, self.dyno_port, self.dyno_baudrate,
            current_limit, min_speed, max_speed, num_repeats=5
        )
        self.worker.status.connect(self._on_status)
        self.worker.data_updated.connect(self._on_data_update)
        self.worker.sweep_done.connect(self._on_sweep_done)
        self.worker.start()

    def _on_status(self, msg: str):
        self.status_label.setText(msg)
        print(msg)

    def _on_data_update(self, data: dict):
        self.times.append(data["time"])
        self.speeds.append(data["speed"])
        self.powers.append(data["power"])
        self.currents.append(data["current"])
        self.temps.append(data["temp"])

        self.curve_speed.setData(self.times, self.speeds)
        self.curve_power.setData(self.times, self.powers)
        self.curve_current.setData(self.times, self.currents)
        self.curve_temp.setData(self.times, self.temps)

        # Auto-scale
        self.plot_speed.enableAutoRange()
        self.plot_power.enableAutoRange()
        self.plot_current.enableAutoRange()
        self.plot_temp.enableAutoRange()

    def _on_sweep_done(self, results: list, ts: str):
        self.status_label.setText("Test complete. Click 'Start Test' for another cycle.")
        self.btn_start.setEnabled(True)
        self.btn_stop.setEnabled(False)

        # Save plots
        for slug, plot in [
            ("speed", self.plot_speed),
            ("power", self.plot_power),
            ("current", self.plot_current),
            ("temp", self.plot_temp),
        ]:
            filename = f"dyno_sweep_{ts}_{slug}.png"
            exporter = pg.exporters.ImageExporter(plot.plotItem)
            exporter.parameters()["width"] = 1200
            exporter.export(filename)
            print(f"Figure saved: {filename}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="DynoSweep Closed-Loop Test (GUI)")
    parser.add_argument("--port", type=str, required=True, help="COM port for firmware")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate for firmware")
    parser.add_argument("--dyno-port", type=str, default=None, help="COM port for dyno (optional)")
    parser.add_argument("--dyno-baud", type=int, default=9600, help="Baud rate for dyno")

    args = parser.parse_args()

    app = QApplication(sys.argv)
    window = DynoSweepWindow(args.port, args.baud, args.dyno_port, args.dyno_baud)
    window.show()

    sys.exit(app.exec_())
