"""Accented letters on every voice (src/csrc/translit.h): the module, the bytes each voice sends, the audio the real
voices make, and the Braille Lite's NVDA driver.  Tomi: typing á é ő ú ű ó ü ö said nothing; "tükör" was "t k r".

    python translit_test.py module    # translit.h on its own: src/csrc/test_translit.c, built here (w64devkit's gcc;
                                      # on Linux $CC or cc)
    python translit_test.py bytes     # the five voices' text paths, no firmware: handwritten byte fixtures (composed
                                      # and decomposed letters, alone and in words, the Spanish unit's own letters,
                                      # several marks, an unattached mark), and inputs the pass must leave alone
                                      # giving the same bytes with it on and off
    python translit_test.py voices [--firmware DIR] [--allow-missing]
                                      # the five voices speaking, on fresh units: the audio, and ASCII text unchanged.
                                      # DIR laid out as firmware/ (the default).  All five are required; a voice
                                      # without its files is a failure unless --allow-missing (a developer's run)
    python translit_test.py driver    # the Braille Lite's NVDA driver (Python front end, nvda/dist/blazie-build): what
                                      # its unit is sent, English and Spanish, and that it speaks
    TRANSLIT_BREAK=1                  # control: the pass off, as before it -- each mode must FAIL (this script sets
                                      # the library's ssv_translit_break; test_translit.c sets its own)
    TRANSLIT_TEST_NEVER_DONE=1        # control (voices): every unit's render made never to report done, its audio
                                      # still coming -- each utterance must FAIL as never done
    SSI263_LIB=<libssi263speech.so>   # the library (default build/win/<arch>/ssi263speech.dll); on Linux the other
                                      # voices from build/linux/libsd_voices_ref.so (SSI263_VOICES_LIB)

Three layers (Astra, Reply 162): the handwritten fixtures here and in test_translit.c own the conversion (never
derived from tl_apply or its table); the no-change inputs and the ASCII audio hold what must not change, with no
reference-side conversion; and the equivalence tests against 0.7.0's drivers (translit_ref.py: current
preprocessing, frozen downstream) hold the rest of the pipeline.

"voices" holds each accented text's audio to its ASCII spelling's, both on a fresh unit (a unit's next utterance
depends on its state, a fresh one's does not): "tükör" must sound as "tukor" does, "á" alone as "a acute", and not
as 0.7's "t k r".  Every utterance must be accepted, render without error or fault, and END (done within MAX_BLOCKS):
a partial utterance is a failure, named with its voice, text and reason.  The voices are made through each one's own
API (blv_, sov_, amv_, asv_: their faults readable), on Windows and Linux alike.  Output is ASCII only (%a).
"""
import ctypes
import hashlib
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
MODE = sys.argv[1] if len(sys.argv) > 1 else "voices"
BROKEN = os.environ.get("TRANSLIT_BREAK") == "1"
NEVER_DONE = os.environ.get("TRANSLIT_TEST_NEVER_DONE") == "1"
results = []


def set_break(lib, on=None):
    """the control: the library's own flag (translit.h's ssv_translit_break; the library reads no environment) set
    when this script runs with TRANSLIT_BREAK=1 (or as on says), cleared otherwise"""
    ctypes.c_int.in_dll(lib, "ssv_translit_break").value = 1 if (BROKEN if on is None else on) else 0


def report(ok, what):
    results.append(ok)
    print("%-4s %s" % ("ok" if ok else "FAIL", what))


def finish(name):
    bad = results.count(False)
    note = " (TRANSLIT_BREAK=1: the pass is off)" if BROKEN else ""
    note += " (TRANSLIT_TEST_NEVER_DONE=1: no unit ever done)" if NEVER_DONE else ""
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


# ---- the voices' C: ssi263speech.dll (Windows), or Linux's two libraries ------------------------------------------
P, I, S, D, Z = ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_double, ctypes.c_size_t
PCM = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(I)]
VOICES = ("blazie", "blazie_es", "speakout", "mini", "sa")
API = {"blazie": "blv", "blazie_es": "blv", "speakout": "sov", "mini": "amv", "sa": "asv"}
# each voice's files in a folder laid out as firmware/ (voices.c's table)
FILES = {"blazie": ["blazie/BL2ENG.BNS", "blazie/bl2_2003_warm.state"],
         "blazie_es": ["blazie/spanish/BL2SPA.BNS", "blazie/spanish/bl2spa_fresh.state"],
         "speakout": ["gw-micro-speakout/SPEAKOUT.HEX"], "mini": ["aicom-accent-mini/SPKEMS.DVC"],
         "sa": ["aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN"]}
RATE = 22050
MAX_BLOCKS = 1000         # 30 s of the unit's time per utterance: the longest text here, the Spanish unit reading
                          # "Room 12, $3.50 and 1,234,567 items." digit by digit, takes 13.2 s


class Lib:
    """The voices' C: on Windows ssi263speech.dll has them all; on Linux the Braille Lite is libssi263speech.so
    (SSI263_LIB) and the others build/linux/libsd_voices_ref.so (the speech-dispatcher module's engines, as
    test_sd_ssi263.py loads them; SSI263_VOICES_LIB).  Each has its own ssv_translit_break, set alike."""

    def __init__(self):
        self.libs = [ctypes.CDLL(os.environ.get("SSI263_LIB")
                                 or os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll"))]
        if not hasattr(self.libs[0], "sov_say_bytes"):
            self.libs.append(ctypes.CDLL(os.environ.get("SSI263_VOICES_LIB")
                                         or os.path.join(REPO, "build", "linux", "libsd_voices_ref.so")))
        self.set_break()

    def set_break(self, on=None):
        for lib in self.libs:
            set_break(lib, on)

    def fn(self, name, res, args):
        for lib in self.libs:
            if hasattr(lib, name):
                f = getattr(lib, name)
                f.restype, f.argtypes = res, args
                return f
        raise AttributeError("no %s in %s" % (name, ", ".join(lib._name for lib in self.libs)))


def say_bytes(lib, voice, text):
    """the bytes the voice sends its unit for this text (its drivers' defaults: packing, numbers on)"""
    raw, buf = text.encode("utf-8"), ctypes.create_string_buffer(4096)
    if voice in ("blazie", "blazie_es"):
        n = lib.fn("blv_say_bytes", I, [S, I, I, S, I])(raw, 1 if voice == "blazie_es" else 0, 1, buf, 4096)
    elif voice == "speakout":
        n = lib.fn("sov_say_bytes", I, [S, S, I])(raw, buf, 4096)
    else:
        n = lib.fn(API[voice] + "_say_bytes", I, [S, I, S, I])(raw, 1, buf, 4096)
    return buf.raw[:n]


FIRMWARE = (sys.argv[sys.argv.index("--firmware") + 1] if "--firmware" in sys.argv
            else os.path.join(REPO, "firmware"))


def available(voice):
    return all(os.path.isfile(os.path.join(FIRMWARE, *f.split("/"))) for f in FILES[voice])


def create(lib, voice, err):
    """a fresh unit through the voice's own API: (handle, its speak(utf8), its prefix)"""
    api, paths = API[voice], [os.path.join(FIRMWARE, *f.split("/")) for f in FILES[voice]]
    if api == "blv":
        v = lib.fn("blv_create", P, [S, S, I, D, I, I, S, I])(paths[0].encode(), paths[1].encode(),
                                                              int(voice == "blazie_es"), float(RATE), 1, 0, err, 256)
        speak = lib.fn("blv_speak", I, [P, S])
        return v, (lambda t: speak(v, t)), api
    if api == "asv":
        roms = [open(p, "rb").read() for p in paths]
        v = lib.fn("asv_create", P, [S, Z, S, Z, S, Z, D, S, I])(roms[0], len(roms[0]), roms[1], len(roms[1]),
                                                                 roms[2], len(roms[2]), float(RATE), err, 256)
    else:
        v = lib.fn(api + "_create", P, [S, D, S, I])(paths[0].encode(), float(RATE), err, 256)
    speak = lib.fn(api + "_speak", I, [P, S, I])
    return v, (lambda t: speak(v, t, 0)), api


def pcm(lib, voice, text):
    """(audio, None) for text on a fresh unit (deterministic, unlike a unit's next utterance), or (the audio so far,
    why it is not a complete utterance): the unit not made, the speak refused, a render error, a fault, or no done
    within MAX_BLOCKS.  Nothing to say (speak 0) is complete once render reports done."""
    err = ctypes.create_string_buffer(256)
    v, speak, api = create(lib, voice, err)
    if not v:
        return b"", "the unit was not made: %s" % err.value.decode("latin-1")
    try:
        r = speak(text.encode("utf-8"))
        if r < 0:
            return b"", "the speak was refused (%d)" % r
        render = lib.fn(api + "_render", I, PCM)
        buf, done, out = ctypes.POINTER(ctypes.c_short)(), I(0), bytearray()
        for k in range(MAX_BLOCKS):
            n = render(v, ctypes.byref(buf), ctypes.byref(done))
            if n < 0:
                return bytes(out), "render failed (%d) after %d blocks" % (n, k)
            if n:
                out += ctypes.string_at(buf, 2 * n)
            if NEVER_DONE:
                done.value = 0                                # the control: audio, and never an end
            if done.value:
                break
        else:
            return bytes(out), "never done after %d blocks (%.2f s of audio so far)" % (
                MAX_BLOCKS, len(out) / 2.0 / RATE)
        if lib.fn(api + "_fault", I, [P])(v):
            return bytes(out), "the unit faulted"
        return bytes(out), None
    finally:
        lib.fn(api + "_destroy", None, [P])(v)


def audio(lib, voice, text):
    """the complete utterance's audio, or None after a FAIL naming the voice, the text and the reason"""
    y, why = pcm(lib, voice, text)
    if why:
        report(False, "%-9s audio   %a: %s" % (voice, text, why))
        return None
    return y


def loud(y):
    a = struct.unpack("<%dh" % (len(y) // 2), y)
    return sum(1 for s in a if abs(s) > 300)


def secs(y):
    return len(y) / 2.0 / RATE


T = "tükör"
END = {"blazie": lambda s: s + b"\r\x06\r\x06", "speakout": lambda s: s + b"\r", "mini": lambda s: s,
       "sa": lambda s: s}
END["blazie_es"] = END["blazie"]
# ---- handwritten fixtures: (text, the bytes its unit is sent, before the voice's line ending), never derived from
# the pass or its table.  The English voices first (all four alike), then the Spanish unit.
EN = [("á", b"a acute"), ("á", b"a acute"), ("Á", b"a acute"), ("ő", b"o double acute"),
      ("ő", b"o double acute"), (T, b"tukor"), ("tükör", b"tukor"), ("Tamás", b"Tamas"),
      ("Tamás", b"Tamas"), ("Geczy Tamás", b"Geczy Tamas"), ("aǘto", b"auto"),
      ("q́ed", b"qed")]
WANT = {v: list(EN) for v in ("blazie", "speakout", "mini", "sa")}
# an unattached mark: left to each voice's own rules, as before (a space; the Braille Lite's lines then join words
# with one)
WANT["blazie"].append(("a ́b", b"a b"))
for v in ("speakout", "mini", "sa"):
    WANT[v].append(("a ́b", b"a  b"))
WANT["blazie_es"] = [("á", b"\xa0"), ("á", b"\xa0"), ("é", b"\x82"), ("ñ", b"\xa4"),
                     ("ñ", b"\xa4"), ("ü", b"\x81"), ("mañana", b"ma\xa4ana"),
                     ("mañana", b"ma\xa4ana"), ("camión", b"cami\xa2n"), ("Él", b"\x90l"),
                     ("ő", b"o double acute"), ("ő", b"o double acute"), (T, b"t\x81k\x94r"),
                     ("Tamás", b"Tam\xa0s"), ("Erdős", b"Erdos"), ("pǘa", b"pua"),
                     ("ǘ", b"\x81"), ("¿Qué tal, niño?", b"\xa8Qu\x82 tal, ni\xa4o?"),
                     ("a ́b", b"a b")]
# ---- no change: what the pass must leave alone gives the same bytes with it on and off (no reference conversion)
SAME = ["Hello there.", "Room 12, $3.50 and 1,234,567 items.", "Wait... what?! (Yes) -- \"no\"; 50% #1 & more.",
        "a", "A", "t k r", "  spaced\ttext\n", "~/code", "½ © ° × ÷ “q” – …",
        "50¢, €5 and £2.63", " ́", "́", "1́ x", "\U0001F389"]
SAME_ES = SAME + ["Mañana, ¿qué tal? Él está aquí. ¡Olé!", "niño", "á"]
# (text, its ASCII spelling: the same audio on a fresh unit)
SOUNDS = {v: [("á", "a acute"), ("á", "a acute"), ("ő", "o double acute"), (T, "tukor"),
              ("Tamás", "Tamas")] for v in ("blazie", "speakout", "mini", "sa")}
SOUNDS["blazie_es"] = [("ő", "o double acute"), ("Erdős", "Erdos"), ("mañana", "mañana")]
ASCII_TEXTS = ["Hello there.", "Room 12, $3.50 and 1,234,567 items.", "Wait... what?! (Yes) -- \"no\"; 50% #1 & more.",
               "a", "A", "t k r", "  spaced\ttext\n", "x"]


def bytes_():
    lib = Lib()
    for v in VOICES:
        got = [(t, say_bytes(lib, v, t), END[v](w)) for t, w in WANT[v]]
        bad = [(t, g, w) for t, g, w in got if g != w]
        report(not bad, "%-9s bytes   %d handwritten fixtures%s" % (v, len(got), "".join(
            "\n       %a -> %a, not %a" % b for b in bad)))
    for v in VOICES:
        texts = SAME_ES if v == "blazie_es" else SAME
        on = [say_bytes(lib, v, t) for t in texts]
        lib.set_break(True)
        off = [say_bytes(lib, v, t) for t in texts]
        lib.set_break()
        bad = [(t, a, b) for t, a, b in zip(texts, on, off) if a != b]
        report(not bad, "%-9s same    %d of %d inputs the pass leaves alone give the same bytes with it on and off%s"
               % (v, len(texts) - len(bad), len(texts), "".join("\n       %a: %a, off %a" % b for b in bad)))
    finish("bytes")


def voices():
    lib = Lib()
    here = []
    for v in VOICES:
        if available(v):
            here.append(v)
        elif "--allow-missing" in sys.argv:
            print("skip %-9s its firmware is not in %s (--allow-missing)" % (v, FIRMWARE))
        else:
            report(False, "%-9s missing: its firmware is not in %s (all five are required)" % (v, FIRMWARE))
    if not here:
        report(False, "no voice's firmware is in %s" % FIRMWARE)
    if NEVER_DONE:                                       # the control: every utterance must fail as never done
        for v in here:
            y = audio(lib, v, "á")
            if y is not None:
                report(True, "%-9s audio   %a alone: %.2f s, done" % (v, "á", secs(y)))
        finish("voices")
    for v in here:
        lone = audio(lib, v, "á")
        if lone is not None:
            report(loud(lone) > 1000, "%-9s audio   %a alone: %.2f s, %d loud samples" % (
                v, "á", secs(lone), loud(lone)))
        for text, spelled in SOUNDS[v]:
            a, b = audio(lib, v, text), audio(lib, v, spelled)
            if a is None or b is None:
                continue
            report(a == b and loud(a) > 1000, "%-9s audio   %a sounds as %a (%.2f s)%s" % (
                v, text, spelled, secs(a), "" if a == b else ": it does not (%.2f s)" % secs(b)))
        a, old = audio(lib, v, T), audio(lib, v, "t k r")
        if a is not None and old is not None:
            report(a != old, "%-9s audio   %a is not 0.7's \"t k r\" (%.2f s against %.2f s)" % (
                v, T, secs(a), secs(old)))
    for v in here:
        on = [audio(lib, v, t) for t in ASCII_TEXTS]
        lib.set_break(True)
        off = [audio(lib, v, t) for t in ASCII_TEXTS]
        lib.set_break()
        same = sum(1 for a, b in zip(on, off) if a is not None and a == b)
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
    set_break(ctypes.CDLL(drv_mod.DLL))         # the add-on's library: its unit and its _translit's one copy
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
        if not g["wait_idle"](60):
            report(False, "%-9s driver  %a: did not finish: %s" % (v, text, g["last_wait_error"][0]))
            continue
        y = g["audio_since"](n0)
        got = [ln for lines in said for ln in lines]
        report(got == want and len(y) > 2000, "%-9s driver  %a: the unit was sent %a, %d samples of audio%s" % (
            v, text, got, len(y), "" if got == want else " (want %a)" % (want,)))
    d.terminate()
    finish("driver")


if __name__ == "__main__":
    {"module": module, "bytes": bytes_, "voices": voices, "driver": driver}[MODE]()
