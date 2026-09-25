"""Trace one Braille Lite utterance block by block: wall time, chip time, preparing, first phoneme."""
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "src"))
from tools import repo_paths             # noqa: E402
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = repo_paths.engine_dir("blazie")
TURBO = float(sys.argv[1]) if len(sys.argv) > 1 else 32.0
chip = SSI263C()
events = []
orig = chip.write


def write(reg, val):
    if reg == 0:
        events.append((time.perf_counter(), chip.time, val & 0x3F, chip.regs[3], globals().get("u") is not None and u.preparing))
    orig(reg, val)


chip.write = write
u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"), os.path.join(B, "bl2_2003_warm.state"),
           chip=chip, menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
u.turbo = TURBO
u.turbo_between_lines = True
u.send(b"\x18")
u.send(b"\r\x06")
u.run(0.3)
u.send(b"\x056V")
u.run(0.05)
for rep in range(2):
    del events[:]
    t0 = time.perf_counter()
    c0 = chip.time
    u.say(["Custom number processing left paren (fix digits above a trillion right paren ) check",
           "box not checked Alt plus u"])
    for i in range(12):
        prep = u.preparing
        y = u.run(0.03)
        loud = next((k for k, v in enumerate(y) if abs(v) > 0.003), None)
        print("rep %d block %2d: wall %4.0f ms, chip %4.0f ms, preparing %-5s loud at %s"
              % (rep, i, (time.perf_counter() - t0) * 1e3, (chip.time - c0) * 1e3, prep, loud))
        if loud is not None:
            break
    print("   R0 writes: " + ", ".join("%02X@%.0fms r3=%02X prep=%d" % (p, (c - c0) * 1e3, r3, pr)
                                     for w, c, p, r3, pr in events[:8]))
    while u.busy():
        u.run(0.03)
u.close()
