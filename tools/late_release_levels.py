"""The release at the NEXT load (a stop held to its end), in absolute terms.  Dev only.

tools/early_release_table.py measures rises over each signal's closure floor, and the
engine's floor is digital silence while the unit's is its line hiss, so a quiet engine burst
reads as a big rise.  Here: the loudest 10 ms in [end - 0.35, end + 0.6] frames, in dB re the
line's loudest 10 ms (95th percentile), unit (aligned end) against engine (its next load),
for stops before PA, a fricative or a closure.

    python tools/late_release_levels.py [--set name=value ...]
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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssi263 import SSI263, Params, drivers  # noqa: E402
import onset_offset as O                  # noqa: E402
from early_release_table import nclass, lev  # noqa: E402


def ref_db(y):
    h = int(0.01 * O.SR)
    v = [10 * np.log10(np.mean(y[k:k + h] ** 2) + 1e-20) for k in range(0, len(y) - h, h // 2)]
    return float(np.percentile(v, 95))


def peak(y, a, b):
    vals = [lev(y, t, t + 0.01) for t in np.arange(a, b - 0.01, 0.0025)]
    vals = [v for v in vals if v is not None]
    return max(vals) if vals else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    over = {}
    for s in a.set:
        k, v = s.split("=", 1)
        try:
            over[k] = json.loads(v)
        except ValueError:
            over[k] = v
    held = {int(l.split()[0]) for l in open(os.path.join(ENGINE, "holdout_lines.txt")) if l[:1].isdigit()}
    segs = collections.defaultdict(list)
    with open(os.path.join(O.MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            sl = int(r["script_line"])
            if r["split"] == "dev" and sl not in held:
                segs[sl].append(r)
    want = collections.defaultdict(list)
    for sl, rs in segs.items():
        rs.sort(key=lambda r: int(r["pos"]))
        for r, n in zip(rs, rs[1:]):
            st, nc = O.base(r["phoneme"]), nclass(n["phoneme"])
            if st in ("B", "D", "P", "T", "K") and nc in ("PA", "fricative", "closure"):
                want[sl].append((int(r["pos"]), st, nc))
    utt = {}
    with open(os.path.join(O.MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(O.MASTER, "master.wav"))
    res = collections.defaultdict(lambda: {"unit": [], "engine": []})
    for sl in sorted(want):
        try:
            rows, x = drivers.master_rows(O.PRED, sl)
        except Exception:
            continue
        chip = SSI263(Params(over), out_rate=O.SR)
        ye = np.asarray(drivers.play_rows(chip, rows), float)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        ru, re_ = ref_db(yr), ref_db(ye)
        rs = {int(r["pos"]): r for r in segs[sl]}
        for k, st, nc in want[sl]:
            if k + 1 >= len(loads):
                continue
            frame = 4096 * (16 - int(rs[k]["R_chip"])) / 1e6
            bu, be = float(rs[k]["t_end_s"]), loads[k + 1]
            pu, pe = peak(yr, bu - 0.35 * frame, bu + 0.6 * frame), peak(ye, be - 0.35 * frame, be + 0.6 * frame)
            if pu is not None and pe is not None:
                res[(st, nc)]["unit"].append(pu - ru)
                res[(st, nc)]["engine"].append(pe - re_)
    print("late window peak, dB re the line's 95th-percentile 10 ms: unit / engine medians (n)   %s" % (over or "defaults"))
    for key in sorted(res):
        u, e = res[key]["unit"], res[key]["engine"]
        print("  %-2s -> %-9s unit %6.1f [%6.1f %6.1f]  engine %6.1f [%6.1f %6.1f]  n=%d"
              % (key[0], key[1], np.median(u), *np.percentile(u, [25, 75]), np.median(e), *np.percentile(e, [25, 75]), len(u)))


if __name__ == "__main__":
    main()
