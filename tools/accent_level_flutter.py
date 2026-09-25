"""Level flutter at the phoneme scale on the Accent: real (accent-demo.wav opening, DEV) vs the
emulation, with the amplitude slew as the variable.  Tomi (2026-09-25): a volume dip in "colon"
after the L and "slight microfluctuations"; Speak-Out and Blazie hold one amplitude code, the
Accent writes one per phoneme (12 on stressed vowels, 4-6 on L, N and weak vowels).

Level: 30 ms windows every 10 ms, dB re the excerpt's peak.  Inside loud stretches (> -25 dB,
>= 150 ms) the residual against a 110 ms moving average measures the phoneme-scale flutter; the
5-ms roughness of accent_amp_variants.py measured the pulse scale instead.

    python tools/accent_level_flutter.py
"""
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

REAL = repo_paths.archive_audio("accent-demo.wav")
DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
TEXT = "Hi, I am Accent. Have you ever heard a computer talking?"
SECONDS = 5.6


def load(path, seconds):
    with wave.open(path, "rb") as f:
        sr, n, w = f.getframerate(), f.getnframes(), f.getsampwidth()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y[:int(seconds * sr)], sr


def band(y, sr, top):
    """Keep 100 Hz..top so the comparison sees what the real recording can hold."""
    Y = np.fft.rfft(y)
    f = np.fft.rfftfreq(len(y), 1 / sr)
    Y[(f < 100) | (f > top)] = 0
    return np.fft.irfft(Y, len(y))


def flutter(y, sr):
    win, hop = int(0.03 * sr), int(0.01 * sr)
    db = np.array([10 * np.log10(np.mean(y[k:k + win] ** 2) + 1e-12) for k in range(0, len(y) - win, hop)])
    db -= db.max()
    loud = db > -25
    k = np.ones(11) / 11
    dev, start = [], None
    for i, v in enumerate(list(loud) + [False]):
        if v and start is None:
            start = i
        elif not v and start is not None:
            if i - start >= 15:
                seg = db[start:i]
                dev += list(np.abs(seg[5:-5] - np.convolve(seg, k, "valid")))
            start = None
    dev = np.array(dev)
    return np.median(dev), np.percentile(dev, 90), np.percentile(dev, 99), len(dev)


def emulate(over):
    chip = SSI263C(over, out_rate=44100)
    a = Accent(DVC, chip=chip)
    a.boot()
    a.say(TEXT + "\r")
    out = [a.run(0.1)]
    while a.busy(quiet=0.3, patience=3.0):
        out.append(a.run(0.05))
    return np.asarray(chip.dsp.concat(out), float)


def main():
    real, sr = load(REAL, SECONDS)
    top = 0.45 * sr
    print("real %d Hz; band 100..%d Hz" % (sr, top))
    print("%-16s flutter median %.2f dB, 90th %.2f, 99th %.2f (n=%d)" % (("real",) + flutter(band(real, sr, top), sr)))
    for name, over in (("slew 1.0 (now)", {}), ("slew 0.5", {"amp_slew_per_phoneme": 0.5}),
                       ("slew 0.25", {"amp_slew_per_phoneme": 0.25}), ("slew 2.0", {"amp_slew_per_phoneme": 2.0})):
        y = emulate(over)
        print("%-16s flutter median %.2f dB, 90th %.2f, 99th %.2f (n=%d)" % ((name,) + flutter(band(y, 44100, top), 44100)))


if __name__ == "__main__":
    main()
