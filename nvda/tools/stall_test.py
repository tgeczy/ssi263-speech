"""Does an utterance stall before its end?  Speak it, then show what was spoken (phonemes),
the ^F bookkeeping, and whether more speech comes out later when the next text arrives.
python stall_test.py speakout|blazie"""
import os
import sys
import time

WHICH = sys.argv[1]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
           encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
TEXT = ("Alex Sample (@samplebird110127@example.space) boosted your post: aha. So here's where we "
        "are with the SSI263. 06:42:09 PM")
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
if WHICH == "blazie":
    import re
    joined = drv_mod.numwords.normalise(drv_mod._clean(TEXT))
    print("lines sent:", drv_mod._lines(joined))
mark = len(notified)
n0 = len(d._player.chunks)
d.speak([TEXT])
ok = wait_idle()
y = audio_since(n0)
print("utterance done=%s, %.2f s audio, %d phonemes" % (ok, len(y) / 44100, len(phon)))
print("  last phonemes: %s" % " ".join(phon[-25:]))
if WHICH == "blazie":
    print("  ^F sent %d echoed %d owed %d, tx tail %s" % (unit.sent_f, unit.echo_f, unit.owed(),
                                                         [hex(b) for b in unit.tx[-6:]]))
del phon[:]
mark = len(notified)
n0 = len(d._player.chunks)
d.speak(["next."])
wait_idle()
y = audio_since(n0)
print("next utterance: %.2f s audio, phonemes: %s" % (len(y) / 44100, " ".join(phon[:40])))
d.terminate()
