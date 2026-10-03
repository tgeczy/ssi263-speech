"""Issue #8: is an Accent SA text done when as_voice says so?  Through the library every front end shares
(ssi263speech.dll / libssi263speech.so: the NVDA add-on's job API, and asv_speak, which the SAPI engine, the
speech-dispatcher module and Android reach through voices.c), each text is rendered until *done, and then the host
is run on for 4 s with nothing new to say.  A phoneme the unit writes then is speech the front end was told had
ended: in NVDA, speech that stopped at "Rate:" and came out with the next key.

Between two clauses the firmware empties its phoneme queue, drops its speaking flag and reads the next clause before
it speaks again; ash_busy() is false there.  0.7.5 and earlier called that done (most dialog labels end in a colon:
"Rate: slider 55 alt+r" from NVDA's rate 60 up, and longer multi-clause texts at every rate).  as_voice now holds
such a silence while the 8085 is still running its rules (outside its waiting loops: as_board.c's WAITING), and gives
its blocks out only if speech follows; asv_set_settle caps the wait (6 s).  A text that never paused keeps its audio
sample for sample (compared with the settle off, which is 0.7.5's done), and one that did begins with that audio.

    python test_as_complete.py [library]          exit 1 when a text ends early or its audio changed
    AS_COMPLETE_BREAK=1: the settle off (0.7.5's done) -- the must-fail control
Default library: $SSI263_LIB (Linux: libssi263speech.so), else the built NVDA add-on's ssi263speech.dll.
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
BREAK = os.environ.get("AS_COMPLETE_BREAK") == "1"
RUN_ON_S = 4.0
RATE = 22050                    # the add-ons' default sample rate


def library():
    if len(sys.argv) > 1:
        return sys.argv[1]
    if os.environ.get("SSI263_LIB") and not os.environ["SSI263_LIB"].endswith("ssi263.dll"):
        return os.environ["SSI263_LIB"]
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    return os.path.join(REPO, "nvda", "dist", "accent-build", "synthDrivers", "_ssi263_accent", "bin", arch,
                        "ssi263speech.dll")


P, I, D = ctypes.c_void_p, ctypes.c_int, ctypes.c_double


class Write(ctypes.Structure):
    _fields_ = [("t", D), ("reg", I), ("val", I)]


lib = ctypes.CDLL(library())
for name, res, args in (
        ("asv_create", P, [ctypes.c_char_p, ctypes.c_size_t] * 3 + [D, ctypes.c_char_p, I]),
        ("asv_destroy", None, [P]), ("asv_set", None, [P, I, I, I, I, I]), ("asv_set_settle", None, [P, D]),
        ("asv_speak", I, [P, ctypes.c_char_p, I]),
        ("asv_render", I, [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]),
        ("asv_begin", I, [P]), ("asv_text", I, [P, ctypes.c_char_p, I]), ("asv_end", I, [P]),
        ("asv_say_bytes", I, [ctypes.c_char_p, I, ctypes.c_char_p, I]), ("asv_host", P, [P]),
        ("ash_run", I, [P, D, D, ctypes.POINTER(ctypes.POINTER(D))]), ("ash_set_int", None, [P, ctypes.c_char_p, I]),
        ("ash_writes", I, [P, ctypes.POINTER(ctypes.POINTER(Write))]), ("ash_clear_writes", None, [P])):
    f = getattr(lib, name)
    f.restype, f.argtypes = res, args


def unit(settle):
    roms = [open(os.path.join(ROMS, n), "rb").read() for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
    err = ctypes.create_string_buffer(256)
    v = lib.asv_create(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]), float(RATE), err, 256)
    if not v:
        sys.exit("asv_create: %s" % err.value.decode("latin-1"))
    if settle is not None:
        lib.asv_set_settle(v, settle)
    lib.ash_set_int(lib.asv_host(v), b"log_writes", 1)
    return v


def phonemes(v):
    """the phonemes written since the last call (PA, 00, is not one)"""
    w = ctypes.POINTER(Write)()
    h = lib.asv_host(v)
    n = lib.ash_writes(h, ctypes.byref(w))
    out = sum(1 for i in range(n) if w[i].reg == 0 and (w[i].val & 0x3F))
    lib.ash_clear_writes(h)
    return out


def prepared(text):
    data = text.encode("utf-8")
    n = lib.asv_say_bytes(data, 1, None, 0)
    buf = ctypes.create_string_buffer(n + 1)
    lib.asv_say_bytes(data, 1, buf, n + 1)
    return buf.raw[:n] + b"\r"


def say(v, text, rate, job):
    """one text as a front end says it: (its audio, phonemes before done, phonemes after)"""
    lib.asv_set(v, rate, 50, 100, 100, 1)
    if job:                                   # the NVDA driver: begin, the text with its carriage return, end
        lib.asv_begin(v)
        line = prepared(text)
        lib.asv_text(v, line, len(line))
    else:                                     # asv_speak: SAPI, speech-dispatcher, Android
        lib.asv_speak(v, text.encode("utf-8"), 0)
    pcm, done, audio, blocks = ctypes.POINTER(ctypes.c_short)(), I(0), [], 0
    while not done.value and blocks < 4000:
        n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        audio.append(ctypes.string_at(pcm, 2 * n) if n > 0 else b"")
        blocks += 1
    if job:
        lib.asv_end(v)
    before = phonemes(v)
    y = ctypes.POINTER(D)()
    t = 0.0
    while t < RUN_ON_S:                       # the host on its own: what the unit still had to say
        lib.ash_run(lib.asv_host(v), 0.03, 0.0005, ctypes.byref(y))
        t += 0.03
    return b"".join(audio), before, phonemes(v)


DIALOG = ["Rate: slider %d alt+r", "Pitch: slider 50 alt+p", "Volume: slider 100 alt+o",
          "Inflection: slider 100 alt+i", "Variant: combo box Voice 5 (default) alt+a", "Voice: combo box Accent SA alt+v"]
WORDS = ("the quick brown fox jumps over a lazy dog extraordinary international communication representative "
         "1234 567 89 2026 $30.50 3.14 NVDA synthesizer settings dialog check box checked button OK").split()
rng = random.Random(8)
CORPUS = ["".join(" ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 9))) + rng.choice(":,;.?!")
                  + " " for _ in range(rng.randint(2, 4))).strip() for _ in range(6)]
CASES = [(t % r if "%d" in t else t, r, j) for r in (50, 60, 75, 100) for t in DIALOG for j in (True, False)
         if r in (60, 100) or t.startswith("Rate")]
CASES += [(t, r, j) for t in CORPUS for r in (0, 50, 100) for j in (True, False)]
bad = changed = paused = 0
for text, rate, job in CASES:                 # a fresh unit each, as booted: the two runs start alike
    fixed = unit(0.0 if BREAK else None)
    old = unit(0.0)                           # the settle off: the audio a text that never paused had
    audio, before, late = say(fixed, text, rate, job)
    ref, ref_before, ref_late = say(old, text, rate, job)
    lib.asv_destroy(fixed)
    lib.asv_destroy(old)
    if late:
        bad += 1
        print("EARLY  rate %3d %-4s %r: %d phonemes before done, %d after" % (rate, "job" if job else "say", text,
                                                                           before, late))
    if ref_late:
        paused += 1                           # 0.7.5's done was early here: the audio goes on now, by design --
        if audio[:len(ref)] != ref:           # after the same samples
            changed += 1
            print("AUDIO  rate %3d %-4s %r: does not begin with the settle-off audio" % (
                rate, "job" if job else "say", text))
    elif audio != ref:
        changed += 1
        print("AUDIO  rate %3d %-4s %r: %d samples, %d with the settle off" % (rate, "job" if job else "say", text,
                                                                            len(audio) // 2, len(ref) // 2))
print("%d of %d texts ended early, %d of %d that never paused changed their audio (%d paused)"
      % (bad, len(CASES), changed, len(CASES) - paused, paused))
print("accent sa completion: %s" % ("FAILED" if bad or changed else "ok"))
sys.exit(1 if bad or changed else 0)
