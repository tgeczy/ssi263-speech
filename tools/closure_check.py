"""Stop closure by duration and predecessor (Astra, Reply 27, item 4).

For every dev stop segment (B D P T K HVC HFC, any duration suffix), grouped by
(phoneme with its duration bits, predecessor class), reports unit vs engine:
  * mid : RMS over the middle 60 % of the segment, dB re the line's loudest segment;
  * deep: the quietest 10 ms inside the segment, same reference -- does it close?
Predecessor classes: 'stop' (a b00-clear closure), 'PA', or 'other'.  The model
predicts that short stops (DR 2-3: 1-2 frames) never close from an open tract.
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

PRED = repo_paths.master_predictions()
MASTER = repo_paths.master_session()
SR = 44100
STOPS = {"B", "D", "P", "T", "K", "HVC", "HFC"}


def levels(y, a, b):
    i, j = int(a * SR), int(b * SR)
    x = y[i:j]
    if len(x) < 441:
        return None
    m = x[int(0.2 * len(x)):int(0.8 * len(x))]
    h = 441
    fr = [np.mean(x[k:k + h] ** 2) for k in range(0, len(x) - h + 1, h // 2)]
    return 10 * np.log10(np.mean(m ** 2) + 1e-20), 10 * np.log10(min(fr) + 1e-20)


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
            if r["trial"] == "inv-r2" and int(r["rep"]) < 2 and sl not in held and r["audible"] == "True":
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    res = collections.defaultdict(list)
    for sl in sorted(segs)[:a.max_lines]:
        if not any(r["phoneme"].split("'")[0] in STOPS for r in segs[sl]):
            continue
        rows, x = drivers.master_rows(PRED, sl)
        chip = SSI263(Params(over), out_rate=SR)
        ye = drivers.play_rows(chip, rows)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        le, lr = {}, {}
        for r in segs[sl]:
            k = int(r["pos"])
            if k + 1 >= len(loads):
                continue
            e = levels(ye, loads[k], loads[k + 1])
            q = levels(yr, float(r["t_start_s"]), float(r["t_end_s"]))
            if e and q:
                le[k], lr[k] = (r, e), (r, q)
        if not le:
            continue
        me = max(v[1][0] for v in le.values())
        mr = max(v[1][0] for v in lr.values())
        for k in le:
            r = le[k][0]
            base = r["phoneme"].split("'")[0]
            if base not in STOPS:
                continue
            pb = r["prev"].split("'")[0]
            pc = "stop" if pb in STOPS else ("PA" if pb in ("PA", "|") else "other")
            key = ("%s/dr%s" % (base, r["dr"]), pc)
            res[key].append((le[k][1][0] - me, lr[k][1][0] - mr, le[k][1][1] - me, lr[k][1][1] - mr))
    print("overrides:", over or "none")
    print("stop/dur  after    n    mid unit/engine dB     deepest 10 ms unit/engine dB")
    for key in sorted(res):
        L = np.array(res[key])
        print("%-9s %-6s %3d    %6.1f / %6.1f          %6.1f / %6.1f"
              % (key[0], key[1], len(L), np.median(L[:, 1]), np.median(L[:, 0]),
                 np.median(L[:, 3]), np.median(L[:, 2])))


if __name__ == "__main__":
    main()
