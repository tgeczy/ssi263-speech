"""D -> J (the "dge" of manager, storage, judge): how fast does the J's frication arrive after
the D's closure?  Dev only.  Tomi (2026-09-25): ours is "more like a zsh", the unit's a fuller
glottal stop then "dzs".

For every dev D -> J pair: the broadband and 2-6 kHz levels in 2.5 ms steps around the
boundary, dB re the J's own level 30-60 ms after the boundary; reported: the closure's level
before the boundary, and the time the J reaches -6 dB of its level (onset time), relative to
the boundary: unit (aligned) and engine (its load).

    python tools/affricate_onset.py [--set name=value ...]
"""
import argparse
import collections
import csv
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssi263 import SSI263, Params, drivers  # noqa: E402
import onset_offset as O                  # noqa: E402

BAND = butter(4, [2000, 6000], "bandpass", fs=O.SR, output="sos")


def env(y):
    z = sosfilt(BAND, y) ** 2
    h, w = int(0.0025 * O.SR), int(0.005 * O.SR)
    return np.array([np.mean(z[k:k + w]) for k in range(0, len(z) - w, h)]) + 1e-20


def onset(e, b):
    i = lambda t: int(t / 0.0025)
    ref = np.median(e[i(b + 0.03):i(b + 0.06)])
    pre = 10 * np.log10(np.median(e[i(b - 0.04):i(b - 0.015)]) / ref)
    for k in range(i(b - 0.04), i(b + 0.06)):
        if e[k] > ref / 4:                        # -6 dB
            return (k - i(b)) * 2.5, pre
    return None, pre


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
            if O.base(r["phoneme"]) == "D" and O.base(n["phoneme"]) == "J":
                want[sl].append(int(r["pos"]))
    utt = {}
    with open(os.path.join(O.MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(O.MASTER, "master.wav"))
    res = {"unit": [], "engine": []}
    for sl in sorted(want):
        rows, x = drivers.master_rows(O.PRED, sl)
        chip = SSI263(Params(over), out_rate=O.SR)
        ye = np.asarray(drivers.play_rows(chip, rows), float)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        rs = {int(r["pos"]): r for r in segs[sl]}
        eu, ee = env(yr), env(ye)
        for k in want[sl]:
            if k + 1 >= len(loads):
                continue
            res["unit"].append(onset(eu, float(rs[k]["t_end_s"])))
            res["engine"].append(onset(ee, loads[k + 1]))
    print("D -> J, 2-6 kHz: onset to -6 dB of the J (ms re boundary) and the closure before it (dB re J)   %s"
          % (over or "defaults"))
    for side in ("unit", "engine"):
        on = [o for o, _ in res[side] if o is not None]
        pre = [p for _, p in res[side]]
        print("  %-6s onset median %6.1f ms [%6.1f %6.1f]   closure %6.1f dB   n=%d"
              % (side, np.median(on), *np.percentile(on, [25, 75]), np.median(pre), len(pre)))


if __name__ == "__main__":
    main()
