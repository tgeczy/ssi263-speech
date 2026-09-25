"""Phoneme strings (ROM names) the Braille Lite firmware sends for numbers, full-numbers mode."""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "src"))
from tools import repo_paths             # noqa: E402
from hosts.blazie import Blazie  # noqa: E402
from ssi263 import SSI263        # noqa: E402

B = repo_paths.engine_dir("blazie")
NUMS = sys.argv[1:] or ["21", "263", "4294967296", "999999999999", "1000000000000", "1234567890123",
                        "100000000000000"]
chip = SSI263(dsp="c")
seq = []
orig = chip.write


def write(reg, val):
    if reg == 0 and (val & 0x3F) and not (chip.regs[3] & 0x80):
        seq.append(chip.rom.names.get(val & 0x3F, "?%02X" % (val & 0x3F)))
    orig(reg, val)


chip.write = write
u = Blazie(B + r"\bns_live.exe", B + r"\BL2ENG.BNS", B + r"\bl2_2003_warm.state", chip=chip,
           menu=("punct_none", "numbers_toggle"))
u.run(0.3)
for n in NUMS:
    del seq[:]
    u.say([n + "."])
    while True:
        u.run(0.02)
        if not u.busy():
            break
    print("%16s: %s" % (n, " ".join(seq)))
u.close()
