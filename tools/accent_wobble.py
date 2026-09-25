"""Where does the emulated Accent wobble?  F0 and level every 10 ms through a phrase, next to
the phoneme frames the driver wrote.  Our own render only (no external recording)."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from hosts.accent import Accent          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
TEXT = sys.argv[1] if len(sys.argv) > 1 else "Have you ever heard a real computer?"
SR = 44100


def f0(x):
    x = x - x.mean()
    if np.sqrt(np.mean(x ** 2)) < 0.01:
        return 0.0
    ac = np.correlate(x, x, "full")[len(x) - 1:]
    lo, hi = int(SR / 300), int(SR / 50)
    i = lo + int(np.argmax(ac[lo:hi]))
    return SR / i if ac[i] > 0.4 * ac[0] else 0.0


chip = SSI263C()
a = Accent(DVC, chip=chip)
a.init()
a.run(2.0)
t0 = chip.time
n = len(a.writes)
a.say(TEXT + "\r")
out = [a.run(0.1)]
while a.busy(quiet=1.0, patience=4.0):
    out.append(a.run(0.05))
y = np.asarray(chip.dsp.concat(out))
names = chip.rom.names
frames = [(t - t0, names.get(v & 0x3F, "?")) for t, r, v in a.writes[n:] if r == 0 and v & 0x3F]
win, hop = int(0.03 * SR), int(0.01 * SR)
rows = []
for k in range(0, len(y) - win, hop):
    seg = y[k:k + win]
    rows.append((k / SR, f0(seg), 20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-9)))
voiced = [r for r in rows if r[1] > 0]
jumps = [abs(12 * np.log2(b[1] / a_[1])) for a_, b in zip(voiced, voiced[1:]) if b[0] - a_[0] < 0.015]
print("voiced frames %d; F0 %.0f-%.0f Hz; median 10-ms F0 step %.2f semitones, 90th pct %.2f"
      % (len(voiced), min(r[1] for r in voiced), max(r[1] for r in voiced), np.median(jumps), np.percentile(jumps, 90)))
fi = 0
for t, hz, db in rows:
    ph = []
    while fi < len(frames) and frames[fi][0] <= t + 0.01:
        ph.append(frames[fi][1])
        fi += 1
    print("%5.2f  %5.0f Hz  %6.1f dB  %s" % (t, hz, db, " ".join(ph)))
