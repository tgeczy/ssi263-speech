"""How fast do formant transitions run?  Engine vs the real unit, dev lines only.

v2 (after Astra's Reply 26): v1 searched only 120 ms ahead and read every
ramp longer than that as ~96 ms.  Now:
  1. 1-4 kHz spectral centroid, 2 ms hop, 10 ms window, 5-frame median smoothing;
  2. the largest rise whose peak lies within 500 ms of its start (voiced frames);
  3. a plateau-ramp-plateau least-squares fit on that stretch plus 60 ms either
     side; the 10-90 % time is 0.8 x the fitted ramp length.
`--selftest` must recover synthetic linear formant ramps (40-300 ms) before the
real numbers mean anything.  Analysis is identical on engine and unit, which
does not by itself make its bias cancel.
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import medfilt

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import repo_paths             # noqa: E402
from ssi263 import SSI263, Params, drivers  # noqa: E402

PRED = repo_paths.master_predictions()
MASTER = repo_paths.master_session()
SR = 44100
HOP = 0.002
LINES = [462, 2513, 3414, 578, 2501, 3411]      # berry / very at rates 2, 10, 14 (tone 13)


def centroid_track(y, hop=HOP, win=0.010):
    h, w = int(hop * SR), int(win * SR)
    f = np.fft.rfftfreq(2048, 1 / SR)
    band = (f > 1000) & (f < 4000)
    win_ = np.hanning(w)
    c, e = [], []
    for i in range(0, len(y) - w, h):
        S = np.abs(np.fft.rfft(y[i:i + w] * win_, 2048)) ** 2
        p = S[band]
        c.append((f[band] * p).sum() / (p.sum() + 1e-20))
        e.append(10 * np.log10(np.mean(y[i:i + w] ** 2) + 1e-20))
    return medfilt(np.array(c), 5), np.array(e)


def ramp_fit(c):
    """Best plateau-ramp-plateau fit; returns (t0, t1) in frames."""
    n = len(c)
    best = (np.inf, 0, 1)
    for t0 in range(0, n - 2):
        for t1 in range(t0 + 2, n, 1 if n < 120 else 2):
            x = np.clip((np.arange(n) - t0) / (t1 - t0), 0, 1)
            A = np.column_stack([np.ones(n), x])
            coef, res, *_ = np.linalg.lstsq(A, c, rcond=None)
            r = ((A @ coef - c) ** 2).sum()
            if r < best[0]:
                best = (r, t0, t1)
    return best[1], best[2]


def rise_time(c, e, look=250, pad=30):
    loud = e > e.max() - 25
    best = None
    for i in range(len(c)):
        if not loud[i]:
            continue
        seg = c[i:i + look]
        j = i + int(np.argmax(np.where(loud[i:i + look], seg, -np.inf)))
        if best is None or c[j] - c[i] > best[0]:
            best = (c[j] - c[i], i, j)
    if best is None or best[0] < 300:
        return None
    _, i, j = best
    a, b = max(0, i - pad), min(len(c), j + pad)
    idx = [k for k in range(a, b) if loud[k]]
    if len(idx) < 8:
        return None
    t0, t1 = ramp_fit(c[idx[0]:idx[-1] + 1])
    return 0.8 * (t1 - t0) * HOP * 1000.0, best[0]


def selftest():
    """Pulse train (122 Hz) through F1 500 Hz and an F2 ramping 1200 -> 3000 Hz."""
    from scipy.signal import lfilter
    print("self-test: true 10-90 % vs measured (ms); pass = within 10 % (or 4 ms)")
    fails = 0
    for T in (0.040, 0.100, 0.144, 0.180, 0.240, 0.300):
        n = int(0.9 * SR)
        x = np.zeros(n)
        x[::int(SR / 122.07)] = 1.0
        y = lfilter([1], [1, -2 * 0.99 * np.cos(2 * np.pi * 500 / SR), 0.99 ** 2], x)
        t = np.arange(n) / SR
        f2 = 1200 + 1800 * np.clip((t - 0.3) / T, 0, 1)
        out = np.zeros(n)
        y1 = y2 = 0.0
        for k in range(n):
            th = 2 * np.pi * f2[k] / SR
            r = np.exp(-np.pi * 100 / SR)
            v = y[k] + 2 * r * np.cos(th) * y1 - r * r * y2
            y2, y1 = y1, v
            out[k] = v
        m = rise_time(*centroid_track(out))
        true = 0.8 * T * 1000
        ok = m is not None and abs(m[0] - true) <= max(4.0, 0.10 * true)
        fails += not ok
        print("  %5.0f  %s  %s" % (true, "%6.1f" % m[0] if m else "  n/a", "pass" if ok else "FAIL (>10 %)"))
    if fails:
        raise SystemExit("self-test: %d ramp(s) outside 10 %%" % fails)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return
    over = {k: eval(v) for k, v in (s.split("=", 1) for s in a.set)}
    held = {int(l.split()[0]) for l in open(os.path.join(ENGINE, "holdout_lines.txt")) if l[:1].isdigit()}
    utt = {}
    with open(os.path.join(MASTER, "utterances.jsonl")) as f:
        for line in f:
            u = json.loads(line)
            utt[u["script_line"]] = u
    wav = sf.SoundFile(os.path.join(MASTER, "master.wav"))
    print("overrides:", over or "none")
    print("line  word    rate   unit 10-90% ms (span Hz)   engine 10-90% ms (span Hz)")
    for sl in LINES:
        assert sl not in held
        rows, x = drivers.master_rows(PRED, sl)
        ye = drivers.play_rows(SSI263(Params(over), out_rate=SR), rows)
        ev = utt[sl]["events"]
        wav.seek(ev["text_sent"])
        yr = wav.read(ev["line_marker"] - ev["text_sent"])
        yr = yr if yr.ndim == 1 else yr[:, 0]
        r = rise_time(*centroid_track(yr))
        e = rise_time(*centroid_track(ye))
        fmt = lambda v: "%6.1f (%4.0f)" % v if v else "   n/a      "
        print("%5d %-7s %4d   %s              %s" % (sl, x["text"], int(x["r2"][-1], 16) >> 4, fmt(r), fmt(e)))


if __name__ == "__main__":
    main()
