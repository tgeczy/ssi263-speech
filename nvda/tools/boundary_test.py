"""Gap at an NVDA utterance boundary: speak(A); on done, speak(B).
trail = silence fed after A's last sound (it plays before the done index fires);
lead = wall time from speak(B) to B's first sound being fed + leading silence in B."""
import os
import sys
import time

import numpy as np

sys.argv = [sys.argv[0], sys.argv[1]]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(2.0)
SR = 44100
THR = 10 ** (-45 / 20)
fed = []
_orig = d._player.feed


def feed(data, onDone=None):
    if data:
        x = np.frombuffer(data, dtype="<i2").astype(float) / 32767
        loud = np.where(np.abs(x) > THR)[0]
        fed.append((time.time(), len(x), loud[0] if len(loud) else None, loud[-1] if len(loud) else None))
    _orig(data, onDone)


d._player.feed = feed


def run(seq):
    global mark
    mark = len(notified)
    k = len(fed)
    t0 = time.time()
    d.speak(seq)
    wait_idle()
    part = fed[k:]
    first = next(((t, lo) for t, n, lo, hi in part if lo is not None), None)
    lead = (first[0] - t0 + first[1] / SR) if first else float("nan")
    tail, total = 0, 0
    for t, n, lo, hi in reversed(part):
        if hi is None:
            tail += n
        else:
            tail += n - 1 - hi
            break
    return lead, tail / SR


for it in range(3):
    la, ta = run(["Select synthesizer", "dialog"])
    lb, tb = run(["Synthesizer:", "combo box", "Speak-Out"])
    print("A lead %3.0f ms trail %3.0f ms | B lead %3.0f ms trail %3.0f ms | boundary ~%3.0f ms"
          % (la * 1e3, ta * 1e3, lb * 1e3, tb * 1e3, (ta + lb) * 1e3))
d.terminate()
