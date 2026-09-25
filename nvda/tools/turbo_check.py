"""Is a higher preparation turbo transparent?  Same lines at turbo 4 and 32 (and with the
between-lines turbo on, as the add-on runs): chip writes must match, and their timing
measured from each line's first phoneme; only the silence before it may shrink."""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "src"))
from tools import repo_paths             # noqa: E402
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = repo_paths.engine_dir("blazie")
LINES = [["Custom number processing left paren (fix digits above a trillion right paren ) check",
          "box not checked Alt plus u"],
         ["Select synthesizer dialog.", "Synthesizer: combo box."], ["Is this a question?"],
         ["Hello there. This is a test of the timing, with a comma."], ["Number 1234567, and 263."]]


def session(turbo):
    chip = SSI263C()
    writes = []
    orig = chip.write

    def write(reg, val):
        writes.append((chip.time, reg, val))
        orig(reg, val)

    chip.write = write
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=chip,
               menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
    u.turbo = turbo
    u.turbo_between_lines = True
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    out = []
    for ln in LINES:
        t0 = chip.time
        k0 = len(writes)
        u.say(ln)
        while True:
            u.run(0.02)
            if not u.busy():
                break
        w = writes[k0:]
        first = next((t for t, r, v in w if r == 0 and (v & 0x3F)), t0)
        out.append((first - t0, chip.time - first, [(t - first, r, v) for t, r, v in w if t >= first]))
    u.close()
    return out


a, b = session(4.0), session(32.0)
for i, ((la, da, wa), (lb, db, wb)) in enumerate(zip(a, b)):
    same = [(r, v) for _, r, v in wa] == [(r, v) for _, r, v in wb]
    shift = max((abs(x[0] - y[0]) for x, y in zip(wa, wb)), default=0.0)
    print("line %d: lead-in %3.0f -> %3.0f ms; after first phoneme: writes %s, speech %.3f vs %.3f s, "
          "worst shift %.2f ms" % (i, la * 1e3, lb * 1e3, "same" if same else "DIFFER", da, db, shift * 1e3))
