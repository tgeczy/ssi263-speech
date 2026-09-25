"""Where does the emulated Accent's pitch jump inside a voiced stretch, and what did the driver
write there?  Our render only."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from hosts.accent import Accent          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
TEXT = sys.argv[1] if len(sys.argv) > 1 else "Hi, I am Accent. Have you ever heard a computer talking?"
SR = 11025
chip = SSI263C(out_rate=SR)
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
regs = [0] * 5
frames = []
for t, r, v in a.writes[n:]:
    regs[r] = v
    if r == 0 and v & 0x3F:
        I = ((regs[2] >> 3) & 1) * 2048 + (regs[2] & 7) + (regs[1] >> 3) * 64
        frames.append((t - t0, names.get(v & 0x3F, "?"), regs[1], regs[2], 1e6 / (8 * (4096 - I))))
win, hop = int(0.03 * SR), int(0.005 * SR)
prev = None
for k in range(0, len(y) - win, hop):
    x = y[k:k + win] - y[k:k + win].mean()
    rms = np.sqrt(np.mean(x ** 2))
    hz = 0.0
    if rms > 0.01:
        ac = np.correlate(x, x, "full")[len(x) - 1:]
        lo, hi = int(SR / 300), int(SR / 55)
        i = lo + int(np.argmax(ac[lo:hi]))
        if ac[i] > 0.45 * ac[0]:
            hz = SR / i
    t = k / SR + 0.015
    if hz and prev and abs(12 * np.log2(hz / prev)) > 1.0:
        near = [f for f in frames if t - 0.06 <= f[0] <= t + 0.01]
        print("%.3f s: %5.0f -> %5.0f Hz; frames: %s" % (t, prev, hz, "; ".join(
            "%s@%.3f R1=%02X R2=%02X target %.0f Hz" % (f[1], f[0], f[2], f[3], f[4]) for f in near)))
    prev = hz if hz else None
