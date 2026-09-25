"""Analyse driver_sim.py runs: python driver_report.py speakout nvda2026 py37 ..."""
import json
import os
import sys

import numpy as np

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
SR = 44100


def gaps(y, thr_db=-45):
    h = int(0.01 * SR)
    env = np.array([20 * np.log10(np.sqrt(np.mean(y[k:k + h] ** 2)) + 1e-9) for k in range(0, len(y) - h, h)])
    loud = env > thr_db
    on = np.where(loud)[0]
    out, run = [], 0
    for k in range(on[0], on[-1] + 1) if len(on) else ():
        if not loud[k]:
            run += 1
        else:
            if run >= 8:
                out.append(run * 10)
            run = 0
    return out


def f0(y):
    h = int(0.04 * SR)
    best = []
    for k in range(0, len(y) - h, int(0.02 * SR)):
        x = y[k:k + h]
        if np.sqrt(np.mean(x ** 2)) < 0.05 * (np.abs(y).max() + 1e-9):
            continue
        x = x - x.mean()
        ac = np.correlate(x, x, "full")[len(x) - 1:]
        i = int(SR / 260) + np.argmax(ac[int(SR / 260):int(SR / 55)])
        if ac[i] > 0.45 * ac[0]:
            best.append(SR / i)
    return np.median(best) if best else float("nan")


which, tags = sys.argv[1], sys.argv[2:]
runs = {t: json.load(open(os.path.join(HERE, "sim_%s_%s.json" % (which, t)))) for t in tags}
for t, r in runs.items():
    print("== %s %s: python %s %d-bit; player kwargs %s; cancel events %s; heavy modules %s"
          % (which, t, r["python"], r["bits"], r["player_kwargs"], r["cancel_events"], r["modules"]))
    for s in r["results"]:
        y = np.frombuffer(open(s["pcm"], "rb").read(), dtype="<i2").astype(float) / 32767
        extra = ""
        if "a" in s["label"].split()[-1:] or s["label"].startswith(("plain", "capital")):
            extra = " F0 %.1f Hz" % f0(y)
        print("  %-17s ok %-5s audio %.2f s, wall %.2f s, first feed %s ms, gaps %s, notified %s%s"
              % (s["label"], s["ok"], s["audio_s"], s["wall_s"],
                 "%.0f" % s["first_feed_ms"] if s["first_feed_ms"] is not None else "-",
                 gaps(y), s["notified"], extra))
if len(tags) >= 2:
    a, b = runs[tags[0]], runs[tags[1]]
    for sa, sb in zip(a["results"], b["results"]):
        ya = np.frombuffer(open(sa["pcm"], "rb").read(), dtype="<i2").astype(int)
        yb = np.frombuffer(open(sb["pcm"], "rb").read(), dtype="<i2").astype(int)
        n = min(len(ya), len(yb))
        print("  %s vs %s %-17s samples %d/%d, max diff %d LSB" % (tags[0], tags[1], sa["label"], len(ya), len(yb),
                                                                 int(np.abs(ya[:n] - yb[:n]).max()) if n else 0))
