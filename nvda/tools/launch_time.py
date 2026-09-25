"""Launch latency as NVDA sees it: SynthDriver() then speak("Hello.") at once; time to the
first audible block fed.  python launch_time.py speakout|blazie"""
import os
import sys
import time

WHICH = sys.argv[1]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
           encoding="utf-8").read()
exec(src.split("d = drv_mod.SynthDriver()")[0])
import numpy as np  # noqa: E402

for trial in range(3):
    t0 = time.perf_counter()
    d = drv_mod.SynthDriver()
    d.speak(["Hello."])
    first = None
    while time.perf_counter() - t0 < 10:
        with d._player.lock:
            for c in d._player.chunks:
                if np.abs(c).max() > 0.01:
                    first = time.perf_counter()
                    break
        if first:
            break
        time.sleep(0.002)
    print("%s launch %d: first sound %.0f ms after SynthDriver()" % (WHICH, trial, (first - t0) * 1e3))
    d.terminate()
    time.sleep(0.3)
