"""The symbol guard (bl_voice.c drop_symbols, blazie.py _symbols; bl_languages.txt's symbols column): a release whose
own reader names symbols with its punctuation set to none -- the Spanish and Portuguese Braille Lites read "(" as
"abre paréntesis", "-" as "guion", "%" as "por ciento", whatever the speech menu says (#68) -- is sent none of the
symbols it names.  NVDA names a symbol itself when its symbol level says so; one it leaves in the text is one the
user chose not to hear, which the English unit, with punctuation none, does not say.  A "$" before a digit stays:
the English unit reads "$5" as "five dollars", the Spanish one as "dólar cinco".  Through the unified library
(ssi263speech.dll, or libssi263speech.so):

  table   the release table: the Spanish and both Portuguese releases name the 25 symbols, the Russian one "@" alone,
          the English one none
  text    the bytes such a release is sent (blv_say_bytes_guarded): each symbol it names a space, a "$" before a digit
          kept, every other character as it was -- on known cases and random texts; another release's text untouched
  unit    the Spanish unit itself (its image looked for in the firmware folder and below; skipped, and said so,
          without it): "alpha (beta) gamma" has the phonemes of "alpha beta gamma", "a-b" those of "a b", "50%"
          those of "50"; and "$5" still says its dollar

    python test_symbols.py [--firmware DIR] [--lib PATH]

Control (SSI263_SYMBOLS_BREAK=1, bl_voice.c blv_symbols_break): the symbols left as they are -- "text" fails, and the
unit names them: "unit" fails.
"""
import argparse
import ctypes
import os
import random
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
import gen_languages  # noqa: E402
import test_releases  # noqa: E402

BREAK = os.environ.get("SSI263_SYMBOLS_BREAK") == "1"
WINDOWS = sys.platform == "win32"
RATE = 22050
SPANISH, ENGLISH, RUSSIAN = "bl2_es_2000_09", "bl2_en_2003_06", "bl2_ru_1998_12"
PORTUGUESE = ("bl2_pt_1998_12", "bl2_pt_br_1998_12")
ALL = "\"#$%&()*+-/<=>@[\\]^_`{|}~"       # the 25 the Spanish and Portuguese units name (measured, 2026-11-21)
TEXT = [("(hola)", "hola"), ("alpha (beta) gamma", "alpha beta gamma"), ("a-b", "a b"), ("$5", "$5"),
        ("$ 5", "5"), ("5$", "5"), ("50%", "50"), ("a@b.c", "a b.c"), ("x=y", "x y"), ("uno, dos.", "uno, dos."),
        ("[1] {2} <3>", "1 2 3"), ("a_b*c+d/e", "a b c d e"), ("#1 & #2", "1 2"), ("it's", "it's"),
        ("why? yes! a: b; c.", "why? yes! a: b; c."), ("$$5", "$5"), ("$a", "a")]


class Write(ctypes.Structure):
    _fields_ = [("t", ctypes.c_double), ("reg", ctypes.c_int), ("val", ctypes.c_int)]


def load(path):
    lib = ctypes.CDLL(path)
    P, I, S = ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p
    lib.blv_release_symbols.restype = S
    lib.blv_release_symbols.argtypes = [I]
    lib.blv_say_bytes_guarded.argtypes = [S, I, I, P, I, S, S, I]
    lib.blv_make_release_state.argtypes = [S, I, S, test_releases.PROGRESS, P, S, I]
    lib.blv_create.restype = P
    lib.blv_create.argtypes = [S, S, I, ctypes.c_double, I, I, S, I]
    lib.blv_set.argtypes = [P, I, I, I, I, I]
    lib.blv_speak.argtypes = [P, S]
    lib.blv_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]
    lib.blv_destroy.argtypes = [P]
    lib.blv_host.restype = P
    lib.blv_host.argtypes = [P]
    lib.bh_set_int.argtypes = [P, S, I]
    lib.bh_writes.argtypes = [P, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.bh_clear_writes.argtypes = [P]
    ctypes.c_int.in_dll(lib, "blv_symbols_break").value = 1 if BREAK else 0
    return lib


def sent(lib, text, symbols, encoding=13):
    """the text of the lines such a release is sent (the line ends and their Ctrl-Fs left out), spaces as one"""
    buf = ctypes.create_string_buffer(8192)
    n = lib.blv_say_bytes_guarded(text.encode("utf-8"), encoding, 1, None, 0, symbols, buf, 8192)
    return " ".join(buf.raw[:n].replace(b"\x06", b"").replace(b"\r", b" ").decode("latin-1").split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie"))
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    ap.add_argument("--lib", default=os.path.join(REPO, "build", "win", arch, "ssi263speech.dll") if WINDOWS
                    else os.path.join(REPO, "build", "linux", "libssi263speech.so"))
    a = ap.parse_args()
    lib = load(a.lib)
    bad = 0

    def result(ok, what, detail):
        nonlocal bad
        print("%-4s %-6s %s" % ("ok" if ok else "FAIL", what, detail))
        bad += not ok

    table, _ = gen_languages.rows()
    ids = {r["id"]: k for k, r in enumerate(table)}

    def named(rid):
        s = lib.blv_release_symbols(ids[rid])
        return None if s is None else s.decode("latin-1")
    got = {rid: named(rid) for rid in (SPANISH, ENGLISH, RUSSIAN) + PORTUGUESE}
    want = dict({SPANISH: ALL, ENGLISH: "", RUSSIAN: "@"}, **{p: ALL for p in PORTUGUESE})
    result(got == want, "table", ", ".join("%s %a" % (rid, got[rid]) for rid in sorted(got)))

    wrong = ["%a -> %a (want %a)" % (t, sent(lib, t, ALL.encode()), w) for t, w in TEXT
             if sent(lib, t, ALL.encode()) != w]
    wrong += ["another release's %a -> %a" % (t, sent(lib, t, b"")) for t, _ in TEXT
              if sent(lib, t, b"") != " ".join(t.split())]
    rng = random.Random(1)
    bits = list(ALL) + ["a", "b", "el", "5", "12", " ", " ", ",", ".", "?", "!", ":", ";", "'", "$5", "(x)"]
    n_random = 400
    for _ in range(n_random):
        t = "".join(rng.choice(bits) for _ in range(rng.randint(3, 14)))
        g = sent(lib, t, ALL.encode())
        left = [c for i, c in enumerate(g) if c in ALL and not (c == "$" and i + 1 < len(g) and g[i + 1].isdigit())]
        kept = [c for c in g if c not in ALL + " "] == [c for c in " ".join(t.split()) if c not in ALL + " "]
        if left or not kept:
            wrong.append("%a -> %a" % (t, g))
    result(not wrong, "text", "; ".join(wrong[:3]) if wrong else
           "%d known cases and %d random texts: each symbol a space, a $ before a digit kept, the rest as it was; "
           "another release's text untouched" % (len(TEXT), n_random))

    found = test_releases.find_images(a.firmware, table)
    if ids[SPANISH] not in found:
        print("skip unit   the Spanish release's image (%s) is not in %s" % (table[ids[SPANISH]]["file"], a.firmware))
        print("symbols: %s" % ("%d FAILED" % bad if bad else "PASS (the unit's cases skipped)"))
        return 1 if bad else 0
    k = ids[SPANISH]
    tmp = tempfile.mkdtemp(prefix="ssi263_symbols_")
    err = ctypes.create_string_buffer(300)
    st = os.path.join(tmp, SPANISH + ".state")
    if not lib.blv_make_release_state(found[k].encode(), k, st.encode(), test_releases.PROGRESS(0), None, err, 300):
        result(False, "unit", "the Spanish unit's state: %s" % err.value.decode(errors="replace"))
        print("symbols: %d FAILED" % bad)
        return 1
    v = lib.blv_create(found[k].encode(), st.encode(), test_releases.TEXT_ID[table[k]["text"]], float(RATE), 1, 0,
                       err, 300)
    lib.blv_set(v, 50, 50, 7, 100, 1)

    def phonemes(text):
        h = lib.blv_host(v)
        lib.bh_set_int(h, b"log_writes", 1)
        lib.bh_clear_writes(h)
        lib.blv_speak(v, text.encode("utf-8"))
        pcm, done, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), 0
        while not done.value and blocks < 2500:
            lib.blv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            blocks += 1
        p = ctypes.POINTER(Write)()
        out = []
        for i in range(lib.bh_writes(h, ctypes.byref(p))):
            if p[i].reg == 0 and (p[i].val & 0x3F) and (not out or out[-1] != p[i].val & 0x3F):
                out.append(p[i].val & 0x3F)
        return out

    phonemes("a.")
    wrong = []
    for text, plain in (("alpha (beta) gamma", "alpha beta gamma"), ("a-b", "a b"), ("50%", "50")):
        a_, b_ = phonemes(text), phonemes(plain)
        if a_ != b_:
            wrong.append("%a %d phonemes, %a %d" % (text, len(a_), plain, len(b_)))
    dollar, five = phonemes("$5"), phonemes("5")
    if len(dollar) < len(five) + 3:
        wrong.append("\"$5\" %d phonemes, \"5\" %d: the dollar not said" % (len(dollar), len(five)))
    lib.blv_destroy(v)
    result(not wrong, "unit", "; ".join(wrong) if wrong else
           "the Spanish unit: \"alpha (beta) gamma\", \"a-b\", \"50%%\" said as without their symbols; \"$5\" %d "
           "phonemes, \"5\" %d: its dollar said" % (len(dollar), len(five)))
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    print("symbols: %s" % ("%d FAILED" % bad if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
