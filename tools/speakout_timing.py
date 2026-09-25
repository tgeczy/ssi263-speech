"""Does the Speak-Out host's scheduling move phrase timing?  (Astra, Reply 27, item 2.)

Same firmware, same text, same v0.9 chip constants; only the host's step size and
CPU instruction budget change.  Reports the phrase's voiced span, and from the
chip's own log the request -> reg-0 write latency and the prime (R0 = 00) ->
phoneme write interval.  Chip writes inside one CPU batch share one chip time,
and the chip model resolves time in tract ticks (1/fc, 38-56 us), so a
sub-tick prime interval cannot matter to this model at all.
"""
import os
import sys

import numpy as np

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
from hosts.speakout import SpeakOut  # noqa: E402

HEX = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
TEXT = "this is rate five.\r"
SR = 44100


def span(y):
    h = int(0.01 * SR)
    env = np.array([np.sqrt(np.mean(y[k:k + h] ** 2)) for k in range(0, len(y) - h, h)])
    on = np.where(env > 0.1 * env.max())[0]
    return (on[-1] - on[0]) / 100.0


def run(step, ips):
    s = SpeakOut(HEX, cpu_ips=ips)
    s.run(2.6, step=step)
    t0 = s.chip.time
    s.say(TEXT)
    y = s.run(3.0, step=step)
    log = [(t, e) for t, e in s.chip.log if t >= t0]
    lat, prime = [], []
    last_req = None
    for i, (t, e) in enumerate(log):
        if e == "request":
            last_req = t
        elif e.startswith("w0=") and e != "w0=00" and last_req is not None:
            lat.append(t - last_req)
            last_req = None
    for i in range(len(log) - 1):
        if log[i][1] == "w0=00":
            nxt = next((t for t, e in log[i + 1:] if e.startswith("w0=") and e != "w0=00"), None)
            if nxt is not None:
                prime.append(nxt - log[i][0])
    return span(y), np.median(lat) * 1e3, np.max(lat) * 1e3, np.median(prime) * 1e6


def main():
    print("text %r at the firmware defaults (rate 5)" % TEXT.strip())
    print("step ms  cpu_ips    span s   req->write med/max ms   prime->phoneme med us")
    for step in (0.002, 0.0005, 0.0001):
        for ips in (750_000, 1_500_000, 3_000_000):
            sp, lm, lx, pm = run(step, ips)
            print("%6.1f  %9d   %6.3f      %6.3f / %6.3f          %6.1f" % (step * 1e3, ips, sp, lm, lx, pm))


if __name__ == "__main__":
    main()
