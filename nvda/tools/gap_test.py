"""Silence gaps in 'Select synthesizer dialog. Synthesizer: combo box' through the real driver."""
import os
import sys
import time

import numpy as np

sys.argv = [sys.argv[0], sys.argv[1]]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(2.0)


def gaps(y, sr=44100, thr_db=-45):
    h = int(0.01 * sr)
    env = np.array([20 * np.log10(np.sqrt(np.mean(y[k:k + h] ** 2)) + 1e-9) for k in range(0, len(y) - h, h)])
    loud = env > thr_db
    on = np.where(loud)[0]
    if not len(on):
        return []
    out, run = [], 0
    for k in range(on[0], on[-1] + 1):
        if not loud[k]:
            run += 1
        else:
            if run >= 8:
                out.append((round(k / 100 - run / 100, 2), run * 10))
            run = 0
    return out


for label, seq in (("one string", ["Select synthesizer dialog. Synthesizer: combo box"]),
                   ("NVDA pieces", ["Select synthesizer", "dialog", "Synthesizer:", "combo box"])):
    for extra in (None,) + (tuple(sys.argv[2:]) if len(sys.argv) > 2 else ()):
        mark = len(notified)
        n0 = len(d._player.chunks)
        d.speak(seq)
        wait_idle()
        y = audio_since(n0)
        print("%-12s %.2f s; silences >= 80 ms (at s, ms): %s" % (label, len(y) / 44100, gaps(y)))
d.terminate()
