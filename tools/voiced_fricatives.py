"""Is there voicing under the voiced fricatives?  Dev only.

Tomi (2026-09-25): the unit's "dzs" in "storage" is harder and harsher than ours.  On the
Reclaim token the unit's J had ~13 dB more energy below 1.5 kHz than ours; the ROM gives
J, Z and THV a VA of 1 (V: 3), so ours are all but unvoiced.  For every dev segment of
a fricative, the middle half: the 80-600 Hz level (voicing) minus the 1.5-6 kHz level
(frication), unit against engine.  The voiceless fricatives (S SCH F TH) are the control:
their low band is the capture path's and the model's floor, not voicing.

    python tools/voiced_fricatives.py [--set name=value ...]
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

LOWB = butter(4, [80, 600], "bandpass", fs=O.SR, output="sos")
HIB = butter(4, [1500, 6000], "bandpass", fs=O.SR, output="sos")
VOICED = ("J", "Z", "THV", "V")
VOICELESS = ("S", "SCH", "F", "TH")


def ratio(y, a, b):
    x = y[int((a + 0.25 * (b - a)) * O.SR):int((b - 0.25 * (b - a)) * O.SR)]
    if len(x) < 400:
        return None
    # voicing = periodicity of the low band at the pitch period (60-160 Hz), 0..1
    z = sosfilt(LOWB, x)
    z = z - z.mean()
    ac = np.correlate(z, z, "full")[len(z) - 1:]
    lo_lag, hi_lag = int(O.SR / 160), int(O.SR / 60)
    if ac[0] <= 0 or len(ac) <= hi_lag:
        return None
    return float(ac[lo_lag:hi_lag].max() / ac[0])


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
            if r["split"] == "dev" and sl not in held and O.base(r["phoneme"]) in VOICED + VOICELESS:
                segs[sl].append(r)
    utt = {}
    with open(os.path.join(O.MASTER, "utterances.jsonl")) as f:
        for l in f:
            u = json.loads(l)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(O.MASTER, "master.wav"))
    res = collections.defaultdict(lambda: {"unit": [], "engine": []})
    for sl in sorted(segs)[:400]:
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
        for r in segs[sl]:
            k = int(r["pos"])
            if k + 1 >= len(loads):
                continue
            u = ratio(yr, float(r["t_start_s"]), float(r["t_end_s"]))
            e = ratio(ye, loads[k], loads[k + 1])
            if u is not None and e is not None:
                res[O.base(r["phoneme"])]["unit"].append(u)
                res[O.base(r["phoneme"])]["engine"].append(e)
    print("voicing: periodicity of the 80-600 Hz band at the pitch period (0 none .. 1 fully voiced): unit / engine medians   %s" % (over or "defaults"))
    for ph in VOICED + VOICELESS:
        u, e = res[ph]["unit"], res[ph]["engine"]
        if u:
            print("  %-4s %-9s unit %5.2f  engine %5.2f  unit-engine %+5.2f  n=%d"
                  % (ph, "voiced" if ph in VOICED else "voiceless", np.median(u), np.median(e),
                     np.median(u) - np.median(e), len(u)))


if __name__ == "__main__":
    main()
