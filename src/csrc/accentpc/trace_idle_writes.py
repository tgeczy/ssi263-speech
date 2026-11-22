"""Issue #8's delayed writes, preserved (Astra, Replies 152-153).  A corpus check of the fix ran each text through
the job API (as the NVDA driver) and then the host on its own for 8 s; at NVDA's rate 100 some jobs, after their done,
had the unit write a phoneme 2.3-5.1 s later.  This replays that session exactly -- 70 random texts (seed 2, clauses
of up to 20 words), each at rates 0, 50 and 100, an 8 s run-on after every one -- up to the first such job, then
prints every block of its run-on that wrote anything, with the firmware's state, and goes on observing for
OBSERVE_S (default 20 s) to show whether anything else follows.

What it shows (2026-10-03).  accent-stall-8's library: text 50 at rate 100; done; the 8085 idle in its waiting
loops (work 0, PC in 1950h's loop), its speaking flag down; at +2.37 s one burst of about 2,900 instructions,
R4=E7 R3=5C R2=E8 R1=77 R0=D9 then R3=70 ... R0=C0 (phoneme 19h, then a PA), and at +2.40 s PAs with R3=01, 02 and
70, the 8085 then in its power-up/Ctrl-X waiting loop (02C9h); the flag never up; nothing else to +20 s.  0.7.5's
library: its first such job is text 0 at rate 100, and that is the issue itself (the rest of the text spoken with the
flag up from +0.69 s); after it, at +7.08-7.11 s, the same PA-only re-initialisation with the flag down.  So the
delayed writes are the firmware re-initialising the chip some seconds into an idle spell (once with a phoneme 19h
write first), not the rest of a text: not speech a done cut off, and not something the NVDA driver meets (it runs
the unit only while a text is being said).  What triggers it and when is not known: left for its own investigation.
The same session with 12 s run-ons never shows it in 210 jobs.  Each found job is classed: SPEECH LEFT (a phoneme
with the speaking flag up: the issue) or IDLE WRITES (flag down throughout).

    python trace_idle_writes.py [library]      (offline; about a minute; not in the test runners)
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
OBSERVE_S = float(os.environ.get("OBSERVE_S", "20"))
P, I, D = ctypes.c_void_p, ctypes.c_int, ctypes.c_double
PCM = ctypes.POINTER(ctypes.c_short)


class Write(ctypes.Structure):
    _fields_ = [("t", D), ("reg", I), ("val", I)]


def library():
    if len(sys.argv) > 1:
        return sys.argv[1]
    if os.environ.get("SSI263_LIB") and not os.environ["SSI263_LIB"].endswith("ssi263.dll"):
        return os.environ["SSI263_LIB"]
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    return os.path.join(REPO, "nvda", "dist", "accent-build", "synthDrivers", "_ssi263_accent", "bin", arch,
                        "ssi263speech.dll")


lib = ctypes.CDLL(library())                # only calls 0.7.5's library has too, so it can be traced the same
for name, res, args in (
        ("asv_create", P, [ctypes.c_char_p, ctypes.c_size_t] * 3 + [D, ctypes.c_char_p, I]),
        ("asv_set", None, [P, I, I, I, I, I]), ("asv_render", I, [P, ctypes.POINTER(PCM), ctypes.POINTER(I)]),
        ("asv_begin", I, [P]), ("asv_text", I, [P, ctypes.c_char_p, I]), ("asv_end", I, [P]),
        ("asv_say_bytes", I, [ctypes.c_char_p, I, ctypes.c_char_p, I]), ("asv_host", P, [P]),
        ("ash_run", I, [P, D, D, ctypes.POINTER(ctypes.POINTER(D))]), ("ash_set_int", None, [P, ctypes.c_char_p, I]),
        ("ash_get_int", I, [P, ctypes.c_char_p]), ("ash_speaking", I, [P]),
        ("ash_writes", I, [P, ctypes.POINTER(ctypes.POINTER(Write))]), ("ash_clear_writes", None, [P])):
    f = getattr(lib, name)
    f.restype, f.argtypes = res, args

WORDS = ("rate slider fifty five alt pitch volume inflection combo box voice default the quick brown fox jumps over "
         "lazy dog extraordinary international communication representative 1234 567 89 2026 $30.50 3.14 "
         "NVDA synthesizer settings dialog check box checked not button OK cancel apply").split()
PUNCT = [":", ",", ";", ".", "?", "!", " -", ":", ":"]
rng = random.Random(2)


def text():
    out = []
    for c in range(rng.randint(2, 4)):
        out.append(" ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 20))) + rng.choice(PUNCT))
    return " ".join(out)


def prepared(t):
    data = t.encode("utf-8")
    n = lib.asv_say_bytes(data, 1, None, 0)
    buf = ctypes.create_string_buffer(n + 1)
    lib.asv_say_bytes(data, 1, buf, n + 1)
    return buf.raw[:n] + b"\r"


roms = [open(os.path.join(ROMS, n), "rb").read() for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
err = ctypes.create_string_buffer(256)
v = lib.asv_create(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]), 44100.0, err, 256)
h = lib.asv_host(v)
lib.ash_set_int(h, b"log_writes", 1)


def writes():
    w = ctypes.POINTER(Write)()
    n = lib.ash_writes(h, ctypes.byref(w))
    out = [(w[i].reg, w[i].val) for i in range(n)]
    lib.ash_clear_writes(h)
    return out


y = ctypes.POINTER(D)()
for i in range(70):
    tx = text()
    for rate in (0, 50, 100):
        lib.asv_set(v, rate, 50, 100, 100, 1)
        lib.asv_begin(v)
        line = prepared(tx)
        lib.asv_text(v, line, len(line))
        pcm, done = PCM(), I(0)
        while not done.value:
            lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        lib.asv_end(v)
        writes()
        rows, t = [], 0.0
        while t < 8.0:
            lib.ash_run(h, 0.03, 0.0005, ctypes.byref(y))
            t += 0.03
            rows.append((t, writes(), lib.ash_speaking(h), lib.ash_get_int(h, b"work"), lib.ash_get_int(h, b"pc")))
        if any(r == 0 and val & 0x3F for _, w, _, _, _ in rows for r, val in w):
            print("text %d at rate %d: %r" % (i, rate, tx))
            t2 = 8.0
            while t2 < OBSERVE_S:
                lib.ash_run(h, 0.03, 0.0005, ctypes.byref(y))
                t2 += 0.03
                rows.append((t2, writes(), lib.ash_speaking(h), lib.ash_get_int(h, b"work"), lib.ash_get_int(h, b"pc")))
            print("  +0.03 s after done: speaking %d work %d pc %04X" % rows[0][2:])
            for t, w, spk, work, pc in rows:
                if w:
                    print("  +%.2f s: speaking %d, work %d, pc %04X after; writes %s" % (
                        t, spk, work, pc, " ".join("R%d=%02X" % rv for rv in w)))
            up = sum(1 for r in rows if r[2])
            print("  observed to +%.0f s: %d blocks with writes, the speaking flag up in %d blocks -- %s" % (
                OBSERVE_S, sum(1 for r in rows if r[1]), up, "SPEECH LEFT" if up else "IDLE WRITES"))
            sys.exit(0)
print("no delayed write in the session")
