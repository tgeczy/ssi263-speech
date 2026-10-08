"""bl_voice's text path against the real driver's, on thousands of random strings: the bytes unit.say() sends
(ssi263_numwords.currencies, _clean, _lines, the encoding, each line CR ^F and one more CR ^F) must be identical.
The audio gate (voice_equiv.py) speaks a few texts; this one hunts the edge cases in the text rules -- currency
amounts, digits and commas, punctuation, odd whitespace, characters the units lack -- in a second.

    python voice_text_equiv.py [count] [seed]
    python voice_text_equiv.py [count] [seed] --numbers
                              # with the driver's "Custom number processing" (its default, the SAPI engine's): the
                              # driver's _numbers against bl_numbers (blazie/bl_numbers.c, numwords_es.c), from
                              # ssi263speech.dll (src/csrc/build_ssi263speech.py)
    VOICE_TEXT_BREAK=1        # control: the Python side skips currencies() -- must FAIL
    VOICE_TEXT_BREAK=numbers  # control, with --numbers: the Python side skips _numbers -- must FAIL
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NUMBERS = "--numbers" in sys.argv
POS = [a for a in sys.argv[1:] if not a.startswith("--")]
COUNT = int(POS[0]) if len(POS) > 0 else 5000
SEED = int(POS[1]) if len(POS) > 1 else 1
sys.argv = [sys.argv[0], "blazie"]
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("d = drv_mod.SynthDriver()")[0])     # the driver module only: no unit is started
BREAK = os.environ.get("VOICE_TEXT_BREAK") == "1"
BREAK_NUMBERS = os.environ.get("VOICE_TEXT_BREAK") == "numbers"

ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
REPO = os.path.dirname(os.path.dirname(HERE))
if NUMBERS:                                         # the unified library: bl_voice with bl_numbers beside it
    lib = ctypes.CDLL(os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll"))
    lib.blv_say_bytes_with.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p,
                                       ctypes.c_int]
    lib.blv_say_bytes_with.restype = ctypes.c_int
    NUMBERS_FN = ctypes.cast(lib.bl_numbers, ctypes.c_void_p)
else:
    ctypes.CDLL(os.path.join(REPO, "src", "ssi263", "_bin", ARCH, "ssi263.dll"))
    lib = ctypes.CDLL(os.path.join(os.path.dirname(HERE), "dist", "blazie-lib", ARCH, "bl.dll"))
lib.blv_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
lib.blv_say_bytes.restype = ctypes.c_int

PIECES = (list("abcdefghij ABCXYZ   0123456789") + [" ", " ", ".", ",", "!", "?", ":", ";", "'", "-", "_", "$"]
          + ["£", "€", "¥", "¢", "£", "€", " €", " ", "\t", "\n", "\r",
             "‘", "’", "“", "”", "–", "—", "…", "é", "ñ", "¿",
             "¡", "\U0001F389", "²", "½", "\x07", "\x7f", " ", "É",
             "ő", "ß", "Á", "tükör ", "é"]
          + ["£2.63", "£1.01", "€0.5", "5 €", "1,234.56", "£.5", "¥1.5", "50¢",
             "£1,23", "£ 12", ".63", "2.635", "word ", "sentence. ", "Next! ", "why? ", "a, b, c, "])
if NUMBERS:                                         # the number words' own edge cases, English and Spanish
    PIECES += ["1.234.567", "3,5", "-4,25", "21st", "3RD", "12th", "$1,234,567,890,123.45", "$12", "$.50", "$1,2",
               "9999999999999999999", "0.5", "192.168.0.1", "1,23,456", "-7", "100", "1000000", "21000", "1001",
               "31000000", "2.000.000.000.000", "x2", "2x", "15.", "é3", "3ñ", "1,234.5.6", "007",
               "$9999999999999.999"]


def py_bytes(text, enc_name, lang, pack):
    text = drv_mod._translit(text, enc_name)        # the accented letters first (translit.h, the driver's ctypes call)
    t = text if BREAK else drv_mod.numwords.currencies(text, lang)
    t = drv_mod._clean(t, enc_name)
    if NUMBERS and not BREAK_NUMBERS:
        t = drv_mod._numbers(t, lang)
    lines = drv_mod._lines(t, pack=pack)
    if not lines:
        return b""
    return b"".join(ln.encode(enc_name, "replace") + b"\r\x06" for ln in lines) + b"\r\x06"


def c_bytes(text, enc, pack):
    raw = text.encode("utf-8", "surrogatepass")
    if NUMBERS:
        n = lib.blv_say_bytes_with(raw, enc, int(pack), NUMBERS_FN, None, 0)
        buf = ctypes.create_string_buffer(max(n, 1))
        lib.blv_say_bytes_with(raw, enc, int(pack), NUMBERS_FN, buf, n)
        return buf.raw[:n]
    n = lib.blv_say_bytes(raw, enc, int(pack), None, 0)
    buf = ctypes.create_string_buffer(max(n, 1))
    lib.blv_say_bytes(raw, enc, int(pack), buf, n)
    return buf.raw[:n]


rng = random.Random(SEED)
bad = 0
for k in range(COUNT):
    text = "".join(rng.choice(PIECES) for _ in range(rng.randint(0, 60)))
    enc, enc_name, lang = (1, "cp850", "es") if k % 5 == 4 else (0, "latin-1", "en")
    pack = k % 3 != 2
    a, b = py_bytes(text, enc_name, lang, pack), c_bytes(text, enc, pack)
    if a != b:
        bad += 1
        if bad <= 5:
            print("DIFF %s pack=%d %a\n  driver %a\n  C      %a" % (enc_name, pack, text, a, b))   # %a: NVDA-safe
print("%d of %d texts give the unit the same bytes (driver vs bl_voice)" % (COUNT - bad, COUNT))
sys.exit(1 if bad else 0)
