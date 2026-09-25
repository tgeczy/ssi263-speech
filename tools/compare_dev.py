"""Engine vs the real unit on DEVELOPMENT material only (never the hold-out).

Dev = MASTER section B, trial inv-r2, reps 0-1 (phoneme_tracks.py's split; reps
2-3 and everything in ../src/HOLDOUT.md stay out).  Each line is driven by its
EMULATED register stream.  Reports the long-term average spectrum in bands,
relative to each signal's own 300-3500 Hz mean, so level is not compared.

The real side still carries the Braille Lite output path and the line-in, so
a difference here is chip OR capture path until the tone ladder separates them.
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
from ssi263 import SSI263, Params, drivers  # noqa: E402

PRED = r"C:\git\blite_sweep\scripts\MASTER_predictions.jsonl"
MASTER = r"C:\git\blite_sweep\sessions\MASTER"
SR = 44100
BANDS = [(100, 300), (300, 700), (700, 1500), (1500, 2500), (2500, 3500), (3500, 4500),
         (4500, 6000), (6000, 8000), (8000, 11000), (11000, 16000), (16000, 21000)]


def dev_lines(limit):
    out = []
    with open(PRED) as f:
        for line in f:
            x = json.loads(line)
            lab = x["label"]
            if lab.get("trial") == "inv-r2" and int(lab.get("rep", 9)) < 2 and x.get("ok"):
                out.append(x["script_line"])
    held = set()
    with open(os.path.join(ENGINE, "holdout_lines.txt")) as f:
        for l in f:
            if l[:1].isdigit():
                held.add(int(l.split()[0]))
    out = [s for s in out if s not in held]
    step = max(1, len(out) // limit)
    return out[::step][:limit]


def power_spectrum(x, n=2048):
    w = np.hanning(n)
    S = np.zeros(n // 2 + 1)
    k = 0
    for i in range(0, len(x) - n, n // 2):
        seg = x[i:i + n]
        if np.sqrt(np.mean(seg ** 2)) < 0.002:
            continue
        S += np.abs(np.fft.rfft(seg * w)) ** 2
        k += 1
    return S / max(k, 1)


def bands_db(S, n=2048):
    f = np.fft.rfftfreq(n, 1 / SR)
    ref = np.mean(S[(f > 300) & (f < 3500)])
    return [10 * np.log10(np.mean(S[(f >= a) & (f < b)]) / ref + 1e-30) for a, b in BANDS]


STOPS = {"B", "D", "P", "T", "K", "HVC", "HFC"}


def seg_db(y, a, b):
    """RMS in dB over the middle 60 % of [a, b) seconds."""
    n = len(y)
    i, j = int((a + 0.2 * (b - a)) * SR), int((b - 0.2 * (b - a)) * SR)
    i, j = max(0, i), min(n, j)
    if j - i < 64:
        return None
    return 10 * np.log10(np.mean(y[i:j] ** 2) + 1e-20)


def levels(lines, over, utt, wav):
    """Per-phoneme level re the line's loudest phoneme: engine vs real (frozen segments)."""
    import csv
    import collections
    segs = collections.defaultdict(list)
    with open(os.path.join(MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            if int(r["script_line"]) in lines and r["audible"] == "True":
                segs[int(r["script_line"])].append(r)
    diff = collections.defaultdict(list)
    for sl in lines:
        if not segs[sl]:
            continue
        rows, x = drivers.master_rows(PRED, sl)
        chip = SSI263(Params(over), out_rate=SR)
        ye = drivers.play_rows(chip, rows)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]   # first w0 is the mode set
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        le, lr = {}, {}
        for r in segs[sl]:
            k = int(r["pos"])
            if k + 1 >= len(loads):
                continue
            de = seg_db(ye, loads[k], loads[k + 1])
            dr = seg_db(yr, float(r["t_start_s"]), float(r["t_end_s"]))
            if de is not None and dr is not None:
                name = r["phoneme"]
                if name.split("'")[0] in STOPS:
                    name += " after stop" if r["prev"].split("'")[0] in STOPS else " after non-stop"
                le[k], lr[k] = (name, de), (name, dr)
        if not le:
            continue
        me, mr = max(v for _, v in le.values()), max(v for _, v in lr.values())
        for k in le:
            diff[le[k][0]].append(((le[k][1] - me), (lr[k][1] - mr)))
    print("per-phoneme level re the line's loudest (median dB): engine, real, engine-real")
    for ph in sorted(diff, key=lambda p: np.median([d[1] for d in diff[p]])):
        e = np.median([d[0] for d in diff[ph]])
        r = np.median([d[1] for d in diff[ph]])
        print("  %-22s n=%-3d %6.1f %6.1f %6.1f" % (ph, len(diff[ph]), e, r, e - r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lines", type=int, default=40)
    ap.add_argument("--set", action="append", default=[],
                    help="parameter override name=python-literal")
    a = ap.parse_args()
    over = {}
    for s in a.set:
        k, v = s.split("=", 1)
        over[k] = eval(v)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    Se = Sr = 0
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    lines = dev_lines(a.lines)
    for sl in lines:
        rows, x = drivers.master_rows(PRED, sl)
        chip = SSI263(Params(over), out_rate=SR)
        Se = Se + power_spectrum(drivers.play_rows(chip, rows))
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        y = wav.read(ev["line_marker"] - ev["text_sent"])
        Sr = Sr + power_spectrum(y if y.ndim == 1 else y[:, 0])
    levels(lines, over, utt, wav)
    e, r = bands_db(Se), bands_db(Sr)
    print("dev lines: %d (section B inv-r2 reps 0-1), overrides %s" % (len(lines), over or "none"))
    print("band Hz        engine   real   engine-real")
    for (lo, hi), ee, rr in zip(BANDS, e, r):
        print("%5d-%5d  %7.1f %7.1f %8.1f" % (lo, hi, ee, rr, ee - rr))


if __name__ == "__main__":
    main()
