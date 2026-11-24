"""A number too long for the scale names -- more than 18 significant digits -- is read digit by digit, as an
identifier: a USPS tracking number is 20 to 22 digits (Tomi, 2026-11-24: "if there's an argument for number processing
past 20, tracking numbers would be it").  It was "large number", which hid every digit, on every voice whose number
words are on by default (the Braille Lite, the Accents, the Mockingboard, the SmarTalk, the SynPhonix).  Up to 18
digits it is words as before; an ordinal that long stays "large number"; a letter-and-digit identifier is left alone.

  python    nvda/shared/ssi263_numwords.normalise (the NVDA drivers): English, and Spain's Spanish
  c         bl_numbers in the speech library (numwords.c, numwords_es.c: SAPI, speech-dispatcher, the phones, and the
            Accents, the Mockingboard, the SmarTalk and the SynPhonix through nw_normalise)

    python long_numbers.py [<ssi263speech library>]          exit 1 on any failure
    NUMWORDS_LARGE_BREAK=1   every implementation says "large number", as it did -- must fail the long cases
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "shared"))
import ssi263_numwords as numwords  # noqa: E402

ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
LIB = sys.argv[1] if len(sys.argv) > 1 else (
    os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll") if os.name == "nt"
    else os.path.join(REPO, "build", "linux", "libssi263_voices.so"))

D = "zero one two three four five six seven eight nine".split()
D_ES = "cero uno dos tres cuatro cinco seis siete ocho nueve".split()
USPS = "9400111899223397658538"
EN = [("tracking " + USPS, "tracking " + " ".join(D[int(c)] for c in USPS)),
      ("1234567890123456789", " ".join(D[int(c)] for c in "1234567890123456789")),
      ("999999999999999999", "nine hundred ninety nine quadrillion nine hundred ninety nine trillion nine hundred "
                             "ninety nine billion nine hundred ninety nine million nine hundred ninety nine thousand "
                             "nine hundred ninety nine"),
      ("the 12345678901234567890th", "the large number"),
      ("1Z999AA10123456784", "1Z999AA10123456784"),
      ("100", "one hundred")]
ES = [(USPS, " ".join(D_ES[int(c)] for c in USPS)), ("10", "diez")]
LONG_CASES = {"tracking " + USPS, "1234567890123456789", USPS}


def main():
    bad = 0
    lib = ctypes.CDLL(LIB) if os.path.isfile(LIB) else None
    if lib is not None:
        lib.bl_numbers.restype = ctypes.c_char_p
        lib.bl_numbers.argtypes = [ctypes.c_char_p, ctypes.c_int]
    for lang, cases in (("en", EN), ("es", ES)):
        for text, want in cases:
            py = numwords.normalise(text) if lang == "en" else numwords.normalise(text, lang="es", decimal_comma=True)
            got = [("python", py)]
            if lib is not None:
                c = lib.bl_numbers(text.encode("utf-8"), 0 if lang == "en" else 13)
                got.append(("c", c.decode("utf-8") if c is not None else None))
            for side, out in got:
                ok = out == want
                bad += not ok
                print("%-4s %-6s %s %-30r -> %r%s" % ("ok" if ok else "FAIL", side, lang, text[:30], out[:70] if out else out,
                                                     "" if ok else " (want %r)" % want[:70]))
    if lib is None:
        print("skip  c: no speech library at %s" % LIB)
    print("long numbers: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
