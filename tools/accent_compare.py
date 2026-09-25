"""The Accent demo's opening, real (accent-demo.wav, DEV material for the Accent front end) vs
the emulated Accent-mini: F0 and level every 10 ms, and how much each wobbles inside voiced
stretches.  accent.wav ("Accent ready") is the hold-out and is not read here.

    python tools/accent_compare.py [seconds]
"""
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
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0


def load(path):
    with wave.open(path, "rb") as f:
        sr, n, w, ch = f.getframerate(), f.getnframes(), f.getsampwidth(), f.getnchannels()
        raw = f.readframes(n)
    if w == 1:
        y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128
    else:
        y = np.frombuffer(raw, "<i2").astype(float) / 32768
    return y.reshape(-1, ch).mean(axis=1), sr


def track(y, sr):
    win, hop = int(0.03 * sr), int(0.01 * sr)
    rows = []
    peak = np.abs(y).max() + 1e-9
    for k in range(0, len(y) - win, hop):
        x = y[k:k + win] - y[k:k + win].mean()
        rms = np.sqrt(np.mean(x ** 2))
        db = 20 * np.log10(rms / peak + 1e-9)
        hz = 0.0
        if rms > 0.02 * peak:
            ac = np.correlate(x, x, "full")[len(x) - 1:]
            lo, hi = int(sr / 180), int(sr / 55)       # this voice: no octave flips
            i = lo + int(np.argmax(ac[lo:hi]))
            if ac[i] > 0.45 * ac[0]:
                hz = sr / i
        rows.append((k / sr, hz, db))
    return rows


def wobble(rows):
    """Inside runs of >= 5 voiced 10-ms frames: level and F0 changes frame to frame, with each
    run's slow trend (a 50-ms moving average) removed -- what a listener hears as wavering."""
    runs, cur = [], []
    for r in rows:
        if r[1] > 0:
            cur.append(r)
        else:
            if len(cur) >= 5:
                runs.append(cur)
            cur = []
    if len(cur) >= 5:
        runs.append(cur)
    lev, pit = [], []
    for run in runs:
        db = np.array([r[2] for r in run])
        st = 12 * np.log2(np.array([r[1] for r in run]))
        k = np.ones(5) / 5
        lev += list(np.abs(db[2:-2] - np.convolve(db, k, "valid")))
        pit += list(np.abs(st[2:-2] - np.convolve(st, k, "valid")))
    return len(runs), np.median(lev) if lev else 0, np.percentile(lev, 90) if lev else 0, \
        np.median(pit) if pit else 0, np.percentile(pit, 90) if pit else 0


real, sr = load(REAL)
real = real[:int(SECONDS * sr)]
chip = SSI263C(out_rate=sr)
a = Accent(DVC, chip=chip)
a.init()
a.run(2.0)                                   # the greeting, discarded
a.say(TEXT + "\r")
out = [a.run(0.1)]
while a.busy(quiet=1.0, patience=4.0):
    out.append(a.run(0.05))
emu = np.asarray(chip.dsp.concat(out))
rt, et = track(real, sr), track(emu, sr)
for name, rows, y in (("real", rt, real), ("emulated", et, emu)):
    n, lm, l90, pm, p90 = wobble(rows)
    voiced = [r[1] for r in rows if r[1] > 0]
    print("%-9s %.2f s, voiced runs %d, F0 median %.0f Hz (%.0f-%.0f); level wobble median %.2f dB (90th %.2f); "
          "pitch wobble median %.3f st (90th %.3f)"
          % (name, len(y) / sr, n, np.median(voiced), np.percentile(voiced, 5), np.percentile(voiced, 95),
             lm, l90, pm, p90))
def runs_of(rows):
    runs, cur = [], []
    for r in rows:
        if r[1] > 0:
            cur.append(r[1])
        else:
            if len(cur) >= 5:
                runs.append(cur)
            cur = []
    return runs


for name, rows in (("real", rt), ("emulated", et)):
    spans = [12 * np.log2(max(r) / min(r)) for r in runs_of(rows)]
    print("%-9s F0 span inside voiced stretches: median %.2f st, 90th %.2f st, max %.2f st"
          % (name, np.median(spans), np.percentile(spans, 90), max(spans)))
if "--tracks" in sys.argv:
    for (t, hz, db), (t2, hz2, db2) in zip(rt, et):
        print("%5.2f  real %5.0f Hz %6.1f dB   emu %5.0f Hz %6.1f dB" % (t, hz, db, hz2, db2))
