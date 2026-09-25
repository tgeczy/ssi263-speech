"""Cut the real Braille Lite's own words out of the Reclaim recording, beside ours, for listening.

The recording (Tomi, 2026-09-26): the unit reading reclaim.txt off the line, 9.7 min, at the
factory settings (F0 81.5 Hz = r1 45h, the warm-reset pitch), one section skipped.  It is
DEV material.  Each target word is rendered through the emulated unit (bns_live, factory
menu) and slid along the recording's log-mel spectrum; the best non-overlapping matches
(as many as the text has) are cut with 60 ms margins.

Output, per word: out/reclaim-<word>.wav = up to 6 unit tokens, then ours, 0.4 s apart,
and a match report.

    python tools/reclaim_words.py [word ...]
"""
import os
import re
import sys
import wave

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(os.path.dirname(HERE), "src")
sys.path.insert(0, ENGINE)
from hosts.blazie import Blazie          # noqa: E402
from ssi263 import SSI263                # noqa: E402

REC = r"C:\git\os2\reclaim-full.wav"
TXT = r"C:\git\bns_backup\reclaim.txt"
B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
OUT = os.path.join(os.path.dirname(ENGINE), "investigation", "out")
SR = 44100
HOP = 0.01
EDGES = np.geomspace(150, 5000, 25)
GAP = np.zeros(int(0.4 * SR))


def features(y):
    win, hop = int(0.025 * SR), int(HOP * SR)
    f = np.fft.rfftfreq(win, 1 / SR)
    idx = [(f >= lo) & (f < hi) for lo, hi in zip(EDGES[:-1], EDGES[1:])]
    w = np.hanning(win)
    out = []
    for k in range(0, len(y) - win, hop):
        p = np.abs(np.fft.rfft(y[k:k + win] * w)) ** 2
        out.append([np.log10(p[i].sum() + 1e-9) for i in idx])
    return np.array(out)


def render(words):
    u = Blazie(B + r"\bns_live.exe", B + r"\BL2ENG.BNS", B + r"\bl2_2003_warm.state",
               chip=SSI263(dsp="c"), menu=())
    u.run(0.3)
    out = {}
    for w in words:
        u.say([w + "."])
        got = []
        while True:
            got.append(np.asarray(u.run(0.02), float))
            if not u.busy():
                break
        y = np.concatenate(got)
        on = np.where(np.abs(y) > 0.003)[0]
        out[w] = y[on[0]:on[-1]] if len(on) else y
    u.close()
    return out


def match(F, T, n):
    """Top-n non-overlapping positions of template T in F (mean-removed rows, cosine)."""
    Tn = T - T.mean(axis=1, keepdims=True)
    Fn = F - F.mean(axis=1, keepdims=True)
    L = len(T)
    view = np.lib.stride_tricks.sliding_window_view(Fn, (L, F.shape[1]))[:, 0]
    num = np.einsum("ijk,jk->i", view, Tn)
    den = np.sqrt(np.einsum("ijk,ijk->i", view, view)) * np.sqrt((Tn ** 2).sum()) + 1e-9
    score = num / den
    picks = []
    for i in np.argsort(-score):
        if all(abs(i - j) > L for j, _ in picks):
            picks.append((int(i), float(score[i])))
        if len(picks) == n:
            break
    return sorted(picks)


def dtw_cost(A, B):
    """Mean per-step cost of the best warping path (cosine distance of mean-removed frames)."""
    An = A - A.mean(axis=1, keepdims=True)
    Bn = B - B.mean(axis=1, keepdims=True)
    An /= np.linalg.norm(An, axis=1, keepdims=True) + 1e-9
    Bn /= np.linalg.norm(Bn, axis=1, keepdims=True) + 1e-9
    C = 1.0 - An @ Bn.T
    n, m = C.shape
    D = np.full((n + 1, m + 1), np.inf)
    D[0, 0] = 0.0
    L = np.zeros((n + 1, m + 1))
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            k = np.argmin((D[i - 1, j - 1], D[i - 1, j], D[i, j - 1]))
            prev = ((i - 1, j - 1), (i - 1, j), (i, j - 1))[k]
            D[i, j] = C[i - 1, j - 1] + D[prev]
            L[i, j] = L[prev] + 1
    return D[n, m] / L[n, m]


def rescore(F, T, picks, n):
    """Re-rank candidates by time-warped cost (tolerates tempo and intonation), keep n."""
    L = len(T)
    scored = []
    for i, s in picks:
        a, b = max(0, i - L // 5), min(len(F), i + L + L // 5)
        best = min((dtw_cost(T, F[a + da:b - db]), a + da) for da in (0, L // 10) for db in (0, L // 10))
        scored.append((best[0], i))
    scored.sort()
    return [(i, c) for c, i in scored[:n]], [c for c, _ in scored]


def save(path, y):
    y = y / (np.abs(y).max() + 1e-9) * 0.7
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())


def main():
    words = sys.argv[1:] or ["program", "programs", "message", "pages", "management", "storage", "large"]
    text = open(TXT, "rb").read().decode("latin-1").lower()
    rec, sr = sf.read(REC)
    rec = rec if rec.ndim == 1 else rec[:, 0]
    assert sr == SR
    print("features of the recording ...", flush=True)
    F = features(rec)
    ours = render(words)
    for w in words:
        n = len(re.findall(r"\b%s\b" % w, text))
        T = features(ours[w])
        cands = match(F, T, max(3 * n, n + 4))
        picks, costs = rescore(F, T, cands, n)
        clips = []
        for i, s in picks[:6]:
            a, b = int((i * HOP - 0.06) * SR), int(((i + len(T)) * HOP + 0.06) * SR)
            clips += [rec[max(0, a):b], GAP]
        clips.append(ours[w])
        path = os.path.join(OUT, "reclaim-%s.wav" % w)
        save(path, np.concatenate(clips))
        print("%-11s in text %2d; warped cost of the kept %s | best rejected %.3f -> %s"
              % (w, n, " ".join("%.3f" % c for _, c in picks[:6]), costs[n] if len(costs) > n else float("nan"),
                 os.path.basename(path)), flush=True)


if __name__ == "__main__":
    main()
