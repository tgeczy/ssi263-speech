"""The comma guard (bl_voice.c detach_commas, blazie.py _commas; bl_languages.txt's commas column): a release whose
own reader repeats digits around commas -- the Spanish Braille Lite: with two commas touching a digit in a sentence
it says digits again, and more each time it meets such a line -- is sent no comma that touches a digit.  Through the
unified library (ssi263speech.dll, or libssi263speech.so):

  table   the release table says repeat for the Spanish release, ok for the English one
  text    the bytes such a release is sent (blv_say_bytes_release): a comma between two digits is a space, one
          beside a digit has a space on that side, every other comma and character as it was -- on known cases and
          on random texts; another release's text is left as it is
  unit    the Spanish unit itself (its image looked for in the firmware folder and below; skipped, and said so,
          without it): each text that set the fault off, three times running on a fresh unit, the number words off
          and -- for the texts they leave digits in -- on: the same length every time, none silent
  same    where the unit read soundly, the guard changes nothing it says: "3,5" has the phonemes it has without
          the guard

    python test_commas.py [--firmware DIR] [--lib PATH]

Control (SSI263_COMMAS_BREAK=1, bl_voice.c blv_commas_break): the commas left as they are -- "text" fails, and the
unit grows: "unit" fails.
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

BREAK = os.environ.get("SSI263_COMMAS_BREAK") == "1"
WINDOWS = sys.platform == "win32"
RATE = 22050
SPANISH, ENGLISH = "bl2_es_2000_09", "bl2_en_2003_06"
REPEAT = 2                                          # bl_firmware.h BLV_COMMAS_REPEAT
TEXT = [("1, 2, 3", "1 , 2 , 3"), ("7,8,9", "7 8 9"), ("3,5 y 4,5", "3 5 y 4 5"), ("1 ,2 ,3", "1 , 2 , 3"),
        ("el 7, y el 8, y", "el 7 , y el 8 , y"), ("MP3, MP4, MP5", "MP3 , MP4 , MP5"), ("1,234,567", "1 234 567"),
        ("uno, dos, tres", "uno, dos, tres"), ("1 , 2 , 3", "1 , 2 , 3"), ("7), 8)", "7), 8)"), ("a,b", "a,b"),
        ("5,", "5 ,"), (",5", ", 5")]
OFF = ["1, 2, 3, 4, 5, 6", "12, 34, 56", "7,8,9", "3,5 y 4,5"]      # the number words off: the unit reads every digit
ON = ["MP3, MP4, MP5", "el n.7, el n.8, y"]                          # ... on: the digits they leave


class Write(ctypes.Structure):
    _fields_ = [("t", ctypes.c_double), ("reg", ctypes.c_int), ("val", ctypes.c_int)]


def load(path):
    lib = ctypes.CDLL(path)
    P, I, S = ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p
    lib.blv_release_commas.argtypes = [I]
    lib.blv_release_id.restype = S
    lib.blv_release_id.argtypes = [I]
    lib.blv_say_bytes_release.argtypes = [S, I, I, P, I, S, I]
    lib.blv_make_release_state.argtypes = [S, I, S, test_releases.PROGRESS, P, S, I]
    lib.blv_create.restype = P
    lib.blv_create.argtypes = [S, S, I, ctypes.c_double, I, I, S, I]
    lib.blv_set.argtypes = [P, I, I, I, I, I]
    lib.blv_set_numbers.argtypes = [P, P]
    lib.blv_speak.argtypes = [P, S]
    lib.blv_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]
    lib.blv_destroy.argtypes = [P]
    lib.blv_host.restype = P
    lib.blv_host.argtypes = [P]
    lib.bh_set_int.argtypes = [P, S, I]
    lib.bh_writes.argtypes = [P, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.bh_clear_writes.argtypes = [P]
    ctypes.c_int.in_dll(lib, "blv_commas_break").value = 1 if BREAK else 0
    return lib


def sent(lib, text, commas, encoding=13):
    """the text of the lines such a release is sent (the line ends and their Ctrl-Fs left out)"""
    buf = ctypes.create_string_buffer(8192)
    n = lib.blv_say_bytes_release(text.encode("utf-8"), encoding, 1, None, commas, buf, 8192)
    return buf.raw[:n].replace(b"\x06", b"").replace(b"\r", b" ").decode("latin-1").strip()


def touching(s):
    return any(c == "," and ((i and s[i - 1].isdigit()) or (i + 1 < len(s) and s[i + 1].isdigit()))
               for i, c in enumerate(s))


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
    got = {rid: lib.blv_release_commas(ids[rid]) for rid in (SPANISH, ENGLISH)}
    result(got[SPANISH] == REPEAT and got[ENGLISH] == 1, "table", "%s %s, %s %s" % (
        SPANISH, {2: "repeat", 1: "ok", 0: "not measured"}.get(got[SPANISH], got[SPANISH]),
        ENGLISH, {2: "repeat", 1: "ok", 0: "not measured"}.get(got[ENGLISH], got[ENGLISH])))

    wrong = ["%a -> %a (want %a)" % (t, sent(lib, t, 1), w) for t, w in TEXT if sent(lib, t, 1) != w]
    wrong += ["another release's %a -> %a" % (t, sent(lib, t, 0)) for t, _ in TEXT if sent(lib, t, 0) != " ".join(t.split())]
    rng = random.Random(1)
    bits = ["1", "2", "7", "12", "345", "0", ",", ",", ",", ", ", ", ", " ,", " ", " ", ".", ";", ":", ")", "(", "%", "-",
            "a", "el", "y", "x", "son", "MP", "n.", "de"]
    n_random = 400
    for _ in range(n_random):
        t = "".join(rng.choice(bits) for _ in range(rng.randint(3, 14)))
        g, plain = sent(lib, t, 1), sent(lib, t, 0)
        # nothing but commas and spaces moved: every other character in its order, and no comma lost unless it
        # stood between two digits
        kept = [c for c in g if c not in ", "] == [c for c in plain if c not in ", "]
        lost = plain.count(",") - g.count(",")
        between = sum(1 for i, c in enumerate(plain) if c == "," and 0 < i < len(plain) - 1
                      and plain[i - 1].isdigit() and plain[i + 1].isdigit())
        if touching(g) or not kept or lost != between:
            wrong.append("%a -> %a" % (t, g))
    result(not wrong, "text", "; ".join(wrong[:3]) if wrong else
           "%d known cases and %d random texts: no comma touches a digit, one between two digits a space, the rest "
           "as it was; another release's text untouched" % (len(TEXT), n_random))

    found = test_releases.find_images(a.firmware, table)
    if ids[SPANISH] not in found:
        print("skip unit   the Spanish release's image (%s) is not in %s" % (table[ids[SPANISH]]["file"], a.firmware))
        print("commas: %s" % ("%d FAILED" % bad if bad else "PASS (the unit's cases skipped)"))
        return 1 if bad else 0
    k = ids[SPANISH]
    tmp = tempfile.mkdtemp(prefix="ssi263_commas_")
    err = ctypes.create_string_buffer(300)
    st = os.path.join(tmp, SPANISH + ".state")
    if not lib.blv_make_release_state(found[k].encode(), k, st.encode(), test_releases.PROGRESS(0), None, err, 300):
        result(False, "unit", "the Spanish unit's state: %s" % err.value.decode(errors="replace"))
        print("commas: %d FAILED" % bad)
        return 1

    def unit(numbers):
        v = lib.blv_create(found[k].encode(), st.encode(), test_releases.TEXT_ID[table[k]["text"]], float(RATE), 1, 0,
                           err, 300)
        lib.blv_set_numbers(v, ctypes.cast(lib.bl_numbers, ctypes.c_void_p) if numbers else None)
        lib.blv_set(v, 50, 50, 7, 100, 1)
        say(v, "a.")
        return v

    def say(v, text):
        """the utterance's seconds"""
        lib.blv_speak(v, text.encode("utf-8"))
        pcm, done, n, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), 0, 0
        while not done.value and blocks < 2500:
            n += max(0, lib.blv_render(v, ctypes.byref(pcm), ctypes.byref(done)))
            blocks += 1
        return n / float(RATE)

    wrong, said = [], 0
    for numbers, texts in ((0, OFF), (1, ON)):
        for text in texts:
            v = unit(numbers)
            secs = [say(v, text)]
            while len(secs) < 3 and abs(secs[-1] - secs[0]) <= 0.1:      # (a text that grows: no need to hear it out)
                secs.append(say(v, text))
            lib.blv_destroy(v)
            said += 1
            if max(secs) - min(secs) > 0.1 or min(secs) < 0.2:
                wrong.append("%a with the number words %s: %s s running" % (
                    text, "on" if numbers else "off", ", ".join("%.2f" % s for s in secs)))
    result(not wrong, "unit", "; ".join(wrong[:3]) if wrong else
           "%d texts with two commas at digits and more, three times running on a fresh Spanish unit (the number words "
           "off, and on): the same length every time" % said)

    def phonemes(v, text):
        h = lib.blv_host(v)
        lib.bh_set_int(h, b"log_writes", 1)
        lib.bh_clear_writes(h)
        say(v, text)
        p = ctypes.POINTER(Write)()
        out = []
        for i in range(lib.bh_writes(h, ctypes.byref(p))):
            if p[i].reg == 0 and (p[i].val & 0x3F) and (not out or out[-1] != p[i].val & 0x3F):
                out.append(p[i].val & 0x3F)
        return out

    brk = ctypes.c_int.in_dll(lib, "blv_commas_break")
    v = unit(0)
    guarded = phonemes(v, "3,5")
    lib.blv_destroy(v)
    brk.value = 1
    v = unit(0)
    own = phonemes(v, "3,5")
    lib.blv_destroy(v)
    brk.value = 1 if BREAK else 0
    result(guarded == own and len(own) > 4, "same", "one decimal alone, \"3,5\": %d phonemes with the guard, %d as the "
           "unit reads it by itself, %s" % (len(guarded), len(own), "the same" if guarded == own else "NOT the same"))
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    print("commas: %s" % ("%d FAILED" % bad if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
