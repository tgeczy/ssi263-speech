"""Which glide model makes the emulated Accent's pitch travel inside voiced stretches like the real
chip's (accent-demo.wav opening, DEV)?  Pure measurement: prints each variant's F0 span stats."""
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
from ssi263.params import DEFAULTS       # noqa: E402

REAL = repo_paths.archive_audio("accent-demo.wav")
DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
TEXT = "Hi, I am Accent. Have you ever heard a computer talking?"
BASE = DEFAULTS["glide_field_mult"][0]


def load(path, seconds):
    with wave.open(path, "rb") as f:
        sr, n, w = f.getframerate(), f.getnframes(), f.getsampwidth()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y[:int(seconds * sr)], sr


def spans(y, sr):
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
    s = [12 * np.log2(max(r) / min(r)) for r in runs]
    med = np.median([v for v in hz if v])
    return np.median(s), np.percentile(s, 90), max(s), med


def emulate(params, sr):
    chip = SSI263C(params, out_rate=sr)
    a = Accent(DVC, chip=chip)
    a.init()
    a.run(2.0)
    a.say(TEXT + "\r")
    out = [a.run(0.1)]
    while a.busy(quiet=1.0, patience=4.0):
        out.append(a.run(0.05))
    return np.asarray(chip.dsp.concat(out))


real, sr = load(REAL, 6.0)
print("%-44s span median %.2f st, 90th %.2f, max %.2f, F0 %.0f Hz" % (("real",) + spans(real, sr)))
m = list(DEFAULTS["glide_field_mult"][0])
def f367(v):
    return {"glide_field_mult": tuple(v if k in (3, 6, 7) else m[k] for k in range(8))}


VARIANTS = [("current (3,6,7 = 1.66, 2.85, 3.25)", {})] + [
    ("fields 3,6,7 = %.2f" % v, f367(v)) for v in (2.462, 1.66, 1.0, 0.6, 0.35, 0.2)]
for name, over in VARIANTS:
    y = emulate(over, sr)
    print("%-44s span median %.2f st, 90th %.2f, max %.2f, F0 %.0f Hz" % ((name,) + spans(y, sr)))
