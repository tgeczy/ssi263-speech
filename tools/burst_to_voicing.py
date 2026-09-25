"""Burst to voicing onset, inside each signal's own audio: no aligned boundary needed.  Dev only.

The v0.10 release rule (a stop releases 1.25 frames before its end) rests on the unit's bursts
sitting ~1 frame before phoneme_tracks' aligned boundary.  The chip cannot see the next
phoneme early (A/R fires once the duration is exceeded, data sheet p.3), yet the unit does
not burst before HVC (tools/stop_pair_release.py).  If the unit's burst is really at the next
phoneme's load, the interval from burst to the vowel's voicing onset is short (the engine's
own voicing starts ~0.2 frames after a load); if the release is early, it is ~1 frame (the
engine's now: ~1.2).

For T / K / P -> vowel: burst = the 5-9 kHz envelope peak in [boundary - 1.5, + 0.5] frames;
its prominence is that peak over the closure's median (stop start + 30 % .. boundary - 1.5
frames).  Unit tokens count only with prominence >= 10 dB.  Voicing onset = the first hop
after the burst where 100-1000 Hz energy is within 10 dB of its median 40-80 ms after the
boundary.  Everything in frames of the line's rate.

    python tools/burst_to_voicing.py
"""
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


def measure(envl, envh, a, b, frame):
    """(interval burst->voicing in frames, burst prominence dB) or None."""
    H = O.HOP
    i0, i1 = int(max(0, b - 1.5 * frame) / H), int((b + 0.5 * frame) / H)
    c0, c1 = int((a + 0.3 * (b - a)) / H), int((b - 1.5 * frame) / H)
    if i1 <= i0 or c1 - c0 < 3:
        return None
    k = i0 + int(np.argmax(envh[i0:i1]))
    prom = 10 * np.log10(envh[k] / np.median(envh[c0:c1]))
    ref = np.median(envl[int((b + 0.04) / H):int((b + 0.08) / H)])
    for i in range(k, min(len(envl), int((b + 0.12) / H))):
        if envl[i] > ref * 0.1:
            return (i - k) * H / frame, prom
    return None


def main(over=None):
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
            if O.base(r["phoneme"]) in ("T", "K", "P") and O.base(n["phoneme"]) in O.VOWELS:
                want[sl].append((int(r["pos"]), O.base(r["phoneme"])))
    utt = {}
    with open(os.path.join(O.MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(O.MASTER, "master.wav"))
    res = collections.defaultdict(lambda: collections.defaultdict(list))
    for sl in sorted(want):
        try:
            rows, x = drivers.master_rows(O.PRED, sl)
        except Exception:
            continue
        chip = SSI263(Params(over or {}), out_rate=O.SR)
        ye = np.asarray(drivers.play_rows(chip, rows), float)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        rs = {int(r["pos"]): r for r in segs[sl]}
        lr, hr = O.band_env(yr, O.LOW), O.band_env(yr, O.HIGH)
        le, he = O.band_env(ye, O.LOW), O.band_env(ye, O.HIGH)
        for k, st in want[sl]:
            if k + 1 >= len(loads):
                continue
            frame = 4096 * (16 - int(rs[k]["R_chip"])) / 1e6
            dr = rs[k]["dr"]
            u = measure(lr, hr, float(rs[k]["t_start_s"]), float(rs[k]["t_end_s"]), frame)
            e = measure(le, he, loads[k], loads[k + 1], frame)
            if u:
                res[st]["unit all"].append(u[0])
                if u[1] >= 10:
                    res[st]["unit burst>=10dB"].append(u[0])
                    res[st]["unit burst>=10dB DR" + dr].append(u[0])
            if e:
                res[st]["engine"].append(e[0])
    print("burst -> voicing onset, frames: median [IQR] n")
    for st in ("T", "K", "P"):
        for key in sorted(res[st]):
            v = res[st][key]
            q = np.percentile(v, [25, 50, 75])
            print("  %s  %-22s %5.2f [%5.2f %5.2f]  n=%d" % (st, key, q[1], q[0], q[2], len(v)))


if __name__ == "__main__":
    main()
