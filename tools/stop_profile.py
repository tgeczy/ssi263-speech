"""Where inside a stop is its noise?  Time profile around T/K/P -> vowel, dev only.

Averages (in dB) the 5-9 kHz band and the full-band level over the stop segment
and into the next phoneme, on a time axis normalised to the stop's own length
(0 = stop start, 1 = stop end / next phoneme start, up to 1.5).  A real stop is
closure first, burst last: its high band should peak near 1, after a dip.
Listeners heard the engine's T as "s" + silence ("Hello STomi").
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
GRID = np.linspace(-0.25, 1.5, 15)


def profile(y, a, b):
    L = b - a
    f = np.fft.rfftfreq(512, 1 / SR)
    m = (f > 5000) & (f < 9000)
    hi, full = [], []
    for g in GRID:
        i = int((a + g * L) * SR)
        if i - 256 < 0 or i + 256 > len(y):
            return None
        x = y[i - 256:i + 256]
        S = np.abs(np.fft.rfft(x * np.hanning(512))) ** 2
        hi.append(10 * np.log10(S[m].sum() + 1e-20))
        full.append(10 * np.log10(S.sum() + 1e-20))
    return np.array(hi), np.array(full)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--max-lines", type=int, default=400)
    a = ap.parse_args()
    over = {k: eval(v) for k, v in (s.split("=", 1) for s in a.set)}
    held = {int(l.split()[0]) for l in open(os.path.join(ENGINE, "holdout_lines.txt")) if l[:1].isdigit()}
    segs = collections.defaultdict(list)
    with open(os.path.join(MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            sl = int(r["script_line"])
            if (r["trial"] == "inv-r2" and int(r["rep"]) < 2 and sl not in held
                    and r["phoneme"] in ("T", "K", "P") and r["next"] in VOWELS):
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    acc = collections.defaultdict(list)
    for sl in sorted(segs)[:a.max_lines]:
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
            pe = profile(ye, loads[k], loads[k + 1])
            pr = profile(yr, float(r["t_start_s"]), float(r["t_end_s"]))
            if pe and pr:
                acc[r["phoneme"]].append((pr, pe))
    print("overrides:", over or "none")
    print("position in the stop:  " + " ".join("%5.2f" % g for g in GRID))
    for ph, L in acc.items():
        for lab, j in (("unit", 0), ("engine", 1)):
            hi = np.mean([p[j][0] for p in L], axis=0)
            full = np.mean([p[j][1] for p in L], axis=0)
            print("%-2s %-6s 5-9k  " % (ph, lab) + " ".join("%5.0f" % (v - hi.max()) for v in hi))
            print("%-2s %-6s full  " % (ph, lab) + " ".join("%5.0f" % (v - full.max()) for v in full))
        print("   (n=%d)" % len(L))


if __name__ == "__main__":
    main()
