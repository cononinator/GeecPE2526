# DynoSweep Closed-Loop Test

Automated closed-loop dyno testing that accelerates to a maximum speed using a current limit, then coasts down. When the dyno speed drops below a minimum threshold, the cycle repeats. Supports running 5 cycles per parameter set, then prompting for new parameters.

## Features

- **Closed-loop control**: Accelerates with fixed current limit, coasts down passively
- **Parameter control**: Specify current limit (A), minimum speed (km/h), maximum speed (km/h)
- **Multi-cycle testing**: Run 5 repeats per parameter set, then optional new parameters
- **Real-time data collection**: Records voltage, current, power, energy, temperature, motor current
- **Dyno speed feedback**: Optional dyno speed input (serial) to control test pacing
- **Data logging**: Saves all samples to CSV with timestamps and phase information
- **Live plotting** (GUI version): Watch voltage, power, current, and temperature in real-time

## Installation

```bash
pip install pyserial pandas
# For GUI version only:
pip install PyQt5 pyqtgraph
```

## CLI Version: `dyno_sweep.py`

Run interactive tests from the command line with manual parameter input between cycles.

### Usage

```bash
# With firmware only (no dyno feedback)
python dyno_sweep.py --port COM3

# With dyno speed feedback
python dyno_sweep.py --port COM3 --dyno-port COM4

# Custom baud rates
python dyno_sweep.py --port COM3 --baud 115200 --dyno-port COM4 --dyno-baud 9600
```

### Operation Flow

1. **Parameter Entry**: Prompts for:
   - Current limit (A)
   - Minimum speed (km/h)
   - Maximum speed (km/h)

2. **5 Repeats**: For each parameter set:
   - Accelerates vehicle to max speed using set current limit
   - Detects when max speed is reached (with 2s stability window)
   - Stops motor control (coasts down)
   - Waits for speed to drop below minimum before next repeat

3. **Data Saved**: As `dyno_sweep_YYYYMMDD-HHMMSS_cycle#.csv`

4. **Choice**: Can run another cycle with different parameters

### Output Files

- **Per-cycle CSV**: `dyno_sweep_YYYYMMDD-HHMMSS_cycle1.csv` (5 repeats merged)
- **Combined CSV**: `dyno_sweep_YYYYMMDD-HHMMSS_all_cycles.csv` (all parameter sets)

Columns include:
- `Phase`: "Accelerating" or "Coasting"
- `Repeat`: Which repeat (1-5)
- `CurrentLimit_A`: Applied current limit
- `Voltage`, `Current`, `Power`, `Energy`, `Temperature`: Electrical measurements
- `MotorCurrent`, `MeasuredDutyCycle`, `TargetDutyCycle`: Motor/driver state
- (Optional) `Dyno_Timestamp_ms`, `Dyno_Speed_kmh`, `Car_Speed_kmh`, `Dyno_Torque_Nm`, `Dyno_Power_W`: Dyno feedback

## GUI Version: `run_dyno_sweep_gui.py`

Interactive PyQt5 GUI with real-time plotting and parameter dialog.

### Usage

```bash
python run_dyno_sweep_gui.py --port COM3 --dyno-port COM4
```

### Features

- **Parameter Dialog**: Spin boxes for quick parameter adjustment
- **Real-time Plots**: 4 synchronized plots updating as test runs
  - Dyno Speed vs. Time
  - Electrical Power vs. Time
  - Motor Current vs. Time
  - Temperature vs. Time
- **Status Display**: Live feedback on acceleration/coasting phases
- **Auto-save**: Plots saved as PNG after each test
- **Data Export**: Full CSV with all samples automatically saved

### Controls

- **Start Test**: Opens parameter dialog, begins test
- **Stop**: Cleanly stops current test (sends stop command to firmware)

## Protocol Details

### Firmware Commands

The firmware expects:
- `0-100`: Set duty cycle (%)
- `C <amps>`: Set current limit
- `S`: Stop (ramp to 0%)
- `R`: Reset energy register

### Data Format

**Firmware sensor data** (parse if line starts with `DATA,`):
```
DATA,Voltage,Current,Power,Energy,Temperature,MotorCurrent,MeasuredDutyCycle,TargetDutyCycle
```

**Dyno speed** (optional, parse if starts with `$` and ends with `;`):
```
$Timestamp_ms Dyno_Speed_kmh Car_Speed_kmh Torque_Nm Power_W;
```
Uses `Car_Speed_kmh` (parts[2]) to control acceleration/coast logic.

## Typical Test Scenario

```
Current Limit: 20.0 A
Min Speed: 10.0 km/h
Max Speed: 20.0 km/h
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[Cycle 1/5] Starting closed-loop sweep...
  Repeat 1/5... Accelerating... Reached 20 km/h Coasting... Dropped to 10 km/h. Done
  Repeat 2/5... Accelerating... Reached 20 km/h Coasting... Dropped to 10 km/h. Done
  ...
  Repeat 5/5... Accelerating... Reached 20 km/h Coasting... Dropped to 10 km/h. Done

Cycle 1 results saved to dyno_sweep_20260320-143022_cycle1.csv

Run another cycle with different parameters? (y/n):
```

## Analysis

Use standard pandas/matplotlib to analyze results:

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('dyno_sweep_20260320-143022_all_cycles.csv')

# Plot power by repeat
for rep in df['Repeat'].unique():
    data = df[df['Repeat'] == rep]
    plt.plot(data.index, data['Power'], label=f'Repeat {rep}')
plt.legend()
plt.xlabel('Sample')
plt.ylabel('Power (W)')
plt.show()

# Statistics by phase
print(df.groupby('Phase')[['Current', 'Power']].agg(['mean', 'std']))
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No firmware data | Check port and baud rate, verify firmware is outputting `DATA,` lines |
| Dyno always at 0 RPM | Check dyno port/baud rate, verify dyno data format starts with `$` |
| Never reaches min speed | Check timeout (Phase 2 waits 120s max) or sensor baseline |
| Test stops unexpectedly | Check serial buffer overflow; firmware may be sending too fast |

## File Structure

```
Code/ClosedLoopTest/
├── dyno_sweep.py              # CLI version (interactive parameter input)
├── run_dyno_sweep_gui.py      # GUI version (real-time plotting)
├── run_interactive.py         # General-purpose serial terminal
├── INA780.h                   # Power sensor driver
└── results/
    ├── dyno_sweep_20260320-143022_cycle1.csv
    ├── dyno_sweep_20260320-143022_all_cycles.csv
    └── dyno_sweep_20260320-143022_speed.png
```

## Implementation Notes

Both `dyno_sweep.py` and `run_dyno_sweep_gui.py` are based on the proven pattern from `DutyCycleSweep/run_sweep.py`:

- **Consistent parsing**: Uses same `parse_sensor_line()` and `parse_dyno_line()` logic
- **Proper serial handling**: Implements serial buffering for non-blocking dyno port (timeout=0)
- **Data format**: All fields from dyno (timestamp, speed, torque, power) are preserved in CSV
- **km/h units**: Adapted for speed input in km/h (not RPM) with 10-20 km/h defaults
- **Simple structure**: CLI version mirrors the straightforward approach of `run_sweep.py`
- **Thread-based GUI**: GUI worker thread pattern matches `run_sweep_gui.py` architecture

## Future Enhancements

- Automatic data analysis and summary statistics
- Parametric sweep (test multiple current/speed sets automatically)
- WebSocket monitoring (live data from browser)
- Thermal modeling during coasting phase
