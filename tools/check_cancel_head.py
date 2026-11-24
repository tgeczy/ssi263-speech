"""After a cancel, every voice's next text starts as it does after a natural end (the voice table, src/csrc/voices.h).

A cut that leaves anything of the cut text in the unit -- a line it kept, a phoneme still fading, the program's own
closing frames -- comes out at the head of the NEXT text: late, longer, the old sound first.  Each device has had it
in turn (the Braille Lite's kept line, #30; the Mockingboard's driver playing on, 3849b75; the SynPhonix's cut
phoneme's fade, #91), each found by a listener and fixed in that voice alone.  This checks the whole voice table, so a
voice added to it is checked the day it is added (Tomi, 2026-11-24: "we cannot make this same mistake each voice we
build").

For every voice whose firmware is here: WORD said NATURAL times after natural ends, their longest its own spread (the
Braille Lites vary by up to 90 ms); then, for each of CUTS, LONG cut after that many blocks (ssv_cancel) and WORD said
at once.  WORD after a cut may not run more than SLACK_MS past that longest: a kept line is half a second and more
(the Braille Lite's, released in lockstep since 0.9.5: 3-4 s of the cut text before WORD).  What it does not see: a
short tail, tens of milliseconds -- each voice's own check finds that from its chip writes (the SynPhonix's, #91:
tools/check_sp_voice.py "a cancel: the next text starts as after an end").  Audio only, so it needs nothing of a voice
but the table.

    python tools/check_cancel_head.py [library] [--firmware DIR] [--only ID,...]

Control: BL_KEPT_LINE_BREAK=1 (bl_host.c: the lockstep cancel as it was, a kept line not released) -- the Braille
Lite must fail (its English voice leaks 3.9 s at the cut after block 5), every voice without a Braille Lite pass.
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
WINDOWS = sys.platform == "win32"
args = list(sys.argv[1:])
FW = os.path.join(ROOT, "firmware")
ONLY = None
if "--firmware" in args:
    k = args.index("--firmware")
    FW = args[k + 1]
    del args[k:k + 2]
if "--only" in args:
    k = args.index("--only")
    ONLY = set(args[k + 1].split(","))
    del args[k:k + 2]
LIB = args[0] if args else os.environ.get("SSI263_LIB") or (
    os.path.join(ROOT, "build", "win", ARCH, "ssi263speech.dll") if WINDOWS
    else os.path.join(ROOT, "build", "linux", "libssi263_voices.so"))

WORD = "Down arrow."
LONG = ("This is a long line of speech that will be cut off somewhere in the middle, and then it goes on and on for a "
        "good while longer, so that there is always more of it left to say when the cut comes.")
CUTS = (2, 5, 12, 20)
NATURAL = 5                 # natural ends measured for the voice's own spread (the Braille Lites': up to 90 ms)
SLACK_MS = 300              # what a text after a cut may run past the longest of them: a kept line is 450 ms and more
MAX_BLOCKS = 4000


class Settings(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("rate", "pitch", "volume", "tone", "pack", "join", "inflection",
                                            "numbers", "run_ahead", "line_lift")]


class Info(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("name", ctypes.c_char_p), ("lang", ctypes.c_char_p)]


def load():
    lib = ctypes.CDLL(LIB)
    lib.ssv_count.restype = ctypes.c_int
    lib.ssv_voice_info.restype = ctypes.POINTER(Info)
    lib.ssv_voice_info.argtypes = [ctypes.c_int]
    lib.ssv_engine_built.argtypes = [ctypes.c_int]
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.ssv_create.restype = ctypes.c_void_p
    lib.ssv_create.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_defaults.argtypes = [ctypes.c_int, ctypes.POINTER(Settings)]
    lib.ssv_sample_rate.argtypes = [ctypes.c_void_p]
    lib.ssv_speak.argtypes = [ctypes.c_void_p, ctypes.POINTER(Settings), ctypes.c_char_p, ctypes.c_int]
    lib.ssv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.ssv_cancel.argtypes = [ctypes.c_void_p]
    lib.ssv_destroy.argtypes = [ctypes.c_void_p]
    return lib


def say(lib, v, s, text, cut=None):
    """the samples rendered (a list of ints); with cut, the render stops after that many blocks and the unit is
    cancelled"""
    pcm, done, out, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], 0
    if lib.ssv_speak(v, s, text.encode("utf-8"), 0) < 0:
        return None
    while not done.value and blocks < MAX_BLOCKS:
        n = lib.ssv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        out.extend(pcm[k] for k in range(n))
        blocks += 1
        if cut is not None and blocks == cut:
            lib.ssv_cancel(v)
            break
    return out


def main():
    lib = load()
    fw = FW.encode("utf-8")
    results, ran = [], 0
    for i in range(lib.ssv_count()):
        vid = lib.ssv_voice_info(i).contents.id.decode()
        if ONLY and vid not in ONLY:
            continue
        if not lib.ssv_engine_built(i) or not lib.ssv_available(i, fw):
            print("skip  %-28s no engine or no firmware here" % vid)
            continue
        err = ctypes.create_string_buffer(256)
        v = lib.ssv_create(i, fw, None, err, 256)
        if not v:
            print("FAIL  %-28s not made: %s" % (vid, err.value.decode("latin-1")))
            results.append(False)
            continue
        try:
            s = Settings()
            lib.ssv_defaults(i, ctypes.byref(s))
            sr = lib.ssv_sample_rate(v)
            ms = lambda n: 1000.0 * n / sr       # noqa: E731
            say(lib, v, s, WORD)
            natural = [len(say(lib, v, s, WORD)) for _ in range(NATURAL)]      # the voice's own spread
            limit = ms(max(natural)) + SLACK_MS
            rows, good = [], True
            for cut in CUTS:
                say(lib, v, s, LONG, cut=cut)
                x = say(lib, v, s, WORD)
                got = ms(len(x)) if x is not None else -1.0
                ok = 0.0 <= got <= limit
                good = good and ok
                rows.append("%d: %.0f%s" % (cut, got, "" if ok else " LEAK"))
            ran += 1
            results.append(good)
            print("%-5s %-28s ms after a cut at block %s (natural ends %.0f-%.0f)" % (
                "ok" if good else "FAIL", vid, "; ".join(rows), ms(min(natural)), ms(max(natural))))
        finally:
            lib.ssv_destroy(v)
    if not ran:
        print("skip  no voice's firmware in %s" % FW)
        return 0
    print("cancel head: %s" % ("all %d voices passed" % ran if all(results) else
                               "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1

if __name__ == "__main__":
    sys.exit(main())
