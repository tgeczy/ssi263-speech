"""Numbers written with leading zeros are read digit by digit, the zeros said (issue #65; Tomi: "the zero reading out
makes sense on the digits", for codes, ZIP codes, the parts of an IP address).

The number words built the value as an integer: "047291" became "forty seven thousand two hundred ninety one", "007"
"seven", "zip 02134" "two thousand one hundred thirty four" -- the zeros never said, a six-digit code read as five
digits.  A whole part of two digits or more that starts with 0 is now said digit by digit; "0" alone, "0.5", every
other number and the ordinals are as they were.

  python    nvda/shared/ssi263_numwords.normalise (the NVDA drivers): English, and Spain's Spanish (decimal comma)
  c         bl_numbers in the speech library (numwords.c, numwords_es.c: SAPI, speech-dispatcher, the phones, and the
            Accents, the Mockingboard and the SmarTalk through nw_normalise)

    python leading_zeros.py [<ssi263speech library>]          exit 1 on any failure
    NUMWORDS_ZERO_BREAK=1   every implementation builds the integer, as it did -- must fail the zero cases
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

EN = [("code 047291", "code zero four seven two nine one"), ("007", "zero zero seven"),
      ("zip 02134", "zip zero two one three four"), ("at 09:05", "at zero nine:zero five"),
      ("0", "zero"), ("0.5", "zero point five"), ("10", "ten"), ("100 and 3rd", "one hundred and third")]
ES = [("007", "cero cero siete"), ("código 0421", "código cero cuatro dos uno"), ("0", "cero"), ("10", "diez")]
ZERO_CASES = {"code 047291", "007", "zip 02134", "at 09:05", "código 0421"}


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
                print("%-4s %-6s %s %-22r -> %r%s" % ("ok" if ok else "FAIL", side, lang, text, out,
                                                     "" if ok else " (want %r)" % want))
    if lib is None:
        print("skip  c: no speech library at %s" % LIB)
    print("leading zeros: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
