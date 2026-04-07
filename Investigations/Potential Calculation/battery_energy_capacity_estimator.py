"""
Battery Energy Capacity Estimator

This script estimates battery energy capacity from voltage and current measurements,
while accounting for voltage sag effects.

Battery voltage is in column 4 (1-indexed) and battery current is in column 3 (1-indexed).
"""

import pandas as pd
import numpy as np
from scipy import signal
import matplotlib.pyplot as plt
from pathlib import Path


class BatteryEnergyEstimator:
    """Estimates battery energy capacity with voltage sag compensation."""
    
    def __init__(self, csv_file):
        """
        Initialize the estimator with a CSV file.
        
        Parameters:
        -----------
        csv_file : str
            Path to the CSV file containing battery data
        """
        self.csv_file = Path(csv_file)
        self.df = None
        self.load_data()
        
    def load_data(self):
        """Load and parse the CSV file."""
        # Load without headers since the CSV doesn't have them
        # Column 3 (1-indexed) = column 2 (0-indexed) = current
        # Column 4 (1-indexed) = column 3 (0-indexed) = voltage
        self.df = pd.read_csv(self.csv_file, header=None)
        
        # Extract relevant columns (0-indexed)
        self.current = self.df.iloc[:, 2].values  # Column 3 (1-indexed)
        self.voltage = self.df.iloc[:, 3].values  # Column 4 (1-indexed)
        self.timestamp = self.df.iloc[:, 0].values  # Column 1 (1-indexed) - time reference
        
        # Calculate time intervals (assuming timestamps are in milliseconds)
        self.time_intervals = np.diff(self.timestamp) / 1000  # Convert to seconds
        
        print(f"Loaded {len(self.df)} data points from {self.csv_file.name}")
        print(f"Voltage range: {self.voltage.min():.3f}V - {self.voltage.max():.3f}V")
        print(f"Current range: {self.current.min():.3f}A - {self.current.max():.3f}A")
        print(f"Duration: {self.time_intervals.sum():.1f} seconds")
        
    def smooth_data(self, window_size=5):
        """
        Apply Savitzky-Golay filter to smooth noisy measurements.
        This helps reduce noise while preserving voltage sag characteristics.
        
        Parameters:
        -----------
        window_size : int
            Window size for the smoothing filter (must be odd)
        """
        if window_size % 2 == 0:
            window_size += 1
            
        order = 2  # Polynomial order
        
        # Ensure window size doesn't exceed data length
        if window_size > len(self.voltage):
            window_size = len(self.voltage) // 2 * 2 - 1
            
        self.voltage_smooth = signal.savgol_filter(self.voltage, window_size, order)
        self.current_smooth = signal.savgol_filter(self.current, window_size, order)
        
        print(f"\nApplied Savitzky-Golay smoothing (window={window_size})")
        
    def estimate_voltage_sag(self):
        """
        Estimate voltage sag characteristics by correlating voltage drops with current levels.
        """
        # Calculate voltage deviation (sag)
        self.voltage_sag = self.voltage.max() - self.voltage
        
        # Identify periods of significant current draw
        mean_current = np.mean(self.current)
        high_current_mask = self.current > mean_current * 1.5
        
        if np.any(high_current_mask):
            sag_during_high_current = self.voltage_sag[high_current_mask]
            avg_sag = np.mean(sag_during_high_current)
            max_sag = np.max(sag_during_high_current)
            
            print(f"\nVoltage Sag Analysis:")
            print(f"  Average sag during high current: {avg_sag:.4f}V")
            print(f"  Maximum sag observed: {max_sag:.4f}V")
            print(f"  Percentage of time with high current: {100*np.sum(high_current_mask)/len(high_current_mask):.1f}%")
        
        return self.voltage_sag
    
    def calculate_energy_capacity_basic(self):
        """
        Calculate total energy using basic integration: E = ∫ V × I dt
        
        Returns:
        --------
        float
            Total energy in Watt-hours (Wh)
        """
        # Calculate power at each time step
        power = self.voltage * self.current  # Power in Watts
        
        # Integrate power over time (using trapezoidal rule)
        energy_wh = np.trapz(power, dx=1) / 3600  # Convert Joules to Wh
        
        return energy_wh
    
    def calculate_energy_capacity_smoothed(self):
        """
        Calculate energy using smoothed data to reduce noise effects.
        
        Returns:
        --------
        float
            Total energy in Watt-hours (Wh) using smoothed data
        """
        if self.voltage_smooth is None:
            self.smooth_data()
        
        power_smooth = self.voltage_smooth * self.current_smooth
        energy_wh = np.trapz(power_smooth, dx=1) / 3600
        
        return energy_wh
    
    def calculate_energy_capacity_compensated(self, sag_compensation_factor=1.0):
        """
        Calculate energy with voltage sag compensation.
        
        This accounts for the fact that voltage sag can cause underestimation
        of actual energy capacity by using a compensated voltage reference.
        
        Parameters:
        -----------
        sag_compensation_factor : float
            Factor to apply for voltage sag compensation (0.0-1.0)
            0.0 = use measured voltage only
            1.0 = full compensation using estimated optimal voltage
        
        Returns:
        --------
        float
            Total energy in Watt-hours (Wh)
        """
        # Use maximum observed voltage as reference (minimum sag)
        v_ref = np.percentile(self.voltage, 95)  # Use 95th percentile to avoid noise
        
        # Calculate compensated voltage
        voltage_compensated = self.voltage.copy()
        sag = v_ref - self.voltage
        
        # Apply partial compensation
        if sag_compensation_factor > 0:
            voltage_compensated += sag * sag_compensation_factor * 0.5  # Conservative compensation
        
        power_compensated = voltage_compensated * self.current
        energy_wh = np.trapz(power_compensated, dx=1) / 3600
        
        return energy_wh
    
    def calculate_energy_by_current_range(self):
        """
        Calculate energy by segmenting into current ranges.
        This helps identify voltage sag effects at different load levels.
        
        Returns:
        --------
        dict
            Dictionary with energy estimates for different current ranges
        """
        results = {}
        
        # Define current ranges
        current_ranges = [
            (0, 2, "Low"),
            (2, 5, "Medium"),
            (5, 20, "High"),
            (20, float('inf'), "Very High")
        ]
        
        for i_min, i_max, label in current_ranges:
            mask = (self.current >= i_min) & (self.current < i_max)
            
            if np.sum(mask) > 0:
                energy = np.trapz(
                    self.voltage[mask] * self.current[mask],
                    dx=1
                ) / 3600
                avg_voltage = np.mean(self.voltage[mask])
                avg_current = np.mean(self.current[mask])
                avg_sag = np.percentile(self.voltage, 95) - avg_voltage
                
                results[label] = {
                    'energy_wh': energy,
                    'avg_voltage': avg_voltage,
                    'avg_current': avg_current,
                    'voltage_sag': avg_sag,
                    'data_points': np.sum(mask)
                }
        
        return results
    
    def generate_report(self):
        """Generate a comprehensive energy capacity report."""
        self.smooth_data()
        self.estimate_voltage_sag()
        
        print("\n" + "="*60)
        print("BATTERY ENERGY CAPACITY REPORT")
        print("="*60)
        
        # Calculate estimates using different methods
        energy_basic = self.calculate_energy_capacity_basic()
        energy_smoothed = self.calculate_energy_capacity_smoothed()
        energy_compensated = self.calculate_energy_capacity_compensated()
        
        print(f"\nEnergy Capacity Estimates:")
        print(f"  Basic calculation (raw data):      {energy_basic:.3f} Wh")
        print(f"  Smoothed calculation (filtered):   {energy_smoothed:.3f} Wh")
        print(f"  Voltage sag compensated:           {energy_compensated:.3f} Wh")
        print(f"  Average:                           {np.mean([energy_basic, energy_smoothed, energy_compensated]):.3f} Wh")
        
        # Breakdown by current range
        print(f"\nEnergy Distribution by Current Level:")
        range_results = self.calculate_energy_by_current_range()
        total_energy_ranges = sum(r['energy_wh'] for r in range_results.values())
        
        for label, data in range_results.items():
            percentage = (data['energy_wh'] / total_energy_ranges * 100) if total_energy_ranges > 0 else 0
            print(f"  {label:12s}: {data['energy_wh']:8.3f} Wh ({percentage:5.1f}%) - Avg V: {data['avg_voltage']:.3f}V, Sag: {data['voltage_sag']:.4f}V")
        
        print(f"\n" + "="*60)
        
        return {
            'basic': energy_basic,
            'smoothed': energy_smoothed,
            'compensated': energy_compensated,
            'by_range': range_results
        }
    
    def plot_analysis(self, output_file=None):
        """
        Generate analysis plots.
        
        Parameters:
        -----------
        output_file : str, optional
            Path to save the plot. If None, displays the plot.
        """
        if self.voltage_smooth is None:
            self.smooth_data()
        
        fig, axes = plt.subplots(4, 1, figsize=(12, 10))
        time_axis = np.arange(len(self.voltage))
        
        # Plot 1: Voltage
        axes[0].plot(time_axis, self.voltage, 'b-', alpha=0.5, label='Raw')
        axes[0].plot(time_axis, self.voltage_smooth, 'r-', linewidth=2, label='Smoothed')
        axes[0].axhline(y=np.percentile(self.voltage, 95), color='g', linestyle='--', label='95th percentile')
        axes[0].set_ylabel('Voltage (V)')
        axes[0].set_title('Battery Voltage with Sag')
        axes[0].legend()
        axes[0].grid(True, alpha=0.3)
        
        # Plot 2: Current
        axes[1].plot(time_axis, self.current, 'b-', alpha=0.5, label='Raw')
        axes[1].plot(time_axis, self.current_smooth, 'r-', linewidth=2, label='Smoothed')
        axes[1].set_ylabel('Current (A)')
        axes[1].set_title('Battery Current')
        axes[1].legend()
        axes[1].grid(True, alpha=0.3)
        
        # Plot 3: Power
        power_raw = self.voltage * self.current
        power_smooth = self.voltage_smooth * self.current_smooth
        axes[2].plot(time_axis, power_raw, 'b-', alpha=0.5, label='Raw')
        axes[2].plot(time_axis, power_smooth, 'r-', linewidth=2, label='Smoothed')
        axes[2].set_ylabel('Power (W)')
        axes[2].set_title('Battery Power')
        axes[2].legend()
        axes[2].grid(True, alpha=0.3)
        
        # Plot 4: Voltage Sag vs Current
        axes[3].scatter(self.current, self.voltage_sag, alpha=0.5, s=10)
        axes[3].set_xlabel('Current (A)')
        axes[3].set_ylabel('Voltage Sag (V)')
        axes[3].set_title('Voltage Sag vs Current (showing sag characteristics)')
        axes[3].grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=150, bbox_inches='tight')
            print(f"\nPlot saved to: {output_file}")
        else:
            plt.show()


def main():
    """Main function to run the battery energy capacity estimator."""
    
    # Specify the CSV file path
    csv_file = Path("DynoResult1.csv")
    
    # Check if file exists
    if not csv_file.exists():
        print(f"Error: File not found at {csv_file}")
        print("Please ensure the file path is correct.")
        return
    
    # Create estimator and generate report
    estimator = BatteryEnergyEstimator(csv_file)
    results = estimator.generate_report()
    
    # Generate visualization
    plot_file = Path(csv_file.parent) / "battery_analysis.png"
    estimator.plot_analysis(output_file=str(plot_file))
    
    # Save results to CSV
    summary_file = Path(csv_file.parent) / "energy_capacity_summary.csv"
    summary_data = {
        'Estimation Method': ['Basic (Raw)', 'Smoothed (Filtered)', 'Voltage Sag Compensated', 'Average'],
        'Energy (Wh)': [
            results['basic'],
            results['smoothed'],
            results['compensated'],
            np.mean([results['basic'], results['smoothed'], results['compensated']])
        ]
    }
    summary_df = pd.DataFrame(summary_data)
    summary_df.to_csv(summary_file, index=False)
    print(f"Summary saved to: {summary_file}")


if __name__ == "__main__":
    main()
