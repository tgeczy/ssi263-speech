"""Run a BUILT add-on driver against stand-in NVDA modules, importing only from one NVDA's
own library.zip (no site-packages, no numpy).  Saves audio + events for driver_report.py.

  python -S driver_sim.py speakout|blazie|accent|accentsa|both "C:\\Program Files\\NVDA" tag
  win7\\py37\\python.exe win7\\run37.py driver_sim.py speakout|blazie|accent|both <nvda2023app> tag

The driver is imported as NVDA does it, as synthDrivers.<name> with the add-on's folder on
synthDrivers.__path__.  "both" loads all three add-ons in one process after a stale top-level
`ssi263` has been imported (the 0.3.0 failure) and has each speak.
"""
import os
import os
import sys

WHICH, NVDA_DIR, TAG = sys.argv[1], sys.argv[2], sys.argv[3]
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
BUILDS = {w: r"C:\git\ssi263-speech\nvda\dist\%s-build\synthDrivers" % w for w in ("speakout", "blazie", "accent")}
MODULE = {"speakout": "speakout", "blazie": "blazie", "accent": "accentmini"}
SA = WHICH == "accentsa"            # the Accent add-on with its "Accent SA" voice
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
module("config", conf={"speech": {"outputDevice": "default"}})     # NVDA 2021-2024 layout
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
module("autoSettingsUtils.driverSetting", BooleanDriverSetting=lambda *a, **k: None)


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


cmds = module("speech.commands", IndexCommand=IndexCommand, PitchCommand=PitchCommand)
module("speech", commands=cmds)

mark = 0


def wait_idle(timeout=30):
    t = time.perf_counter()
    while time.perf_counter() - t < timeout:
        if d._queue.empty() and any(n[0] == "done" for n in notified[mark:]):
            return True
        time.sleep(0.02)
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
        d = m.SynthDriver()
        time.sleep(0.3)
        mark = len(notified)
        n0 = len(d._player.chunks)
        d.speak(["Hello from the %s." % w])
        ok = wait_idle()
        audio = sum(len(c) for c in d._player.chunks[n0:]) / 2 / 44100.0
        d.terminate()
        print("both: %-8s chip from %s (own %s), spoke %.2f s, done %s"
              % (w, m.SSI263C.__module__, own, audio, ok))
        all_ok = all_ok and own and ok and audio > 0.3
    print("both: python %s %d-bit, stale top-level ssi263 present, %s"
          % (sys.version.split()[0], 8 * struct.calcsize("P"), "ALL OK" if all_ok else "FAILED"))
    sys.exit(0 if all_ok else 1)
sd.__path__ = [BUILDS[WHICH]]
drv = importlib.import_module("synthDrivers." + MODULE[WHICH])
d = drv.SynthDriver()
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
                    "wall_s": time.perf_counter() - ts, "audio_s": len(pcm) / 2 / 44100.0})


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
print("%s %s: python %s %d-bit, %d scenarios, all ok %s, player kwargs %s, numpy/unicorn/csv loaded: %s"
      % (WHICH, TAG, sys.version.split()[0], 8 * struct.calcsize("P"), len(results),
         all(r["ok"] for r in results), player_kwargs,
         sorted(m for m in sys.modules if m.split(".")[0] in ("numpy", "unicorn", "csv"))))
