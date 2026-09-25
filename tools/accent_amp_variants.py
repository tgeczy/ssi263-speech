"""Creak / stod on the emulated Accent: does the amplitude slew make it?  Renders the demo opening
with amp_slew_per_phoneme variants (WAVs for Tomi) and measures fine-grain level roughness inside
voiced stretches (5-ms level vs its 25-ms moving average), real (accent-demo.wav opening, DEV) vs
each variant."""
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
OUT = os.path.join(os.path.dirname(HERE), "investigation", "out")


def load(path, seconds):
    with wave.open(path, "rb") as f:
        sr, n, w = f.getframerate(), f.getnframes(), f.getsampwidth()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y[:int(seconds * sr)], sr


def roughness(y, sr):
    hop, win = int(0.005 * sr), int(0.012 * sr)
    peak = np.abs(y).max() + 1e-9
    db = np.array([20 * np.log10(np.sqrt(np.mean(y[k:k + win] ** 2)) / peak + 1e-9)
                   for k in range(0, len(y) - win, hop)])
    loud = db > -25
    k = np.ones(5) / 5
    dev = []
    start = None
    for i, v in enumerate(list(loud) + [False]):
        if v and start is None:
            start = i
        elif not v and start is not None:
            if i - start >= 10:
                seg = db[start:i]
                dev += list(np.abs(seg[2:-2] - np.convolve(seg, k, "valid")))
            start = None
    return np.median(dev), np.percentile(dev, 90), np.percentile(dev, 99)


def emulate(over, sr):
    chip = SSI263C(over, out_rate=sr)
    a = Accent(DVC, chip=chip)
    a.init()
    a.run(2.0)
    a.say(TEXT + "\r")
    out = [a.run(0.1)]
    while a.busy(quiet=1.0, patience=4.0):
        out.append(a.run(0.05))
    return np.asarray(chip.dsp.concat(out)), chip


real, sr = load(REAL, 6.0)
print("%-22s level roughness median %.2f dB, 90th %.2f, 99th %.2f" % (("real",) + roughness(real, sr)))
for name, over in (("A current (1.0)", {}), ("B slew 0.5", {"amp_slew_per_phoneme": 0.5}),
                   ("C slew 0.25", {"amp_slew_per_phoneme": 0.25})):
    y, chip = emulate(over, 44100)
    print("%-22s level roughness median %.2f dB, 90th %.2f, 99th %.2f" % ((name,) + roughness(y, 44100)))
    path = os.path.join(OUT, "accent-opening-%s.wav" % name.split()[0])
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(chip.dsp.pcm16(y, 1.0))
