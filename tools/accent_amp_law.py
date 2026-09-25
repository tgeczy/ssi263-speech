"""Which amplitude law does the Accent's own level contour ask for?  DEV only: the demo opening,
real (accent-demo.wav) vs emulated, aligned by DTW on level-free spectral shape.  For every vowel
the driver writes, compare the real and the emulated level (middle half of the vowel) against the
amplitude code the driver gave it.  If the chip's law is right, real - emulated is flat in the
code; a compressed top shows as a falling slope.

Laws compared: 'linear' (the model's R3 amplitude = code/15), and 'BL' (the Braille Lite volume
sweep VM1: proportional to 7, then 0.964 0.936 0.905 0.881 0.854 0.833 0.807 0.781 of
proportional at 8..15).  accent.wav (H9) is not read.

    python tools/accent_amp_law.py [seconds]
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
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 5.6
BL = {8: 0.964, 9: 0.936, 10: 0.905, 11: 0.881, 12: 0.854, 13: 0.833, 14: 0.807, 15: 0.781}
HOP = 0.01


def load(path):
    with wave.open(path, "rb") as f:
        sr, n, w, ch = f.getframerate(), f.getnframes(), f.getsampwidth(), f.getnchannels()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else \
        np.frombuffer(raw, "<i2").astype(float) / 32768
    return y.reshape(-1, ch).mean(axis=1), sr


def resample(y, sr, to):
    t = np.arange(int(len(y) * to / sr)) / to
    return np.interp(t, np.arange(len(y)) / sr, y)


def frames(y, sr, fmax):
    """30 ms windows every 10 ms: level (dB) and a level-free log spectrum up to fmax."""
    win, hop = int(0.03 * sr), int(HOP * sr)
    f = np.fft.rfftfreq(win, 1 / sr)
    edges = np.geomspace(100, fmax, 21)
    lev, shape = [], []
    for k in range(0, len(y) - win, hop):
        x = (y[k:k + win] - y[k:k + win].mean()) * np.hanning(win)
        p = np.abs(np.fft.rfft(x)) ** 2
        lev.append(10 * np.log10(np.mean(x ** 2) + 1e-12))
        b = np.array([p[(f >= lo) & (f < hi)].sum() for lo, hi in zip(edges[:-1], edges[1:])])
        s = 10 * np.log10(b + 1e-12)
        shape.append(s - s.mean())
    return np.array(lev), np.array(shape)


def dtw(a, b):
    n, m = len(a), len(b)
    cost = np.sqrt(((a[:, None, :] - b[None, :, :]) ** 2).sum(-1))
    D = np.full((n + 1, m + 1), np.inf)
    D[0, 0] = 0
    for i in range(1, n + 1):
        row = cost[i - 1]
        prev = D[i - 1]
        best = np.minimum(prev[1:], prev[:-1])
        cur = D[i]
        for j in range(1, m + 1):              # left neighbour needs the running row
            cur[j] = row[j - 1] + min(best[j - 1], cur[j - 1])
    i, j, path = n, m, []
    while i > 0 and j > 0:
        path.append((i - 1, j - 1))
        k = np.argmin((D[i - 1, j - 1], D[i - 1, j], D[i, j - 1]))
        i, j = (i - 1, j - 1) if k == 0 else (i - 1, j) if k == 1 else (i, j - 1)
    return path[::-1]


def main():
    real, rsr = load(REAL)
    real = real[:int(SECONDS * rsr)]
    chip = SSI263C(out_rate=44100)
    a = Accent(DVC, chip=chip)
    a.keep_writes = True
    a.boot()
    n0, t0 = len(a.writes), chip.time
    a.say(TEXT + "\r")
    out = [a.run(0.1)]
    while a.busy(quiet=0.3, patience=3.0):
        out.append(a.run(0.05))
    emu = resample(np.asarray(chip.dsp.concat(out), float), 44100, rsr)
    fmax = min(5000, rsr / 2 * 0.9)
    lr, sr_ = frames(real, rsr, fmax)
    le, se = frames(emu, rsr, fmax)
    # trim both to speech (first/last frame within 30 dB of the peak)
    def span(lev):
        on = np.where(lev > lev.max() - 30)[0]
        return on[0], on[-1] + 1
    (r0, r1), (e0, e1) = span(lr), span(le)
    path = dtw(se[e0:e1], sr_[r0:r1])
    to_real = {}
    for i, j in path:
        to_real.setdefault(i + e0, []).append(j + r0)
    names = chip.rom.names
    regs, ev = [0] * 5, []
    for t, r, v in a.writes[n0:]:
        regs[r] = v
        if r == 0:
            ev.append((t - t0, names.get(v & 0x3F, "?"), regs[3] & 15))
    vowels = []
    for k, (t, nm, amp) in enumerate(ev):
        if not (nm[0] in "AEIOU" or nm.startswith("ER")) or amp == 0:
            continue
        t2 = ev[k + 1][0] if k + 1 < len(ev) else t + 0.05
        if t2 - t < 0.03:
            continue
        # fold consecutive writes of one vowel: the driver re-writes a vowel to glide it
        a_ = int(round((t + (t2 - t) * 0.25) / HOP)), int(round((t2 - (t2 - t) * 0.25) / HOP))
        fr = [i for i in range(a_[0], a_[1] + 1) if i in to_real and e0 <= i < e1]
        if not fr:
            continue
        rj = sorted({j for i in fr for j in to_real[i]})
        if np.mean(lr[rj]) < lr.max() - 40:        # past the end of the real excerpt
            continue
        vowels.append((t, nm, amp, float(np.mean(le[fr])), float(np.mean(lr[rj]))))
    print("%-6s %-5s %4s %8s %8s %8s %8s" % ("t", "vowel", "amp", "emu dB", "real dB", "d lin", "d BL"))
    d_lin, d_bl, amps = [], [], []
    for t, nm, amp, e, r in vowels:
        bl = e + 20 * np.log10(BL.get(amp, 1.0))
        d_lin.append(r - e)
        d_bl.append(r - bl)
        amps.append(amp)
        print("%6.3f %-5s %4d %8.1f %8.1f %8.1f %8.1f" % (t, nm, amp, e, r, r - e, r - bl))
    amps, d_lin, d_bl = map(np.array, (amps, d_lin, d_bl))
    for tag, d in (("linear", d_lin), ("BL", d_bl)):
        slope = np.polyfit(amps, d, 1)[0]
        print("%-6s real-emu: spread (sd) %.2f dB, slope vs code %+.2f dB/code, n=%d"
              % (tag, np.std(d - d.mean()), slope, len(d)))
    # the same vowel at different codes: remove each vowel's own level error first
    groups = [nm for _, nm, _, _, _ in vowels]
    for tag, d in (("linear", d_lin), ("BL", d_bl)):
        dd = d.copy()
        keep = np.zeros(len(d), bool)
        for g in set(groups):
            idx = [k for k, x in enumerate(groups) if x == g]
            if len({amps[k] for k in idx}) > 1:
                dd[idx] -= d[idx].mean()
                keep[idx] = True
        slope = np.polyfit(amps[keep], dd[keep], 1)[0]
        print("%-6s within-vowel: residual sd %.2f dB, slope %+.2f dB/code, n=%d in %d vowels"
              % (tag, np.std(dd[keep]), slope, keep.sum(), len({g for g, k in zip(groups, keep) if k})))
    lo, hi = amps <= 7, amps >= 10
    print("mean real-emu, codes <=7 vs >=10: linear %.2f / %.2f, BL %.2f / %.2f"
          % (d_lin[lo].mean(), d_lin[hi].mean(), d_bl[lo].mean(), d_bl[hi].mean()))


if __name__ == "__main__":
    main()
