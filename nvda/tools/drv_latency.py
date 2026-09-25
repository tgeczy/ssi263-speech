"""speak() to the first audible block fed, through the real driver (stand-in NVDA).
python drv_latency.py speakout|blazie"""
import os
import sys
import time

WHICH = sys.argv[1]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
           encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
TEXTS = {"short": ["Hello."], "tab": ["Select synthesizer", "dialog"],
         "long": ["Custom number processing  left paren (fix digits above a trillion  right paren ) ",
                  "check box", "not checked", "Alt plus ", "u"]}
for name, seq in TEXTS.items():
    res = []
    for rep in range(3):
        mark = len(notified)
        n0 = len(d._player.chunks)
        t0 = time.perf_counter()
        d.speak(seq)
        first = lead_ms = None
        while time.perf_counter() - t0 < 10:
            with d._player.lock:
                chunks = d._player.chunks[n0:]
            fed = 0
            for c in chunks:
                k = np.where(np.abs(c) > 0.01)[0]
                if len(k):
                    first = time.perf_counter()
                    lead_ms = (fed + k[0]) / 44.1
                    break
                fed += len(c)
            if first:
                break
            time.sleep(0.001)
        wait_idle()
        res.append("%.0f ms (+%.0f ms silence fed)" % ((first - t0) * 1e3, lead_ms))
    print("%-8s %-5s %s" % (WHICH, name, ",  ".join(res)))
d.terminate()
