"""The 0.8 add-on's driver (synthDrivers/ssi263.py) against each unit's own driver, byte for byte: under the stand-in
NVDA (fake_nvda_driver_test.py), the same NVDA sequences with the same settings -- every PCM byte fed to the player
and the order of the index and done notifications must be equal, and every notification must name the driver NVDA
was given (NVDA drops one whose synth is not getSynth(): the units' notifySynth).

    python unified_driver_equiv.py [braillelite2000 braillelite2000-es speakout accentsa accentmini mockingboard]
    UNIFIED_EQUIV_BREAK=notify    control: the units report as themselves (notifySynth ignored) -- every case with
                                  a notification must fail on the synth, the PCM still equal
    UNIFIED_EQUIV_BREAK=memory    control: the wrapper does not forward the variant -- the variant cases must DIFFER

The reference is the 0.7-style add-on build of that unit (nvda/dist/<blazie|speakout|accent>-build); the
Mockingboard's, its own driver from the 0.8 build's private package.  The cases are native_driver_equiv.py's (the
Braille Lite: its COMMON ones and its own tones), cancels made at a fixed count of fed blocks on both sides.  Each
side runs in its own process (the units' module names).  Build first: nvda/build_ssi263.py.
"""
import os
import pickle
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DIST = os.path.join(REPO, "nvda", "dist")
BREAK = os.environ.get("UNIFIED_EQUIV_BREAK", "")
sys.path.insert(0, HERE)
import native_driver_equiv as nde      # noqa: E402  (its cases)

T, I, P, case = nde.T, nde.I, nde.P, nde.case


def L(lang):
    """NVDA's LangChangeCommand: the Braille Lite switches to that language's unit for what follows"""
    return ("l", lang)


# firmware type -> (the reference add-on, its driver module, the reference driver's voice, the wrapper's language)
UNITS = {
    "braillelite2000": ("blazie", "blazie", "blazie", "en"),
    "braillelite2000-es": ("blazie", "blazie", "blazie_es", "es"),
    "speakout": ("speakout", "speakout", None, "en"),
    "accentsa": ("accent", "accentmini", "sa", "en"),
    "accentmini": ("accent", "accentmini", "mini", "en"),
    "mockingboard": (None, "mockingboard", None, "en"),
}
BRAILLE_LITE = [c for c in nde.COMMON if "sr" not in c["settings"]] + [
    case("tone 0", [T("Tone zero, the lowest.")], variant="0"),
    case("tone 26", [T("Tone twenty six.")], variant="26"),
    case("short pauses off", [T("One. Two, three! Four? Five.")], short=False),
    case("numbers off", [T("Room 100, 45000 and $6723.")], numbers=False),
    case("tone 7 again", [T("Back to tone seven.")], variant="7", short=True, numbers=True),
    case("language switch", [T("Hello."), L("es"), T("Hola, buenos d\u00edas."), I(9), L("en"),
                             T("Back in English.")]),
]
MOCKINGBOARD = [c for c in nde.COMMON] + [
    case("numbers off", [T("Room 100, 45000 and $6723.")], numbers=False),
    case("long text in parts", [T(nde.LONG + " " + nde.LONG + " And then some more words to say.")], numbers=True),
]


def cases(fw):
    if fw.startswith("braillelite2000"):
        return BRAILLE_LITE
    if fw == "speakout":
        return nde.SPEAKOUT
    if fw == "mockingboard":
        return MOCKINGBOARD
    return nde.ACCENT


# ---- one side, in its own process -------------------------------------------------------------------------------
def child(fw, side, out):
    addon, module, voice, lang = UNITS[fw]
    if side == "unit":
        which = module
        os.environ["SSI263_SYNTH_DRIVERS"] = (os.path.join(DIST, "%s-build" % addon, "synthDrivers") if addon else
                                              os.path.join(DIST, "ssi263-build", "synthDrivers", "_ssi263_unified"))
        os.environ.pop("SSI263_FIRMWARE_TYPE", None)
    else:
        which = "ssi263"
        os.environ["SSI263_SYNTH_DRIVERS"] = os.path.join(DIST, "ssi263-build", "synthDrivers")
        os.environ["SSI263_FIRMWARE_TYPE"] = fw.split("-")[0]
    sys.argv = [sys.argv[0], which]
    g = {"__file__": os.path.join(HERE, "fake_nvda_driver_test.py"), "__name__": "harness"}
    src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read().split("time.sleep(2.0)")[0]
    exec(compile(src, g["__file__"], "exec"), g)
    d, player, notifier = g["d"], g["FakePlayer"], g["Notifier"]
    IndexCommand, PitchCommand, LangChangeCommand = g["IndexCommand"], g["PitchCommand"], g["LangChangeCommand"]
    if side == "wrapper":
        if BREAK == "notify":
            d.inner.notifySynth = None
        if BREAK == "memory":
            type(d)._set_variant = lambda self, v: None
        d._set_voice(lang)
    elif voice:
        d._set_voice(voice)
    if fw.startswith("braillelite2000"):
        d._set_keepOpen(False)          # the channel kept open feeds on a clock: not part of a byte comparison
    synths = []
    notify = notifier.notify

    def notify_synth(self, **kw):
        synths.append(kw.get("synth") is d)
        return notify(self, **kw)
    notifier.notify = notify_synth

    feeds = []
    arm = {"n": None, "fed": None, "index": None}
    fired = threading.Event()
    orig_feed = player.feed

    def feed(self, data, onDone=None):
        r = orig_feed(self, data, onDone)
        if data:
            feeds.append(bytes(data))
            if arm["n"] is not None and len(feeds) - arm["n0"] == arm["n"]:
                arm["n"] = None
                arm["fed"] = len(feeds)
                d.cancel()
                fired.set()
        return r
    player.feed = feed
    # a cancel as an index is reached: from the unit's own _notifyIndex, as native_driver_equiv.py does (onDone runs
    # inside the stand-in player's lock, which cancel's player.stop() takes)
    unit = d.inner if side == "wrapper" else d
    notify_index = unit._notifyIndex

    def index_hook(index):
        r = notify_index(index)
        if arm["index"] is not None and index == arm["index"]:
            arm["index"] = None
            arm["fed"] = len(feeds)
            d.cancel()
            fired.set()
        return r
    unit._notifyIndex = index_hook

    def nvda(seq):
        return [s if k == "t" else IndexCommand(s) if k == "i" else LangChangeCommand(s) if k == "l"
                else PitchCommand(s) for k, s in seq]

    results = []
    only = os.environ.get("UNIFIED_EQUIV_ONLY")
    for c in cases(fw):
        if only and only not in c["label"]:
            continue
        s = c["settings"]
        if "voice" in s:                 # the other Accent: another unit for the reference, another type here
            if side == "wrapper":
                d._set_firmwareType({"sa": "accentsa", "mini": "accentmini"}[s["voice"]])
            else:
                d._set_voice(s["voice"])
        for name, setter in (("rate", "_set_rate"), ("pitch", "_set_pitch"), ("volume", "_set_volume"),
                             ("inflection", "_set_inflection"), ("variant", "_set_variant"),
                             ("join", "_set_joinPhrases"), ("short", "_set_shortPauses"),
                             ("numbers", "_set_numberWords")):
            if name in s and hasattr(d, setter):
                getattr(d, setter)(s[name])
        for name, default in (("rate", 50), ("pitch", 50), ("volume", 100)):
            if name not in s:
                getattr(d, "_set_" + name)(default)
        if "sr" in s:
            d._set_sampleRate(str(s["sr"]))
        n0, k0, s0 = len(feeds), len(g["notified"]), len(synths)
        if c["cut"] or c["cut_index"] is not None:
            fired.clear()
            arm.update(n=c["cut"], n0=n0, fed=None, index=c["cut_index"])
            d.speak(nvda(c["seq"]))
            if not fired.wait(60):
                sys.exit("%s: the driver never reached the cut in %r (notified since: %s)"
                         % (side, c["label"], g["notified"][k0:]))
            cut_at = arm["fed"] - n0
            d.speak(nvda(c["after"]))
        else:
            cut_at = None
            d.speak(nvda(c["seq"]))
        if not g["wait_idle"](60):
            sys.exit("%s: the driver did not finish %r: %s" % (side, c["label"], g["last_wait_error"][0]))
        results.append(dict(label=c["label"], pcm=b"".join(feeds[n0:]), cut_at=cut_at,
                            events=list(g["notified"][k0:]), synth_ok=all(synths[s0:]), n_events=len(synths) - s0))
    d.terminate()
    with open(out, "wb") as f:
        pickle.dump(results, f)


# ---- both sides, compared ---------------------------------------------------------------------------------------
def run_side(fw, side, tmp):
    out = os.path.join(tmp, "%s-%s.pickle" % (fw, side))
    p = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", fw, side, out],
                       env=dict(os.environ, PYTHON_COLORS="0"), capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=600)
    if p.returncode or not os.path.isfile(out):
        sys.exit("%s %s side failed (%d):\n%s" % (fw, side, p.returncode, (p.stdout + p.stderr)[-3000:]))
    with open(out, "rb") as f:
        return pickle.load(f)


def compare(fw, ref, new):
    bad = 0
    for o, n in zip(ref, new):
        why = []
        if not o["pcm"]:
            why.append("NO AUDIO from the unit's own driver")
        if o["pcm"] != n["pcm"]:
            why.append("PCM differs: unit %d / wrapper %d samples, first difference at sample %d"
                       % (len(o["pcm"]) // 2, len(n["pcm"]) // 2, nde.first_diff(o["pcm"], n["pcm"])))
        if o["cut_at"] != n["cut_at"]:
            why.append("cut after %s / %s blocks" % (o["cut_at"], n["cut_at"]))
        if o["events"] != n["events"]:
            why.append("notifications differ: %s / %s" % (o["events"], n["events"]))
        if not n["synth_ok"]:
            why.append("a notification NOT from the wrapper (of %d)" % n["n_events"])
        if why:
            bad += 1
            print("DIFF  %-40s %s" % ("%s %s" % (fw, o["label"]), "; ".join(why)))
        else:
            print("same  %-40s %6d samples, %d notifications%s" % ("%s %s" % (fw, o["label"]), len(o["pcm"]) // 2,
                                                                 len(o["events"]),
                                                                 ", cut" if o["cut_at"] is not None else ""))
    return bad, len(ref)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        return child(*sys.argv[2:5])
    fws = [a for a in sys.argv[1:] if not a.startswith("--")] or list(UNITS)
    if not os.path.isdir(os.path.join(DIST, "ssi263-build", "synthDrivers")):
        sys.exit("no nvda/dist/ssi263-build: run nvda/build_ssi263.py first")
    if not os.path.isfile(os.path.join(DIST, "ssi263-build", "synthDrivers", "_ssi263_unified", "mockingboard.py")):
        fws = [f for f in fws if f != "mockingboard"]
        print("note: the build has no Mockingboard (no firmware file): not compared")
    if BREAK:
        print("CONTROL: %s" % {"notify": "the units report as themselves",
                               "memory": "the wrapper drops the variant"}[BREAK])
    t0 = time.perf_counter()
    bad = total = 0
    with tempfile.TemporaryDirectory() as tmp:
        from concurrent.futures import ThreadPoolExecutor
        jobs = [(f, s) for f in fws for s in ("unit", "wrapper")]
        with ThreadPoolExecutor(max_workers=len(jobs)) as pool:
            got = dict(zip(jobs, pool.map(lambda j: run_side(j[0], j[1], tmp), jobs)))
        for f in fws:
            b, t = compare(f, got[(f, "unit")], got[(f, "wrapper")])
            bad, total = bad + b, total + t
    print("%d of %d sequences byte-identical to the units' own drivers, notifications the same and the wrapper's "
          "(%.0f s)" % (total - bad, total, time.perf_counter() - t0))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
