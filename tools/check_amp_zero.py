"""R3 amplitude zero must not speak a newly loaded voiced/noise phoneme.

REG_vest_e and REG_vest_e2: VEST's 66 ms E prime is silent on the real chip.
The Hungarian Braille Lite similarly primes OU at zero. Test both source kinds,
without resetting between speech and the prime. Existing filter tails may settle;
the test starts 10 ms after the load, following a settled PA.

AMP_ZERO_BREAK=slew restores the old source behavior; every ZERO case must fail.
--scope checks that writes without a new load keep the old audio exactly. Its
AMP_ZERO_BREAK=reload control inserts an unwanted load at the zero write.
Plain Python (no numpy): the Linux suite runs on boxes without it (the Pi).
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(__file__)), "src"))
from ssi263.chip import SSI263
from ssi263.native import SSI263C

FS = 44100
FAILURES = []


def norm(x):
    return math.sqrt(sum(v * v for v in x))


def render(make, phone, zero, recover=False):
    chip = make({"amp_zero_sources": os.environ.get("AMP_ZERO_BREAK") != "slew"})
    for r, v in ((3, 0xDF), (0, 0xC0), (3, 0x5F), (1, 0x80), (2, 0xCF), (4, 0xE7), (0, 0x0E)):
        chip.write(r, v)
    chip.run(.30)
    chip.write(0, 0)
    chip.run(.30)
    chip.write(3, 0x50 if zero else 0x5F)
    chip.write(0, phone)
    y = [float(v) for v in chip.run(.066)]
    if recover:
        chip.write(3, 0x56)  # no R0: restore the held phoneme's targets, not a new load
        y = [float(v) for v in chip.run(.30)]
    return y


def main():
    if "--scope" in sys.argv:
        return scope()
    for tag, make in (("Python", lambda p: SSI263(p, out_rate=FS)),
                      ("C", lambda p: SSI263C(p, out_rate=FS))):
        for name, phone in (("E", 1), ("OU", 0x12), ("S", 0x30)):
            silent, loud = render(make, phone, True), render(make, phone, False)
            window = slice(int(.010 * FS), int(.055 * FS))
            ratio = norm(silent[window]) / norm(loud[window])
            good = ratio < .02
            print("%s ZERO %s %s: %.6f of nonzero control (limit .02)" % ("ok" if good else "FAIL", tag, name, ratio))
            if not good:
                FAILURES.append((tag, name))
            restored, reference = render(make, phone, True, True), render(make, phone, False, True)
            ratio = norm(restored[-2205:]) / norm(reference[-2205:])
            good = .9 < ratio < 1.1
            print("%s RECOVER %s %s: %.6f of nonzero control (limit .9..1.1)" %
                  ("ok" if good else "FAIL", tag, name, ratio))
            if not good:
                FAILURES.append((tag, name, "recovery"))
    return 1 if FAILURES else 0


def scope():
    failures = 0
    for tag, make in (("Python", lambda p: SSI263(p, out_rate=FS)),
                      ("C", lambda p: SSI263C(p, out_rate=FS))):
        for name, phone in (("AH", 0x0E), ("OU", 0x12), ("S", 0x30)):
            audio = []
            for enabled in (False, True):
                chip = make({"amp_zero_sources": enabled})
                for r, v in ((3, 0xDF), (0, 0xC0), (3, 0x5F), (1, 0x80),
                             (2, 0x8F), (4, 0xE7), (0, phone)):
                    chip.write(r, v)
                chip.run(.25)
                chip.write(3, 0x50)
                if enabled and os.environ.get("AMP_ZERO_BREAK") == "reload":
                    chip.write(0, phone)  # control: wrongly treat the write as a new load
                y = list(chip.run(.08))
                chip.write(3, 0x59)
                y.extend(chip.run(.15))
                audio.append([float(v) for v in y])
            same = audio[0] == audio[1]
            print("%s SCOPE %s %s: zero/rise without reload unchanged" %
                  ("ok" if same else "FAIL", tag, name))
            failures += not same
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
