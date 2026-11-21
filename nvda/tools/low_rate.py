"""The Braille Lite at a sample rate below 10 kHz speaks, not a full-scale tone (issue #81).

The board's 5 kHz low-pass (bl_host.c, a one-pole made with tan(pi * fc / rate)) has its cutoff above Nyquist below
10 kHz: the tangent turns negative, the pole lies outside the unit circle, and every sample after the first grows by
2.4 times with its sign flipping, clipped to full scale: the Python wheel's BrailleLite(rate=8000) and its --rate
8000 gave a tone at half the sample rate for the whole utterance, no speech and no error.  At such a rate the
low-pass has nothing in the band to take away: it is left out.

  rate      "Hello there." through the C voice (blv_create, as the wheel makes it) at 8000 and 9600 Hz, and at 22050 for
            comparison: at most 1 percent of the samples at full scale, and as long as the 22050 Hz speech (within a
            fifth)

    python low_rate.py <ssi263speech library> <BL2ENG.BNS> <its state>          exit 1 on any failure
    LOW_RATE_BREAK=1   bl_host.c bh_lowpass_break: the low-pass kept above Nyquist, as it was -- must fail 8000, 9600
"""
import ctypes
import os
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    lib_path, fw, st = (os.path.abspath(a) for a in sys.argv[1:])
    if not (os.path.isfile(fw) and os.path.isfile(st)):
        print("skipped: the Braille Lite's firmware or state is not there")
        return 0
    lib = ctypes.CDLL(lib_path)
    P = ctypes.c_void_p
    lib.blv_create.restype = P
    lib.blv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_double, ctypes.c_int,
                               ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.blv_speak.argtypes = [P, ctypes.c_char_p]
    lib.blv_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(ctypes.c_int)]
    lib.blv_destroy.argtypes = [P]
    if os.environ.get("LOW_RATE_BREAK"):
        ctypes.c_int.in_dll(lib, "bh_lowpass_break").value = 1
    seconds = {}
    bad = 0
    for rate in (22050, 8000, 9600):
        err = ctypes.create_string_buffer(256)
        v = lib.blv_create(os.fsencode(fw), os.fsencode(st), 0, float(rate), 1, 0, err, 256)
        if not v:
            print("FAIL rate %5d the voice did not start: %s" % (rate, err.value.decode("latin-1")))
            bad += 1
            continue
        lib.blv_speak(v, b"Hello there.")
        pcm, done, out = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), []
        for _ in range(2000):
            k = lib.blv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            out.extend(pcm[:k])
            if done.value:
                break
        lib.blv_destroy(v)
        full = sum(1 for x in out if x >= 32767 or x <= -32767)
        seconds[rate] = len(out) / float(rate)
        share = full / float(len(out) or 1)
        ok = len(out) > 0 and share <= 0.01
        if rate != 22050 and 22050 in seconds:
            ok = ok and abs(seconds[rate] - seconds[22050]) <= 0.2 * seconds[22050]
        bad += not ok
        print("%-4s rate %5d %.2f s of audio, %.1f percent of it at full scale" % ("ok" if ok else "FAIL", rate,
                                                                                seconds[rate], 100.0 * share))
    print("low rate: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
