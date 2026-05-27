import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

np.random.seed(42)

fig = plt.figure(figsize=(10, 5))
gs = gridspec.GridSpec(3, 2, figure=fig, wspace=0.45, hspace=0.2,
                       left=0.08, right=0.95, top=0.9, bottom=0.08,
                       height_ratios=[1, 1, 2])

ax_left = fig.add_subplot(gs[:, 0])
ax_cur  = fig.add_subplot(gs[0, 1])
ax_out  = fig.add_subplot(gs[1:, 1])

# ─── Left: No Hysteresis ──────────────────────────────────────
t1        = np.linspace(0, 1, 8000)
vref      = 2.5
noise_amp = 0.12
signal    = 3.8 - 2.6 * t1 + noise_amp * np.random.randn(len(t1))
vout      = np.where(signal > vref, 3.3, 0.0)

ax_left.plot(t1, vout, color='steelblue', linewidth=0.7)
ax_left.set_ylim(-0.5, 4.0)
ax_left.set_yticks([0, 3.3])
ax_left.set_yticklabels(['LOW', 'HIGH'], fontsize=9)
ax_left.set_ylabel('$V_{out}$', fontsize=10)
ax_left.set_xlabel('Time', fontsize=9)
ax_left.set_xticks([])
ax_left.set_title('No Hysteresis', fontweight='bold', fontsize=11)
ax_left.grid(True, alpha=0.2)

near = np.where((signal > vref - noise_amp * 2.5) & (signal < vref + noise_amp * 2.5))[0]
if len(near):
    ax_left.axvspan(t1[near[0]], t1[near[-1]], alpha=0.12, color='red')

# ─── Right: No Latching with PWM ──────────────────────────────
I_on   = 2.1
I_off  = 1.9
n      = 20000
t2     = np.linspace(0, 1, n)
dt     = t2[1] - t2[0]
slope_up      =  5.0
slope_down    = -4.0    # comparator tripped (within PWM high)
slope_pwm_off = -1.5    # freewheeling during PWM low

# ~2.5 PWM periods across the time window
pwm_period = 0.4
pwm = ((t2 % pwm_period) < (0.6 * pwm_period)).astype(float)

current   = np.zeros(n)
lout      = np.zeros(n)
comp_high = False
I         = I_off + 0.01

for i in range(n):
    current[i] = I

    if I >= I_on:
        comp_high = True
    elif I <= I_off:
        comp_high = False

    lout[i] = 1.0 if comp_high else 0.0

    switch_on = bool(pwm[i]) and not comp_high
    if switch_on:
        I += slope_up * dt
    elif bool(pwm[i]):          # PWM high but comparator tripped
        I += slope_down * dt
    else:                       # PWM low — freewheeling
        I += slope_pwm_off * dt
    I = max(I, 0.0)

# Current — small top subplot
ax_cur.plot(t2, current, color='darkorange', linewidth=0.8)
ax_cur.axhline(I_on,  color='crimson', linestyle='--', linewidth=0.9)
ax_cur.axhline(I_off, color='navy',    linestyle='--', linewidth=0.9)
ax_cur.set_ylabel('Current (A)', fontsize=8)
ax_cur.set_ylim(1.3, 2.4)
ax_cur.set_yticks([I_off, I_on])
ax_cur.set_yticklabels([f'{I_off}', f'{I_on}'], fontsize=7)
ax_cur.set_xticks([])
ax_cur.grid(True, alpha=0.2)
ax_cur.set_title('No Latching', fontweight='bold', fontsize=11)

# Output + PWM — large bottom subplot (same axis)
ax_out.fill_between(t2, pwm, step='post', alpha=0.15, color='slateblue')
ax_out.step(t2, pwm,  where='post', color='slateblue', linewidth=0.9,
            label='PWM (60%)', linestyle='--')
ax_out.step(t2, lout, where='post', color='seagreen',  linewidth=1.1,
            label='Output')
ax_out.fill_between(t2, lout, step='post', alpha=0.18, color='seagreen')
ax_out.set_ylabel('Signal', fontsize=10)
ax_out.set_ylim(-0.3, 1.5)
ax_out.set_yticks([0, 1])
ax_out.set_yticklabels(['LOW', 'HIGH'], fontsize=9)
ax_out.set_xlabel('Time', fontsize=9)
ax_out.set_xticks([])
ax_out.legend(fontsize=8, loc='upper right')
ax_out.grid(True, alpha=0.2)

plt.savefig('comparator_effects.png', dpi=180, bbox_inches='tight')
print("Saved comparator_effects.png")
plt.show()
