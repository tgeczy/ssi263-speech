"""so_voice's text path against the real Speak-Out driver's, on thousands of random strings: the bytes box.say() is
given for a text item (ssi263_numwords.currencies, _clean, strip(), Latin-1, the carriage return) must be identical.
The audio gate (so_voice_equiv.py) speaks a few texts; this one hunts the edge cases of the text rules -- currency
amounts, quotes and dashes, control characters, characters beyond Latin-1, and the whitespace strip() removes after
_clean (the space, U+0085 and U+00A0) -- in a second.  0.7.0's driver predates the accented-letter pass
(src/csrc/translit.h), so it is given the text after it (translit_ref.py): current-preprocessing / frozen-downstream
equivalence, not an oracle for the pass (translit_test.py's handwritten fixtures are).

    python so_voice_text_equiv.py [count] [seed]
    SO_VOICE_TEXT_BREAK=1        # control: the driver's strip() reduced to spaces only -- must FAIL
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
sys.argv = [sys.argv[0], "speakout"]
# the reference is 0.7.0's Python driver (since 0.7.5 the add-on's driver has no Python front end)
sys.path.insert(0, HERE)
import legacy_drivers  # noqa: E402
import translit_ref  # noqa: E402
os.environ["SSI263_SYNTH_DRIVERS"] = legacy_drivers.synth_drivers("speakout")
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("d = drv_mod.SynthDriver()")[0])     # the driver module only: no box is started
BREAK = os.environ.get("SO_VOICE_TEXT_BREAK") == "1"

ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
ctypes.CDLL(os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "ssi263", "_bin", ARCH, "ssi263.dll"))
lib = ctypes.CDLL(os.path.join(os.path.dirname(HERE), "dist", "speakout-lib", ARCH, "so_voice.dll"))
lib.sov_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
lib.sov_say_bytes.restype = ctypes.c_int

PIECES = (list("abcdefghij ABCXYZ   0123456789") + [" ", " ", ".", ",", "!", "?", ":", ";", "'", "-", "_", "$", "~"]
          + ["£", "€", "¥", "¢", " €", "\t", "\n", "\r", "\x05", "\x18", "\x7f", "\x85", "\xa0", "\xa0 ", " \x85",
             "‘", "’", "“", "”", "–", "—", "…", "é", "ñ", "¿", "¡", "\U0001F389", "²", "½", "ÿ", "É", " ",
             "　", "ő", "ß", "Á", "tükör ", "é", "Æ"]
          + ["£2.63", "£1.01", "€0.5", "5 €", "1,234.56", "£.5", "¥1.5", "50¢", "$3.50", "£1,23", "£ 12", ".63",
             "word ", "sentence. ", "Next! ", "why? ", "a, b, c, "])


def py_bytes(text):
    text = translit_ref.translit(text)      # 0.7.0's driver predates the accented-letter pass: it gets its output
    t = drv_mod._clean(drv_mod.numwords.currencies(text))
    t = t.strip(" ") if BREAK else t.strip()
    return (t + "\r").encode("latin-1", "replace") if t else b""


def c_bytes(text):
    raw = text.encode("utf-8", "surrogatepass")
    n = lib.sov_say_bytes(raw, None, 0)
    buf = ctypes.create_string_buffer(max(n, 1))
    lib.sov_say_bytes(raw, buf, n)
    return buf.raw[:n]


if BREAK:
    print("CONTROL: the driver's strip() reduced to spaces")
rng = random.Random(SEED)
bad = 0
for k in range(COUNT):
    text = "".join(rng.choice(PIECES) for _ in range(rng.randint(0, 40)))
    a, b = py_bytes(text), c_bytes(text)
    if a != b:
        bad += 1
        if bad <= 5:
            print("DIFF %a\n  driver %a\n  C      %a" % (text, a, b))        # %a: NVDA-safe
print("%d of %d texts give the box the same bytes (driver vs so_voice)" % (COUNT - bad, COUNT))
sys.exit(1 if bad else 0)
