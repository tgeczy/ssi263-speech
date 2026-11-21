"""The Mockingboard voices: every part of a longer utterance starts from a tract at rest, as each utterance does
(issue #75).

The voice says a text in parts of at most MBV_PART (120) characters, each part a whole text to the firmware, which
ends one by powering the chip down with the last phoneme still at full amplitude.  The tract was brought to rest
before each UTTERANCE (mb_voice.c rest; tools/check_mb_cancel.py), but not before the parts after its first: each of
those began with the pop and the old phoneme that rest was written to remove (on 1.1), or ringing and late (early).

For each version: two sentences said as ONE utterance, which the voice cuts into two parts
(mbv_parts says so), against the same two said as two utterances, one after the other on a fresh voice.  The first
part and the head of the second (HEAD_S, where a pop and the old phoneme would be) must be the same, sample for
sample: the second part starts exactly as a second utterance does.

    python tools/check_mb_parts.py [library]     (default build/win/x64/ssi263speech.dll)

Control (MB_PART_REST_BREAK=1, mb_voice.c): no rest between the parts of an utterance, as it was -- both versions
must fail.
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware", "sweet-micro-mockingboard")
P1 = "Warning, the quick brown fox jumps over the lazy dog."     # a cut is taken from 40 characters on
P2 = "Then it rests a while in the shade of the old oak tree by the river."
R = 22050
HEAD_S = 0.4                    # of the second part compared (its start)


def load(path):
    lib = ctypes.CDLL(path)
    p = ctypes.c_void_p
    lib.mbv_create.restype = p
    lib.mbv_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.mbv_speak.argtypes = [p, ctypes.c_char_p, ctypes.c_int]
    lib.mbv_render.argtypes = [p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(ctypes.c_int)]
    lib.mbv_parts.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.mbv_destroy.argtypes = [p]
    return lib


def say(lib, v, text, limit=None):
    """the text's audio, to its end or `limit` samples"""
    pcm, done, out = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), []
    lib.mbv_speak(v, text.encode(), 0)
    for _ in range(2000):
        if limit is not None and len(out) >= limit:
            break
        k = lib.mbv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        if k > 0:
            out.extend(pcm[:k])
        if done.value or k < 0:
            break
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "win", "x64", "ssi263speech.dll")
    names = ("mockingboard-tts-1.1.bin", "mockingboard-tts-early.bin")
    if not all(os.path.isfile(os.path.join(FW, n)) for n in names):
        print("skip  the Mockingboard parts check: its files are not in firmware/sweet-micro-mockingboard")
        return 0
    lib = load(path)
    buf = ctypes.create_string_buffer(4096)
    n = lib.mbv_parts((P1 + " " + P2).encode(), 1, buf, len(buf))
    parts = [p for p in buf.value.decode("latin-1").split("|") if p]
    failures = 0
    if len(parts) != 2 or parts[0] != P1:
        print("FAIL  the text is not cut in two parts after %r: %r" % (P1, parts))
        failures += 1
    err = ctypes.create_string_buffer(300)
    for name in names:
        img = open(os.path.join(FW, name), "rb").read()
        apart = lib.mbv_create(img, len(img), float(R), err, 300)
        whole = lib.mbv_create(img, len(img), float(R), err, 300)
        if not apart or not whole:
            print("FAIL  %s: %s" % (name, err.value.decode()))
            failures += 1
            continue
        head = int(HEAD_S * R)
        a1 = say(lib, apart, P1)
        a2 = say(lib, apart, P2, head)[:head]
        w = say(lib, whole, P1 + " " + P2, len(a1) + head)[:len(a1) + head]
        lib.mbv_destroy(apart)
        lib.mbv_destroy(whole)
        same = w == a1 + a2
        k = next((i for i in range(min(len(w), len(a1 + a2))) if w[i] != (a1 + a2)[i]), min(len(w), len(a1 + a2)))
        print("%-5s %s: one utterance in two parts, its first %d samples; the two said apart %d + %d; %s" % (
            "ok" if same else "FAIL", name, len(w), len(a1), len(a2),
            "the same, sample for sample" if same else "first different at sample %d (the second part starts at %d)"
            % (k, len(a1))))
        failures += not same
    print("mb parts: %s" % ("all passed" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
