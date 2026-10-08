"""The native Speak-Out and Accent drivers (0.7.5: ssi263speech.dll, no Python host) against 0.7.0's Python drivers,
byte for byte: both, under the stand-in NVDA (fake_nvda_driver_test.py), speak the same NVDA sequences with the same
settings, and every PCM byte each feeds its player, and the order of its index and done notifications, must be equal.

The sequences are NVDA's, not single texts: several texts with IndexCommands between them (the second text's head is
not trimmed), pieces joined and not, a capital (PitchCommand(30) ... PitchCommand()) alone and inside a sequence,
a capital whose pitch runs over an index into the next text, numbers, money and punctuation, the settings' extremes,
every sample rate, both Accent voices with their variants, inflection and number processing, and cancels: after a
fixed number of 30 ms blocks (the same block on both sides) inside a sequence, and at an item boundary (at an index),
each followed by the next utterance.

    python native_driver_equiv.py [speakout|mini|sa ...] [--arch-check]      # exit 1 on any difference
    NATIVE_EQUIV_BREAK=per-text   control: the native driver re-begins its job before every text (the voices'
                                  one-text sov_speak) -- the multi-text cases must DIFFER, the one-text cases not
    NATIVE_EQUIV_BREAK=cancel     control: the native side's cancels one block late -- the block cuts must DIFFER

The native side is nvda/dist/<addon>-build (or SSI263_NATIVE_SYNTH_DRIVERS_<SPEAKOUT|ACCENT>); the reference is
legacy_drivers.py's.  Each side runs in its own process (both drivers are synthDrivers.<name>).  0.7.0's drivers
predate the accented-letter pass (src/csrc/translit.h): the reference is given each text after it (translit_ref.py);
translit_test.py holds the pass itself.
"""
import os
import pickle
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BREAK = os.environ.get("NATIVE_EQUIV_BREAK", "")
BLOCK_S = 0.03
LONG = ("This sentence is going to be interrupted very soon by a cancel from NVDA, and it keeps going on and on "
        "for quite a while.")
ADDON = {"speakout": "speakout", "mini": "accent", "sa": "accent"}


def T(s):
    return ("t", s)


def I(n):
    return ("i", n)


def P(o=0):
    return ("p", o)


def case(label, seq, cut=None, cut_index=None, after=None, **settings):
    """seq: items; cut = cancel after that many 30 ms blocks, cut_index = cancel as that index is reached; then
    `after` spoken (a sequence)"""
    return dict(label=label, seq=seq, cut=cut, cut_index=cut_index, after=after, settings=settings)


COMMON = [
    case("plain", [T("Hello there.")]),
    case("indexes", [I(1), T("Hello there."), I(2), T("This is a test of the driver."), I(3)]),
    case("pieces joined", [T("Select synthesizer"), T("dialog")], join=True),
    case("pieces apart", [T("Select synthesizer"), T("dialog")], join=False),
    case("capital", [P(30), T("A"), P()]),
    case("after capital", [T("a")]),
    case("capital in a sequence", [T("cap"), P(30), T("B"), P(), I(5), T("after the capital.")]),
    case("capital over an index", [P(30), T("C"), I(6), T("D")]),
    case("numbers", [T("Room 12, 3/4/1995, 1,234,567 items and 100 more; 45000 or 2.5.")]),
    case("money", [T("It costs $3.50, £2.63, €2.50 and 50¢; $6723 too.")]),
    case("punctuation", [T("Wait... what?! (Yes) — “quoted” text – it’s café, naïve; "
                           "a/b: 50% off #1 & more… ~/code")]),
    case("empty items", [T("  "), I(7), T(""), T("Then words.")], join=False),
    case("rate 0 pitch 0", [T("Slow and low, quieter.")], rate=0, pitch=0, volume=40),
    case("rate 100 pitch 100", [T("Fast and high.")], rate=100, pitch=100),
    case("rate 100 capital", [P(30), T("Q"), P()], rate=100, pitch=100),
    case("11025 Hz", [T("Eleven kilohertz now, 7 items.")], sr=11025),
    case("44100 Hz", [T("Forty four kilohertz now, 12 items."), I(8), T("Second sentence.")], sr=44100),
    case("cancel in a sequence", [I(1), T("Short one."), I(2), T(LONG), I(3), T("Never said.")], cut=45,
         after=[T("After.")]),
    case("cancel at an index", [I(1), T("First part."), I(2), T("Second part, never said."), I(3)], cut_index=2,
         after=[T("After the index.")]),
    case("capital cancel", [P(30), T("Alpha bravo charlie delta echo foxtrot golf hotel."), P()], cut=10,
         after=[T("After the capital.")]),
    case("capital again", [P(30), T("B"), P()]),
    case("cancel after a capital", [T(LONG)], cut=12, after=[T("After.")]),
    case("pitch change after cancel", [T("Higher now, after the cancel.")], pitch=70),
    case("cancel at 11025", [T(LONG), I(4), T("More.")], cut=20, after=[T("Done.")], sr=11025),
    case("22050 Hz again", [T("Back at twenty two.")], sr=22050),
]
SPEAKOUT = COMMON + [
    case("tone a", [T("Tone A, the lowest.")], variant="a"),
    case("tone z", [T("Tone Z, the highest.")], variant="z"),
    case("join, short off", [T("One. Two, three! Four? Five.")], join=False, short=False),
    case("tone i again", [T("Back to tone I.")], variant="i"),
]
ACCENT = COMMON + [
    case("voice 0", [T("Voice zero.")], variant="0"),
    case("voice 9", [T("Voice nine, with a capital"), P(30), T("X"), P()], variant="9"),
    case("monotone", [T("Is it monotone? Yes.")], inflection=0),
    case("inflection 25", [T("Is it level two?")], inflection=25),
    case("inflection 75", [T("Is it level four?")], inflection=75),
    case("numbers off", [T("Room 100, 45000 and $6723.")], numbers=False),
    case("rate 75", [T("A little faster, 1,234.")], rate=75),
    case("defaults again", [T("Back to the defaults.")], variant="5", inflection=100, numbers=True, rate=50),
]
OTHER = {"mini": "sa", "sa": "mini"}


def cases(voice):
    if voice == "speakout":
        return SPEAKOUT
    # the other Accent in the middle: the card switched there and back (rebooted each time, as 0.7.0 did)
    return ACCENT + [case("other voice", [T("The other Accent.")], voice=OTHER[voice]),
                     case("back", [T("And back again, 12 items.")], voice=voice)]


# ---- one side, in its own process -------------------------------------------------------------------------------
def child(voice, side, out):
    which = ADDON[voice]
    sys.argv = [sys.argv[0], which]
    if side == "legacy":
        sys.path.insert(0, HERE)
        import legacy_drivers
        os.environ["SSI263_SYNTH_DRIVERS"] = legacy_drivers.synth_drivers(which)
    else:
        os.environ.pop("SSI263_SYNTH_DRIVERS", None)
        own = os.environ.get("SSI263_NATIVE_SYNTH_DRIVERS_" + which.upper())
        if own:
            os.environ["SSI263_SYNTH_DRIVERS"] = own
    g = {"__file__": os.path.join(HERE, "fake_nvda_driver_test.py"), "__name__": "harness"}
    src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read().split("time.sleep(2.0)")[0]
    exec(compile(src, g["__file__"], "exec"), g)
    d, player, notified = g["d"], g["FakePlayer"], g["notified"]
    IndexCommand, PitchCommand = g["IndexCommand"], g["PitchCommand"]
    native = not hasattr(g["drv_mod"], "numwords")
    if (side == "native") != native:
        sys.exit("%s side: the driver at %s is %s" % (side, g["BUILD"], "native" if native else "0.7.0's Python"))
    feeds = []
    orig_feed = player.feed

    def feed(self, data, onDone=None):
        if data:
            feeds.append(bytes(data))
        return orig_feed(self, data, onDone)
    player.feed = feed

    arm = {"n": None, "runs": 0, "fed": None, "index": None}
    fired = threading.Event()
    patched = set()

    def fire():
        arm["fed"] = len(feeds)
        d.cancel()
        fired.set()

    def counted(fn, is_block):
        def wrapper(self, *a, **k):
            r = fn(self, *a, **k)
            if arm["n"] and is_block(a, k):
                arm["runs"] += 1
                if arm["runs"] == arm["n"]:
                    arm["n"] = None
                    fire()
            return r
        return wrapper

    def patch_box():
        cls = type(d._box)
        if cls in patched:
            return
        patched.add(cls)
        if native:
            cls.render = counted(cls.render, lambda a, k: True)
            if BREAK == "per-text":
                say = cls.say

                def say_per_text(self, text):
                    self.end()
                    self.begin()
                    return say(self, text)
                cls.say = say_per_text
        else:
            cls.run = counted(cls.run, lambda a, k: (a[0] if a else k.get("seconds")) == BLOCK_S)

    speak_job = d._speakJob

    def job(items):
        patch_box()
        return speak_job(items)
    d._speakJob = job
    notify_index = d._notifyIndex

    def index_hook(index):
        r = notify_index(index)
        if arm["index"] is not None and index == arm["index"]:
            arm["index"] = None
            fire()
        return r
    d._notifyIndex = index_hook

    # 0.7.0's drivers predate the accented-letter pass (src/csrc/translit.h): the reference is given each text after
    # it (translit_ref.py, the same C), so everything after the pass is held byte for byte ("café, naïve" below)
    if side == "legacy":
        import translit_ref
        text = translit_ref.translit
    else:
        def text(s):
            return s

    def nvda(seq):
        return [text(s) if k == "t" else IndexCommand(s) if k == "i" else PitchCommand(s) for k, s in seq]

    if which == "accent":
        d._set_voice(voice)
    results = []
    for c in cases(voice):
        s = c["settings"]
        if which == "accent" and "voice" in s:
            d._set_voice(s["voice"])
        for name, setter in (("rate", "_set_rate"), ("pitch", "_set_pitch"), ("volume", "_set_volume"),
                             ("inflection", "_set_inflection"), ("variant", "_set_variant"), ("join", "_set_joinPhrases"),
                             ("short", "_set_shortPauses"), ("numbers", "_set_numberWords")):
            if name in s and hasattr(d, setter):
                getattr(d, setter)(s[name])
        for name, default in (("rate", 50), ("pitch", 50), ("volume", 100)):
            if name not in s:
                getattr(d, "_set_" + name)(default)
        if "sr" in s:
            d._set_sampleRate(str(s["sr"]))
        n0, k0 = len(feeds), len(notified)
        cut_at = None
        if c["cut"] or c["cut_index"] is not None:
            fired.clear()
            n = c["cut"] + (1 if BREAK == "cancel" and native else 0) if c["cut"] else None
            arm.update(n=n, runs=0, fed=None, index=c["cut_index"])
            d.speak(nvda(c["seq"]))
            if not fired.wait(60):
                sys.exit("%s: the driver never reached the cut in %r" % (side, c["label"]))
            cut_at = arm["fed"] - n0
            d.speak(nvda(c["after"]))
        else:
            d.speak(nvda(c["seq"]))
        if not g["wait_idle"](60):
            sys.exit("%s: the driver did not finish %r: %s" % (side, c["label"], g["last_wait_error"][0]))
        results.append(dict(label=c["label"], pcm=b"".join(feeds[n0:]), cut_at=cut_at,
                            cut_bytes=len(b"".join(feeds[n0:n0 + cut_at])) if cut_at is not None else None,
                            events=list(notified[k0:])))
    d.terminate()
    with open(out, "wb") as f:
        pickle.dump(dict(results=results, rate_log=None), f)


# ---- both sides, compared ---------------------------------------------------------------------------------------
def run_side(voice, side, tmp):
    out = os.path.join(tmp, "%s-%s.pickle" % (voice, side))
    env = dict(os.environ, PYTHON_COLORS="0")
    p = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", voice, side, out], env=env,
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
    if p.returncode or not os.path.isfile(out):
        sys.exit("%s %s side failed (%d):\n%s" % (voice, side, p.returncode, (p.stdout + p.stderr)[-3000:]))
    with open(out, "rb") as f:
        return pickle.load(f)["results"]


def first_diff(a, b):
    n = min(len(a), len(b)) // 2 * 2
    return next((i // 2 for i in range(0, n, 2) if a[i:i + 2] != b[i:i + 2]), n // 2)


def compare(voice, old, new):
    bad = 0
    for o, n in zip(old, new):
        label = "%s %s" % (voice, o["label"])
        why = []
        if not o["pcm"]:
            why.append("NO AUDIO from 0.7.0's driver")
        if o["pcm"] != n["pcm"]:
            why.append("PCM differs: 0.7.0 %d / native %d samples, first difference at sample %d"
                       % (len(o["pcm"]) // 2, len(n["pcm"]) // 2, first_diff(o["pcm"], n["pcm"])))
        if o["cut_bytes"] != n["cut_bytes"]:
            why.append("cut after %s / %s samples" % (o["cut_bytes"] and o["cut_bytes"] // 2,
                                                         n["cut_bytes"] and n["cut_bytes"] // 2))
        if o["events"] != n["events"]:
            why.append("notifications differ: %s / %s" % (o["events"], n["events"]))
        if why:
            bad += 1
            print("DIFF  %-34s %s" % (label, "; ".join(why)))
        else:
            print("same  %-34s %6d samples, %d notifications%s" % (label, len(o["pcm"]) // 2, len(o["events"]),
                                                                 ", cut" if o["cut_at"] is not None else ""))
    return bad, len(old)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        return child(*sys.argv[2:5])
    voices = [a for a in sys.argv[1:] if not a.startswith("--")] or ["speakout", "mini", "sa"]
    if BREAK:
        print("CONTROL: %s" % {"per-text": "the native driver re-begins its job before every text",
                               "cancel": "the native side cancels one block late"}[BREAK])
    t0 = time.perf_counter()
    bad = total = 0
    with tempfile.TemporaryDirectory() as tmp:
        from concurrent.futures import ThreadPoolExecutor
        jobs = [(v, s) for v in voices for s in ("legacy", "native")]
        with ThreadPoolExecutor(max_workers=len(jobs)) as pool:
            got = dict(zip(jobs, pool.map(lambda j: run_side(j[0], j[1], tmp), jobs)))
        for v in voices:
            b, t = compare(v, got[(v, "legacy")], got[(v, "native")])
            bad, total = bad + b, total + t
    print("%d of %d sequences byte-identical to 0.7.0's drivers, notifications in the same order (%.0f s)"
          % (total - bad, total, time.perf_counter() - t0))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
