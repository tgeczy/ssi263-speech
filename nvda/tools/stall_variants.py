"""Which shape of NVDA sequence makes the Braille Lite stop part-way (the rest coming out only
when the next text arrives)?  For each variant: was all of it spoken in the utterance, and how
much speech leaked into the next one."""
import os
import sys
import time

sys.argv = [sys.argv[0], sys.argv[1] if len(sys.argv) > 1 else "blazie"]
WHICH = sys.argv[1]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
           encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
unit = d._unit if WHICH == "blazie" else d._box
chip = unit.chip
names = chip.rom.names
phon = []
orig = chip.write


def write(reg, val):
    if reg == 0 and (val & 0x3F) and not (chip.regs[3] & 0x80):
        phon.append(names.get(val & 0x3F, "?"))
    orig(reg, val)


chip.write = write
I = IndexCommand
A = "Alex Sample (@samplebird110127@example.space) boosted your post:"
B_ = "aha. So here's where we are with the SSI263."
C = "06:42:09 PM"
MOST_A = "Alex Sample  left paren (at samplebird110127 at examplesite dot space  right paren ) boosted your post colon"
VARIANTS = [
    ("one string", [A + " " + B_ + " " + C]),
    ("pieces", [A, B_, C]),
    ("pieces+index", [I(1), A, I(2), B_, I(3), C, I(4)]),
    ("colon line end", [A]),
    ("most pieces", [MOST_A, B_, C]),
    ("most+index", [I(1), MOST_A, I(2), B_, I(3), C, I(4)]),
]
for join in (True, False):
    d._join = join
    for label, seq in VARIANTS:
        del phon[:]
        mark = len(notified)
        d.speak(seq)
        ok = wait_idle()
        spoke = len(phon)
        tail = " ".join(phon[-6:])
        del phon[:]
        mark = len(notified)
        d.speak(["next."])
        wait_idle()
        leak = len(phon) - 5          # "next" is N EH K S T
        print("join %-5s %-15s done %-5s %3d phonemes, ends '%s'; next utterance leaked %d phonemes%s"
              % (join, label, ok, spoke, tail, max(0, leak), "  <-- STALL" if leak > 2 else ""))
d.terminate()
