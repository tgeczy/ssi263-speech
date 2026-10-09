"""The gate for so_voice (src/csrc/speakout/so_voice.c): the real NVDA Speak-Out driver, under the stand-in NVDA, and
the C voice speak the same texts with the same settings, and every PCM byte the driver feeds its player must equal
what sov_render returns.  On the path: the settings commands (rate, pitch, tone, join, short pauses), currencies and
the text clean-up, numbers and punctuation left to the firmware, a capital (PitchCommand: snap_pitch, the pitch said
again after it), the lead trim, volume, every sample rate (the box rebooted at each), and a cancel mid-utterance
(after a fixed number of 30 ms blocks on both sides) followed by a new utterance, plain and after a capital.

    python so_voice_equiv.py         # exit 1 on any difference
    SO_VOICE_EQUIV_BREAK=currencies  # control: the driver's currencies() skipped -- the currency case must DIFFER
    SO_VOICE_EQUIV_BREAK=timing      # control: the C host's cpu_ips 1 % fast -- the first case must DIFFER

The driver is 0.7.0's, the Python host's last (legacy_drivers.py), on its default core (mame-steps); the C side is
nvda/dist/speakout-lib/<arch>/so_voice.dll (src/csrc/speakout/build_board.py).  0.7.0's driver predates the
accented-letter pass (src/csrc/translit.h): it is given each text after it (translit_ref.py) -- current-preprocessing
/ frozen-downstream equivalence, not an oracle for the pass (translit_test.py's handwritten fixtures are).
"""
import ctypes
import os
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if os.environ.get("SSI263_SPEAKOUT_CORE", "mame-steps") not in ("", "mame-steps"):
    sys.exit("so_voice_equiv: SSI263_SPEAKOUT_CORE=%s; the C voice is the mame-steps host"
             % os.environ["SSI263_SPEAKOUT_CORE"])
sys.argv = [sys.argv[0], "speakout"]
# the reference is 0.7.0's Python driver (since 0.7.5 the add-on's driver is so_voice itself: native_driver_equiv.py)
sys.path.insert(0, HERE)
import legacy_drivers  # noqa: E402
import translit_ref  # noqa: E402
os.environ["SSI263_SYNTH_DRIVERS"] = legacy_drivers.synth_drivers("speakout")
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])

ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
LIB = os.path.join(os.path.dirname(HERE), "dist", "speakout-lib", ARCH, "so_voice.dll")
CHIP = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "ssi263", "_bin", ARCH, "ssi263.dll")
BREAK = os.environ.get("SO_VOICE_EQUIV_BREAK", "")
TONES = "abcdefghijklmnopqrstuvwxyz"
BLOCK_S = 0.03

DEF = dict(rate=50, pitch=50, tone=8, volume=100, join=True, short=True, sr=22050)
LONG = ("This sentence is going to be interrupted very soon by a cancel from NVDA, and it keeps going on and on "
        "for quite a while.")


def case(label, text, offset=0, cut=None, after=None, **kw):
    """One utterance: text with a PitchCommand(offset) before it (a capital) when offset; cut = cancel after that many
    30 ms blocks, then `after` spoken."""
    s = dict(DEF, **kw)
    return dict(label=label, text=text, offset=offset, cut=cut, after=after, s=s)


CASES = [
    case("plain", "Hello there."),
    case("numbers+currency", "Room 12, $3.50, £2.63 and 1,234,567 items on 3/4/1995."),
    case("punctuation", "Wait... what?! (Yes) — “quoted” text – it’s café, naïve; a/b: 50% off #1 & more…"),
    case("capital", "A", offset=30),
    case("after capital", "a"),
    case("rate 0 pitch 0", "Slow and low, tone A, quieter.", rate=0, pitch=0, tone=0, volume=40),
    case("rate 100 pitch 100", "Fast and high, at the top tone.", rate=100, pitch=100, tone=25),
    case("join, short off", "One. Two, three! Four? Five.", join=False, short=False),
    case("11025 Hz", "Eleven kilohertz now, 7 items.", sr=11025),
    case("44100 Hz", "Forty four kilohertz now, 12 items.", sr=44100),
    case("cancel", LONG, cut=12, after="After."),
    case("capital cancel", "Alpha bravo charlie delta echo foxtrot golf hotel.", offset=30, cut=10,
         after="After the capital."),
    case("22050 Hz again", "Back at twenty two.", sr=22050),
    # a capital leaves its pitch restore unconfirmed (_pitch_dirty) until an utterance ends; one cut before then has
    # the pitch said again with a snap (_resend_pitch), then a pitch change from the settings.  (The driver's clearing
    # of that snap at the next first phoneme, _snap_until_speech, is ported but not seen here: the firmware's own
    # intonation changes the pitch at once, and the chip uses the snap up on that change.)
    case("capital again", "B", offset=30),
    case("cancel after a capital", LONG, cut=12, after="After."),
    case("pitch change after cancel", "Higher now, after the cancel.", pitch=70),
    case("cancel at 11025", LONG, cut=20, after="Done.", sr=11025),
]

# ---- the driver ------------------------------------------------------------------------------------------------
feeds = []                     # every non-empty block the driver fed, in order (every player: a rate makes a new one)
_orig_feed = FakePlayer.feed


def _feed(self, data, onDone=None):
    if data:
        feeds.append(bytes(data))
    return _orig_feed(self, data, onDone)


FakePlayer.feed = _feed
if BREAK == "currencies":
    print("CONTROL: the driver's currencies() is skipped")
    drv_mod.numwords.currencies = lambda text, lang="en": text

cut_at = {"n": None, "runs": 0, "fed": None}
cut_done = threading.Event()


def arm_run(box_cls):
    """box.run, counted: after the n-th 30 ms block of the armed utterance, NVDA's cancel (on the worker, at once)."""
    orig = box_cls.run

    def run(self, seconds, *a, **k):
        y = orig(self, seconds, *a, **k)
        if cut_at["n"] and seconds == BLOCK_S:
            cut_at["runs"] += 1
            if cut_at["runs"] == cut_at["n"]:
                cut_at["n"] = None
                cut_at["fed"] = len(feeds)
                d.cancel()
                cut_done.set()
        return y
    box_cls.run = run


def driver_side():
    out = []
    while d._box is None:
        time.sleep(0.05)
    arm_run(type(d._box))
    for c in CASES:
        s = c["s"]
        d._rate, d._pitch, d._tone, d._volume = s["rate"], s["pitch"], TONES[s["tone"]], s["volume"]
        d._join, d._short = s["join"], s["short"]
        d._set_sampleRate(str(s["sr"]))
        # 0.7.0's driver predates the accented-letter pass (translit.h): it is given the pass's output
        seq = ([PitchCommand(c["offset"])] if c["offset"] else []) + [translit_ref.translit(c["text"])]
        if c["offset"]:
            seq.append(PitchCommand())                   # NVDA's reset after a capital
        n0 = len(feeds)
        if c["cut"]:
            cut_at.update(n=c["cut"], runs=0, fed=None)
            cut_done.clear()
            d.speak(seq)
            if not cut_done.wait(60):
                sys.exit("the driver never reached block %d of %r" % (c["cut"], c["label"]))
            n1 = cut_at["fed"]
            d.speak([translit_ref.translit(c["after"])])
            if not wait_idle(60):
                sys.exit("the driver did not finish %r: %s" % (c["after"], last_wait_error[0]))
            out.append([b"".join(feeds[n0:n1]), b"".join(feeds[n1:])])
        else:
            d.speak(seq)
            if not wait_idle(60):
                sys.exit("the driver did not finish %r: %s" % (c["label"], last_wait_error[0]))
            out.append([b"".join(feeds[n0:])])
    return out


# ---- the C voice -----------------------------------------------------------------------------------------------
def c_side():
    ctypes.CDLL(CHIP)                                    # so_voice.dll imports it: load that copy first
    lib = ctypes.CDLL(LIB)
    P = ctypes.c_void_p
    lib.sov_create.restype = P
    lib.sov_create.argtypes = [ctypes.c_char_p, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.sov_set.argtypes = [P] + [ctypes.c_int] * 6
    lib.sov_set_sample_rate.argtypes = [P, ctypes.c_double]
    lib.sov_set_sample_rate.restype = ctypes.c_int
    lib.sov_speak.argtypes = [P, ctypes.c_char_p, ctypes.c_int]
    lib.sov_speak.restype = ctypes.c_int
    lib.sov_render.argtypes = [P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(ctypes.c_int)]
    lib.sov_cancel.argtypes = [P]
    lib.sov_destroy.argtypes = [P]
    lib.sov_host.restype = P
    lib.sov_host.argtypes = [P]
    lib.soh_set_double.argtypes = [P, ctypes.c_char_p, ctypes.c_double]
    err = ctypes.create_string_buffer(256)
    v = lib.sov_create(drv_mod.FIRMWARE.encode("mbcs"), float(DEF["sr"]), err, 256)
    if not v:
        sys.exit("sov_create: %s" % err.value.decode())

    def perturb():
        if BREAK == "timing":
            lib.soh_set_double(lib.sov_host(v), b"cpu_ips", 1515000.0)
    if BREAK == "timing":
        print("CONTROL: the C host's cpu_ips is 1 % fast")
    perturb()
    pcm, done = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0)

    def render(limit=None):
        """blocks until done (or `limit` blocks: the last one's audio dropped, as the driver never feeds it)"""
        chunks, k = [], 0
        done.value = 0
        while not done.value:
            n = lib.sov_render(v, ctypes.byref(pcm), ctypes.byref(done))
            k += 1
            if limit and k == limit:
                return b"".join(chunks), bool(done.value)
            if n:
                chunks.append(ctypes.string_at(pcm, 2 * n))
        return b"".join(chunks), True

    out = []
    for c in CASES:
        s = c["s"]
        lib.sov_set(v, s["rate"], s["pitch"], s["tone"], s["volume"], int(s["join"]), int(s["short"]))
        if lib.sov_set_sample_rate(v, float(s["sr"])) < 0:
            sys.exit("sov_set_sample_rate %d failed" % s["sr"])
        perturb()
        lib.sov_speak(v, c["text"].encode("utf-8"), c["offset"])
        if c["cut"]:
            a, ended = render(c["cut"])
            if ended:
                sys.exit("%r ended before block %d: not a cut mid-utterance" % (c["label"], c["cut"]))
            lib.sov_cancel(v)
            lib.sov_speak(v, c["after"].encode("utf-8"), 0)
            out.append([a, render()[0]])
        else:
            out.append([render()[0]])
    lib.sov_destroy(v)
    return out


def compare(a, b):
    bad = total = 0
    for c, xs, ys in zip(CASES, a, b):
        for part, x, y in zip(("", " (cut)", " (after)")[(1 if c["cut"] else 0):], xs, ys):
            total += 1
            label = c["label"] + (part if c["cut"] else "")
            if x == y and x:
                print("same  %-26s %6d samples at %d Hz" % (label, len(x) // 2, c["s"]["sr"]))
                continue
            bad += 1
            if not x and not y:
                print("DIFF  %-26s no audio on either side" % label)
                continue
            k = next((i for i in range(0, min(len(x), len(y)), 2) if x[i:i + 2] != y[i:i + 2]), min(len(x), len(y)))
            print("DIFF  %-26s driver %d / C %d samples, first difference at sample %d  %r"
                  % (label, len(x) // 2, len(y) // 2, k // 2, (c["after"] if part == " (after)" else c["text"])[:30]))
    return bad, total


t0 = time.perf_counter()
bad, total = compare(driver_side(), c_side())
d.terminate()
print("%d of %d utterances byte-identical to the NVDA driver (%.0f s)" % (total - bad, total, time.perf_counter() - t0))
sys.exit(1 if bad else 0)
