"""
Power Electronics Delay – Sensor Analysis
Signal chain: Isense(nonIso) -> Isense(iso) -> Isense(LP)

Columns: CH_1_s, Isense(nonIso)_mv, Isense(iso)_mv, Isense(Low Pass)_mv, PWM_Out_mv
"""

import csv
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
from scipy.signal import butter, sosfilt, sosfilt_zi

# ── Config ──────────────────────────────────────────────────────────────────────
FILE             = Path(__file__).parent / "PartA.csv"
PROBE_CH1        = 10       # Isense(nonIso) — 10X probe
PROBE_CH3        = 10       # Isense(LP)     — 10X probe
SENSOR_MV_PER_A  = 50       # nonIso sensitivity (mV/A)
PWM_THRESHOLD_MV = 2500     # PWM edge detection mid-point (mV)
EDGE_BLANK_NS    = 400      # samples to blank either side of each PWM edge
LP_CUTOFF_HZ     = 5000     # software low-pass filter cutoff frequency (Hz)
# ────────────────────────────────────────────────────────────────────────────────


# ── Load ─────────────────────────────────────────────────────────────────────────
def load(path):
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader)
        data = np.array([[float(v) for v in row] for row in reader])
    return data


# ── Edge masking ──────────────────────────────────────────────────────────────────
def edge_mask(pwm_mv, threshold_mv, blank_ns, dt_s):
    """Boolean mask — False within ±blank_ns of any PWM edge (rising or falling)."""
    blank = int(np.ceil(blank_ns * 1e-9 / dt_s))
    above = pwm_mv > threshold_mv
    edges = np.where(above[:-1] != above[1:])[0] + 1
    mask = np.ones(len(pwm_mv), dtype=bool)
    for idx in edges:
        mask[max(0, idx - blank): idx + blank + 1] = False
    return mask


# ── Low-pass filter ───────────────────────────────────────────────────────────────
def lowpass(signal, cutoff_hz, fs_hz, order=1):
    sos = butter(order, cutoff_hz / (fs_hz / 2), btype="low", output="sos")
    zi = sosfilt_zi(sos) * signal[0]   # initialise at DC level of first sample
    y, _ = sosfilt(sos, signal, zi=zi)
    return y


# ── Main ──────────────────────────────────────────────────────────────────────────
def main():
    data = load(FILE)
    time_s        = data[:, 0]
    dt_s          = np.median(np.diff(time_s))
    fs_hz         = 1.0 / dt_s

    # Apply probe factors; convert nonIso to Amperes
    noniso_a  = data[:, 1] * PROBE_CH1 / SENSOR_MV_PER_A   # A
    iso_mv    = data[:, 2]                                   # mV (gain unknown)
    lp_mv     = data[:, 3] * PROBE_CH3                      # mV
    pwm_mv    = data[:, 4]                                   # mV

    print(f"Loaded {len(data):,} samples  |  fs = {fs_hz/1e6:.0f} MSa/s")

    # ── 1. Edge mask ──────────────────────────────────────────────────────────
    mask = edge_mask(pwm_mv, PWM_THRESHOLD_MV, EDGE_BLANK_NS, dt_s)
    print(f"Edge mask: {mask.sum():,} / {len(mask):,} samples kept "
          f"({100*mask.mean():.1f}%) after blanking ±{EDGE_BLANK_NS} ns")

    # ── 2. Gain of Isense(iso): linear regression iso_mv = gain*noniso_a + offset
    x = noniso_a[mask]
    y = iso_mv[mask]
    A = np.column_stack([x, np.ones_like(x)])
    (gain_iso, offset_iso), *_ = np.linalg.lstsq(A, y, rcond=None)
    iso_pred = gain_iso * noniso_a + offset_iso
    rmse_iso = np.sqrt(np.mean((iso_mv[mask] - iso_pred[mask]) ** 2))
    print(f"\nIsense(iso) gain:  {gain_iso:.4f} mV/A  |  offset: {offset_iso:.2f} mV  |  RMSE: {rmse_iso:.3f} mV")

    # ── 3. Software low-pass filter on nonIso, compare with Isense(LP)
    noniso_lp_mv  = lowpass(noniso_a, LP_CUTOFF_HZ, fs_hz) * SENSOR_MV_PER_A  # back to mV for comparison
    # Scale factor between filtered nonIso and measured LP
    x2 = noniso_lp_mv[mask]
    y2 = lp_mv[mask]
    A2 = np.column_stack([x2, np.ones_like(x2)])
    (gain_lp, offset_lp), *_ = np.linalg.lstsq(A2, y2, rcond=None)
    lp_pred = gain_lp * noniso_lp_mv + offset_lp
    rmse_lp = np.sqrt(np.mean((lp_mv[mask] - lp_pred[mask]) ** 2))
    print(f"Isense(LP)  gain:  {gain_lp:.4f} mV/mV  |  offset: {offset_lp:.2f} mV  |  RMSE: {rmse_lp:.3f} mV")

    # ── Plots ─────────────────────────────────────────────────────────────────
    time_us = time_s * 1e6

    # Fig 1: raw signals overview
    fig, axes = plt.subplots(4, 1, figsize=(14, 9), sharex=True)
    for ax, sig, lbl, unit, col in zip(axes,
            [noniso_a, iso_mv, lp_mv, pwm_mv],
            ["Isense(nonIso)", "Isense(iso)", "Isense(LP)", "PWM_Out"],
            ["A", "mV", "mV", "mV"],
            ["tab:blue", "tab:orange", "tab:green", "tab:red"]):
        ax.plot(time_us, sig, color=col, linewidth=0.4)
        ax.set_ylabel(f"{lbl}\n({unit})", fontsize=8)
        ax.grid(True, linewidth=0.3)
    axes[-1].set_xlabel("Time (µs)")
    fig.suptitle("Raw signals (probe-corrected)", fontsize=11)
    fig.tight_layout()

    # Fig 2: nonIso → iso mapping (scatter + fit)
    fig2, ax2 = plt.subplots(figsize=(7, 5))
    ax2.scatter(noniso_a[~mask], iso_mv[~mask], s=1, color="lightgrey", label="blanked (edge ringing)")
    ax2.scatter(noniso_a[mask],  iso_mv[mask],  s=1, color="tab:orange", alpha=0.3, label="data used for fit")
    x_line = np.linspace(noniso_a[mask].min(), noniso_a[mask].max(), 500)
    ax2.plot(x_line, gain_iso * x_line + offset_iso, color="k", linewidth=1.5,
             label=f"fit: {gain_iso:.2f} mV/A  +  {offset_iso:.1f} mV\nRMSE = {rmse_iso:.2f} mV")
    ax2.set_xlabel("Isense(nonIso)  (A)")
    ax2.set_ylabel("Isense(iso)  (mV)")
    ax2.set_title("nonIso → iso gain")
    ax2.legend(fontsize=9, markerscale=6)
    ax2.grid(True, linewidth=0.3)
    fig2.tight_layout()

    # Fig 3: software LP vs measured LP
    fig3, axes3 = plt.subplots(2, 1, figsize=(14, 7), sharex=True)
    axes3[0].plot(time_us, lp_mv,        color="tab:green",  linewidth=0.5, label="Isense(LP) measured")
    axes3[0].plot(time_us, noniso_lp_mv, color="tab:blue",   linewidth=0.5, alpha=0.7,
                  label=f"nonIso filtered @ {LP_CUTOFF_HZ} Hz (scaled to mV)")
    axes3[0].set_ylabel("mV")
    axes3[0].legend(fontsize=8)
    axes3[0].grid(True, linewidth=0.3)
    axes3[0].set_title("Software LP vs measured LP channel")

    residual = lp_mv - lp_pred
    axes3[1].plot(time_us, residual, color="tab:red", linewidth=0.4)
    axes3[1].axhline(0, color="k", linewidth=0.5)
    axes3[1].set_ylabel("Residual (mV)")
    axes3[1].set_xlabel("Time (µs)")
    axes3[1].set_title(f"Residual  (RMSE = {rmse_lp:.2f} mV  on non-blanked samples)")
    axes3[1].grid(True, linewidth=0.3)
    fig3.tight_layout()

    plt.show()


if __name__ == "__main__":
    main()
