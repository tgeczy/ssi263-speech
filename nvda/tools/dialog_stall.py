"""Issue #8: tabbing through NVDA's voice settings dialog, the Accent "stalls" after "Rate:" or "Pitch:", and the
rest ("slider 55 alt+r") comes out with the next speech.

NVDA's labels there end in a colon ("Rate:", gui/settingsDialogs.py), and its focus speech is one utterance:
["Rate:", "slider", "55", "alt+r"] and its final index; the driver joins the pieces into one text.  The BUILT
add-on's driver (fake_nvda_driver_test.py's stand-ins) is driven as NVDA drives it in that dialog: a burst of Tabs
(cancel() and the next control's announcement) at key-repeat gaps, then one announcement let finish.

When that announcement has completed (its own final index reached, then a done), the unit must have nothing left to
say: the test runs it on for 2 s with no new text and counts the phonemes it still writes.  Any is the stall the
issue describes -- the driver called the text done in the silence after the colon while the unit still held the
rest, and NVDA's next speech (a Tab's cancel, landing while the driver is idle, flushes nothing) brought it out.

    python dialog_stall.py accent|accentsa|speakout [runs per rate]          exit 1 on a stall
    DIALOG_STALL_RATES=50,55,60,75,100 (NVDA's rates);  FAKE_PLAYER=wasapi: on wasapi_player.py
    DIALOG_STALL_BREAK=1: the Accent SA's settle off (asv_set_settle 0, 0.7.5's done) -- the must-fail control
    SSI263_SYNTH_DRIVERS=<0.7.0's synthDrivers> (legacy_drivers.py): 0.7.0's Python drivers; they stall the same
"""
import ctypes
import os
import random
import sys
import time

WHICH = sys.argv[1]
SA = WHICH == "accentsa"
RUNS = int(sys.argv[2]) if len(sys.argv) > 2 else 3
sys.argv = [sys.argv[0], "accent" if SA else WHICH]
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
            encoding="utf-8").read()
exec(_src.split("time.sleep(2.0)")[0])
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from write_spy import watch_writes          # noqa: E402

phon = []                       # (mode, phoneme code) for every phoneme the unit writes
mode = ["other"]                # "render" while the driver renders, "late" while run_on() runs it, else "other"


NATIVE = None                   # the unit is ssi263speech.dll's (0.7.5); else a 0.7.0 Python box (legacy_drivers.py)


def _hook(u):
    """count u's phonemes, marked by what ran the unit: the driver's render (0.7.5) or run (0.7.0's box)"""
    global NATIVE
    NATIVE = hasattr(u, "_host_ptr")
    if getattr(u, "_dialog_stall_hooked", False):    # once per unit: a second hook would count every phoneme twice
        return u
    u._dialog_stall_hooked = True
    if NATIVE and getattr(u, "_api", "") == "asv" and os.environ.get("DIALOG_STALL_BREAK") == "1":
        f = u._lib.asv_set_settle               # the control: 0.7.5's done (as_voice.h), the settle off
        f.restype, f.argtypes = None, [ctypes.c_void_p, ctypes.c_double]
        f(u._v, 0.0)
    name = "render" if NATIVE else "run"
    step = getattr(u, name)

    def on(t, reg, val):
        if reg == 0 and (val & 0x3F):
            phon.append((mode[0], val & 0x3F))

    def stepped(*a, **k):
        if mode[0] == "other":
            mode[0] = "render"
        try:
            return step(*a, **k)
        finally:
            if mode[0] == "render":
                mode[0] = "other"
    watch_writes(u, on)
    setattr(u, name, stepped)
    return u


def unit():
    return d._box


def run_on(seconds):
    """the unit run on with nothing new to say: what it still says, the driver called complete too early.  Only
    while the driver's worker waits for speech (nothing else touches the unit then)."""
    u = unit()
    k = len(phon)
    mode[0] = "late"
    try:
        t = 0.0
        if NATIVE:
            lib, h = u._lib, u._host_ptr()
            run = getattr(lib, u._host + "_run")
            run.restype = ctypes.c_int
            run.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double,
                            ctypes.POINTER(ctypes.POINTER(ctypes.c_double))]
            getattr(lib, u._host + "_set_int")(h, b"log_writes", 1)
            out = ctypes.POINTER(ctypes.c_double)()
            while t < seconds:
                run(h, 0.03, 0.0005, ctypes.byref(out))
                t += 0.03
            u._drain()
        else:
            while t < seconds:
                u.run(0.03)
                t += 0.03
    finally:
        mode[0] = "other"
    return [c for m, c in phon[k:] if m == "late"]


if hasattr(d, "_boot"):
    _boot = d._boot

    def boot():
        b = _boot()
        _hook(b)
        return b
    d._boot = boot
time.sleep(2.0)
if SA:
    d._set_voice("sa")                             # the stand-in has no properties: the setter itself
    d.speak(["ready"])
    wait_idle()
_hook(unit())
CONTROLS = [["Rate:", "slider", "%d", "alt+r"], ["Pitch:", "slider", "50", "alt+p"],
            ["Inflection:", "slider", "100", "alt+i"], ["Volume:", "slider", "100", "alt+o"],
            ["Variant:", "combo box", "Voice 5 (default)", "alt+a"],
            ["Synthesizer:", "Accent SA and Mini (SSI-263 emulation)", "Change...", "button", "alt+c"],
            ["Punctuation/symbol level:", "combo box", "some", "alt+y"],
            ["Capital pitch change percentage:", "spin button", "30", "alt+h"]]


def announce(k, rate):
    return [p % rate if "%d" in p else p for p in CONTROLS[k % len(CONTROLS)]]


def spoken(seq):
    """a Tab: cancel(), speak seq, wait for its completion -- (its rendered phonemes, None) or (None, why)"""
    d.cancel()
    k = len(phon)
    d.speak(seq)
    try:
        wait_done(timeout=20)
    except CompletionError as e:
        return None, str(e)
    # the done is the worker's last act before it waits for speech again (the Speak-Out driver keeps no phase)
    while getattr(d, "_phase", ("waiting for speech",))[0] != "waiting for speech":
        time.sleep(0.001)
    time.sleep(0.02)
    return [c for m, c in phon[k:] if m == "render"], None


d._player.pace = True
rng = random.Random(8)
RATES = [int(r) for r in os.environ.get("DIALOG_STALL_RATES", "50,55,60,75,100").split(",")]
total = bad = 0
for rate in RATES:
    d._set_rate(rate)                              # NVDA's rate setter (the stand-in has no properties)
    fails = []
    for run in range(RUNS):
        for gap in (0.03, 0.05, 0.1, 0.2):
            n = rng.randint(1, 4)
            start = rng.randrange(len(CONTROLS))
            for i in range(n):                     # a burst of Tabs
                d.cancel()
                d.speak(announce(start + i, rate))
                time.sleep(gap * rng.uniform(0.7, 1.4) / SIM_SPEED)
            k = start + n
            got, why = spoken(announce(k, rate))
            total += 1
            name = CONTROLS[k % len(CONTROLS)][0].rstrip(":")
            if why:
                fails.append("gap %d ms, %s %s" % (gap * 1000, name, why))
                continue
            late = run_on(2.0)
            if late:
                fails.append("gap %d ms, %s: %d phonemes before its completion, %d more still in the unit"
                             % (gap * 1000, name, len(got), len(late)))
    bad += len(fails)
    print("rate %3d: %2d of %2d announcements complete too early%s" % (rate, len(fails), RUNS * 4,
                                                                        (" -- " + fails[0]) if fails else ""))
d.terminate()
print("%s: %d of %d announcements complete too early -- %s" % (WHICH, bad, total, "STALL" if bad else "ok"))
sys.exit(1 if bad else 0)
