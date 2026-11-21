"""The Accent-mini speaks a long text item whole (issue #67).

One text item of 3,000 characters (a long paragraph, a licence, a line with no breaks) through amv_speak, rendered to
its end at the default settings and at rate 20.  The driver takes the text only as its buffer empties, at speaking
pace, so the call that hands it the text runs as long as the speech: the host took any call still waiting after 30 s
of the card's time for a stalled card, stopped it mid-sentence, and NVDA rebooted the card and dropped the rest
(measured: 2,600 characters and up gave 29.95 s and a fault).  A call that keeps the card talking is not stalled.

  whole     no fault, and the audio as long as the text needs (over 100 s; it was 29.95 s)

    python am_long_text.py [ssi263speech library]                exit 1 on any failure
    AM_LONG_TEXT_BREAK=1   am_host.c amh_call_stall_break: 30 s from the call's start, as it was -- must fail: whole
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
WINDOWS = sys.platform == "win32"
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
LIB = (_args[0] if _args else os.environ.get("SSI263_VOICES_LIB") or (
    os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll") if WINDOWS
    else os.path.join(REPO, "build", "linux", "libssi263_voices.so")))
DVC = os.environ.get("SSI263_ACCENT_DVC") or os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
RATE = 22050
SENTENCE = "The quick brown fox jumps over the lazy dog, and then it runs on into the next field. "
TEXT = (SENTENCE * (3000 // len(SENTENCE) + 1))[:3000]


def main():
    if not os.path.isfile(DVC):
        print("skipped: the Accent-mini's SPKEMS.DVC is not there (%s)" % DVC)
        return 0
    lib = ctypes.CDLL(LIB)
    lib.amv_create_mem.restype = ctypes.c_void_p
    lib.amv_create_mem.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.amv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 6
    lib.amv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.amv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.amv_fault.argtypes = [ctypes.c_void_p]
    lib.amv_destroy.argtypes = [ctypes.c_void_p]
    if os.environ.get("AM_LONG_TEXT_BREAK"):
        ctypes.c_int.in_dll(lib, "amh_call_stall_break").value = 1
    with open(DVC, "rb") as f:
        dvc = f.read()
    bad = 0
    for label, rate in (("default", None), ("rate 20", 20)):
        err = ctypes.create_string_buffer(256)
        v = lib.amv_create_mem(dvc, len(dvc), float(RATE), err, 256)
        if not v:
            print("FAIL whole     %s: the voice did not start: %s" % (label, err.value.decode("latin-1")))
            bad += 1
            continue
        if rate is not None:
            lib.amv_set(v, rate, 50, 100, 100, 1, 5)       # the driver's defaults but the rate
        lib.amv_speak(v, TEXT.encode("utf-8"), 0)
        pcm, done, n, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), 0, 0
        while not done.value and blocks < 200000:
            n += lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            blocks += 1
        fault = lib.amv_fault(v)
        lib.amv_destroy(v)
        seconds = n / float(RATE)
        ok = not fault and seconds > 100.0
        bad += not ok
        print("%-4s whole     %s: %d characters, %.2f s of audio, fault %s" % ("ok" if ok else "FAIL", label,
                                                                             len(TEXT), seconds, bool(fault)))
    print("accent-mini long text: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
