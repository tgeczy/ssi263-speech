"""Run ahead: a text of many lines stopped part way, then at once a short one (issue 30: a phone's screen reader moving
on from a long list item) -- the short one must begin with its own phonemes, never with a line of the stopped text.
Through the voice (bl_voice.h's blv_*, as the Android engine, speech-dispatcher and the wheel drive the unit).

The finding (2026-11-19).  The unit speaks a line when the NEXT line's end has come; until then it keeps it, as it
keeps its flush line.  ^X empties its unread input, and with it the line end that would have released a line it had
already taken: that line then waits, silent -- a second ^X changes nothing -- and is spoken whole in front of the next
text (1 to 3 s of it).  The unit run ahead has read on while the listener is mid-line, so a stop met it there at 50 of
268 cancel times over this text (rate 75; 83 of 449 at rate 50, 27 of 137 at rate 100), the lockstep unit at 2 of
271.  The fix (bl_host.c cut_kept_line): after the cut, a lone CR releases a kept line, and it is cut the moment it
loads a spoken phoneme.  With it: 0 of 268, 0 of 902 at rate 25, 0 of 114 over a text of short lines.

Here: the text stopped every 0.05 s over its first line and a half (rate 75, 11025 Hz, as in the report).  The short
text's spoken phonemes must equal those of the short text said after the same history, exactly; the host must never
report a kept line it could not resolve (bh_get_int "cancel_held" 3); and it must have found a kept line at some
stop, or this sweep no longer meets the state it is for.

English only.  The Spanish unit kept no line in the same sweeps (rates 50, 75, 100); a text stopped in its last half
second leads the next there (12 of 313), with or without this fix: another matter, open.  The lockstep's 2 of 271
are not gated either: the fix is for the cancel of a run-ahead utterance alone.
NOT reproduced through the driver's host (native_blazie.py, run_ahead_cancel.py's flow) with the same lines: 0 of 40
with the fix off, at three rates.  Why that flow never meets the kept line is not known.

    python run_ahead_kept_line.py
    RUN_AHEAD_KEPT_BREAK=1   run_ahead.h's RA_BRK_HELD: the kept line is left kept (must fail)
"""
import ctypes
import os
import sys
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ENG = os.path.join(REPO, "nvda", "dist", "blazie-build", "synthDrivers", "_ssi263_blazie")
DLL = os.path.join(REPO, "build", "win", "x64" if sys.maxsize > 2 ** 32 else "x86", "ssi263speech.dll")
BREAK = 10 if os.environ.get("RUN_AHEAD_KEPT_BREAK") else 0      # run_ahead.h's RA_BRK_HELD
OUT = 11025
RATE = 75                              # the voice's 0-100: an Android request at 200 % on its default slider
LONG = ("This is the first of several sentences in a long list item, the kind a mail program shows. It goes on for "
        "a while, with commas, and with more than one clause, so that the unit has many lines to read. Then it adds "
        "another sentence about nothing in particular, and one more after that, to pass three hundred characters "
        "with room to spare. The last sentence is here.")
SHORT = "OK button"
STOPS = [0.1 + 0.05 * k for k in range(31)]                      # 0.10 ... 1.60 s of the long text heard
P, I, D, S = ctypes.c_void_p, ctypes.c_int, ctypes.c_double, ctypes.c_char_p


class Write(ctypes.Structure):
    _fields_ = [("t", D), ("reg", I), ("val", I)]


class Unit:
    def __init__(self):
        lib = self.lib = ctypes.CDLL(DLL)
        lib.blv_create.restype = P
        lib.blv_create.argtypes = [S, S, I, D, I, I, S, I]
        lib.blv_destroy.argtypes = [P]
        lib.blv_set.argtypes = [P, I, I, I, I, I]
        lib.blv_set_run_ahead.argtypes = [P, I]
        lib.blv_speak.argtypes = [P, S]
        lib.blv_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]
        lib.blv_cancel.argtypes = [P]
        lib.blv_host.restype = P
        lib.blv_host.argtypes = [P]
        lib.bh_get_int.argtypes = [P, S]
        lib.bh_set_int.argtypes = [P, S, I]
        lib.bh_writes.argtypes = [P, ctypes.POINTER(ctypes.POINTER(Write))]
        lib.bh_clear_writes.argtypes = [P]
        err = ctypes.create_string_buffer(256)
        self.v = lib.blv_create(os.path.join(ENG, "BL2ENG.BNS").encode("mbcs"),
                                os.path.join(ENG, "bl2_2003_warm.state").encode("mbcs"), 0, float(OUT), 1, 0, err, 256)
        if not self.v:
            raise RuntimeError("the unit did not boot: %s" % err.value.decode("latin-1", "replace"))
        lib.blv_set(self.v, RATE, 50, 7, 100, 1)
        lib.blv_set_run_ahead(self.v, 1)
        self.h = lib.blv_host(self.v)
        lib.bh_set_int(self.h, b"log_writes", 1)
        lib.bh_set_int(self.h, b"run_ahead_break", BREAK)
        self.r3 = 0

    def close(self):
        self.lib.blv_destroy(self.v)

    def spoken(self):
        """the spoken phoneme codes loaded since the last call"""
        ws = ctypes.POINTER(Write)()
        out = []
        for i in range(self.lib.bh_writes(self.h, ctypes.byref(ws))):
            if ws[i].reg == 3:
                self.r3 = ws[i].val
            if ws[i].reg == 0 and not self.r3 & 0x80 and ws[i].val & 0x3F:
                out.append(ws[i].val & 0x3F)
        self.lib.bh_clear_writes(self.h)
        return out

    def say(self, text, stop_after=None):
        """the samples rendered: to the end, or until stop_after of them (no cancel here)"""
        self.lib.blv_speak(self.v, text.encode("utf-8"))
        pcm, done, n = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), 0
        while not done.value:
            n += self.lib.blv_render(self.v, ctypes.byref(pcm), ctypes.byref(done))
            if stop_after is not None and n >= stop_after:
                break
        return n


def one(at):
    """the short text's spoken phonemes and the host's verdict on a kept line: after the long text stopped `at`
    seconds in, or (None) after the same history alone"""
    u = Unit()
    try:
        u.say(SHORT)                           # a history: one utterance spoken to its end
        held = 0
        if at is not None:
            u.say(LONG, int(at * OUT))
            u.lib.blv_cancel(u.v)
            held = u.lib.bh_get_int(u.h, b"cancel_held")
        u.spoken()
        u.say(SHORT)
        return u.spoken(), held
    finally:
        u.close()


def main():
    if not os.path.isfile(DLL):
        print("skip: %s is not built (src/csrc/build_ssi263speech.py)" % os.path.relpath(DLL, REPO))
        return 0
    print("emulated time (Z180 cycles, the chip model): not a measurement of a real unit")
    # a few workers: run_tests.py runs the checks in parallel already
    with Pool(min(4, os.cpu_count() or 1)) as pool:
        want = pool.map(one, [None])[0][0]
        got = pool.map(one, STOPS)
    led = [(at, g) for at, (g, _) in zip(STOPS, got) if g != want]
    unresolved = [at for at, (_, h) in zip(STOPS, got) if h == 3]
    cut = [at for at, (_, h) in zip(STOPS, got) if h == 2]
    failures = 0
    print("%-4s a text of many lines, rate %d: %d of %d stops led by other phonemes%s" % (
        "FAIL" if led else "ok", RATE, len(led), len(STOPS),
        "".join("; %.2f s: %s" % (at, g[:6]) for at, g in led[:4])))
    failures += bool(led)
    print("%-4s a kept line the host could not resolve: %d of %d stops%s" % (
        "FAIL" if unresolved else "ok", len(unresolved), len(STOPS), "".join(" %.2f" % at for at in unresolved[:6])))
    failures += bool(unresolved)
    if not BREAK:
        print("%-4s the sweep meets the kept line: released and cut at %d of %d stops" % (
            "ok" if cut else "FAIL", len(cut), len(STOPS)))
        failures += not cut
    print("run ahead kept line: %s" % ("all ok" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
