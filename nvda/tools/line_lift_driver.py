"""The line-start lift through the REAL driver (the built add-on under stand-in NVDA modules): with "Lift line starts"
on, speech that follows a cancel (moving by line while speech goes on), speech into a pause (moving by line in
silence) and the first utterance are lifted -- their first slots at the pitch byte + 1Bh, as the unit's note-taking
mode lifts a line moved to (src/csrc/blazie/test_line_lift.c holds that to the emulated unit) -- and speech queued
behind other speech (say-all) is not; with the setting off nothing is.  A device-paced player (each block blocks for
its own duration), wall-clock pauses.

    python line_lift_driver.py
    LINE_LIFT_BREAK=noarm   the driver's request never reaches the unit (must fail the lifted cases)
"""
import importlib
import os
import sys
import threading
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools import repo_paths             # noqa: E402

BUILD = repo_paths.synth_drivers("blazie")
BREAK = os.environ.get("LINE_LIFT_BREAK", "")


# ---- stand-ins ---------------------------------------------------------------------------------------------------
class Player:
    def __init__(self, *a, samplesPerSec=22050, **k):
        self.rate = samplesPerSec

    def feed(self, data, onDone=None):
        if data:
            time.sleep(len(data) / 2.0 / self.rate)    # a device: a block blocks for its own duration
        if onDone:
            onDone()

    def stop(self):
        pass

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


notified = []
_lock = threading.Lock()


class Notifier:
    def __init__(self, name):
        self.name = name

    def notify(self, **kw):
        with _lock:
            notified.append((self.name, kw.get("index")))


class Base:
    VoiceSetting = RateSetting = PitchSetting = VolumeSetting = VariantSetting = InflectionSetting = \
        staticmethod(lambda: None)

    def __init__(self):
        pass


class Log:
    def isEnabledFor(self, level):
        return False

    def debug(self, msg):
        pass

    def warning(self, msg, exc_info=False):
        print("LOG WARNING:", msg)

    def error(self, msg, exc_info=False):
        print("LOG ERROR:", msg)


class IndexCommand:
    def __init__(self, index):
        self.index = index


class PitchCommand:
    def __init__(self, offset=0):
        self.offset = offset


class LangChangeCommand:
    def __init__(self, lang=None):
        self.lang = lang


module("nvwave", WavePlayer=Player)
module("config", conf={"audio": {"outputDevice": "default"}, "speech": {"outputDevice": "default"}})
module("synthDriverHandler", SynthDriver=Base, VoiceInfo=lambda *a: a, synthIndexReached=Notifier("index"),
       synthDoneSpeaking=Notifier("done"))
module("autoSettingsUtils")
module("autoSettingsUtils.utils", StringParameterInfo=lambda *a: a)
module("autoSettingsUtils.driverSetting", BooleanDriverSetting=lambda *a, **k: None, DriverSetting=lambda *a, **k: None)
module("logHandler", log=Log())
cmds = module("speech.commands", IndexCommand=IndexCommand, PitchCommand=PitchCommand,
              LangChangeCommand=LangChangeCommand)
module("speech", commands=cmds)
module("synthDrivers", __path__=[BUILD])
drv = importlib.import_module("synthDrivers.blazie")
NB = drv.NativeBlazie
if NB is None:
    sys.exit("the built add-on has no in-process unit (bl.dll)")
if not hasattr(NB, "line_lift"):
    print("FAIL the built add-on's host has no line_lift\nline lift: FAILED")
    sys.exit(1)

# ---- every write and say of the unit, in order -------------------------------------------------------------------
LOG = []                 # ("w", reg, val) | ("say", text)
_say = NB.say
_lift = NB.line_lift


def say(self, text):
    LOG.append(("say", " ".join(text) if isinstance(text, list) else text))
    return _say(self, text)


NB.say = say
if BREAK == "noarm":
    NB.line_lift = property(_lift.fget, lambda self, v: None)
_boot = drv.SynthDriver._boot


def boot(self, voice="blazie"):
    u = _boot(self, voice)
    u.on_write = lambda t, r, v: LOG.append(("w", r, v))
    return u


drv.SynthDriver._boot = boot


def lifted_says():
    """{said text: whether a pitch byte 1Bh above the utterance's first one was written before the next say}"""
    out, cur, first = {}, None, None
    for e in LOG:
        if e[0] == "say":
            cur, first = e[1], None
            out[cur] = False
        elif e[0] == "w" and cur is not None and e[1] == 1:
            if first is None:
                first = e[2]
            elif e[2] == (first + 0x1B) & 0xFF:
                out[cur] = True
    return out


def wait_done(index, limit=20.0):
    t0 = time.monotonic()
    while ("index", index) not in notified or notified[-1][0] != "done":
        if time.monotonic() - t0 > limit:
            sys.exit("FAIL index %d never done" % index)
        time.sleep(0.01)


def main():
    d = drv.SynthDriver()
    t0 = time.monotonic()
    while d._unit is None and time.monotonic() - t0 < 20:
        time.sleep(0.05)
    d._set_lineLift(True)
    cases = []          # (name, text, lifted wanted)
    # the first utterance: lifted
    d.speak(["Writing functions", IndexCommand(1)])
    wait_done(1)
    cases.append(("first", "Writing functions", True))
    # say-all: two lines queued at once, the second behind the first
    d.speak(["Backspace b chord", IndexCommand(2)])
    d.speak(["Carriage return forty six chord", IndexCommand(3)])
    wait_done(3)
    cases.append(("queued", "Carriage return forty six chord", False))
    # moving by line while speaking: a cancel, then the line
    d.speak(["Insert i chord, text, e chord", IndexCommand(4)])
    time.sleep(0.3)
    d.cancel()
    d.speak(["Delete d chord", IndexCommand(5)])
    wait_done(5)
    cases.append(("cancel", "Delete d chord", True))
    # moving by line in a pause: no cancel, a second of silence first
    time.sleep(1.0)
    d.speak(["Paste three four six chord", IndexCommand(6)])
    wait_done(6)
    cases.append(("pause", "Paste three four six chord", True))
    # off: neither a cancel nor a pause lifts
    d._set_lineLift(False)
    d.cancel()
    d.speak(["Copy one two six chord", IndexCommand(7)])
    wait_done(7)
    time.sleep(1.0)
    d.speak(["Set mark m chord", IndexCommand(8)])
    wait_done(8)
    cases += [("off_cancel", "Copy one two six chord", False), ("off_pause", "Set mark m chord", False)]
    d.terminate()
    got = lifted_says()
    bad = 0
    for name, text, want in cases:
        ok = got.get(text) == want
        bad += not ok
        print("%-4s %s: %r %s (wanted %s)" % ("ok" if ok else "FAIL", name, text,
                                              "lifted" if got.get(text) else "not lifted", "lifted" if want else "not"))
    print("line lift: %s" % ("all passed" if not bad else "%d FAILED" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
