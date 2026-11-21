"""The Accent-mini stops at once when a long utterance is cancelled (the stack slot found with issue #74).

The DOS driver takes its text only as its buffer empties, so a text longer than the buffer (about 2,000 characters)
is still being handed over while it is spoken: a background call.  A cancel ends that call by writing "one character
left" into the count the client pushed, then sends Ctrl-X.  The slot was taken in the stack segment the DRIVER was on
when the call was left running (its INT 17h switches to its own, 0800h), not the call's: the write missed, and the
client went on to the text's end, the card run through all of it (its audio thrown away; the next utterance waited).

  plain     a 3,000-character text, cancelled after 4 blocks: the cancel takes under 2 s of the card's time (it took
            87.8 s; 2,400 characters 42 s), leaves no fault, and the next utterance speaks

    python am_cancel_capital.py [ssi263speech library]                exit 1 on any failure
    AM_CANCEL_CAPITAL_BREAK=ss        am_host.c amh_drop_ss_break: the slot in the driver's segment -- must fail
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
    P = ctypes.c_void_p
    lib.amv_create_mem.restype = P
    lib.amv_create_mem.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.amv_speak.argtypes = [P, ctypes.c_char_p, ctypes.c_int]
    lib.amv_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(ctypes.c_int)]
    lib.amv_cancel.argtypes = [P]
    lib.amv_fault.argtypes = [P]
    lib.amv_destroy.argtypes = [P]
    lib.amv_host.restype = P
    lib.amv_host.argtypes = [P]
    lib.amh_get_double.restype = ctypes.c_double
    lib.amh_get_double.argtypes = [P, ctypes.c_char_p]
    brk = os.environ.get("AM_CANCEL_CAPITAL_BREAK", "")
    if brk == "ss":
        ctypes.c_int.in_dll(lib, "amh_drop_ss_break").value = 1
    with open(DVC, "rb") as f:
        dvc = f.read()
    bad = 0
    for label, offset in (("plain", 0),):
        err = ctypes.create_string_buffer(256)
        v = lib.amv_create_mem(dvc, len(dvc), float(RATE), err, 256)
        if not v:
            print("FAIL %-9s the voice did not start: %s" % (label, err.value.decode("latin-1")))
            bad += 1
            continue
        pcm, done = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0)
        lib.amv_speak(v, TEXT.encode("utf-8"), offset)
        for _ in range(4):
            lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        t0 = lib.amh_get_double(lib.amv_host(v), b"time")
        lib.amv_cancel(v)
        spent = lib.amh_get_double(lib.amv_host(v), b"time") - t0
        fault = lib.amv_fault(v)
        n, done.value = 0, 0
        lib.amv_speak(v, b"After the cancel.", 0)
        while not done.value:
            n += lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        lib.amv_destroy(v)
        ok = spent < 2.0 and not fault and n > RATE // 4
        bad += not ok
        print("%-4s %-9s 3,000 characters at pitch offset %+d, cancelled after 4 blocks: the cancel took %.2f s of the "
              "card's time, fault %s; the next utterance %.2f s" % ("ok" if ok else "FAIL", label, offset, spent,
                                                                   bool(fault), n / float(RATE)))
    print("accent-mini cancel: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
