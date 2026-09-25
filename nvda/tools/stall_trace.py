"""Trace the Braille Lite through the stalling send: XON/XOFF, ^F echoes, phonemes, busy()."""
import os
import sys

sys.path.insert(0, r"C:\git\ssi263-speech\src")
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
LINES = ["Alex Sample left paren (at samplebird110127 at examplesite dot space right paren ) boosted",
         "your post colon aha. So here's where we are with the SSI263.",
         "six:forty two:nine PM"]
chip = SSI263C()
u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"), os.path.join(B, "bl2_2003_warm.state"),
           chip=chip, menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
u.turbo_between_lines = True
u.send(b"\x18")
u.send(b"\r\x06")
u.run(0.3)
u.send(b"\x056V")
u.run(0.05)
names = chip.rom.names
nphon = [0]
orig = chip.write


def write(reg, val):
    if reg == 0 and (val & 0x3F) and not (chip.regs[3] & 0x80):
        nphon[0] += 1
    orig(reg, val)


chip.write = write
t0 = chip.time
print("bytes sent: %d" % sum(len(ln) + 2 for ln in LINES))
k = len(u.tx)
u.say(LINES)
last = None
while True:
    u.run(0.02)
    new = u.tx[k:]
    k = len(u.tx)
    for b in new:
        print("  %.3f s  unit sent %s   (phonemes so far %d, sent_f %d echo_f %d owed %d)"
              % (chip.time - t0, {0x06: "^F", 0x11: "XON", 0x13: "XOFF"}.get(b, hex(b)), nphon[0],
                 u.sent_f, u.echo_f, u.owed()))
    if not u.busy():
        break
print("busy() false at %.3f s, %d phonemes, last speech at %.3f s"
      % (chip.time - t0, nphon[0], u.last_speech - t0))
before = u.sent_f
u._cmd("D")
print("undelivered ^F markers still queued in the emulator: %d" % (before - u.sent_f))
u.close()
