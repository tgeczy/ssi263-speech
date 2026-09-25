"""Does the shorter boot only shift timing?  Same lines after the MASTER 8M/10M boot and after
the add-on's 3M/1.5M boot: compare the chip writes with their chip times, and the line gaps."""
import os
import sys

sys.path.insert(0, r"C:\git\ssi263-speech\src")
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
LINES = [["Select synthesizer dialog.", "Synthesizer: combo box."], ["Number 1234567, and 263."],
         ["Is this a question?"], ["Hello there. This is a test of the timing."]] * 3


def session(start, gap):
    chip = SSI263C()
    writes = []
    orig = chip.write

    def write(reg, val):
        writes.append((chip.time, reg, val))
        orig(reg, val)

    chip.write = write
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=chip,
               menu=("punct_none", "numbers_toggle"), key_start=start, key_gap=gap)
    del writes[:]
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    per_line = []
    for ln in LINES:
        t0 = chip.time
        k0 = len(writes)
        u.say(ln)
        while True:
            u.run(0.02)
            if not u.busy():
                break
        per_line.append((chip.time - t0, [(t - t0, r, v) for t, r, v in writes[k0:]]))
    u.close()
    return per_line


ref = session(8000000, 10000000)
new = session(3000000, 1500000)
worst_shift = 0.0
for i, ((da, wa), (db, wb)) in enumerate(zip(ref, new)):
    same_seq = [(r, v) for _, r, v in wa] == [(r, v) for _, r, v in wb]
    shift = max((abs(ta - tb) for (ta, _, _), (tb, _, _) in zip(wa, wb)), default=0.0)
    worst_shift = max(worst_shift, shift)
    print("line %2d: writes %s, line length %.4f vs %.4f s, worst write shift %.2f ms"
          % (i, "same" if same_seq else "DIFFER", da, db, shift * 1e3))
print("worst write-time shift over %d lines: %.2f ms" % (len(ref), worst_shift * 1e3))
