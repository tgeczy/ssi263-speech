"""Stop bursts and noise tails, measured separately (Astra, Reply 27, item 3).

For T / K / P -> vowel (dev: section B inv-r2 reps 0-1):
  * the BURST is located in each signal's own audio: the 10 ms frame of greatest
    5-9 kHz energy within [boundary - 1.5 frames, boundary + 0.5 frame], where the
    boundary is the stop's end (unit: phoneme_segments t_end_s; engine: its next
    reg-0 load).  Its time relative to the boundary is reported, and its spectrum
    is taken over a 12 ms Hann window centred on it;
  * the TAIL is [boundary, boundary + 25 ms].
Windows are fully covered by the FFT (no truncation).  Alignment of the unit's
boundary is only as good as phoneme_tracks' model timing; the burst position
carries that uncertainty.
"""
import argparse
import collections
import csv
import json
import os
import sys

import numpy as np
import soundfile as sf

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import repo_paths             # noqa: E402
from ssi263 import SSI263, Params, drivers  # noqa: E402
from noise_decay import VOWELS  # noqa: E402

PRED = repo_paths.master_predictions()
MASTER = repo_paths.master_session()
SR = 44100
NFFT = 2048
F = np.fft.rfftfreq(NFFT, 1 / SR)
BANDS = [(1000, 2500), (2500, 4000), (4000, 6000), (6000, 9000), (9000, 14000)]
STOPS = ("T", "K", "P")


def power(x):
    x = x * np.hanning(len(x))
    return np.abs(np.fft.rfft(x, NFFT)) ** 2


def hf_level(y, t):
    i = int(t * SR)
    x = y[max(0, i - 220):i + 221]
    if len(x) < 441:
        return -np.inf
    S = power(x)
    return 10 * np.log10(S[(F > 5000) & (F < 9000)].sum() + 1e-20)


def locate_burst(y, boundary, frame):
    ts = np.arange(boundary - 1.5 * frame, boundary + 0.5 * frame, 0.002)
    lv = [hf_level(y, t) for t in ts]
    return ts[int(np.argmax(lv))]


def describe(S):
    m = (F > 1000) & (F < 10000)
    c = (F[m] * S[m]).sum() / S[m].sum()
    tot = S[(F > 1000) & (F < 14000)].sum()
    return c, [10 * np.log10(S[(F >= lo) & (F < hi)].sum() / tot + 1e-20) for lo, hi in BANDS]


def window(y, a, b):
    i, j = int(a * SR), int(b * SR)
    if i < 0 or j > len(y) or j - i < 64:
        return None
    return power(y[i:j])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    over = {k: eval(v) for k, v in (s.split("=", 1) for s in a.set)}
    held = {int(l.split()[0]) for l in open(os.path.join(ENGINE, "holdout_lines.txt")) if l[:1].isdigit()}
    segs = collections.defaultdict(list)
    with open(os.path.join(MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            sl = int(r["script_line"])
            if (r["trial"] == "inv-r2" and int(r["rep"]) < 2 and sl not in held
                    and r["phoneme"].split("'")[0] in STOPS and r["next"] in VOWELS):
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    res = collections.defaultdict(lambda: {"pos": ([], []), "burst": [0, 0], "tail": [0, 0], "n": 0})
    for sl in sorted(segs):
        rows, x = drivers.master_rows(PRED, sl)
        chip = SSI263(Params(over), out_rate=SR)
        ye = drivers.play_rows(chip, rows)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        for r in segs[sl]:
            k = int(r["pos"])
            if k + 1 >= len(loads):
                continue
            frame = 4096 * (16 - int(r["R_chip"])) / 1e6
            be, br = loads[k + 1], float(r["t_end_s"])
            te, tr = locate_burst(ye, be, frame), locate_burst(yr, br, frame)
            Be, Br = window(ye, te - 0.006, te + 0.006), window(yr, tr - 0.006, tr + 0.006)
            Te, Tr = window(ye, be, be + 0.025), window(yr, br, br + 0.025)
            if any(v is None for v in (Be, Br, Te, Tr)):
                continue
            d = res[r["phoneme"]]
            d["pos"][0].append((tr - br) / frame)
            d["pos"][1].append((te - be) / frame)
            norm = lambda S: S / S[(F > 1000) & (F < 14000)].sum()
            d["burst"][0] = d["burst"][0] + norm(Br)
            d["burst"][1] = d["burst"][1] + norm(Be)
            d["tail"][0] = d["tail"][0] + norm(Tr)
            d["tail"][1] = d["tail"][1] + norm(Te)
            d["n"] += 1
    print("overrides:", over or "none")
    for ph in sorted(res):
        d = res[ph]
        pr, pe = np.array(d["pos"][0]), np.array(d["pos"][1])
        print("%s  n=%d   burst position re boundary, frames: unit median %+.2f (IQR %+.2f..%+.2f), "
              "engine %+.2f (IQR %+.2f..%+.2f)" % (ph, d["n"], np.median(pr), *np.percentile(pr, [25, 75]),
                                                  np.median(pe), *np.percentile(pe, [25, 75])))
        for lab in ("burst", "tail"):
            cr, br = describe(d[lab][0])
            ce, be = describe(d[lab][1])
            print("   %-5s centroid unit %5.0f / engine %5.0f Hz   bands(1-2.5 2.5-4 4-6 6-9 9-14k) "
                  "unit %s | engine %s" % (lab, cr, ce, " ".join("%5.1f" % v for v in br),
                                           " ".join("%5.1f" % v for v in be)))


if __name__ == "__main__":
    main()
