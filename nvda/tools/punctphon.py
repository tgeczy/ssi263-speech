"""Phonemes for NVDA's 'most' punctuation text, unit punctuation some (factory) vs none."""
import sys

sys.path.insert(0, r"C:\git\ssi263-speech\src")
from hosts.blazie import Blazie  # noqa: E402
from ssi263 import SSI263        # noqa: E402

B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
TEXTS = ["eti dash-eloquence.", "left paren (test right paren ) done."]
for tag, menu in (("factory", ()), ("none", ("punct_none", "numbers_toggle"))):
    chip = SSI263(dsp="c")
    seq = []
    orig = chip.write

    def write(reg, val, seq=seq, orig=orig, chip=chip):
        if reg == 0 and (val & 0x3F) and not (chip.regs[3] & 0x80):
            seq.append(chip.rom.names.get(val & 0x3F, "?"))
        orig(reg, val)

    chip.write = write
    u = Blazie(B + r"\bns_live.exe", B + r"\BL2ENG.BNS", B + r"\bl2_2003_warm.state", chip=chip, menu=menu)
    u.run(0.3)
    for t in TEXTS:
        del seq[:]
        u.say([t])
        while True:
            u.run(0.02)
            if not u.busy():
                break
        print("%-8s %-40s %s" % (tag, t, " ".join(seq)))
    u.close()
