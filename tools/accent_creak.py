"""Creak at the pulse level: jitter (period-to-period) and shimmer (peak-to-peak) inside steady
voiced stretches, real accent-demo.wav opening (DEV) vs the emulated Accent at the same rate."""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from hosts.accent import Accent          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

REAL = r"Y:\content from streamers\DecTalk archive\Synthesizers\accent-demo.wav"
DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
TEXT = "Hi, I am Accent. Have you ever heard a computer talking?"


def load(path, seconds):
    with wave.open(path, "rb") as f:
        sr, n, w = f.getframerate(), f.getnframes(), f.getsampwidth()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y[:int(seconds * sr)], sr


def pulses(y, sr):
    """Per 40-ms voiced window: period by autocorrelation, then cycle-by-cycle peaks."""
    win, hop = int(0.04 * sr), int(0.04 * sr)
    peak = np.abs(y).max() + 1e-9
    jit, shim, alt = [], [], []
    for k in range(0, len(y) - win, hop):
        x = y[k:k + win] - y[k:k + win].mean()
        if np.sqrt(np.mean(x ** 2)) < 0.05 * peak:
            continue
        ac = np.correlate(x, x, "full")[len(x) - 1:]
        lo, hi = int(sr / 180), int(sr / 55)
        T = lo + int(np.argmax(ac[lo:hi]))
        if ac[T] < 0.6 * ac[0]:
            continue
        # strongest point in each period, then refine by searching +-15% around the last + T
        idx = [int(np.argmax(np.abs(x[:T])))]
        while idx[-1] + int(1.15 * T) < len(x):
            a0, a1 = idx[-1] + int(0.85 * T), idx[-1] + int(1.15 * T)
            idx.append(a0 + int(np.argmax(np.abs(x[a0:a1]))))
        if len(idx) < 4:
            continue
        per = np.diff(idx).astype(float)
        amp = np.abs(x[idx])
        jit.append(np.mean(np.abs(np.diff(per))) / np.mean(per))
        shim.append(np.mean(np.abs(np.diff(amp))) / np.mean(amp))
        # period doubling: correlation at 2T against T
        if 2 * T < len(ac):
            alt.append(ac[2 * T] / ac[T])
    return np.median(jit) * 100, np.median(shim) * 100, np.median(alt), len(jit)


real, sr = load(REAL, 6.0)
print("real      jitter %.2f %%, shimmer %.2f %%, r(2T)/r(T) %.2f  (%d windows)" % pulses(real, sr))
chip = SSI263C(out_rate=sr)
a = Accent(DVC, chip=chip)
a.init()
a.run(2.0)
a.say(TEXT + "\r")
out = [a.run(0.1)]
while a.busy(quiet=1.0, patience=4.0):
    out.append(a.run(0.05))
emu = np.asarray(chip.dsp.concat(out))
print("emulated  jitter %.2f %%, shimmer %.2f %%, r(2T)/r(T) %.2f  (%d windows)" % pulses(emu, sr))
