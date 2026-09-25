"""Does the unit release a stop early?  By stop, duration code and what follows.  Dev only.

The v0.10 rule releases every b01 stop 1.25 frames (scaled by length) before its end.  The unit
does that for T and P before vowels (tools/burst_to_voicing.py: burst 1.07-1.18 frames before
voicing), but not for K before HVC, nor for the 2-frame D before J or D
(tools/stop_pair_release.py), and K's clear bursts before vowels sit on the voicing onset.

For every dev stop segment (B D P T K), grouped by (stop, DR, next class): the rise of the
broadband 10 ms level in the EARLY window [end - 1.3 frames, end - 0.35 frames] over the
closure's median [start + 30 %, end - 1.3 frames], unit (aligned end) and engine (its own next
load).  The window stops 0.35 frames short of the end because the alignment's own slop is about
a quarter frame (tools/onset_offset.py).  A released stop rises by tens of dB there; a held one
stays on its floor.

    python tools/early_release_table.py [--set name=value ...]
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

CLOSURES = {"B", "D", "P", "T", "K", "HVC", "HFC"}
FRICATIVES = {"S", "Z", "SCH", "J", "F", "V", "TH", "THV", "HF"}


def nclass(n):
    b = O.base(n)
    if b in O.VOWELS or b in ("R", "R1", "R2", "L", "L1", "LF", "W", "M", "N", "NG", "LB"):
        return "open"
    if b in CLOSURES:
        return "closure"
    if b in FRICATIVES:
        return "fricative"
    return b


def lev(y, a, b):
    x = y[max(0, int(a * O.SR)):max(0, int(b * O.SR))]
    return 10 * np.log10(np.mean(x ** 2) + 1e-20) if len(x) > 64 else None


def rise(y, a, b, frame):
    e0, e1 = b - 1.3 * frame, b - 0.35 * frame
    c0, c1 = a + 0.3 * (b - a), b - 1.3 * frame
    if e1 - e0 < 0.01 or c1 - c0 < 0.01:
        return None
    floor = [lev(y, t, t + 0.01) for t in np.arange(c0, c1 - 0.01, 0.0025)]
    peak = [lev(y, t, t + 0.01) for t in np.arange(e0, e1 - 0.01 + 1e-9, 0.0025)]
    floor, peak = [v for v in floor if v is not None], [v for v in peak if v is not None]
    if not floor or not peak:
        return None
    return max(peak) - float(np.median(floor))


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
            if O.base(r["phoneme"]) in ("B", "D", "P", "T", "K"):
                want[sl].append((int(r["pos"]), O.base(r["phoneme"]), r["dr"], nclass(n["phoneme"])))
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
        rs = {int(r["pos"]): r for r in segs[sl]}
        for k, st, dr, nc in want[sl]:
            if k + 1 >= len(loads):
                continue
            frame = 4096 * (16 - int(rs[k]["R_chip"])) / 1e6
            u = rise(yr, float(rs[k]["t_start_s"]), float(rs[k]["t_end_s"]), frame)
            e = rise(ye, loads[k], loads[k + 1], frame)
            if u is not None and e is not None:
                res[(st, dr, nc)]["unit"].append(u)
                res[(st, dr, nc)]["engine"].append(e)
    print("early-window rise over the closure floor, dB: unit / engine medians (n)   %s" % (over or "defaults"))
    for key in sorted(res):
        u, e = res[key]["unit"], res[key]["engine"]
        print("  %-2s DR%s -> %-9s unit %5.1f [%5.1f %5.1f]  engine %5.1f   n=%d"
              % (key[0], key[1], key[2], np.median(u), *np.percentile(u, [25, 75]), np.median(e), len(u)))


if __name__ == "__main__":
    main()
