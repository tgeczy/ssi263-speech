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
# SSI263_SYNTH_DRIVERS: another add-on folder's synthDrivers (legacy_drivers.py: 0.7.0's Python drivers, the reference)
BUILD = os.environ.get("SSI263_SYNTH_DRIVERS") or repo_paths.synth_drivers(WHICH)


# SIM_SPEED: a paced player plays this many times faster than real time (tests that sleep between steps
# scale those sleeps by the same factor), so long fuzz runs keep their proportions in a fraction of the time
SIM_SPEED = float(os.environ.get("SIM_SPEED", "1"))


# ---- stand-ins ------------------------------------------------------------------
class FakePlayer:
    def __init__(self, *a, **k):
        self.chunks, self.events, self.lock = [], [], threading.Lock()
        self.pace = False          # True: block like a real device, so a cancel lands mid-speech

    def feed(self, data, onDone=None):
        if self.pace and data:
            time.sleep(len(data) / 2 / 44100.0 / SIM_SPEED)
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
# FAKE_PLAYER=wasapi: NVDA's WASAPI player as it behaves (wasapi_player.py): a play clock, onDone only from a later
# feed() or sync() once played, a stop that drops what is pending.  FakePlayer calls onDone inside feed() at once.
if os.environ.get("FAKE_PLAYER") == "wasapi":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from wasapi_player import WasapiPlayer as _Wasapi     # noqa: E402

    class _WasapiFake(_Wasapi):
        def __init__(self, *a, **k):
            super().__init__(*a, speed=SIM_SPEED, **k)

        def keep(self, data):      # FakePlayer's chunks: float arrays (audio_since)
            return np.frombuffer(data, dtype="<i2").astype(float) / 32767

    nvwave.WavePlayer = _WasapiFake
sys.modules["nvwave"] = nvwave
config = types.ModuleType("config")
config.conf = {"audio": {"outputDevice": "default"}, "speech": {"outputDevice": "default"}}
sys.modules["config"] = config
notified = []
notify_hooks = []           # f(position in notified), run on the notifying thread just before it is visible there
_notify_lock = threading.Lock()


class Notifier:
    def __init__(self, name):
        self.name = name

    def notify(self, **kw):
        with _notify_lock:
            for hook in notify_hooks:
                hook(len(notified))
            notified.append((self.name, kw.get("index")))


class _Setting:
    """a driver setting as the drivers declare it: its id (the 0.8 driver, synthDrivers/ssi263.py, reads them)"""

    def __init__(self, id, *a, **k):
        self.id, self.defaultVal, self.configSpec, self.useConfig = id, k.get("defaultVal"), "string()", True


class Base:
    VoiceSetting = staticmethod(lambda: _Setting("voice"))
    RateSetting = staticmethod(lambda: _Setting("rate"))
    PitchSetting = staticmethod(lambda: _Setting("pitch"))
    VolumeSetting = staticmethod(lambda: _Setting("volume"))
    VariantSetting = staticmethod(lambda: _Setting("variant"))
    InflectionSetting = staticmethod(lambda: _Setting("inflection"))

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
asu_ds.BooleanDriverSetting = _Setting
asu_ds.DriverSetting = _Setting
sys.modules["autoSettingsUtils.driverSetting"] = asu_ds
lh = types.ModuleType("logHandler")


class _Log:
    debug_lines = []

    def isEnabledFor(self, level):
        return True

    def debug(self, msg):
        self.debug_lines.append(msg)

    def debugWarning(self, msg, exc_info=False):
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


class LangChangeCommand:
    def __init__(self, lang=None):
        self.lang = lang


cmds.IndexCommand, cmds.PitchCommand = IndexCommand, PitchCommand
cmds.LangChangeCommand = LangChangeCommand
speech.commands = cmds
sys.modules["speech"] = speech
sys.modules["speech.commands"] = cmds

# ---- the real driver -------------------------------------------------------------
_sd = types.ModuleType("synthDrivers")          # NVDA's package, with the add-on's folder on it
_sd.__path__ = [BUILD]
sys.modules["synthDrivers"] = _sd
drv_mod = importlib.import_module("synthDrivers." + {"accent": "accentmini"}.get(WHICH, WHICH))
d = drv_mod.SynthDriver()
# SSI263_FIRMWARE_TYPE: the 0.8 driver (WHICH ssi263) on that firmware type, as NVDA's setting would switch it
if os.environ.get("SSI263_FIRMWARE_TYPE"):
    d._set_firmwareType(os.environ["SSI263_FIRMWARE_TYPE"])


# ---- completion, owned (Astra, Reply 104) -------------------------------------------
# "The queue is empty and a done came since the speak" took the done of the utterance BEFORE: its job was still
# finishing when this one was queued, and an empty queue only means the worker has taken this job, not finished it.
# So every sequence spoken here ends with an IndexCommand of its own (as NVDA's speech manager ends each utterance
# with an index), and an utterance is complete when THAT index has been reached and a done follows it with no other
# utterance's index in between.  Running out of time, or the driver's worker dying, is an error, never a pass.
OWNED_BASE = 900000         # far above the indices tools send themselves (1, 2, 3)
owned = []                  # the tokens, in speak order
spoke_at = {}               # token -> len(notified) when it was spoken


class CompletionError(RuntimeError):
    """An utterance's completion could not be identified: its message starts with the reason, in capitals."""


_speak = d.speak


def _owned_speak(speechSequence):
    tok = OWNED_BASE + len(owned)
    owned.append(tok)
    spoke_at[tok] = len(notified)
    _speak(list(speechSequence) + [IndexCommand(tok)])


d.speak = _owned_speak


def owner_of(pos):
    """The owned token whose index came last before notification pos (None: none since the start)."""
    for i in range(pos - 1, -1, -1):
        n = notified[i]
        if n[0] == "index" and isinstance(n[1], int) and n[1] >= OWNED_BASE:
            return n[1]
    return None


def find_completion(tok):
    """Position in notified of tok's completion: its own index reached, then a done, no other owned index between;
    None while not yet.  Raises MISSING DONE when the next utterance's index comes first."""
    try:
        p = notified.index(("index", tok), spoke_at[tok])
    except ValueError:
        return None
    for q in range(p + 1, len(notified)):
        name, idx = notified[q]
        if name == "done":
            return q
        if name == "index" and isinstance(idx, int) and idx >= OWNED_BASE:
            raise CompletionError("MISSING DONE: index %d reached, then index %d with no done between" % (tok, idx))
    return None


def worker_dead():
    w = getattr(d, "_worker", None)
    return w is not None and not w.is_alive()


def wait_done(tok=None, timeout=20):
    """Block until utterance tok (default: the last spoken) has completed; the position of its done in notified.
    Raises CompletionError naming why not: TIMEOUT, MISSING INDEX, MISSING DONE, WORKER DIED, STALE DONE."""
    tok = owned[-1] if tok is None else tok
    t0 = time.monotonic()
    while True:
        q = find_completion(tok)
        if q is not None:
            if owner_of(q) != tok:      # the rule above guarantees it; kept separate so a broken rule is caught
                raise CompletionError("STALE DONE accepted for index %d: the done at notification %d follows %s"
                                      % (tok, q, "index %s" % owner_of(q) if owner_of(q) else "no owned index"))
            return q
        if worker_dead():
            raise CompletionError("WORKER DIED: the driver's worker thread ended before index %d completed" % tok)
        if time.monotonic() - t0 >= timeout:
            since = notified[spoke_at[tok]:]
            dones = sum(1 for n in since if n[0] == "done")
            if ("index", tok) in since:
                raise CompletionError("MISSING DONE: index %d reached, no done after it in %.1f s" % (tok, timeout))
            if dones:
                raise CompletionError("MISSING INDEX: index %d never reached, though %d done(s) came since it was "
                                      "spoken (%.1f s)" % (tok, dones, timeout))
            raise CompletionError("TIMEOUT: neither index %d nor any done in %.1f s" % (tok, timeout))
        time.sleep(0.005)


last_wait_error = [None]


def wait_idle(timeout=20):
    """The last spoken utterance's own completion (wait_done); False, with the reason in last_wait_error, if none."""
    if not owned:
        return False
    try:
        wait_done(owned[-1], timeout)
        return True
    except CompletionError as e:
        last_wait_error[0] = str(e)
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
