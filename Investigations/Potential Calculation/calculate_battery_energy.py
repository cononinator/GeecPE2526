"""
Simple battery energy calculator
Calculates energy discharged from battery using E = ∫ V × I dt
"""

import pandas as pd
import numpy as np
from pathlib import Path

def calculate_energy(csv_file):
    """
    Calculate total energy discharged by the battery.
    
    Parameters:
    -----------
    csv_file : str
        Path to the CSV file containing battery data
    
    Returns:
    --------
    float
        Total energy in Watt-hours (Wh)
    """
    # Load data without headers
    df = pd.read_csv(csv_file, header=None)
    
    # Extract columns (0-indexed)
    current = df.iloc[:, 1].values   # Column 3 (1-indexed)
    voltage = df.iloc[:, 3].values   # Column 4 (1-indexed)
    timestamp = df.iloc[:, 0].values  # Column 1 (1-indexed)
    
    # Display data info
    print(f"Loaded {len(df)} data points")
    print(f"Voltage range: {voltage.min():.3f}V - {voltage.max():.3f}V")
    print(f"Current range: {current.min():.3f}A - {current.max():.3f}A")
    print(f"Duration: {(timestamp[-1] - timestamp[0])/1000:.1f} seconds\n")
    
    # Calculate power at each time step
    power = voltage * current  # Power in Watts
    
    # Integrate power over time (using trapezoidal rule)
    time_seconds = timestamp / 1000.0  # Convert ms to seconds
    energy_wh = np.trapezoid(power, x=time_seconds) / 3600  # Convert Joules to Wh
    
    return energy_wh, voltage, current, power

if __name__ == "__main__":
    csv_file = Path("DynoResult1.csv")
    
    if csv_file.exists():
        energy, voltage, current, power = calculate_energy(str(csv_file))
        print(f"Total energy discharged: {energy:.3f} Wh")
        print(f"Average power: {np.mean(power):.3f} W")
        print(f"Peak power: {np.max(power):.3f} W")
    else:
        print(f"Error: File not found at {csv_file}")
