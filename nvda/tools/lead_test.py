"""First-audio latency and gaps for multi-sentence text, packing on vs off."""
import os
import sys
import time

import numpy as np

sys.argv = [sys.argv[0], sys.argv[1]]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "boundary_test.py"),
          encoding="utf-8").read().split("for it in range(3):")[0])


def gaps(y, sr=44100, thr_db=-45):
    h = int(0.01 * sr)
    env = np.array([20 * np.log10(np.sqrt(np.mean(y[k:k + h] ** 2)) + 1e-9) for k in range(0, len(y) - h, h)])
    loud = env > thr_db
    on = np.where(loud)[0]
    out, run_ = [], 0
    for k in range(on[0], on[-1] + 1) if len(on) else ():
        if not loud[k]:
            run_ += 1
        else:
            if run_ >= 8:
                out.append(run_ * 10)
            run_ = 0
    return out


TEXT = ("Settings. Voice settings. Synthesizer: combo box. Blazie. Collapsed. Alt plus y. Press enter. "
        "Tab for more.")
for short in (False, True, False, True):
    d._short = short
    n0 = len(d._player.chunks)
    lead, tail = run([TEXT])
    y = audio_since(n0)
    print("pack=%-5s first sound %3.0f ms, total %.2f s, gaps %s" % (short, lead * 1e3, len(y) / 44100, gaps(y)))
d.terminate()
