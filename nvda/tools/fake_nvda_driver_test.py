"""Drive the real add-on driver files against stand-in NVDA modules."""
import importlib
import os
import sys
import threading
import time
import types

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
from tools import repo_paths             # noqa: E402

WHICH = sys.argv[1]            # speakout | blazie | accent
BUILD = repo_paths.synth_drivers(WHICH)


# ---- stand-ins ------------------------------------------------------------------
class FakePlayer:
    def __init__(self, *a, **k):
        self.chunks, self.events, self.lock = [], [], threading.Lock()
        self.pace = False          # True: block like a real device, so a cancel lands mid-speech

    def feed(self, data, onDone=None):
        if self.pace and data:
            time.sleep(len(data) / 2 / 44100.0)
        with self.lock:
            if data:
                self.chunks.append(np.frombuffer(data, dtype="<i2").astype(float) / 32767)
            if onDone:
                self.events.append(("done", sum(len(c) for c in self.chunks)))
                onDone()

    def stop(self):
        with self.lock:
            self.events.append(("stop", sum(len(c) for c in self.chunks)))

    def idle(self):
        pass

    def pause(self, s):
        pass

    def close(self):
        pass


nvwave = types.ModuleType("nvwave")
nvwave.WavePlayer = FakePlayer
sys.modules["nvwave"] = nvwave
config = types.ModuleType("config")
config.conf = {"audio": {"outputDevice": "default"}, "speech": {"outputDevice": "default"}}
sys.modules["config"] = config
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


sdh = types.ModuleType("synthDriverHandler")
sdh.SynthDriver = Base
sdh.VoiceInfo = lambda *a: a
sdh.synthIndexReached = Notifier("index")
sdh.synthDoneSpeaking = Notifier("done")
sys.modules["synthDriverHandler"] = sdh
asu = types.ModuleType("autoSettingsUtils")
asu_utils = types.ModuleType("autoSettingsUtils.utils")
asu_utils.StringParameterInfo = lambda *a: a
sys.modules["autoSettingsUtils"] = asu
sys.modules["autoSettingsUtils.utils"] = asu_utils
asu_ds = types.ModuleType("autoSettingsUtils.driverSetting")
asu_ds.BooleanDriverSetting = lambda *a, **k: None
sys.modules["autoSettingsUtils.driverSetting"] = asu_ds
lh = types.ModuleType("logHandler")


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


lh.log = _Log()
sys.modules["logHandler"] = lh
speech = types.ModuleType("speech")
cmds = types.ModuleType("speech.commands")


class IndexCommand:
    def __init__(self, index):
        self.index = index


class PitchCommand:
    def __init__(self, offset=0):
        self.offset = offset


cmds.IndexCommand, cmds.PitchCommand = IndexCommand, PitchCommand
speech.commands = cmds
sys.modules["speech"] = speech
sys.modules["speech.commands"] = cmds

# ---- the real driver -------------------------------------------------------------
_sd = types.ModuleType("synthDrivers")          # NVDA's package, with the add-on's folder on it
_sd.__path__ = [BUILD]
sys.modules["synthDrivers"] = _sd
drv_mod = importlib.import_module("synthDrivers." + {"accent": "accentmini"}.get(WHICH, WHICH))
d = drv_mod.SynthDriver()


def wait_idle(timeout=20):
    t = time.time()
    while time.time() - t < timeout:
        if d._queue.empty() and any(n[0] == "done" for n in notified[mark:]):
            return True
        time.sleep(0.05)
    return False


def audio_since(n0):
    with d._player.lock:
        return np.concatenate(d._player.chunks[n0:]) if len(d._player.chunks) > n0 else np.zeros(0)


def f0(y, sr=44100):
    h = int(0.04 * sr)
    best = []
    for k in range(0, len(y) - h, int(0.02 * sr)):
        x = y[k:k + h]
        if np.sqrt(np.mean(x ** 2)) < 0.05 * np.abs(y).max():
            continue
        x = x - x.mean()
        ac = np.correlate(x, x, "full")[len(x) - 1:]
        i = int(sr / 260) + np.argmax(ac[int(sr / 260):int(sr / 55)])
        if ac[i] > 0.45 * ac[0]:
            best.append(sr / i)
    return np.median(best) if best else float("nan")


time.sleep(2.0)               # boot
mark = len(notified)
n0 = len(d._player.chunks)
d.speak([IndexCommand(1), "Hello there.", IndexCommand(2), "This is a test of the driver.", IndexCommand(3)])
ok = wait_idle()
y = audio_since(n0)
print("utterance: done=%s audio %.2f s, notifications %s" % (ok, len(y) / 44100, notified[mark:]))
mark = len(notified)
n0 = len(d._player.chunks)
d.speak(["Select synthesizer", "dialog"])
wait_idle()
yj = audio_since(n0)
d._join = False
mark = len(notified)
n0 = len(d._player.chunks)
d.speak(["Select synthesizer", "dialog"])
wait_idle()
yn = audio_since(n0)
d._join = True
print("pieces joined %.2f s, native %.2f s" % (len(yj) / 44100, len(yn) / 44100))
for label, seq in (("plain a", ["a"]), ("capital A", [PitchCommand(30), "A", PitchCommand()]), ("plain a again", ["a"])):
    mark = len(notified)
    n0 = len(d._player.chunks)
    d.speak(seq)
    wait_idle()
    y = audio_since(n0)
    print("%-14s %.2f s audio, F0 %.1f Hz" % (label, len(y) / 44100, f0(y)))
mark = len(notified)
n0 = len(d._player.chunks)
d._player.pace = True
d.speak(["this sentence is going to be interrupted very soon by a cancel from NVDA, and it keeps going"])
time.sleep(0.6)
d.cancel()
time.sleep(0.3)
d._player.pace = False
d.speak(["After."])
wait_idle()
print("after cancel: player events %s; notifications %s" % (d._player.events[-3:], notified[mark:]))
d.terminate()
