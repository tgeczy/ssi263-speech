"""Which multi-line sends stall the Braille Lite firmware?  Each case: say(lines), run until
busy() is false; 'complete' = every line's ^F came back."""
import os
import sys

sys.path.insert(0, r"C:\git\ssi263-speech\src")
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
L1 = "Alex Sample left paren (at samplebird110127 at examplesite dot space right paren ) boosted"
L2 = "your post colon aha. So here's where we are with the SSI263."
L3 = "six:forty two:nine PM"
S1 = "Alex Sample boosted"
CASES = [("L1 L2 L3", [L1, L2, L3]), ("L1 x y", [L1, "x", "y"]), ("L3 L2 L1", [L3, L2, L1]),
         ("S1 L2 L3", [S1, L2, L3]), ("80x3", ["word " * 16] * 3),
         ("very long line", ["samplebird110127 examplesite110127 " * 6, "end"])]


def fresh():
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=SSI263C(),
               menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
    u.turbo_between_lines = True
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    return u


for name, lines in CASES:
    u = fresh()
    t0 = u.chip.time
    u.say(lines)
    while True:
        u.run(0.05)
        if not u.busy():
            break
    ok = u.owed() <= 0
    print("%-10s %3d bytes, lines %s: %s after %.1f s (echoes %d of %d)"
          % (name, sum(len(x) + 2 for x in lines) + 2, [len(x) for x in lines],
             "complete" if ok else "STALLED", u.chip.time - t0, u.echo_f - 1, len(lines)))
    u.close()
