"""
PartB – Switching Event Visualisation
One comparator-triggered switching event, showing ~2 full PWM periods.
Subplot 1: Isense(iso) vs DAC threshold
Subplot 2: Comparator & PWM output
Time zeroed at the PWM falling edge (switch turn-off).
Sized for an A1 poster at ~40 mm × 30 mm.
"""

import csv
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────────
FILE    = Path(__file__).parent / "PartB.csv"
PRE_US  = 10.0   # µs before PWM falling edge (shows on-time current ramp)
POST_US = 50.0   # µs after (~2 full PWM periods)

# Poster output dimensions (mm → inches)
FIG_W_MM, FIG_H_MM = 200, 100
DPI = 600
# ─────────────────────────────────────────────────────────────────────────────


def load(path):
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader)
        data = np.array([[float(v) for v in row] for row in reader])
    return data


def main():
    data = load(FILE)
    time_s    = data[:, 0]
    dac_mv    = data[:, 5]
    isense_mv = data[:, 2]
    comp_mv   = data[:, 6]
    pwm_mv    = data[:, 4]

    time_us  = time_s * 1e6
    dt_us    = np.median(np.diff(time_us))
    pwm_mid  = (pwm_mv.max()  + pwm_mv.min())  / 2
    comp_mid = (comp_mv.max() + comp_mv.min()) / 2

    above   = pwm_mv > pwm_mid
    falling = np.where(above[:-1] & ~above[1:])[0] + 1

    # Comparator-triggered events only (comp LOW at PWM turn-off)
    comp_trig = np.array([off for off in falling if comp_mv[off] < comp_mid])
    if len(comp_trig) == 0:
        print("No comp-triggered events found.")
        return

    # Pick one event from the middle of the dataset
    ev_idx = comp_trig[len(comp_trig) // 2]
    t_ev   = time_us[ev_idx]
    print(f"Showing event at t = {t_ev:.2f} µs")

    pre_s  = int(PRE_US  / dt_us)
    post_s = int(POST_US / dt_us)
    i0     = max(0, ev_idx - pre_s)
    i1     = min(len(time_us) - 1, ev_idx + post_s)
    lev    = ev_idx - i0

    t   = time_us[i0:i1] - t_ev
    ise = isense_mv[i0:i1]
    dac = dac_mv[i0:i1]
    cmp = comp_mv[i0:i1]
    pwm = pwm_mv[i0:i1]

    # ── Isense crossing above DAC (smoothed, last before PWM falls) ───────────
    k      = 10
    ise_sm = np.convolve(ise[:lev], np.ones(k) / k, mode="same")
    dac_sm = np.convolve(dac[:lev], np.ones(k) / k, mode="same")
    below  = ise_sm < dac_sm
    rise_ix  = np.where(below[:-1] & ~below[1:])[0] + 1
    isense_t = t[rise_ix[-1]] if len(rise_ix) else float("nan")

    # ── Comparator 50% crossing after Isense crossing ─────────────────────────
    sf      = rise_ix[-1] if len(rise_ix) else 0
    comp_hi = cmp[sf:] > comp_mid
    drop_ix = np.where(comp_hi[:-1] & ~comp_hi[1:])[0]
    comp_t  = t[sf + drop_ix[0] + 1] if len(drop_ix) else float("nan")

    print(f"Isense > DAC: {isense_t*1e3:.0f} ns  |  Comp 50%: {comp_t*1e3:.0f} ns")

    # ── Poster-sized plot ─────────────────────────────────────────────────────
    # All sizes scaled for legibility at 40 mm × 30 mm print.
    MM = 1 / 25.4
    LW   = 2    # pt — main signal line width
    FS   = 13    # pt — base font size
    FS_L = 13    # pt — legend font size

    plt.rcParams.update({
        "font.size":        FS,
        "axes.labelsize":   FS,
        "xtick.labelsize":  FS - 0.5,
        "ytick.labelsize":  FS - 0.5,
        "legend.fontsize":  FS_L,
        "axes.linewidth":   0.4,
        "xtick.major.width": 0.4,
        "ytick.major.width": 0.4,
        "xtick.major.size":  2,
        "ytick.major.size":  2,
        "grid.linewidth":   0.25,
        "grid.alpha":       0.5,
    })

    fig, (ax1, ax2) = plt.subplots(
        2, 1,
        figsize=(FIG_W_MM * MM, FIG_H_MM * MM),
        sharex=True,
        constrained_layout=True,
    )

    # ── Subplot 1: Isense + DAC ───────────────────────────────────────────────
    ax1.plot(t, ise, color="tab:orange", lw=LW,          label="$I_{sense}$")
    ax1.plot(t, dac, color="tab:blue",   lw=LW, ls="--", label="DAC")

    sig_lo = min(np.percentile(ise, 2),  np.percentile(dac, 2))
    sig_hi = max(np.percentile(ise, 98), np.percentile(dac, 98))
    m = (sig_hi - sig_lo) * 0.15 + 50
    ax1.set_ylim(0, sig_hi + m)
    ax1.legend(loc="lower right", framealpha=0.8, handlelength=1.0,
               borderpad=0.2, labelspacing=0.1)
    ax1.grid(True)
    ax1.yaxis.set_major_locator(ticker.MaxNLocator(nbins=3, integer=True))
    plt.setp(ax1.get_xticklabels(), visible=False)

    # ── Subplot 2: Comparator + PWM ───────────────────────────────────────────
    ax2.plot(t, cmp, color="tab:red",   lw=LW, label="Comparator")
    ax2.plot(t, pwm, color="tab:green", lw=LW, label="Gate Drive")

    bot_lo = min(np.percentile(cmp, 3), np.percentile(pwm, 3)) - 200
    bot_hi = max(np.percentile(cmp, 97), np.percentile(pwm, 97)) + 200
    ax2.set_ylim(bot_lo, bot_hi)
    ax2.set_xlabel("Time relative to PWM falling edge (µs)")
    ax2.legend(loc="center right", framealpha=0.8, handlelength=1.0,
               borderpad=0.2, labelspacing=0.1)
    ax2.grid(True)
    # Format bottom y-ticks as "Xk" to save horizontal space
    ax2.yaxis.set_major_locator(ticker.MaxNLocator(nbins=3, integer=True))
    ax2.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda v, _: f"{v/1000:.0f}k" if abs(v) >= 1000 else f"{v:.0f}"
    ))
    ax2.xaxis.set_major_locator(ticker.MultipleLocator(20))

    # Single shared y-axis label
    fig.supylabel("Voltage (mV)", fontsize=FS)
    fig.suptitle("Comparator-Triggered Switching Event", fontsize=16, fontweight="bold")    

    out = Path(__file__).parent / "switching_events.png"
    plt.savefig(out, dpi=DPI, bbox_inches="tight")
    print(f"Saved {out}  ({FIG_W_MM}×{FIG_H_MM} mm @ {DPI} dpi)")
    plt.show()


if __name__ == "__main__":
    main()
