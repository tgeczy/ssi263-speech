"""Accented letters on every voice (src/csrc/translit.h): the module, the bytes each voice sends, the audio the real
voices make, and the Braille Lite's NVDA driver.  Tomi: typing á é ő ú ű ó ü ö said nothing; "tükör" was "t k r".

    python translit_test.py module    # translit.h on its own: src/csrc/test_translit.c, built here (w64devkit's gcc;
                                      # on Linux $CC or cc)
    python translit_test.py bytes     # the five voices' text paths: the bytes each sends its unit for á, ő, tükör,
                                      # Tamás (the Spanish unit: its own á é ñ ü kept, ő falling back); no firmware
    python translit_test.py voices [--firmware DIR]
                                      # the five voices speaking, on fresh units: the audio, and ASCII text unchanged
                                      # (DIR laid out as firmware/, the default; a voice whose files are not there is
                                      # skipped, said so)
    python translit_test.py driver    # the Braille Lite's NVDA driver (Python front end, nvda/dist/blazie-build): what
                                      # its unit is sent, English and Spanish, and that it speaks
    TRANSLIT_BREAK=1                  # control: the pass off, as before it -- each mode must FAIL
    SSI263_LIB=<libssi263speech.so>   # the library (default build/win/<arch>/ssi263speech.dll)

"voices" holds each accented text's audio to its ASCII spelling's, both on a fresh unit (a unit's next utterance
depends on its state, a fresh one's does not): "tükör" must sound as "tukor" does, "á" alone as "a acute", and not
as 0.7's "t k r".  ASCII text must give the same PCM with the pass on and off (a child process with TRANSLIT_BREAK=1).
Output is ASCII only (%a), for the console and a screen reader.
"""
import ctypes
import hashlib
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
MODE = sys.argv[1] if len(sys.argv) > 1 else "voices"
BROKEN = os.environ.get("TRANSLIT_BREAK") == "1"
results = []


def report(ok, what):
    results.append(ok)
    print("%-4s %s" % ("ok" if ok else "FAIL", what))


def finish(name):
    bad = results.count(False)
    note = " (TRANSLIT_BREAK=1: the pass is off)" if BROKEN else ""
    print("translit %s: %s%s" % (name, "%d of %d FAILED" % (bad, len(results)) if bad else "all %d ok" % len(results),
                                 note))
    sys.exit(1 if bad else 0)


# ---- module: test_translit.c ---------------------------------------------------------------------------------------
def module():
    sys.path.insert(0, REPO)
    from tools import repo_paths
    src = os.path.join(REPO, "src", "csrc", "test_translit.c")
    deps = [src, os.path.join(REPO, "src", "csrc", "translit.h"), os.path.join(REPO, "src", "csrc", "blazie", "bl_cp850.h")]
    out = os.path.join(HERE, "out")
    os.makedirs(out, exist_ok=True)
    win = sys.platform == "win32"
    exe = os.path.join(out, "test_translit" + (".exe" if win else ""))
    if not os.path.isfile(exe) or os.path.getmtime(exe) < max(os.path.getmtime(p) for p in deps):
        if win:
            bindir = repo_paths.bin_dir("W64DEVKIT")
            cc, env = os.path.join(bindir, "gcc.exe"), dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
        else:
            cc, env = os.environ.get("CC", "cc"), None
        r = subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                            "-I" + os.path.join(REPO, "src", "csrc"), "-o", exe, src], env=env, capture_output=True,
                           text=True)
        if r.returncode:
            sys.exit("FAILED: building test_translit.c\n" + r.stdout + r.stderr)
    r = subprocess.run([exe], capture_output=True, text=True, errors="replace")
    sys.stdout.write(r.stdout)
    sys.exit(r.returncode)


# ---- voices: ssi263speech.dll --------------------------------------------------------------------------------------
P, I, S = ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p
VOICES = ("blazie", "blazie_es", "speakout", "mini", "sa")       # ssv indexes 0-4 (voices.h)


def load():
    lib = ctypes.CDLL(os.environ.get("SSI263_LIB") or os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll"))
    for name, res, args in (("ssv_create", P, [I, S, P, S, I]), ("ssv_speak", I, [P, P, S, I]),
                            ("ssv_available", I, [I, S]),
                            ("ssv_render", I, [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]),
                            ("ssv_destroy", None, [P]), ("blv_say_bytes", I, [S, I, I, S, I]),
                            ("sov_say_bytes", I, [S, S, I]), ("amv_say_bytes", I, [S, I, S, I]),
                            ("asv_say_bytes", I, [S, I, S, I]), ("ssv_translit", I, [S, I, I, S, I])):
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = res, args
    return lib


def say_bytes(lib, voice, text):
    """the bytes the voice sends its unit for this text (its drivers' defaults: packing, numbers on)"""
    raw, buf = text.encode("utf-8"), ctypes.create_string_buffer(4096)
    if voice in ("blazie", "blazie_es"):
        n = lib.blv_say_bytes(raw, 1 if voice == "blazie_es" else 0, 1, buf, 4096)
    elif voice == "speakout":
        n = lib.sov_say_bytes(raw, buf, 4096)
    else:
        n = getattr(lib, "amv_say_bytes" if voice == "mini" else "asv_say_bytes")(raw, 1, buf, 4096)
    return buf.raw[:n]


FIRMWARE = (sys.argv[sys.argv.index("--firmware") + 1] if "--firmware" in sys.argv
            else os.path.join(REPO, "firmware"))


def pcm(lib, voice, text):
    """the audio of text on a fresh unit (deterministic, unlike a unit's next utterance)"""
    err = ctypes.create_string_buffer(256)
    v = lib.ssv_create(VOICES.index(voice), FIRMWARE.encode(), None, err, 256)
    if not v:
        sys.exit("FAILED: %s: %s" % (voice, err.value.decode("latin-1")))
    try:
        lib.ssv_speak(v, None, text.encode("utf-8"), 0)
        buf, done, out = ctypes.POINTER(ctypes.c_short)(), I(0), bytearray()
        for _ in range(4000):
            n = lib.ssv_render(v, ctypes.byref(buf), ctypes.byref(done))
            if n > 0:
                out += ctypes.string_at(buf, 2 * n)
            if done.value:
                break
        return bytes(out)
    finally:
        lib.ssv_destroy(v)


def loud(y):
    a = struct.unpack("<%dh" % (len(y) // 2), y)
    return sum(1 for s in a if abs(s) > 300)


T = "tükör"
B = {"blazie": lambda s: s + b"\r\x06\r\x06", "speakout": lambda s: s + b"\r", "mini": lambda s: s, "sa": lambda s: s}
B["blazie_es"] = B["blazie"]
# (text, the bytes its unit is sent): the four for every English voice, and the Spanish unit's own letters kept
WANT = {v: [("á", b"a acute"), ("ő", b"o double acute"), (T, b"tukor"), ("Tamás", b"Tamas"),
            ("Geczy Tamás", b"Geczy Tamas")] for v in ("blazie", "speakout", "mini", "sa")}
WANT["blazie_es"] = [("á", b"\xa0"), ("é", b"\x82"), ("ñ", b"\xa4"), ("ü", b"\x81"),
                     ("ő", b"o double acute"), (T, b"t\x81k\x94r"), ("Tamás", b"Tam\xa0s"),
                     ("Erdős", b"Erdos")]
# (text, its ASCII spelling: the same audio on a fresh unit)
SOUNDS = {v: [("á", "a acute"), ("ő", "o double acute"), (T, "tukor"), ("Tamás", "Tamas")]
          for v in ("blazie", "speakout", "mini", "sa")}
SOUNDS["blazie_es"] = [("ő", "o double acute"), ("Erdős", "Erdos")]
ASCII_TEXTS = ["Hello there.", "Room 12, $3.50 and 1,234,567 items.", "Wait... what?! (Yes) -- \"no\"; 50% #1 & more.",
               "a", "A", "t k r", "  spaced\ttext\n", "x"]


def ascii_hashes(lib, here):
    return {v: [hashlib.sha256(pcm(lib, v, t)).hexdigest() for t in ASCII_TEXTS] for v in here}


def bytes_():
    lib = load()
    for v in VOICES:
        got = [(t, say_bytes(lib, v, t), B[v](w)) for t, w in WANT[v]]
        bad = [(t, g, w) for t, g, w in got if g != w]
        report(not bad, "%-9s bytes   %s%s" % (v, " ".join("%a" % t for t, _, _ in got),
                                               "".join("\n       %a -> %a, not %a" % b for b in bad)))
    finish("bytes")


def voices():
    lib = load()
    here = [v for i, v in enumerate(VOICES) if lib.ssv_available(i, FIRMWARE.encode())]
    if "--ascii-child" in sys.argv:                                   # the other side of the ASCII check
        print(json.dumps(ascii_hashes(lib, here)))
        return
    for v in VOICES:
        if v not in here:
            print("skip %-9s its firmware is not in %s" % (v, FIRMWARE))
    if "blazie" not in here:
        report(False, "the English Braille Lite's firmware is not in %s" % FIRMWARE)
    for v in here:
        lone = pcm(lib, v, "á")
        report(loud(lone) > 1000, "%-9s audio   %a alone: %.2f s, %d loud samples" % (
            v, "á", len(lone) / 2 / 22050.0, loud(lone)))
        for text, spelled in SOUNDS[v]:
            a, b = pcm(lib, v, text), pcm(lib, v, spelled)
            report(a == b and loud(a) > 1000, "%-9s audio   %a sounds as %a (%.2f s)%s" % (
                v, text, spelled, len(a) / 2 / 22050.0, "" if a == b else ": it does not (%.2f s)" % (
                    len(b) / 2 / 22050.0)))
        a, old = pcm(lib, v, T), pcm(lib, v, "t k r")
        report(a != old, "%-9s audio   %a is not 0.7's \"t k r\" (%.2f s against %.2f s)" % (
            v, T, len(a) / 2 / 22050.0, len(old) / 2 / 22050.0))
    on = ascii_hashes(lib, here)
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "voices", "--ascii-child", "--firmware", FIRMWARE],
                       capture_output=True, text=True, env=dict(os.environ, TRANSLIT_BREAK="1"))
    off = json.loads(r.stdout) if r.returncode == 0 else {}
    for v in here:
        same = sum(1 for a, b in zip(on[v], off.get(v, [])) if a == b)
        report(same == len(ASCII_TEXTS), "%-9s ascii   %d of %d ASCII texts give the same PCM with the pass on and off"
               % (v, same, len(ASCII_TEXTS)))
    finish("voices")


# ---- driver: the Braille Lite's NVDA driver --------------------------------------------------------------------------
def driver():
    sys.argv = [sys.argv[0], "blazie"]
    src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read().split("d = drv_mod.SynthDriver()")
    g = {"__file__": os.path.join(HERE, "fake_nvda_driver_test.py"), "__name__": "harness"}
    exec(compile(src[0], g["__file__"], "exec"), g)
    drv_mod = g["drv_mod"]
    said = []
    native_say = drv_mod.NativeBlazie.say

    def say(self, lines, *a, **k):
        said.append([lines] if isinstance(lines, str) else list(lines))
        return native_say(self, lines, *a, **k)
    drv_mod.NativeBlazie.say = say
    exec(compile("d = drv_mod.SynthDriver()" + src[1].split("time.sleep(2.0)")[0], g["__file__"], "exec"), g)
    d = g["d"]
    cases = [("blazie", "á", ["a acute"]), ("blazie", "ő", ["o double acute"]), ("blazie", T, ["tukor"]),
             ("blazie", "Tamás", ["Tamas"]), ("blazie", "Hello there.", ["Hello there."]),
             ("blazie_es", "á", ["á"]), ("blazie_es", "ñ", ["ñ"]),
             ("blazie_es", "ő", ["o double acute"]), ("blazie_es", T, [T]), ("blazie_es", "Erdős", ["Erdos"])]
    voice = None
    for v, text, want in cases:
        if v != voice:
            if v not in d._get_availableVoices():
                report(False, "%-9s the voice is not there (its firmware?)" % v)
                continue
            d._set_voice(v)
            voice = v
        del said[:]
        n0 = len(d._player.chunks)
        d.speak([text])
        g["wait_idle"](60)
        y = g["audio_since"](n0)
        got = [ln for lines in said for ln in lines]
        report(got == want and len(y) > 2000, "%-9s driver  %a: the unit was sent %a, %d samples of audio%s" % (
            v, text, got, len(y), "" if got == want else " (want %a)" % (want,)))
    d.terminate()
    finish("driver")


if __name__ == "__main__":
    {"module": module, "bytes": bytes_, "voices": voices, "driver": driver}[MODE]()
