"""How short can the Braille Lite's boot keys be?  For each (start, gap), boot with the
add-on's menu, then speak a fixed set of lines and record every chip register write.
A timing is usable only if the writes are IDENTICAL to the reference boot's."""
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
MENU = ("punct_none", "numbers_toggle")
LINES = [["Number 1234567, and 263."], ["eti dash-eloquence left paren (test right paren ) done."],
         ["Is this a question?"], ["Hello there. This is a test of the timing."]]


def session(start, gap):
    chip = SSI263C()
    writes = []
    orig = chip.write

    def write(reg, val):
        writes.append((reg, val))
        orig(reg, val)

    chip.write = write
    t0 = time.perf_counter()
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=chip, menu=MENU, key_start=start, key_gap=gap)
    boot = time.perf_counter() - t0
    boot_regs = list(chip.regs)
    del writes[:]
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    for ln in LINES:
        u.say(ln)
        while True:
            u.run(0.02)
            if not u.busy():
                break
    u.close()
    return boot, boot_regs, writes


ref_boot, ref_regs, ref = session(8000000, 10000000)
print("reference 8M/10M: boot %.3f s, %d writes after boot" % (ref_boot, len(ref)))
for start, gap in ((2000000, 2000000), (2500000, 2000000), (3000000, 1500000), (3000000, 1200000),
                   (3000000, 1000000), (2500000, 1500000)):
    boot, regs, w = session(start, gap)
    same = w == ref
    print("start %.1fM gap %.1fM: boot %.3f s, writes %s (%d), boot regs %s"
          % (start / 1e6, gap / 1e6, boot, "IDENTICAL" if same else "DIFFER", len(w),
             "same" if regs == ref_regs else "%s vs %s" % (regs, ref_regs)))
