"""Speak-to-first-phoneme latency as a listener gets it, for the Braille Lite's turbo rules.
Playback starts when the first block is fed and runs in real time, so the first phoneme is
heard at max(wall time its block was fed, first feed + its offset in the audio)."""
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
LINES = {"short": ["Hello."], "tab": ["Select synthesizer", "dialog"],
         "long": ["Custom number processing left paren (fix digits above a trillion right paren ) check",
                  "box not checked Alt plus u"]}
BLOCK = 0.03


def trial(turbo, prep_step, until_any=False):
    chip = SSI263C()
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=chip,
               menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
    u.turbo = turbo
    u.prep_step = prep_step
    u.turbo_between_lines = True
    if until_any:                       # turbo ends at the unit's first R0 write, PA included
        orig = chip.write

        def write(reg, val):
            if reg == 0:
                u.preparing = False
            orig(reg, val)
        chip.write = write
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    res = {}
    for name, ln in LINES.items():
        for rep in range(2):
            t0 = time.perf_counter()
            u.say(ln)
            first_feed = heard = None
            audio = 0.0
            while True:
                y = u.run(BLOCK)
                now = time.perf_counter() - t0
                if first_feed is None:
                    first_feed = now
                if heard is None:
                    k = next((i for i, v in enumerate(y) if abs(v) > 0.003), None)
                    if k is not None:
                        heard = max(now, first_feed + audio + k / 44100.0)
                        trimmed = now + 0.005        # leading silence dropped, 5 ms pre-roll kept
                audio += len(y) / 44100.0
                if not u.busy():
                    break
            res.setdefault(name, []).append(heard * 1e3)
            res.setdefault(name + " trimmed", []).append(trimmed * 1e3)
    u.close()
    return res


for turbo, prep_step in ((4, None), (8, None), (16, None), (32, None)):
    r = trial(turbo, prep_step)
    print("turbo x%-2d prep step %-6s " % (turbo, prep_step)
          + "  ".join("%s %s ms" % (k, "/".join("%.0f" % v for v in vs)) for k, vs in r.items()))
