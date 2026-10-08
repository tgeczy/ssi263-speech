"""The gate for am_voice (src/csrc/accentmini/am_voice.c): the real NVDA Accent driver's Accent-mini voice, under the
stand-in NVDA, and the C voice speak the same texts with the same settings, and every PCM byte the driver feeds its
player must equal what amv_render returns.  Numbers on (the driver's default) and off, punctuation, a capital
(PitchCommand), rate, pitch, voice (the variant) and inflection at their extremes, volume above and below 100, every
sample rate the driver offers (a new card at each, as the driver reboots it), and a cancel mid-utterance (plain, and
during a capital, which says the pitch again) followed by a new utterance.

    python am_voice_equiv.py         # exit 1 on any difference
    AM_EQUIV_BREAK=cancel            # control: the C side cancels one block late -- the cancel cases must FAIL
    AM_EQUIV_BREAK=numbers           # control: the C side with number processing flipped -- the number cases must FAIL
                                     # (a control runs the 15 cases at 22050 Hz only)

The cancel is made deterministic on both sides: the driver's is called from its player's feed after the Nth block
of audio (so its worker sees it before its next block, as NVDA's would land between two), and the C side calls
amv_cancel after its Nth block.  A capital is one PitchCommand(offset) before the text, the one-offset-per-utterance
shape of amv_speak.  Needs nvda/dist/accentmini-lib/x64/accent_mini.dll (src/csrc/accentmini/build_am.py).
0.7.0's driver predates the accented-letter pass (src/csrc/translit.h): it is given each text after it
(translit_ref.py), so everything after the pass is still held byte for byte; translit_test.py holds the pass itself.
"""
import ctypes
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.argv = [sys.argv[0], "accent"]
# the reference is 0.7.0's Python driver (since 0.7.5 the add-on's driver is am_voice itself: native_driver_equiv.py)
sys.path.insert(0, HERE)
import legacy_drivers  # noqa: E402
import translit_ref  # noqa: E402
os.environ["SSI263_SYNTH_DRIVERS"] = legacy_drivers.synth_drivers("accent")
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])

ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
LIB = os.path.join(os.path.dirname(HERE), "dist", "accentmini-lib", ARCH, "accent_mini.dll")
CHIP = os.path.join(REPO, "src", "ssi263", "_bin", ARCH, "ssi263.dll")
BREAK = os.environ.get("AM_EQUIV_BREAK", "")
LONG = ("This is a longer sentence, with a comma early on, that keeps going well past where a listener would "
        "press a key to stop it, and then goes on some more.")

# (label, text, rate, pitch, inflection, volume, voice, numbers, pitch offset, sample rate, cancel after N blocks)
CASES = [
    ("hello", "Hello there.", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("numbers", "Room 12, $3.50, 1,234,567 or $6723 at 10:30.", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("punctuation", "Wait... what?! (Yes) -- \"no\"; 50% #1 & ~/code.", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("unicode", "It’s “quoted” – café… \U0001F389 £2.63.", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("capital", "A", 50, 50, 100, 100, 5, True, 30, 22050, 0),
    ("after-capital", "after the capital", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("fastest", "Fastest, highest, monotone.", 100, 100, 0, 100, 0, True, 0, 22050, 0),
    ("slowest", "Slow and low.", 0, 0, 25, 40, 9, True, 0, 22050, 0),
    ("middle", "Inflection three, voice three, 42 things.", 63, 37, 50, 100, 3, True, 0, 22050, 0),
    ("numbers-off", "Room 100, 12345 and $6723.", 50, 50, 75, 100, 7, False, 0, 22050, 0),
    ("nothing", "~~~", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("cancel", LONG, 50, 50, 100, 100, 5, True, 0, 22050, 6),
    ("after-cancel", "After the cancel.", 50, 50, 100, 100, 5, True, 0, 22050, 0),
    ("cancel-capital", "B " + LONG, 60, 40, 100, 100, 5, True, 30, 22050, 4),
    ("after-cancel-capital", "Next after the capital's cancel, 7 items.", 60, 40, 100, 100, 5, True, 0, 22050, 0),
    ("rate-11k", "Eleven kilohertz, 99 bottles.", 50, 50, 100, 100, 5, True, 0, 11025, 0),
    ("capital-11k", "Z", 70, 20, 0, 100, 2, True, 30, 11025, 0),
    ("cancel-11k", LONG, 70, 20, 0, 100, 2, True, 0, 11025, 3),
    ("after-11k", "And again at eleven.", 70, 20, 0, 100, 2, True, 0, 11025, 0),
    ("rate-44k", "Forty-four, $1,000,000.", 50, 50, 100, 100, 5, True, 0, 44100, 0),
    ("extremes-44k", "Every extreme.", 0, 100, 0, 0, 9, False, -30, 44100, 0),
    ("volume-44k", "Quieter.", 0, 100, 0, 35, 9, False, 0, 44100, 0),
    ("cancel-44k", LONG, 100, 0, 100, 100, 0, True, 0, 44100, 8),
    ("after-44k", "The last one.", 100, 0, 100, 100, 0, True, 0, 44100, 0),
]


def driver_side(cases):
    """Every case through the real driver; the PCM each one fed its player (a player made at a rate change too)."""
    out = []
    raw = []
    state = {"blocks": 0, "cancel_at": 0, "next": None}
    orig = FakePlayer.feed

    def feed(self, data, onDone=None):
        r = orig(self, data, onDone)
        if data:
            raw.append(bytes(data))
            state["blocks"] += 1
            if state["cancel_at"] and state["blocks"] == state["cancel_at"]:
                state["cancel_at"] = 0
                d.cancel()                              # on the worker, between this block and its next
                if state["next"] is not None:
                    d.speak(state["next"])
        return r
    FakePlayer.feed = feed
    try:
        k = 0
        while k < len(cases):
            label, text, rate, pitch, infl, vol, voice, numbers, off, sr, cancel_n = cases[k]
            d._set_rate(rate)
            d._set_pitch(pitch)
            d._set_inflection(infl)
            d._set_volume(vol)
            d._set_variant(str(voice))
            d._set_numberWords(numbers)
            d._set_sampleRate(str(sr))
            # 0.7.0's driver predates the accented-letter pass (translit.h): it is given the pass's output
            seq = ([PitchCommand(off)] if off else []) + [translit_ref.translit(text)]
            del raw[:]
            state["blocks"] = 0
            if cancel_n:
                # the next case is spoken from the feed, right after the cancel: its settings must already be these
                nxt = cases[k + 1]
                assert nxt[2:7] == cases[k][2:7] and nxt[9] == sr and nxt[10] == 0, "a cancel's next case: same settings"
                state["cancel_at"] = cancel_n
                state["next"] = ([PitchCommand(nxt[8])] if nxt[8] else []) + [translit_ref.translit(nxt[1])]
                n_owned = len(owned)
                d.speak(seq)
                t = time.monotonic()
                while len(owned) < n_owned + 2:         # the cancelled utterance's token, then the next one's
                    if time.monotonic() - t > 120 or worker_dead():
                        sys.exit("the driver never reached block %d of %s to cancel it" % (cancel_n, label))
                    time.sleep(0.005)
                if not wait_idle(120):
                    sys.exit("the driver did not finish %s: %s" % (nxt[0], last_wait_error[0]))
                cut = cancel_n
                out.append(b"".join(raw[:cut]))
                out.append(b"".join(raw[cut:]))
                k += 2
                continue
            d.speak(seq)
            if not wait_idle(120):
                sys.exit("the driver did not finish %s: %s" % (label, last_wait_error[0]))
            out.append(b"".join(raw))
            k += 1
    finally:
        FakePlayer.feed = orig
    return out


def c_side(cases, dvc):
    ctypes.CDLL(CHIP)                                   # accent_mini.dll imports it: load that copy first
    lib = ctypes.CDLL(LIB)
    lib.amv_create.restype = ctypes.c_void_p
    lib.amv_create.argtypes = [ctypes.c_char_p, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.amv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 6
    lib.amv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.amv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.amv_cancel.argtypes = [ctypes.c_void_p]
    lib.amv_fault.argtypes = [ctypes.c_void_p]
    lib.amv_destroy.argtypes = [ctypes.c_void_p]
    err = ctypes.create_string_buffer(256)
    out = []
    v, v_rate = None, None
    for label, text, rate, pitch, infl, vol, voice, numbers, off, sr, cancel_n in cases:
        if sr != v_rate:                                # the driver's _switch_rate: a new card at the new rate
            if v:
                lib.amv_destroy(v)
            v = lib.amv_create(dvc.encode("mbcs"), float(sr), err, 256)
            if not v:
                sys.exit("amv_create: %s" % err.value.decode())
            v_rate = sr
        lib.amv_set(v, rate, pitch, infl, vol, int(numbers) ^ (BREAK == "numbers"), voice)
        if lib.amv_speak(v, text.encode("utf-8"), off) < 0:
            sys.exit("amv_speak faulted on %s" % label)
        pcm, done, chunks, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], 0
        stop_at = cancel_n + (1 if BREAK == "cancel" else 0) if cancel_n else 0
        while not done.value:
            n = lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            if n:
                chunks.append(ctypes.string_at(pcm, 2 * n))
                blocks += 1
                if stop_at and blocks == stop_at:
                    lib.amv_cancel(v)
                    break
        if lib.amv_fault(v):
            sys.exit("am_voice faulted on %s" % label)
        out.append(b"".join(chunks))
    lib.amv_destroy(v)
    return out, lib


PIECES = ["a", "Z", "e", " ", "  ", ".", ",", "$", "$1", "0", "7", "12", "100", "1,234", "12345", ".5", "$.75",
           "£2.63", "€5", "¥100", "~", "\t", "\n", "\x1b", "\x18", "\x7f", "’", "“", "”", "–", "—", "…", "é",
           "ñ", "ç", "ü", "ő", "ß", "Á", "tükör "," ", "\U0001F389", "-", "%", "#", "@", "&", "'s", "Mr.", "3rd", "10:30", "555-1234"]


def text_check(lib, n, seed):
    """The text amv_speak sends (amv_say_bytes) = the driver's (_clean(currencies(text)).strip(), then _numbers),
    on random texts, numbers on and off.  Returns the number of differences."""
    import random
    rng = random.Random(seed)
    lib.amv_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 16)
    bad = 0
    for i in range(n):
        text = "".join(rng.choice(PIECES) for _ in range(rng.randint(0, 24)))
        numbers = i % 2 == 0
        # 0.7.0's driver predates the accented-letter pass (translit.h): it is given the pass's output
        want = drv_mod._clean(drv_mod.numwords.currencies(translit_ref.translit(text))).strip()
        if numbers:
            want = drv_mod._numbers(want)
        k = lib.amv_say_bytes(text.encode("utf-8"), int(numbers) ^ (BREAK == "numbers"), buf, len(buf))
        got = buf.value.decode("latin-1") if 0 <= k < len(buf) else None
        if got != want:
            bad += 1
            if bad <= 3:
                print("TEXT DIFF numbers %d: %a -> driver %a, C %a" % (numbers, text, want, got))
    print("%s %d of %d random texts give the driver's text" % ("same " if not bad else "DIFF ", n - bad, n))
    return bad


def labels(cases):
    """The compared parts, in order: a cancel case is its cut audio, then the next case's."""
    return [c[0] for c in cases]


def compare(names, a, b):
    bad = 0
    for name, x, y in zip(names, a, b):
        if x == y:
            print("same  %-22s %7d samples" % (name, len(x) // 2))
            continue
        bad += 1
        k = next((i for i in range(0, min(len(x), len(y)), 2) if x[i:i + 2] != y[i:i + 2]), min(len(x), len(y)))
        print("DIFF  %-22s driver %d / C %d samples, first difference at sample %d" % (name, len(x) // 2, len(y) // 2,
                                                                                       k // 2))
    return bad


def stale():
    """The DLL older than a source it is made of: rebuild it (src/csrc/accentmini/build_am.py) before trusting it."""
    sys.path.insert(0, os.path.join(REPO, "src", "csrc", "accentmini"))
    import build_am
    t = os.path.getmtime(LIB)
    return [os.path.relpath(p, REPO) for p in [s for s, _, _ in build_am.SOURCES] + build_am.HEADERS
            if os.path.getmtime(p) > t]


def main():
    if not os.path.isfile(LIB):
        sys.exit("%s not built: python src/csrc/accentmini/build_am.py" % os.path.relpath(LIB, REPO))
    old = stale()
    if old:
        sys.exit("accent_mini.dll is older than %s: python src/csrc/accentmini/build_am.py" % ", ".join(old))
    t0 = time.monotonic()
    while d._box is None:
        time.sleep(0.05)
    if d._model != "mini":
        sys.exit("the driver's first voice is %r, not the Accent-mini" % d._model)
    # a control runs the 22050 Hz cases only (both breaks show there; the suite's time is short)
    cases = [c for c in CASES if c[9] == 22050] if BREAK else CASES
    a = driver_side(cases)
    t1 = time.monotonic()
    b, lib = c_side(cases, drv_mod.DRIVER)
    t2 = time.monotonic()
    d.terminate()
    bad = compare(labels(cases), a, b)
    total = len(cases)
    print("driver %.1f s, C %.1f s" % (t1 - t0, t2 - t1))
    text_bad = text_check(lib, 4000, 1)
    print("%d of %d utterances byte-identical to the NVDA driver" % (total - bad, total))
    if BREAK:
        print("(control %s: the C side was broken on purpose)" % BREAK)
    return 1 if bad or text_bad else 0


sys.exit(main())
