"""The pauses in NVDA's checkbox announcement on the Braille Lite, and what the driver makes of it.
python checkbox_gaps.py [max_words] [turbo]"""
import os
import re
import sys
import time

ARGS = sys.argv[1:]
sys.argv = [sys.argv[0], "blazie"]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(1.0)
if ARGS:
    mw = int(ARGS[0])
    old_lines = drv_mod._lines
    drv_mod._lines = lambda text, max_words=14, pack=True: old_lines(text, mw, pack)
if len(ARGS) > 1:
    time.sleep(0.5)
    d._unit.turbo = float(ARGS[1])


def gaps(y, sr=44100, thr_db=-45):
    h = int(0.01 * sr)
    env = np.array([20 * np.log10(np.sqrt(np.mean(y[k:k + h] ** 2)) + 1e-9) for k in range(0, len(y) - h, h)])
    loud = env > thr_db
    on = np.where(loud)[0]
    out, run = [], 0
    for k in range(on[0], on[-1] + 1) if len(on) else ():
        if not loud[k]:
            run += 1
        else:
            if run >= 8:
                out.append((round(k / 100 - run / 100, 2), run * 10))
            run = 0
    return out, (on[0] * 10 if len(on) else None)


# NVDA 2026 at punctuation "most": the name with its brackets named and preserved, then role,
# state, and the shortcut, whose letter comes wrapped in character mode
NAME_MOST = "Custom number processing  left paren (fix digits above a trillion  right paren ) "
CASES = [("desktop", ["desktop", "list", "Folder View", "list view"]),
         ("desktop2", ["Desktop  list", "Folder View  list view", "Recycle Bin", "1 of 12"]),
         ("raw pieces", ["Custom number processing (fix digits above a trillion)", "check box", "not checked",
                         "Alt+", "u"]),
         ("most pieces", [NAME_MOST, "check box", "not checked", "Alt plus ", "u"])]
phon = []
_chip = d._unit.chip
_orig = _chip.write
_names = None


def _write(reg, val):
    if reg == 0 and (val & 0x3F) and not (_chip.regs[3] & 0x80):
        phon.append((_chip.time, _names.get(val & 0x3F, "?")))
    _orig(reg, val)


_names = _chip.rom.names
_chip.write = _write
for label, seq in CASES:
    joined = drv_mod._joined([("text", s) for s in seq])[0][1]
    lines = drv_mod._lines(drv_mod._clean(joined))
    mark = len(notified)
    n0 = len(d._player.chunks)
    del phon[:]
    t_audio0 = _chip.time
    d.speak(seq)
    wait_idle()
    y = audio_since(n0)
    g, onset = gaps(y)
    print("%-12s lines %s" % (label, [ln for ln in lines]))
    print("%-12s %.2f s audio, first sound at %s ms of audio, gaps (at s, ms) %s"
          % ("", len(y) / 44100, onset, g))
    for at, ms in g:
        t = t_audio0 + at
        before = " ".join(n for tt, n in phon if t - 0.35 <= tt < t)
        after = " ".join(n for tt, n in phon if t <= tt < t + ms / 1000.0 + 0.3)
        print("      gap %3d ms: ...%s | %s..." % (ms, before, after))
d.terminate()
