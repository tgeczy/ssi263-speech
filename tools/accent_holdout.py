"""One-time check on the Accent HOLD-OUT (H9, accent.wav = the real "Accent ready"): the
emulated driver's own boot greeting, F0 median and travel inside voiced stretches, with the
glide fields before and after the 2026-09-25 change.  Report only; never tune on this."""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import repo_paths             # noqa: E402
from hosts.accent import Accent          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

HOLDOUT = repo_paths.archive_audio("accent.wav")
DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")


def load(path):
    with wave.open(path, "rb") as f:
        sr, n, w, ch = f.getframerate(), f.getnframes(), f.getsampwidth(), f.getnchannels()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y.reshape(-1, ch).mean(axis=1), sr


def stats(y, sr):
    win, hop = int(0.03 * sr), int(0.01 * sr)
    peak = np.abs(y).max() + 1e-9
    hz = []
    for k in range(0, len(y) - win, hop):
        x = y[k:k + win] - y[k:k + win].mean()
        v = 0.0
        if np.sqrt(np.mean(x ** 2)) > 0.02 * peak:
            ac = np.correlate(x, x, "full")[len(x) - 1:]
            lo, hi = int(sr / 180), int(sr / 55)
            i = lo + int(np.argmax(ac[lo:hi]))
            if ac[i] > 0.45 * ac[0]:
                v = sr / i
        hz.append(v)
    runs, cur = [], []
    for v in hz + [0]:
        if v:
            cur.append(v)
        else:
            if len(cur) >= 5:
                runs.append(cur)
            cur = []
    s = [12 * np.log2(max(r) / min(r)) for r in runs] or [0.0]
    return np.median([v for v in hz if v]), np.median(s), max(s), len(y) / sr


real, sr = load(HOLDOUT)
print("H9 real      F0 %.0f Hz, travel median %.2f st, max %.2f st, %.2f s" % stats(real, sr))
old = (1.000, 0.962, 1.226, 1.66, 2.092, 2.462, 2.85, 3.25)
for name, over in (("old fields", {"glide_field_mult": old}), ("new fields", {})):
    chip = SSI263C(over, out_rate=sr)
    a = Accent(DVC, chip=chip)
    a.init()
    y = np.asarray(a.run(2.0))
    nz = np.where(np.abs(y) > 0.003)[0]
    y = y[max(0, nz[0] - 200):nz[-1] + 200] if len(nz) else y
    print("%-12s F0 %.0f Hz, travel median %.2f st, max %.2f st, %.2f s" % ((name,) + stats(y, sr)))
