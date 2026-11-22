"""Run a BUILT add-on driver against stand-in NVDA modules, importing only from one NVDA's
own library.zip (no site-packages, no numpy).  Saves audio + events for driver_report.py.

  python -S driver_sim.py speakout|blazie|accent|accentsa|both "C:\\Program Files\\NVDA" tag
  win7\\py37\\python.exe win7\\run37.py driver_sim.py speakout|blazie|accent|both <nvda2023app> tag

The driver is imported as NVDA does it, as synthDrivers.<name> with the add-on's folder on
synthDrivers.__path__.  "both" loads all three add-ons in one process after a stale top-level
`ssi263` has been imported (the 0.3.0 failure) and has each speak.
"""
import os
import sys

WHICH, NVDA_DIR, TAG = sys.argv[1], sys.argv[2], sys.argv[3]
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
# nvda/dist/<name>-build/synthDrivers, found from here (as tools/repo_paths.synth_drivers
# does; this script can't import it: sys.path is replaced with NVDA's own library below)
DIST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "dist")
BUILDS = {w: os.path.join(DIST, "%s-build" % w, "synthDrivers") for w in ("speakout", "blazie", "accent")}
MODULE = {"speakout": "speakout", "blazie": "blazie", "accent": "accentmini"}
SA = WHICH == "accentsa"            # the Accent add-on with its "Accent-PC" voice
if SA:
    WHICH = "accent"
sys.path[:] = [os.path.join(NVDA_DIR, "library.zip"), NVDA_DIR]

import json       # noqa: E402
import struct     # noqa: E402
import threading  # noqa: E402
import time       # noqa: E402
import types      # noqa: E402


class FakePlayer:
    def __init__(self, *a, **k):
        self.kwargs = sorted(k)
        self.rate = k.get("samplesPerSec")
        self.chunks, self.events, self.lock = [], [], threading.Lock()
        self.samples = 0
        self.feeds = []            # (wall time, samples) per non-empty feed
        self.pace = False          # True: block like a real device, so a cancel lands mid-speech

    def feed(self, data, onDone=None):
        if self.pace and data:
            time.sleep(len(data) / 2 / 44100.0)
        with self.lock:
            if data:
                self.chunks.append(bytes(data))
                self.samples += len(data) // 2
                self.feeds.append((time.perf_counter(), len(data) // 2))
            if onDone:
                self.events.append(("done", self.samples))
                onDone()

    def stop(self):
        with self.lock:
            self.events.append(("stop", self.samples))

    def idle(self):
        pass

    def pause(self, s):
        pass

    def close(self):
        pass


def module(name, **attrs):
    m = types.ModuleType(name)
    m.__dict__.update(attrs)
    sys.modules[name] = m
    return m


module("nvwave", WavePlayer=FakePlayer)
class _Speech(dict):
    """config.conf["speech"], recording every key read: a driver must NOT read its own section in __init__ (NVDA
    registers the settings after it and caches a failed lookup; the 0.6.0 draft failed setSynth that way)."""
    touched = []

    def __getitem__(self, k):
        _Speech.touched.append(k)
        return dict.__getitem__(self, k)


module("config", conf={"speech": _Speech(outputDevice="default")})     # NVDA 2021-2024 layout
DRIVER_NAMES = ("blazie", "speakout", "accentmini")


def config_guard(where):
    bad = [k for k in _Speech.touched if k in DRIVER_NAMES]
    del _Speech.touched[:]
    if bad:
        print("CONFIG READ IN __init__ (%s): %s -- this breaks NVDA's loadSettings" % (where, bad))
        sys.exit(1)
notified = []


class Notifier:
    def __init__(self, name):
        self.name = name

    def notify(self, **kw):
        notified.append((self.name, kw.get("index")))


class Base:
    VoiceSetting = RateSetting = PitchSetting = VolumeSetting = VariantSetting = InflectionSetting = staticmethod(lambda: None)

    def __init__(self):
        pass


module("synthDriverHandler", SynthDriver=Base, VoiceInfo=lambda *a: a,
       synthIndexReached=Notifier("index"), synthDoneSpeaking=Notifier("done"))
module("autoSettingsUtils")
module("autoSettingsUtils.utils", StringParameterInfo=lambda *a: a)
module("autoSettingsUtils.driverSetting", BooleanDriverSetting=lambda *a, **k: None, DriverSetting=lambda *a, **k: None)


class _Log:
    debug_lines = []

    def isEnabledFor(self, level):
        return True

    def debug(self, msg):
        self.debug_lines.append(msg)

    def warning(self, msg):
        print("LOG WARNING:", msg)

    def error(self, msg, exc_info=False):
        print("LOG ERROR:", msg)
        if exc_info:
            import traceback
            traceback.print_exc()


module("logHandler", log=_Log())


class IndexCommand:
    def __init__(self, index):
        self.index = index


class PitchCommand:
    def __init__(self, offset=0):
        self.offset = offset


class LangChangeCommand:
    def __init__(self, lang=None):
        self.lang = lang


cmds = module("speech.commands", IndexCommand=IndexCommand, PitchCommand=PitchCommand, LangChangeCommand=LangChangeCommand)
module("speech", commands=cmds)

mark = 0
# Completion, owned (Astra, Reply 104; the same rule as fake_nvda_driver_test.py): "the queue empty and a done since
# the speak" took the previous utterance's done while this one was still being spoken.  Each sequence spoken here ends
# with an index of its own; it is complete when that index has been reached and a done follows it, with no other
# utterance's index between.  A dead worker or running out of time is False, never a pass.
OWNED_BASE = 900000
owned = []
# the must-fail control: the old rule put back, and a done forged after each speak (a previous utterance's, late)
STALE = os.environ.get("DRIVER_SIM_STALE") == "1"


def own(drv):
    """Give every sequence drv speaks a trailing index of its own (as NVDA's speech manager ends each utterance)."""
    speak = drv.speak

    def owned_speak(seq):
        owned.append((OWNED_BASE + len(owned), len(notified)))
        speak(list(seq) + [IndexCommand(owned[-1][0])])
        if STALE:
            notified.append(("done", None))
    drv.speak = owned_speak
    return drv


def completed(tok, since):
    if STALE:
        return d._queue.empty() and any(n[0] == "done" for n in notified[since:])
    try:
        p = notified.index(("index", tok), since)
    except ValueError:
        return False
    for name, idx in notified[p + 1:]:
        if name == "done":
            return True
        if name == "index" and isinstance(idx, int) and idx >= OWNED_BASE:
            return False                     # the next utterance's index before this one's done: never done
    return False


def wait_idle(timeout=30):
    if not owned:
        return False
    tok, since = owned[-1]
    t = time.perf_counter()
    while time.perf_counter() - t < timeout:
        if completed(tok, since):
            return True
        w = getattr(d, "_worker", None)
        if w is not None and not w.is_alive():
            return False
        time.sleep(0.005)
    return False


t0 = time.perf_counter()
import importlib  # noqa: E402
sd = module("synthDrivers")                    # NVDA's package: add-on folders on its __path__
if WHICH == "both":
    sd.__path__ = [BUILDS[w] for w in MODULE]
    stale = module("ssi263")                   # what an older add-on left in the process
    stale.__path__ = []
    all_ok = True
    for w in MODULE:
        m = importlib.import_module("synthDrivers." + MODULE[w])
        own = m.SSI263C.__module__ == "synthDrivers._ssi263_%s.ssi263.native" % w
        d = own(m.SynthDriver())
        config_guard(w)
        time.sleep(0.3)
        mark = len(notified)
        n0 = len(d._player.chunks)
        d.speak(["Hello from the %s." % w])
        ok = wait_idle()
        audio = sum(len(c) for c in d._player.chunks[n0:]) / 2 / float(d._player.rate)
        d.terminate()
        print("both: %-8s chip from %s (own %s), spoke %.2f s, done %s"
              % (w, m.SSI263C.__module__, own, audio, ok))
        all_ok = all_ok and own and ok and audio > 0.3
    print("both: python %s %d-bit, stale top-level ssi263 present, %s"
          % (sys.version.split()[0], 8 * struct.calcsize("P"), "ALL OK" if all_ok else "FAILED"))
    sys.exit(0 if all_ok else 1)
sd.__path__ = [BUILDS[WHICH]]
drv = importlib.import_module("synthDrivers." + MODULE[WHICH])
d = own(drv.SynthDriver())
config_guard(WHICH)
if SA:
    d._set_voice("sa")               # as NVDA does after loading the driver, from its config
    TAG = TAG + "-sa"
time.sleep(0.5)


results = []


def scenario(label, seq, setup=None):
    global mark
    if setup:
        setup()
    mark = len(notified)
    n0 = len(d._player.chunks)
    f0 = len(d._player.feeds)
    ts = time.perf_counter()
    d.speak(seq)
    ok = wait_idle()
    feeds = d._player.feeds[f0:]
    pcm = b"".join(d._player.chunks[n0:])
    fn = os.path.join(HERE, "sim_%s_%s_%d.pcm" % (WHICH, TAG, len(results)))
    open(fn, "wb").write(pcm)
    results.append({"label": label, "ok": ok, "pcm": fn, "notified": notified[mark:],
                    "first_feed_ms": (feeds[0][0] - ts) * 1e3 if feeds else None,
                    "wall_s": time.perf_counter() - ts, "audio_s": len(pcm) / 2 / float(d._player.rate), "rate": d._player.rate})


def join_off():
    d._join = False


def join_on():
    d._join = True


# wait for the boot to finish: the first job runs after it
scenario("warmup", ["ok."])
scenario("indexes", [IndexCommand(1), "Hello there.", IndexCommand(2), "This is a test of the driver.",
                     IndexCommand(3)])
scenario("pieces joined", ["Select synthesizer", "dialog"], join_on)
scenario("pieces native", ["Select synthesizer", "dialog"], join_off)
scenario("colon one string", ["Select synthesizer dialog. Synthesizer: combo box"], join_on)
scenario("plain a", ["a"])
scenario("capital A", [PitchCommand(30), "A", PitchCommand()])
scenario("plain a again", ["a"])
if WHICH == "accent":
    # every setting is an ESC command to the driver; each must leave it talking
    def setter(name, value):
        return lambda: getattr(d, "_set_" + name)(value)     # what NVDA's property does
    for name, values in (("variant", [str(n) for n in range(10)] + ["5"]), ("rate", [0, 25, 75, 100, 50]),
                         ("pitch", [0, 100, 50]), ("inflection", [0, 25, 50, 75, 100])):
        for v in values:
            scenario("%s %s" % (name, v), ["Hello there."], setter(name, v))

rate_ok = True
if WHICH == "blazie":
    # The firmware takes ^E n E modulo 16, so unit rate 16 speaks at rate 10: NVDA's
    # fastest settings must stop at 15, and speaking at 100 must really be faster.
    units = [d._unit_rate(r) for r in range(101)]
    rate_ok = (min(units) >= 1 and max(units) == 15 and units[50] == drv.DEFAULT_RATE
               and all(a <= b for a, b in zip(units, units[1:])))
    line = ["The quick brown fox jumps over the lazy dog, and then it runs away."]
    scenario("rate 50", line, lambda: d._set_rate(50))
    scenario("rate 100", line, lambda: d._set_rate(100))
    rate_ok = rate_ok and results[-1]["audio_s"] < 0.6 * results[-2]["audio_s"]
    print("blazie rate map: units %d..%d, 50 -> %d, rate 100 %.2f s vs rate 50 %.2f s: %s"
          % (min(units), max(units), units[50], results[-1]["audio_s"], results[-2]["audio_s"],
             "ok" if rate_ok else "FAILED"))
    d._set_rate(50)

    # tones 0-26 (the chords' 0-16 plus what speech-box mode takes); tone 0 must reach the chip as R4 = E0h
    tones = sorted(int(k) for k in d._get_availableVariants())
    scenario("tone 0", ["Tone zero."], lambda: d._set_variant("0"))
    r4 = d._unit.chip.regs[4]
    d._set_variant("30")                      # beyond the list: ignored
    tone_ok = tones == list(range(27)) and r4 == 0xE0 and d._get_variant() == "0"
    print("blazie tones: %d..%d (%d), tone 0 -> R4 %02X, '30' ignored: %s"
          % (tones[0], tones[-1], len(tones), r4, "ok" if tone_ok else "FAILED"))
    rate_ok = rate_ok and tone_ok
    d._set_variant("7")
    # voice inflection (the unit's status-menu yes/no): off, a '?' must not raise R1; back on, it must
    def r1_during(text):
        global mark
        mark = len(notified)
        d.speak(["ok."])                      # the worker restarts the unit before this job
        wait_idle()
        seen = []
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from write_spy import watch_writes
        stop = watch_writes(d._unit, lambda t, reg, val: seen.append(val) if reg == 1 else None)
        mark = len(notified)
        d.speak([text])
        wait_idle()
        stop()
        return max(seen) if seen else 0
    d._set_voiceInflection(False)
    r1_off = r1_during("Is it ready?")
    d._set_voiceInflection(True)
    r1_on = r1_during("Is it ready?")
    infl_ok = r1_on > r1_off and d._get_voiceInflection() is True
    print("blazie inflection: '?' raises R1 to %02X with it on, %02X with it off: %s" % (r1_on, r1_off, "ok" if infl_ok else "FAILED"))
    rate_ok = rate_ok and infl_ok
    # 0.7: the unit runs in-process (bl.dll); the pipe host is only the fallback, and a silent fallback is a failure
    host = type(d._unit).__name__
    want = "Blazie" if os.environ.get("SSI263_BLAZIE_PIPE") == "1" else "NativeBlazie"
    print("blazie host: %s (want %s): %s" % (host, want, "ok" if host == want else "FAILED"))
    rate_ok = rate_ok and host == want
    # the unit's hiss / whine (hosts/blazie.py whine_wave): each choice speaks, and switches the chip's carrier
    whine_ok = sorted(d._get_availableWhines()) == ["hiss", "off", "whine"]
    for w in ("whine", "hiss", "off"):
        d._set_whine(w)
        mark = len(notified)
        n0 = len(d._player.chunks)
        d.speak(["Hello there."])
        whine_ok = whine_ok and wait_idle() and d._unit.whine == (None if w == "off" else w)
    d._set_whine("bogus")                     # not offered: ignored
    whine_ok = whine_ok and d._get_whine() == "off"
    print("blazie whine: off / hiss / whine each speak, the host follows: %s" % ("ok" if whine_ok else "FAILED"))
    rate_ok = rate_ok and whine_ok
    # Spanish: the voice, then NVDA's language switching inside an English utterance
    voices = d._get_availableVoices()
    es_ok = "blazie_es" in voices and voices["blazie_es"][2] == "es"
    if es_ok:
        d._set_voice("blazie_es")
        scenario("spanish voice", ["Hola, \u00bfc\u00f3mo est\u00e1s? El a\u00f1o 2003, a 3,5 grados."])
        es_ok = results[-1]["ok"] and results[-1]["audio_s"] > 1.0 and "blazie_es" in d._units
        d._set_voice("blazie")
        scenario("language switching", ["Hello there.", LangChangeCommand("es"), "Buenos d\u00edas.",
                                        LangChangeCommand("en"), "Goodbye."])
        es_ok = es_ok and results[-1]["ok"] and results[-1]["audio_s"] > 1.5
    print("blazie spanish: voice listed %s, spoken, language switching: %s" % ("blazie_es" in voices, "ok" if es_ok else "FAILED"))
    rate_ok = rate_ok and es_ok
    # 0.8: the release table's voices (bl_languages.txt), when the add-on carries them: Hungarian, Czech (a slower
    # firmware: its own boot keys), Turkish (code page 857) and Russian (866) each speak a sentence of its own; NVDA's
    # language switching reaches Hungarian
    rel_want = {"blazie_hu": ("hu", "Jó napot, hogy vagy? Ez egy próba."),
                "blazie_cs": ("cs", "Dobrý den, jak se máte?"),
                "blazie_tr": ("tr", "İyi günler, nasılsınız?"),
                "blazie_ru": ("ru", "Добрый день, как "
                                    "дела?")}
    present = [v for v in rel_want if v in voices]
    rel_ok = True
    for v in present:
        d._set_voice(v)
        scenario("%s voice" % v, [rel_want[v][1]])
        good = results[-1]["ok"] and results[-1]["audio_s"] > 1.0 and v in d._units and voices[v][2] == rel_want[v][0]
        print("blazie %s: listed as %r (%s), spoken %.1f s: %s" % (v, voices[v][1], voices[v][2],
                                                                 results[-1]["audio_s"], "ok" if good else "FAILED"))
        rel_ok = rel_ok and good
    if "blazie_hu" in present:
        # its letters go out as the unit's own bytes (bl_cphun.h, through the library's blv_text_byte): o double acute
        # as A7h in both cases, the others each their byte -- not the base letters of the old plain-letter path
        enc = d._units["blazie_hu"].encoding
        got = "Jó, tükörfúrógép, őŐ űÁ".encode(enc, "replace")
        want = b"J\xa2, t\x81k\x94rf\xa3r\xa2g\x82p, \xa7\xa7 \x96\x8f"
        good = got == want
        print("blazie hungarian letters (%s): %r: %s" % (enc, got, "ok" if good else "FAILED, want %r" % want))
        rel_ok = rel_ok and good
        d._set_voice("blazie")
        scenario("language switching (hu)", ["Hello there.", LangChangeCommand("hu"), "Jó napot.",
                                             LangChangeCommand("en"), "Goodbye."])
        good = results[-1]["ok"] and results[-1]["audio_s"] > 1.5
        print("blazie language switching to Hungarian: %s" % ("ok" if good else "FAILED"))
        rel_ok = rel_ok and good
    if not present:
        print("blazie release voices: none in this build (skipped)")
    d._set_voice("blazie")
    rate_ok = rate_ok and rel_ok

if SA:
    # The shipping default is MAME's C host; the Python core is a development reference.
    # A silent fallback would be a failure.
    host = type(d._box).__name__
    want = "AccentPCC" if (os.environ.get("SSI263_ACCENT_PC_CORE", "").strip().lower() or "c") == "c" else "AccentPC"
    print("accent pc host: %s (want %s): %s" % (host, want, "ok" if host == want else "FAILED"))
    rate_ok = rate_ok and host == want

# the Sample rate combo box: 22 kHz by default; each rate gets its own player and the same speech (length and level)
from array import array  # noqa: E402
srate_default = d._out_rate == 22050 and d._get_sampleRate() == "22050"
srate = {}
for r in ("11025", "22050", "44100"):
    d._set_sampleRate(r)
    mark = len(notified)
    d.speak(["Hello there, this is a sample rate test."])
    ok = wait_idle()
    pcm = array("h", b"".join(d._player.chunks))
    rms = (sum(v * v for v in pcm) / max(1, len(pcm))) ** 0.5
    srate[r] = (ok, d._player.rate, len(pcm) / float(r), rms)
d._set_sampleRate("8000")                      # not offered: ignored
ref = srate["44100"]
srate_ok = srate_default and d._get_sampleRate() == "44100" and all(
    ok and ref[2] > 0 and ref[3] > 0 and rate == int(r) and abs(secs / ref[2] - 1) < 0.03 and abs(rms / ref[3] - 1) < 0.15
    for r, (ok, rate, secs, rms) in srate.items())
print("sample rate: default 22 kHz %s; %s: %s" % (srate_default, ", ".join("%s %.2f s rms %.0f" % (r, v[2], v[3])
                                                                     for r, v in srate.items()), "ok" if srate_ok else "FAILED"))

# cancel mid-sentence, then speak again
mark = len(notified)
n0 = len(d._player.chunks)
d._player.pace = True
d.speak(["this sentence is going to be interrupted very soon by a cancel from NVDA, and it keeps going"])
time.sleep(0.6)
d.cancel()
time.sleep(0.3)
d._player.pace = False
scenario("after cancel", ["After."])
cancel_events = d._player.events[-4:]
player_kwargs = d._player.kwargs
d.terminate()
json.dump({"which": WHICH, "tag": TAG, "python": sys.version.split()[0], "bits": 8 * struct.calcsize("P"),
           "import_and_boot_s": None, "results": results, "cancel_events": cancel_events,
           "player_kwargs": player_kwargs, "modules": sorted(m for m in sys.modules if m.split(".")[0] in
                                                             ("numpy", "unicorn", "csv"))},
          open(os.path.join(HERE, "sim_%s_%s.json" % (WHICH, TAG)), "w"), indent=1)
all_ok = all(r["ok"] for r in results) and rate_ok and srate_ok
print("%s %s: python %s %d-bit, %d scenarios, all ok %s, player kwargs %s, numpy/unicorn/csv loaded: %s"
      % (WHICH, TAG, sys.version.split()[0], 8 * struct.calcsize("P"), len(results),
         all_ok, player_kwargs,
         sorted(m for m in sys.modules if m.split(".")[0] in ("numpy", "unicorn", "csv"))))
sys.exit(0 if all_ok else 1)
