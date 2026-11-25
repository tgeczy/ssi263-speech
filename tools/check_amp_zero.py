"""R3 amplitude zero must not speak a newly loaded voiced/noise phoneme.

REG_vest_e and REG_vest_e2: VEST's 66 ms E prime is silent on the real chip.
The Hungarian Braille Lite similarly primes OU at zero. Test both source kinds,
without resetting between speech and the prime. Existing filter tails may settle;
the test starts 10 ms after the load, following a settled PA.

AMP_ZERO_BREAK=slew restores the old source behavior; every ZERO case must fail.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(__file__)), "src"))
from ssi263.chip import SSI263
from ssi263.native import SSI263C

FS = 44100
FAILURES = []


def render(make, phone, zero):
    chip = make({"amp_zero_sources": os.environ.get("AMP_ZERO_BREAK") != "slew"})
    for r, v in ((3, 0xDF), (0, 0xC0), (3, 0x5F), (1, 0x80), (2, 0xCF), (4, 0xE7), (0, 0x0E)):
        chip.write(r, v)
    chip.run(.30)
    chip.write(0, 0)
    chip.run(.30)
    chip.write(3, 0x50 if zero else 0x5F)
    chip.write(0, phone)
    y = np.asarray(chip.run(.066), dtype=float)
    return y


def main():
    for tag, make in (("Python", lambda p: SSI263(p, out_rate=FS)),
                      ("C", lambda p: SSI263C(p, out_rate=FS))):
        for name, phone in (("E", 1), ("OU", 0x12), ("S", 0x30)):
            silent, loud = render(make, phone, True), render(make, phone, False)
            window = slice(int(.010 * FS), int(.055 * FS))
            ratio = np.linalg.norm(silent[window]) / np.linalg.norm(loud[window])
            good = ratio < .02
            print("%s ZERO %s %s: %.6f of nonzero control (limit .02)" % ("ok" if good else "FAIL", tag, name, ratio))
            if not good:
                FAILURES.append((tag, name))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
