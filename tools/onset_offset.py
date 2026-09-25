"""Where do the unit's events sit against phoneme_tracks' aligned boundaries?  Dev lines only.

The v0.10 release rule (a stop releases 1.25 frames before its end) rests on the unit's stop
bursts sitting about a frame before the aligned boundary (tools/burst_tail.py).  But the chip
cannot know the next phoneme a frame early (A/R asks only once the duration is exceeded, data
sheet p.3), and the firmware writes K identically before HVC ("give") and before a vowel
("kit"), yet the unit releases only before the vowel (tools/stop_pair_release.py).

So: is the alignment itself early or late?  For X -> vowel pairs, the time the vowel's voicing
starts (100-1000 Hz energy first within 10 dB of its level 40-80 ms into the vowel) against
the boundary, in frames, unit (aligned boundary) against engine (its own load).  Nasals and
fricatives have no burst, so their onset lag is the alignment's own offset.  For stops the
5-9 kHz burst peak is reported too.

    python tools/onset_offset.py
"""
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
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import repo_paths             # noqa: E402
from ssi263 import SSI263, Params, drivers  # noqa: E402

PRED = repo_paths.master_predictions()
MASTER = repo_paths.master_session()
SR = 44100
VOWELS = {"E", "E1", "Y", "YI", "AY", "IE", "I", "A", "AI", "EH", "EH1", "AE", "AE1", "AH", "AH1", "AW",
          "O", "OU", "OO", "IU", "IU1", "U", "U1", "UH", "UH1", "UH2", "UH3", "ER"}
CLASSES = {"fricative": {"S", "SCH", "F", "TH", "HF"}, "nasal": {"M", "N"}, "stop": {"T", "K", "P"}}
LOW = butter(4, [100, 1000], "bandpass", fs=SR, output="sos")
HIGH = butter(4, [5000, 9000], "bandpass", fs=SR, output="sos")
HOP = 0.0025


def base(n):
    return n.split("'")[0]


def band_env(y, sos):
    z = sosfilt(sos, y) ** 2
    h = int(HOP * SR)
    w = int(0.01 * SR)
    return np.array([np.mean(z[k:k + w]) for k in range(0, len(z) - w, h)]) + 1e-20


def onset(envl, b, frame):
    """Voicing onset near boundary b (s): first hop from b - 2 frames where the low band is
    within 10 dB of its median 40-80 ms after b."""
    i0 = int(max(0, b - 2 * frame) / HOP)
    ref = np.median(envl[int((b + 0.04) / HOP):int((b + 0.08) / HOP)])
    for i in range(i0, min(len(envl), int((b + 0.1) / HOP))):
        if envl[i] > ref * 0.1:
            return (i * HOP + 0.005 - b) / frame
    return None


def burst(envh, b, frame):
    i0, i1 = int(max(0, b - 1.5 * frame) / HOP), int((b + 0.5 * frame) / HOP)
    if i1 <= i0:
        return None
    k = i0 + int(np.argmax(envh[i0:i1]))
    return (k * HOP + 0.005 - b) / frame


def main():
    held = {int(l.split()[0]) for l in open(os.path.join(ENGINE, "holdout_lines.txt")) if l[:1].isdigit()}
    segs = collections.defaultdict(list)
    with open(os.path.join(MASTER, "phoneme_segments.csv")) as f:
        for r in csv.DictReader(f):
            sl = int(r["script_line"])
            if r["split"] == "dev" and sl not in held:
                segs[sl].append(r)
    want = {}
    for sl, rs in segs.items():
        rs.sort(key=lambda r: int(r["pos"]))
        for r, n in zip(rs, rs[1:]):
            if base(n["phoneme"]) in VOWELS:
                for cls, ph in CLASSES.items():
                    if base(r["phoneme"]) in ph:
                        want.setdefault(sl, []).append((int(r["pos"]), cls))
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    res = collections.defaultdict(lambda: collections.defaultdict(list))
    for sl in sorted(want)[:300]:
        try:
            rows, x = drivers.master_rows(PRED, sl)
        except Exception:
            continue
        chip = SSI263(out_rate=SR)
        ye = np.asarray(drivers.play_rows(chip, rows), float)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        rs = {int(r["pos"]): r for r in segs[sl]}
        lr, hr, le, he = band_env(yr, LOW), band_env(yr, HIGH), band_env(ye, LOW), band_env(ye, HIGH)
        for k, cls in want[sl]:
            if k + 1 >= len(loads):
                continue
            rate = int(rs[k]["R_chip"])
            frame = 4096 * (16 - rate) / 1e6
            bu, be = float(rs[k]["t_end_s"]), loads[k + 1]
            for side, envl, envh, b in (("unit", lr, hr, bu), ("engine", le, he, be)):
                o = onset(envl, b, frame)
                if o is not None:
                    res[cls][side + " onset"].append(o)
                if cls == "stop":
                    bb = burst(envh, b, frame)
                    if bb is not None:
                        res[cls][side + " burst"].append(bb)
    print("frames relative to the boundary (unit: aligned t_end; engine: its own next load); median [IQR], n")
    for cls in ("nasal", "fricative", "stop"):
        for key in ("unit onset", "engine onset", "unit burst", "engine burst"):
            v = res[cls].get(key)
            if v:
                q = np.percentile(v, [25, 50, 75])
                print("  %-9s %-13s %+5.2f [%+5.2f %+5.2f]  n=%d" % (cls, key, q[1], q[0], q[2], len(v)))


if __name__ == "__main__":
    main()
