"""The accented-letter pass (src/csrc/translit.h) for the references that predate it.

From 0.8 (it is not in 0.7.7 or any release before) every voice's text path starts with it -- "tükör" -> "tukor", a
lone "á" -> "a acute" -- and 0.7.0's Python drivers (legacy_drivers.py), the reference the C voices, the native
drivers and the SAPI voices are held to, never had it.  The tests that compare against them hand the reference the
text after the same pass, from the same C (ssv_translit in ssi263speech.dll), so everything after it is still held
byte for byte: current-preprocessing / frozen-downstream equivalence.  That is no oracle for the pass itself (the
same C on both sides cannot catch a wrong table entry): translit_test.py's and test_translit.c's handwritten
fixtures own the conversion, and its no-change inputs (the pass on against off, no reference conversion) what must
stay as it was.  Python 3.7 to 3.13, 32- and 64-bit; ctypes only.

    translit(text, charset)     charset ASCII (the English Braille Lite, the Speak-Out, the Accents) or CP850
"""
import ctypes
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
DLL = os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll")
ASCII, CP850 = 0, 1                                  # voices.h SSV_ASCII, SSV_CP850
_fn = []


def _load(path=None):
    if not _fn:
        fn = ctypes.CDLL(path or DLL).ssv_translit
        fn.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        fn.restype = ctypes.c_int
        _fn.append(fn)
    return _fn[0]


def translit(text, charset=ASCII, dll=None):
    """text after the pass, for a voice whose firmware knows charset"""
    if not isinstance(text, str) or not text:
        return text
    fn = _load(dll)
    raw = text.encode("utf-8", "surrogatepass")
    n = fn(raw, len(raw), charset, None, 0)
    if n < 0:
        raise MemoryError("ssv_translit")
    out = ctypes.create_string_buffer(n + 1)
    fn(raw, len(raw), charset, out, n + 1)
    return out.raw[:n].decode("utf-8", "surrogatepass")


def charset_of(encoding):
    """a driver's unit encoding ("latin-1", "cp850", "ascii") -> its charset"""
    return CP850 if encoding == "cp850" else ASCII
