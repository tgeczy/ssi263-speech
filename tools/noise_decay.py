"""How long does fricative noise linger into the next vowel?  Engine vs unit, dev only.

For S / T / SCH / HF'1 followed by a vowel (section B inv-r2 reps 0-1): the 5-9 kHz
band level in 4 ms frames, relative to its median over the fricative's middle;
reports the time after the fricative's end until that level has fallen 15 dB
(and 25 dB).  Vowels carry little 5-9 kHz energy, so a slow fall is noise
still running under the vowel -- Tomi's "breath underneath".
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

PRED = r"C:\git\blite_sweep\scripts\MASTER_predictions.jsonl"
MASTER = r"C:\git\blite_sweep\sessions\MASTER"
SR = 44100
FRIC = ("S", "T", "SCH", "HF'1")
VOWELS = {"E", "E1", "I", "A", "EH", "EH1", "AE", "AE1", "AH", "AH1", "AW", "O", "OU", "OO",
          "U", "U1", "UH", "UH1'1", "UH2", "UH3", "ER", "IE", "YI'1", "O'1", "OU'1", "U'1"}


def band_track(y, hop=0.004, win=0.008):
    h, w = int(hop * SR), int(win * SR)
    f = np.fft.rfftfreq(1024, 1 / SR)
    m = (f > 5000) & (f < 9000)
    wn = np.hanning(w)
    return np.array([10 * np.log10((np.abs(np.fft.rfft(y[i:i + w] * wn, 1024)) ** 2)[m].sum() + 1e-20)
                     for i in range(0, len(y) - w, h)])


def decay(track, a, b, hop=0.004):
    """ms after b until the band falls 15 / 25 dB below its level mid-fricative."""
    i0, i1 = int((a + 0.3 * (b - a)) / hop), int((a + 0.7 * (b - a)) / hop)
    if i1 <= i0:
        return None
    ref = np.median(track[i0:i1])
    k = int(b / hop)
    out = []
    for drop in (15, 25):
        j = next((j for j in range(k - 10, min(len(track), k + 100)) if j >= 0 and track[j] < ref - drop), None)
        out.append(None if j is None else (j - k) * hop * 1000)
    return out


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
                    and r["phoneme"] in FRIC and r["next"] in VOWELS):
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    res = collections.defaultdict(lambda: ([], []))
    for sl in sorted(segs)[:80]:
        rows, x = drivers.master_rows(PRED, sl)
        chip = SSI263(Params(over), out_rate=SR)
        ye = drivers.play_rows(chip, rows)
        loads = [t for t, e in chip.log if e.startswith("w0=")][1:]
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["end_with_tail"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        te, tr = band_track(ye), band_track(yr)
        for r in segs[sl]:
            k = int(r["pos"])
            if k + 1 >= len(loads):
                continue
            de = decay(te, loads[k], loads[k + 1])
            dr = decay(tr, float(r["t_start_s"]), float(r["t_end_s"]))
            if de and dr:
                res[r["phoneme"]][0].append(de)
                res[r["phoneme"]][1].append(dr)
    print("overrides:", over or "none")
    print("fricative  n   ms to -15 dB (unit / engine)   ms to -25 dB (unit / engine)")
    for ph in FRIC:
        e, r = res[ph]
        if not e:
            continue
        med = lambda L, i: np.median([v[i] for v in L if v[i] is not None]) if any(v[i] is not None for v in L) else float("nan")
        print("%-8s %3d      %6.1f / %6.1f              %6.1f / %6.1f"
              % (ph, len(e), med(r, 0), med(e, 0), med(r, 1), med(e, 1)))


if __name__ == "__main__":
    main()
