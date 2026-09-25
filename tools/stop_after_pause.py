"""A stop that starts on silence: does it sound before it closes?  Dev only.

A closure phoneme runs closure_delay_frames of open tract before it shuts, so a vowel can
fade into it.  After a pause there is no vowel, and the open tract voices the stop's own
target for that frame: on the Accent that was a 10 ms "tock" before the T of "still"
(Accent PA'3 with the amplitude at 0, then D'0), which the real card does not make.

For every dev stop (B D P T K), by what precedes it (the line's start, PA or open): the loudest 10 ms level in
its first frame [start, start + 1 frame], in dB re the line's own loud level (95th percentile
of its 10 ms levels), unit (aligned start) and engine (its own load).

    python tools/stop_after_pause.py [--set name=value ...]
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
from early_release_table import nclass    # noqa: E402


def levels(y):
    n = int(0.01 * O.SR)
    h = int(0.0025 * O.SR)
    return np.array([10 * np.log10(np.mean(y[i:i + n] ** 2) + 1e-20) for i in range(0, max(1, len(y) - n), h)])


def onset(y, a, frame, ref):
    x = y[max(0, int(a * O.SR)):max(0, int((a + frame) * O.SR))]
    if len(x) < int(0.012 * O.SR):
        return None
    return float(levels(x).max()) - ref


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
        if O.base(rs[0]["phoneme"]) in ("B", "D", "P", "T", "K"):
            want[sl].append((int(rs[0]["pos"]), O.base(rs[0]["phoneme"]), "start"))
        for p, r in zip(rs, rs[1:]):
            if O.base(r["phoneme"]) in ("B", "D", "P", "T", "K"):
                pc = "PA" if O.base(p["phoneme"]) == "PA" else nclass(p["phoneme"])
                if pc in ("PA", "open"):
                    want[sl].append((int(r["pos"]), O.base(r["phoneme"]), pc))
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
        ru, rengine = np.percentile(levels(yr), 95), np.percentile(levels(ye), 95)
        rs = {int(r["pos"]): r for r in segs[sl]}
        for k, st, pc in want[sl]:
            if k >= len(loads):
                continue
            frame = 4096 * (16 - int(rs[k]["R_chip"])) / 1e6
            u = onset(yr, float(rs[k]["t_start_s"]), frame, ru)
            e = onset(ye, loads[k], frame, rengine)
            if u is not None and e is not None:
                res[(st, pc)]["unit"].append(u)
                res[(st, pc)]["engine"].append(e)
    print("first-frame peak, dB re the line's loud level: unit / engine medians (n)   %s" % (over or "defaults"))
    for key in sorted(res):
        u, e = res[key]["unit"], res[key]["engine"]
        print("  %-2s after %-5s unit %6.1f [%6.1f %6.1f]  engine %6.1f [%6.1f %6.1f]  n=%d"
              % (key[0], key[1], np.median(u), *np.percentile(u, [25, 75]),
                 np.median(e), *np.percentile(e, [25, 75]), len(u)))


if __name__ == "__main__":
    main()
