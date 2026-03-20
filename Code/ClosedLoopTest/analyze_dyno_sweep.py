#!/usr/bin/env python3
"""
analyze_dyno_sweep.py — Quick analysis and plotting of dyno sweep results

Generates summary statistics and plots from saved CSV files.
"""

import pandas as pd
import argparse
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("Install matplotlib: pip install matplotlib numpy")
    sys.exit(1)


def analyze_file(filepath):
    """Load and analyze a dyno sweep CSV file."""

    try:
        df = pd.read_csv(filepath)
    except FileNotFoundError:
        print(f"File not found: {filepath}")
        sys.exit(1)

    print(f"\n{'='*70}")
    print(f"Analysis of: {Path(filepath).name}")
    print(f"{'='*70}\n")

    print(f"Total rows: {len(df)}")
    print(f"Columns: {', '.join(df.columns)}\n")

    # Check what we have
    has_phase = 'Phase' in df.columns
    has_repeat = 'Repeat' in df.columns
    has_dyno = 'Dyno_Speed_RPM' in df.columns
    has_current = 'CurrentLimit_A' in df.columns

    # Summary by repeat
    if has_repeat:
        print("Summary by Repeat:")
        print("-" * 70)
        for rep in sorted(df['Repeat'].unique()):
            data = df[df['Repeat'] == rep]
            print(f"\nRepeat {int(rep)}:")
            print(f"  Samples: {len(data)}")
            if 'Power' in df.columns:
                print(f"  Power:  {data['Power'].mean():.1f}W ± {data['Power'].std():.1f}W "
                      f"(max {data['Power'].max():.1f}W)")
            if 'Current' in df.columns:
                print(f"  Current: {data['Current'].mean():.2f}A ± {data['Current'].std():.2f}A")
            if has_dyno:
                max_speed = data['Dyno_Speed_RPM'].max()
                print(f"  Max Speed: {max_speed:.0f} RPM")
                if has_phase:
                    accel = data[data['Phase'] == 'Accelerating']
                    coast = data[data['Phase'] == 'Coasting']
                    if len(accel) > 0:
                        print(f"    - Accel duration: {len(accel)} samples")
                    if len(coast) > 0:
                        print(f"    - Coast duration: {len(coast)} samples")

    # Summary by phase
    if has_phase:
        print("\n\nSummary by Phase:")
        print("-" * 70)
        for phase in df['Phase'].unique():
            data = df[df['Phase'] == phase]
            print(f"\n{phase}:")
            print(f"  Samples: {len(data)}")
            if 'Power' in df.columns:
                print(f"  Power:  {data['Power'].mean():.1f}W ± {data['Power'].std():.1f}W")
            if 'Current' in df.columns:
                print(f"  Current: {data['Current'].mean():.2f}A ± {data['Current'].std():.2f}A")
            if 'Temperature' in df.columns:
                print(f"  Temp:   {data['Temperature'].mean():.1f}°C ± {data['Temperature'].std():.1f}°C")

    # Overall statistics
    print("\n\nOverall Statistics:")
    print("-" * 70)
    numeric_cols = df.select_dtypes(include=['float64', 'int64']).columns
    print(df[numeric_cols].describe().to_string())

    # Generate plots
    print("\n\nGenerating plots...")
    _generate_plots(df, filepath)


def _generate_plots(df, filepath):
    """Generate and save representative plots."""

    base_name = Path(filepath).stem
    has_phase = 'Phase' in df.columns
    has_repeat = 'Repeat' in df.columns
    has_dyno = 'Dyno_Speed_RPM' in df.columns

    # Plot 1: Power by repeat (if available)
    if has_repeat and 'Power' in df.columns:
        plt.figure(figsize=(12, 6))
        for rep in sorted(df['Repeat'].unique()):
            data = df[df['Repeat'] == rep]
            plt.plot(data.index, data['Power'], label=f'Repeat {int(rep)}', alpha=0.7)
        plt.xlabel('Sample')
        plt.ylabel('Power (W)')
        plt.title('Electrical Power by Repeat')
        plt.legend()
        plt.grid(alpha=0.3)
        fname = f"{base_name}_power_by_repeat.png"
        plt.savefig(fname, dpi=100, bbox_inches='tight')
        print(f"  → {fname}")
        plt.close()

    # Plot 2: Phase comparison (box plot)
    if has_phase:
        fig, axes = plt.subplots(2, 2, figsize=(12, 10))
        fig.suptitle('Accelerating vs Coasting Phases')

        cols = ['Power', 'Current', 'Temperature', 'Voltage']
        ylabels = ['Power (W)', 'Current (A)', 'Temperature (°C)', 'Voltage (V)']

        for idx, (col, ylabel) in enumerate(zip(cols, ylabels)):
            ax = axes[idx // 2, idx % 2]
            if col in df.columns:
                accel = df[df['Phase'] == 'Accelerating'][col]
                coast = df[df['Phase'] == 'Coasting'][col]
                ax.boxplot([accel.dropna(), coast.dropna()], labels=['Accel', 'Coast'])
                ax.set_ylabel(ylabel)
                ax.grid(alpha=0.3)

        fname = f"{base_name}_phase_comparison.png"
        plt.savefig(fname, dpi=100, bbox_inches='tight')
        print(f"  → {fname}")
        plt.close()

    # Plot 3: Dyno speed (if available)
    if has_dyno:
        plt.figure(figsize=(12, 6))
        if has_repeat:
            for rep in sorted(df['Repeat'].unique()):
                data = df[df['Repeat'] == rep]
                plt.plot(data.index, data['Dyno_Speed_RPM'], label=f'Repeat {int(rep)}', alpha=0.7)
        else:
            plt.plot(df.index, df['Dyno_Speed_RPM'])
        plt.xlabel('Sample')
        plt.ylabel('Dyno Speed (RPM)')
        plt.title('Dyno Speed Profile')
        if has_repeat:
            plt.legend()
        plt.grid(alpha=0.3)
        fname = f"{base_name}_dyno_speed.png"
        plt.savefig(fname, dpi=100, bbox_inches='tight')
        print(f"  → {fname}")
        plt.close()

    # Plot 4: Time-series energy
    if 'Energy' in df.columns:
        plt.figure(figsize=(12, 6))
        if has_repeat:
            for rep in sorted(df['Repeat'].unique()):
                data = df[df['Repeat'] == rep]
                plt.plot(data.index, data['Energy'], label=f'Repeat {int(rep)}', alpha=0.7)
        else:
            plt.plot(df.index, df['Energy'])
        plt.xlabel('Sample')
        plt.ylabel('Energy (J)')
        plt.title('Cumulative Energy by Repeat')
        if has_repeat:
            plt.legend()
        plt.grid(alpha=0.3)
        fname = f"{base_name}_energy.png"
        plt.savefig(fname, dpi=100, bbox_inches='tight')
        print(f"  → {fname}")
        plt.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Analyze DynoSweep Results')
    parser.add_argument('file', type=str, help='CSV file to analyze')
    args = parser.parse_args()

    analyze_file(args.file)
