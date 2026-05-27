"""
Motor Efficiency Investigation — Switching Frequency Comparison
Compares efficiency at 5 kHz, 10 kHz, 30 kHz, 45 kHz, and 60 kHz
under nearly identical operating conditions.
"""

import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from scipy.interpolate import interp1d

# ── Configuration ─────────────────────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

DATASETS = {
    "5 kHz":  "duty_sweep_results_20260226-133023.csv",
    "10 kHz": "duty_sweep_results_20260226-132431.csv",
    "30 kHz": "duty_sweep_results_20260226-131941.csv",
    "45 kHz": "duty_sweep_results_20260226-133459.csv",
    "60 kHz": "duty_sweep_results_20260226-133929.csv",
}

# Minimum RPM to consider the motor as loaded (filter out unspun rows)
MIN_RPM = 2.0
# Minimum output power threshold (W) to trust the dyno reading
MIN_OUTPUT_W = 0.5

COLORS = {
    "5 kHz":  "#1f77b4",
    "10 kHz": "#ff7f0e",
    "30 kHz": "#2ca02c",
    "45 kHz": "#d62728",
    "60 kHz": "#9467bd",
}

STYLE = dict(marker="o", markersize=5, linewidth=1.8)


# ── Load data ─────────────────────────────────────────────────────────────────
dfs: dict[str, pd.DataFrame] = {}
for label, fname in DATASETS.items():
    path = os.path.join(BASE_DIR, fname)
    df = pd.read_csv(path)
    # Efficiency is only meaningful when the motor is spinning with load
    df_spin = df[(df["Dyno_RPM"] >= MIN_RPM) & (df["Dyno_Power_W"] >= MIN_OUTPUT_W) & (df["Power_W"] > 0)].copy()
    df_spin["Efficiency_%"] = (df_spin["Dyno_Power_W"] / df_spin["Power_W"]) * 100.0
    df_spin["Loss_W"] = df_spin["Power_W"] - df_spin["Dyno_Power_W"]
    dfs[label] = df_spin
    print(f"{label}: {len(df_spin)} loaded operating points  "
          f"(peak η = {df_spin['Efficiency_%'].max():.1f}%, "
          f"V_bus = {df.loc[df.index[len(df)//2], 'Voltage_V']:.2f} V)")


# ── Figure 1 — Efficiency vs Duty Cycle ───────────────────────────────────────
fig1, ax1 = plt.subplots(figsize=(9, 5.5))
for label, df in dfs.items():
    ax1.plot(df["SetDutyCycle"], df["Efficiency_%"],
             label=label, color=COLORS[label], **STYLE)

ax1.set_xlabel("Set Duty Cycle (%)", fontsize=12)
ax1.set_ylabel("Efficiency (%)", fontsize=12)
ax1.set_title("Motor Efficiency vs Duty Cycle — Switching Frequency Comparison", fontsize=13)
ax1.legend(title="Switching Freq.", fontsize=10)
ax1.grid(True, alpha=0.35)
ax1.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=0))
ax1.set_ylim(bottom=0)
plt.tight_layout()


# ── Figure 2 — Efficiency vs Shaft Speed ─────────────────────────────────────
fig2, ax2 = plt.subplots(figsize=(9, 5.5))
for label, df in dfs.items():
    ax2.plot(df["Dyno_RPM"], df["Efficiency_%"],
             label=label, color=COLORS[label], **STYLE)

ax2.set_xlabel("Shaft Speed (RPM)", fontsize=12)
ax2.set_ylabel("Efficiency (%)", fontsize=12)
ax2.set_title("Motor Efficiency vs Shaft Speed — Switching Frequency Comparison", fontsize=13)
ax2.legend(title="Switching Freq.", fontsize=10)
ax2.grid(True, alpha=0.35)
ax2.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=0))
ax2.set_ylim(bottom=0)
plt.tight_layout()


# ── Figure 3 — Input vs Output Power (Sankey proxy) ──────────────────────────
fig3, ax3 = plt.subplots(figsize=(9, 5.5))
for label, df in dfs.items():
    ax3.plot(df["Power_W"], df["Dyno_Power_W"],
             label=label, color=COLORS[label], **STYLE)

# Ideal 100 % efficiency line
max_p = max(df["Power_W"].max() for df in dfs.values())
ax3.plot([0, max_p], [0, max_p], "k--", linewidth=1, label="Ideal (100 %)")

ax3.set_xlabel("Input Power (W)", fontsize=12)
ax3.set_ylabel("Output Shaft Power (W)", fontsize=12)
ax3.set_title("Input vs Output Power — Switching Frequency Comparison", fontsize=13)
ax3.legend(title="Switching Freq.", fontsize=10)
ax3.grid(True, alpha=0.35)
plt.tight_layout()


# ── Figure 4 — Total Losses vs Duty Cycle ────────────────────────────────────
fig4, ax4 = plt.subplots(figsize=(9, 5.5))
for label, df in dfs.items():
    ax4.plot(df["SetDutyCycle"], df["Loss_W"],
             label=label, color=COLORS[label], **STYLE)

ax4.set_xlabel("Set Duty Cycle (%)", fontsize=12)
ax4.set_ylabel("Total Losses (W)", fontsize=12)
ax4.set_title("Total Drive Losses vs Duty Cycle — Switching Frequency Comparison", fontsize=13)
ax4.legend(title="Switching Freq.", fontsize=10)
ax4.grid(True, alpha=0.35)
plt.tight_layout()


# ── Figure 5 — Efficiency vs Torque ─────────────────────────────────────────
fig5, ax5 = plt.subplots(figsize=(9, 5.5))
for label, df in dfs.items():
    ax5.plot(df["Dyno_Torque_Nm"], df["Efficiency_%"],
             label=label, color=COLORS[label], **STYLE)

ax5.set_xlabel("Shaft Torque (Nm)", fontsize=12)
ax5.set_ylabel("Efficiency (%)", fontsize=12)
ax5.set_title("Motor Efficiency vs Torque — Switching Frequency Comparison", fontsize=13)
ax5.legend(title="Switching Freq.", fontsize=10)
ax5.grid(True, alpha=0.35)
ax5.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=0))
ax5.set_ylim(bottom=0)
plt.tight_layout()


# ── Figure 6 — Efficiency at Common Duty-Cycle Points (bar chart) ─────────────
# Find duty-cycle values that appear in ALL datasets
common_dc = None
for df in dfs.values():
    pts = set(df["SetDutyCycle"].round(1).values)
    common_dc = pts if common_dc is None else common_dc & pts

common_dc = sorted(common_dc)
print(f"\nCommon duty-cycle operating points: {common_dc}")

bar_data = {label: [] for label in DATASETS}
for label, df in dfs.items():
    for dc in common_dc:
        row = df[df["SetDutyCycle"].round(1) == dc]
        val = row["Efficiency_%"].mean() if len(row) > 0 else np.nan
        bar_data[label].append(val)

x = np.arange(len(common_dc))
width = 0.16
fig6, ax6 = plt.subplots(figsize=(12, 5.5))
for i, (label, vals) in enumerate(bar_data.items()):
    offset = (i - 2) * width
    bars = ax6.bar(x + offset, vals, width, label=label,
                   color=COLORS[label], alpha=0.85, edgecolor="white")

ax6.set_xlabel("Set Duty Cycle (%)", fontsize=12)
ax6.set_ylabel("Efficiency (%)", fontsize=12)
ax6.set_title("Efficiency at Common Operating Points — Switching Frequency Comparison", fontsize=13)
ax6.set_xticks(x)
ax6.set_xticklabels([f"{int(d)}%" for d in common_dc])
ax6.legend(title="Switching Freq.", fontsize=10)
ax6.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=0))
ax6.set_ylim(0, 100)
ax6.grid(True, axis="y", alpha=0.35)
plt.tight_layout()


# ── Figure 7 — Motor Current and Torque vs Duty Cycle (dual axis) ─────────────
fig7, ax7a = plt.subplots(figsize=(9, 5.5))
ax7b = ax7a.twinx()

for label, df in dfs.items():
    ax7a.plot(df["SetDutyCycle"], df["MotorCurrent_A"],
              label=label, color=COLORS[label], linestyle="-", **STYLE)
    ax7b.plot(df["SetDutyCycle"], df["Dyno_Torque_Nm"],
              color=COLORS[label], linestyle="--", marker="s", markersize=4, linewidth=1.4)

# Proxy lines for legend (solid=current, dashed=torque)
from matplotlib.lines import Line2D
legend_freq  = [Line2D([0], [0], color=COLORS[l], **STYLE, label=l) for l in DATASETS]
legend_style = [
    Line2D([0], [0], color="grey", linestyle="-",  marker="o", linewidth=1.8, markersize=5, label="Motor Current"),
    Line2D([0], [0], color="grey", linestyle="--", marker="s", linewidth=1.4, markersize=4, label="Torque"),
]
ax7a.legend(handles=legend_freq + legend_style, title="Freq / Quantity", fontsize=9, loc="upper left")

ax7a.set_xlabel("Set Duty Cycle (%)", fontsize=12)
ax7a.set_ylabel("Motor Phase Current (A)", fontsize=12, color="black")
ax7b.set_ylabel("Shaft Torque (Nm)", fontsize=12, color="black")
ax7a.set_title("Motor Current & Torque vs Duty Cycle — Switching Frequency Comparison", fontsize=13)
ax7a.grid(True, alpha=0.35)
plt.tight_layout()


# ── Summary statistics table ──────────────────────────────────────────────────
print("\n── Efficiency Summary ──────────────────────────────────────────────────")
print(f"{'Freq':<10} {'Peak η (%)':>12} {'@ Duty (%)':>12} {'Mean η (%)':>12} {'Avg Loss (W)':>14} {'Avg Vbus (V)':>14}")
print("-" * 68)

for label, df in dfs.items():
    peak_idx  = df["Efficiency_%"].idxmax()
    peak_eta  = df.loc[peak_idx, "Efficiency_%"]
    peak_dc   = df.loc[peak_idx, "SetDutyCycle"]
    mean_eta  = df["Efficiency_%"].mean()
    mean_loss = df["Loss_W"].mean()
    mean_vbus = df["Voltage_V"].mean() if "Voltage_V" in df.columns else float("nan")
    print(f"{label:<10} {peak_eta:>12.1f} {peak_dc:>12.0f} {mean_eta:>12.1f} {mean_loss:>14.2f} {mean_vbus:>14.3f}")

plt.show()
