"""A stop followed by another closure: does the first one release?  Dev lines only.

Tomi (2026-09-25), on the Braille Lite add-on against his memory of the unit: "program" has
a glottal stop between prog and gram, with one hard G on the "gram" side, where ours adds a
softer G before the stop too; and the "ge" of "manager" and "image" is a Hungarian "dzs"
with a glottal stop before it.  The firmware writes the hard g as K + HVC and "ge" as D'2 + J,
so both are a stop whose next phoneme keeps the tract closed.

For every dev segment pair (K -> HVC, D -> J, D -> D, and any stop -> closure pair), level in
5 ms frames re the line's loudest segment, over the first stop's second half, the boundary
and the next phoneme's first 60 ms: unit (frozen segment times) against engine (its own
phoneme loads).  A release burst shows as a peak in the first stop's last frames.

    python tools/stop_pair_release.py [--set name=value ...]
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
HOP = 0.005
PAIRS = {("K", "HVC"), ("D", "J"), ("D", "D"), ("T", "SCH"), ("K", "K"), ("P", "HFC")}


def base(name):
    return name.split("'")[0]


def env(y, t0, t1, ref):
    out = []
    t = t0
    while t < t1 - 1e-9:
        i, j = int(t * SR), int((t + HOP) * SR)
        x = y[max(0, i):max(0, j)]
        out.append(10 * np.log10(np.mean(x ** 2) + 1e-20) - ref if len(x) else -99.0)
        t += HOP
    return out


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
    with open(os.path.join(MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            sl = int(r["script_line"])
            if r["split"] == "dev" and sl not in held:
                segs[sl].append(r)
    cases = []
    for sl, rs in segs.items():
        rs.sort(key=lambda r: int(r["pos"]))
        for r, n in zip(rs, rs[1:]):
            if (base(r["phoneme"]), base(n["phoneme"])) in PAIRS:
                cases.append((sl, int(r["pos"])))
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    groups = collections.defaultdict(lambda: {"unit": [], "engine": []})
    for sl in sorted({c[0] for c in cases}):
        try:
            rows, x = drivers.master_rows(PRED, sl)
        except Exception:
            continue
        chip = SSI263(Params(over), out_rate=SR)
        ye = np.asarray(drivers.play_rows(chip, rows), float)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        rs = {int(r["pos"]): r for r in segs[sl]}
        # each side's reference: its loudest audible segment's RMS
        def seg_rms(y, a_, b_):
            x = y[int(a_ * SR):int(b_ * SR)]
            return 10 * np.log10(np.mean(x ** 2) + 1e-20) if len(x) > 64 else -99.0
        ks = sorted(k for k in rs if k + 1 < len(loads))
        if not ks:
            continue
        ref_u = max(seg_rms(yr, float(rs[k]["t_start_s"]), float(rs[k]["t_end_s"])) for k in ks)
        ref_e = max(seg_rms(ye, loads[k], loads[k + 1]) for k in ks)
        for s2, k in cases:
            if s2 != sl or k + 2 >= len(loads) or k + 1 not in rs:
                continue
            r, n = rs[k], rs[k + 1]
            key = "%s -> %s" % (r["phoneme"], base(n["phoneme"]))
            ua, ub = float(r["t_start_s"]), float(r["t_end_s"])
            ea, eb = loads[k], loads[k + 1]
            # second half of the first stop, then 60 ms of the next phoneme, on each side's clock
            u = env(yr, ua + 0.5 * (ub - ua), ub + 0.06, ref_u)
            e = env(ye, ea + 0.5 * (eb - ea), eb + 0.06, ref_e)
            nb_u = int(round((ub - (ua + 0.5 * (ub - ua))) / HOP))
            nb_e = int(round((eb - (ea + 0.5 * (eb - ea))) / HOP))
            groups[key]["unit"].append((u, nb_u, sl, r["word"], r["rate"]))
            groups[key]["engine"].append((e, nb_e, sl, r["word"], r["rate"]))
    for key in sorted(groups):
        g = groups[key]
        print("== %s: %d tokens (%s)" % (key, len(g["unit"]),
              ", ".join(sorted({"%s r%s" % (w, rt) for _, _, _, w, rt in g["unit"]}))))
        for side in ("unit", "engine"):
            peaks, holds, nexts = [], [], []
            for v, nb, *_ in g[side]:
                tail = v[max(0, nb - 6):nb]           # last 30 ms of the first stop
                peaks.append(max(tail) if tail else -99)
                holds.append(np.median(v[:max(1, nb - 6)]))
                nexts.append(max(v[nb:nb + 12]) if len(v) > nb else -99)
            print("   %-6s first stop, 2nd half before its last 30 ms: median %6.1f dB | last 30 ms peak: median %6.1f dB"
                  " | next phoneme's first 60 ms peak: median %6.1f dB"
                  % (side, np.median(holds), np.median(peaks), np.median(nexts)))
        v, nb, sl, w, rt = g["unit"][0]
        ve = g["engine"][0][0]
        nbe = g["engine"][0][1]
        print("   e.g. line %d '%s' rate %s, 5 ms frames, | = boundary" % (sl, w, rt))
        print("     unit   " + " ".join(("|" if i == nb else "") + "%.0f" % x for i, x in enumerate(v)))
        print("     engine " + " ".join(("|" if i == nbe else "") + "%.0f" % x for i, x in enumerate(ve)))


if __name__ == "__main__":
    main()
