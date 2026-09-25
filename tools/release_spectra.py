"""The first 25 ms after a stop releases into a vowel: engine vs unit, dev only.

Listeners heard the engine's T as "ch".  A "ch" is palatal noise at 2.5-4 kHz;
the unit's T sits at 6-9 kHz.  This measures the release spectrum (1-10 kHz
centroid, band shares) right after T / K / P / D'2 -> vowel boundaries
(section B inv-r2 reps 0-1; real boundaries from phoneme_segments.csv, engine
boundaries from its own reg-0 loads).
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
from ssi263 import SSI263, Params, drivers  # noqa: E402
from noise_decay import VOWELS  # noqa: E402

PRED = r"C:\git\blite_sweep\scripts\MASTER_predictions.jsonl"
MASTER = r"C:\git\blite_sweep\sessions\MASTER"
SR = 44100
N = 1024
F = np.fft.rfftfreq(N, 1 / SR)
BANDS = [(1000, 2500), (2500, 4000), (4000, 6000), (6000, 9000), (9000, 14000)]
STOPS = ("T", "K", "P", "D'2", "B", "B'1", "D")


def spec_at(y, t, dur=0.025):
    i = int(t * SR)
    x = y[i:i + int(dur * SR)]
    if len(x) < 256:
        return None
    x = np.pad(x * np.hanning(len(x)), (0, max(0, N - len(x))))[:N]
    return np.abs(np.fft.rfft(x)) ** 2


def describe(S):
    m = (F > 1000) & (F < 10000)
    c = (F[m] * S[m]).sum() / S[m].sum()
    tot = S[(F > 1000) & (F < 14000)].sum()
    return c, [10 * np.log10(S[(F >= lo) & (F < hi)].sum() / tot + 1e-20) for lo, hi in BANDS]


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
                    and r["phoneme"] in STOPS and r["next"] in VOWELS):
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    acc = collections.defaultdict(lambda: [0, 0, 0])
    for sl in sorted(segs)[:80]:
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
            se, sr_ = spec_at(ye, loads[k + 1]), spec_at(yr, float(r["t_end_s"]))
            if se is None or sr_ is None:
                continue
            acc[r["phoneme"]][0] += se / se[(F > 1000) & (F < 14000)].sum()
            acc[r["phoneme"]][1] += sr_ / sr_[(F > 1000) & (F < 14000)].sum()
            acc[r["phoneme"]][2] += 1
    print("overrides:", over or "none")
    print("release  n   centroid unit/engine Hz   band share dB (1-2.5 2.5-4 4-6 6-9 9-14 kHz) unit | engine")
    for ph in STOPS:
        if not acc[ph][2]:
            continue
        ce, be = describe(acc[ph][0])
        cr, br = describe(acc[ph][1])
        print("%-7s %3d   %5.0f / %5.0f        %s | %s" % (ph, acc[ph][2], cr, ce,
              " ".join("%5.1f" % v for v in br), " ".join("%5.1f" % v for v in be)))


if __name__ == "__main__":
    main()
