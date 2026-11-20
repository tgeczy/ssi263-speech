"""Say all's lines without a sentence end, through the REAL driver (the built add-on under stand-in NVDA modules).
NVDA sends up to ten such lines as ONE utterance with an index before each.  Given to the unit as a line each, every
line is spoken as a sentence of its own (a listener, 2026-11-19, the Sounding Board: "it pauses after every line.
Most synths don't do that.  It's supposed to send indexes as indexes, not pause there").  So with "Join phrases" on
the driver gives them to the unit as one line and reports each index when playback reaches its place.

    python say_all_flow.py speakout|accent|mockingboard|smartalk [voice]
    SAY_ALL_FLOW_BREAK=1          the driver's FLOW off: each text a line of its own again (must fail "flows")
    SAY_ALL_FLOW_BREAK=estimate   the Braille Lite's own index marks unused, each index estimated (must fail "own")

What is checked, on "1" to "5" as five lines of one utterance:
  flows       the utterance is as long as the same five words given as one text (within SAME_MS): what each line
              cost as a sentence of its own is saved (330 ms on the Sounding Board, 270 on the Accent-mini, 150 on
              the Accent-PC and the SmarTalk, 30 on the Mockingboard, which hardly ends its sentences)
  indexes     all five are reached, in order: the first before any sound; each later one within WORD of its share
              of the audio (its line's place is an estimate, by characters: good to a word or so), and before the
              utterance's own end
  alone       an utterance of one text is as long as without the flow
  capital     a pitch command between two texts keeps them apart: as long as without the flow, every index reached
  join off    with "Join phrases" off each line is a sentence of its own again: nothing is saved
  cancel      a cancel in the line, then the five lines again: all five indexes are reached, in order, each
              within WORD of its share of that utterance, which is whole (within a tenth of its length before:
              where the cancel lands depends on the machine's load, and the unit's next utterance is a few ms
              shorter or longer for it)
  long        ten lines of six words, NVDA's largest chunk (the unit's own lines and the voices' parts are
              shorter than it): all ten indexes reached, in order, each within LINE of its line's start (the lines
              are the same six words, so line k starts at (k - 1) / 10 of the audio), and no longer than the ten
              lines as sentences of their own
  blank       a blank line in say all: two indexes with no text between them.  Both are reached, at the same
              place, in order, and the utterance is as long as without the second one (said twice first: some
              units say a text a little differently straight after another one; the SmarTalk by 90 ms)
  own         (the Braille Lite, whose unit has index marks of its own: a Ctrl-F, echoed when speech reaches it)
              "w w w w" and "it is on this" as two lines: the second line's index comes where the unit says,
              after the four long letter names -- past OWN_AT of the audio -- not at the line's share of the
              characters (0.36)
Two utterances of the same text are not the same to the sample on these units (the chip's phoneme timer runs on
between them, in steps of about 4 ms): lengths are held to TICK_MS.

On a Braille Lite voice whose release was not measured for its own index marks (blazie_es, blazie_de), "gated" takes
"own"'s place: no Ctrl-F of a mark is sent to the unit, counted in the host's own count of them.  Control
SAY_ALL_FLOW_BREAK=gate: the driver told every release's marks are usable -- "gated" must fail.
"""
import importlib
import os
import sys
import threading
import time
import types

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools import repo_paths             # noqa: E402

WHICH = sys.argv[1] if len(sys.argv) > 1 else "speakout"
VOICE = sys.argv[2] if len(sys.argv) > 2 else None
BREAK = os.environ.get("SAY_ALL_FLOW_BREAK", "") == "1"
ESTIMATE = os.environ.get("SAY_ALL_FLOW_BREAK", "") == "estimate"
GATE = os.environ.get("SAY_ALL_FLOW_BREAK", "") == "gate"
OWN_VOICES = (None, "blazie")     # the Braille Lite voices whose release was measured for its own index marks
# SSI263_SYNTH_DRIVERS: another folder's drivers (the 0.8 add-on's units: synthDrivers/_ssi263_unified)
BUILD = os.environ.get("SSI263_SYNTH_DRIVERS") or repo_paths.synth_drivers(WHICH)
TICK_MS = 6.0
BLOCK_MS = 30.0                   # a render block: the silence between two texts of one job is whole blocks, one more
                                  # or less by where the unit is when the next text reaches it (the capital sequence
                                  # six times in a row on the Speak-Out: its sound ending at 1046-1058 ms, once 1079)
SAME_MS = 15.0                    # two utterances of five words on these units: up to 13 ms apart (the SmarTalk)
if VOICE == "blazie_de":            # the German Braille Lite says the same two lines 1,444 to 1,473 ms long from
    SAME_MS = 35.0                # one run to the next, with no mark sent to it (four runs)
WORD = 0.15                       # of the utterance: how far an index may be from its share
LINE = 0.06                       # of a ten-line utterance: how far an index may be from its line's start
OWN_AT = 0.5                      # of the utterance: where "it is on this" starts after "w w w w", at the least
END = 900000                      # this test's own index, after each utterance: its completion


# ---- stand-ins ---------------------------------------------------------------------------------------------------
class Player:
    """every block kept; a callback runs at once, where the audio fed so far ends"""
    current = None

    def __init__(self, *a, **k):
        self.chunks, self.lock = [], threading.Lock()
        Player.current = self

    def fed(self):
        with self.lock:
            return sum(len(c) for c in self.chunks)

    def feed(self, data, onDone=None):
        if data:
            with self.lock:
                self.chunks.append(np.frombuffer(data, dtype="<i2").astype(float) / 32767)
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
    for k, v in attrs.items():
        setattr(m, k, v)
    sys.modules[name] = m
    return m


notified = []                     # (name, index, the samples fed when it came)


class Notifier:
    def __init__(self, name):
        self.name = name

    def notify(self, **kw):
        notified.append((self.name, kw.get("index"), Player.current.fed() if Player.current else 0))


class _Setting:
    def __init__(self, id, *a, **k):
        self.id, self.defaultVal = id, k.get("defaultVal")


class Base:
    VoiceSetting = staticmethod(lambda: _Setting("voice"))
    RateSetting = staticmethod(lambda: _Setting("rate"))
    PitchSetting = staticmethod(lambda: _Setting("pitch"))
    VolumeSetting = staticmethod(lambda: _Setting("volume"))
    VariantSetting = staticmethod(lambda: _Setting("variant"))
    InflectionSetting = staticmethod(lambda: _Setting("inflection"))

    def __init__(self):
        pass


class _Log:
    def isEnabledFor(self, level):
        return False

    def debug(self, msg):
        pass

    def debugWarning(self, msg, exc_info=False):
        pass

    def warning(self, msg):
        print("LOG WARNING:", msg)

    def error(self, msg, exc_info=False):
        print("LOG ERROR:", msg)
        if exc_info:
            import traceback
            traceback.print_exc()


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
module("autoSettingsUtils.driverSetting", BooleanDriverSetting=_Setting, DriverSetting=_Setting)
module("logHandler", log=_Log())
_cmds = module("speech.commands", IndexCommand=IndexCommand, PitchCommand=PitchCommand,
               LangChangeCommand=LangChangeCommand)
module("speech", commands=_cmds)

# ---- the real driver ---------------------------------------------------------------------------------------------
_sd = types.ModuleType("synthDrivers")          # NVDA's package, with the add-on's folder on it
_sd.__path__ = [BUILD]
sys.modules["synthDrivers"] = _sd
drv_mod = importlib.import_module("synthDrivers." + {"accent": "accentmini"}.get(WHICH, WHICH))
if ESTIMATE:
    drv_mod.OWN_MARKS = False
if GATE and hasattr(drv_mod, "_own_marks"):
    drv_mod._own_marks = lambda firmware: True     # the control: every echoing unit's marks taken for usable
d = drv_mod.SynthDriver()
failures = 0
ends = [0]


def check(name, ok, detail):
    global failures
    print("%-4s %-8s %s" % ("ok" if ok else "FAIL", name, detail))
    failures += not ok


def say(seq, timeout=30.0):
    """the utterance's audio, and where in it each of its indexes was reached (samples)"""
    player = d._player
    start, first = player.fed(), len(notified)
    ends[0] += 1
    mine = END + ends[0]
    d.speak(list(seq) + [IndexCommand(mine)])
    t0 = time.monotonic()
    while True:
        since = notified[first:]
        at = [k for k, n in enumerate(since) if n[0] == "index" and n[1] == mine]
        if at and any(n[0] == "done" for n in since[at[0] + 1:]):
            break
        if time.monotonic() - t0 > timeout:
            raise RuntimeError("the utterance did not complete in %.0f s: %s" % (timeout, since[-4:]))
        time.sleep(0.005)
    with player.lock:
        audio = np.concatenate(player.chunks) if player.chunks else np.zeros(0)
    reached = {n[1]: n[2] - start for n in notified[first:] if n[0] == "index" and n[1] < END}
    return audio[start:], reached


def sound_end(audio):
    """the last sample that sounds (a fiftieth of the loudest), plus one: an utterance's length without the silence
    after it, which is a 30 ms block more or less from one utterance to the next"""
    if not len(audio):
        return 0
    loud = np.nonzero(np.abs(audio) > np.max(np.abs(audio)) / 50.0)[0]
    return int(loud[-1]) + 1 if len(loud) else 0


def flow(on):
    """the driver with its lines joined, or without (each text a line of its own)"""
    drv_mod.FLOW = bool(on) and not BREAK


def main():
    booted = getattr(d, "booted", None)
    if booted is not None and (not booted.wait(60) or getattr(d, "bootError", None)):
        print("FAIL the unit did not boot: %s" % getattr(d, "bootError", None))
        return 1
    if VOICE:
        d._set_voice(VOICE)
        if d._get_voice() != VOICE:
            print("skip: the %s's files are not in the built add-on" % VOICE)
            return 0
    flow(True)
    say(["ready"])                        # the unit warm (and the other voice booted): a first utterance has its own lead
    rate = float(d._out_rate)
    ms = lambda n: n * 1000.0 / rate      # noqa: E731
    lines = ["1", "2", "3", "4", "5"]
    seq = []
    for k, t in enumerate(lines):
        seq += [IndexCommand(k + 1), t]
    audio, reached = say(seq)
    one, _ = say([" ".join(lines)])
    flow(False)
    apart, _ = say(seq)
    flow(True)
    cost = ms(len(apart)) - ms(len(one))          # what the five lines cost as sentences of their own
    check("flows", abs(ms(len(audio)) - ms(len(one))) <= SAME_MS and cost > 2 * SAME_MS,
          "five lines %.0f ms, the same words as one text %.0f ms, each line a sentence of its own %.0f ms" % (
              ms(len(audio)), ms(len(one)), ms(len(apart))))
    total = float(len(audio)) or 1.0
    order = [reached.get(k) for k in range(1, 6)]
    far = [k for k in range(2, 6) if order[k - 1] is None or abs(order[k - 1] / total - (k - 1) / 5.0) > WORD]
    rising = all(x is not None for x in order) and all(order[k] < order[k + 1] for k in range(4))
    check("indexes", rising and order[0] == 0 and not far and order[4] < total,
          "reached at %s ms of %.0f; in order %s; far from their share: %s" % (
              " ".join("-" if x is None else "%.0f" % ms(x) for x in order), ms(total), rising, far or "none"))

    a1, _ = say(["one two three"])
    flow(False)
    r1, _ = say(["one two three"])
    flow(True)
    check("alone", abs(ms(len(a1)) - ms(len(r1))) <= 3 * TICK_MS, "one text: %.0f ms, without the flow %.0f ms" % (
        ms(len(a1)), ms(len(r1))))

    cap = [IndexCommand(1), "see", PitchCommand(20), "B", PitchCommand(0), IndexCommand(2), "and so on"]
    a2, reached2 = say(cap)
    flow(False)
    r2, _ = say(cap)
    flow(True)
    check("capital", abs(ms(sound_end(a2)) - ms(sound_end(r2))) <= 3 * TICK_MS + BLOCK_MS and sorted(reached2) == [1, 2],
          "a capital between two texts: its sound ends at %.0f ms, without the flow at %.0f ms; indexes %s" % (
              ms(sound_end(a2)), ms(sound_end(r2)), sorted(reached2)))

    d._set_joinPhrases(False)
    off, reached3 = say(seq)
    d._set_joinPhrases(True)
    check("join off", abs(ms(len(off)) - ms(len(apart))) <= SAME_MS and sorted(reached3) == [1, 2, 3, 4, 5],
          "\"Join phrases\" off: %.0f ms (on: %.0f ms); indexes %s" % (ms(len(off)), ms(len(audio)), sorted(reached3)))

    d.speak(seq + [IndexCommand(7)])
    time.sleep(0.02)
    d.cancel()
    after, reached5 = say(seq)
    again = [reached5.get(k) for k in range(1, 6)]
    t5 = float(len(after)) or 1.0
    far5 = [k + 1 for k in range(1, 5) if again[k] is None or abs(again[k] / t5 - k / 5.0) > WORD]
    check("cancel", all(x is not None for x in again) and all(again[k] < again[k + 1] for k in range(4))
          and not far5 and again[0] == 0 and abs(len(after) - len(audio)) <= 0.1 * len(audio),
          "the five lines after a cancel in them: %.0f ms (before: %.0f ms), indexes at %s ms" % (
              ms(len(after)), ms(len(audio)), " ".join("-" if x is None else "%.0f" % ms(x) for x in again)))

    ten = []
    for k in range(10):
        ten += [IndexCommand(k + 1), "this is one more plain line"]
    a10, r10 = say(ten, timeout=90.0)
    flow(False)
    apart10, _ = say(ten, timeout=90.0)
    flow(True)
    at10 = [r10.get(k) for k in range(1, 11)]
    t10 = float(len(a10)) or 1.0
    off = [k + 1 for k in range(1, 10) if at10[k] is None or abs(at10[k] / t10 - k / 10.0) > LINE]
    ordered = all(x is not None for x in at10) and all(at10[k] <= at10[k + 1] for k in range(9))
    check("long", ordered and not off and at10[0] == 0 and ms(len(a10)) <= ms(len(apart10)) + SAME_MS,
          "ten lines %.0f ms (as sentences of their own %.0f ms); indexes in order %s; off their line's start: %s" % (
              ms(len(a10)), ms(len(apart10)), ordered, off or "none"))

    say([IndexCommand(1), "one two", IndexCommand(2), "three four"])
    plain2, rp = say([IndexCommand(1), "one two", IndexCommand(2), "three four"])
    blank, rb = say([IndexCommand(1), "one two", IndexCommand(2), IndexCommand(3), "three four"])
    same_place = rb.get(2) is not None and rb.get(3) is not None and abs(ms(rb[3]) - ms(rb[2])) <= 35.0
    check("blank", sorted(rb) == [1, 2, 3] and same_place and rb[2] <= rb[3]
          and abs(ms(len(blank)) - ms(len(plain2))) <= SAME_MS,
          "two indexes with no text between: reached at %s and %s ms; %.0f ms, with one index %.0f ms" % (
              "-" if rb.get(2) is None else "%.0f" % ms(rb[2]), "-" if rb.get(3) is None else "%.0f" % ms(rb[3]),
              ms(len(blank)), ms(len(plain2))))
    if WHICH == "blazie":
        long_short = [IndexCommand(1), "w w w w", IndexCommand(2), "it is on this"]
        unit = d._units.get(d._get_voice())
        # the Ctrl-Fs the unit is sent for the utterance, against the same with the unit's own marks unused: the
        # difference is the marks the driver gave it
        keep = drv_mod.OWN_MARKS
        drv_mod.OWN_MARKS = False
        sent = unit.geti("sent_f")
        say(long_short)
        plain = unit.geti("sent_f") - sent
        drv_mod.OWN_MARKS = keep
        sent = unit.geti("sent_f")
        a3, reached4 = say(long_short)
        used = unit.geti("sent_f") - sent - plain
        at = reached4.get(2, 0) / (float(len(a3)) or 1.0)
        if VOICE in OWN_VOICES:
            check("own", sorted(reached4) == [1, 2] and at >= OWN_AT and reached4.get(2, 0) < len(a3) and used == 1,
                  "\"w w w w\" then \"it is on this\": the second line's index at %.2f of the audio (its share of "
                  "the characters: 0.36), %d Ctrl-F of its own sent for it" % (at, used))
        else:
            # a release whose own marks change its speech (the Spanish unit says more phonemes, the German one changes a
            # phoneme: bl_languages.txt's marks column): no Ctrl-F of a mark reaches it, the indexes estimated
            check("gated", sorted(reached4) == [1, 2] and used == 0 and reached4.get(2, 0) < len(a3),
                  "this unit's own marks are not used: %d Ctrl-F sent for the index (0 wanted), the index at %.2f "
                  "of the audio" % (used, at))
    if WHICH in ("speakout", "smartalk", "accent"):
        # the Speak-Out's and the Sounding Board's own index marks (so_voice.h): the second line's index where the box says speech is, not
        # at its share of the characters; and the line as long as without them (the box's speech is the same)
        long_short = [IndexCommand(1), "w w w w", IndexCommand(2), "it is on this"]
        keep = drv_mod.OWN_MARKS
        drv_mod.OWN_MARKS = False
        say(long_short)
        est, _ = say(long_short)
        drv_mod.OWN_MARKS = keep
        a3, reached4 = say(long_short)
        at = reached4.get(2, 0) / (float(len(a3)) or 1.0)
        check("own", sorted(reached4) == [1, 2] and at >= OWN_AT and reached4.get(2, 0) < len(a3)
              and abs(ms(sound_end(a3)) - ms(sound_end(est))) <= SAME_MS + BLOCK_MS,
              "\"w w w w\" then \"it is on this\": the second line's index at %.2f of the audio (its share of "
              "the characters: 0.36); its sound ends at %.0f ms, with the indexes estimated at %.0f ms" % (
                  at, ms(sound_end(a3)), ms(sound_end(est))))
    print("say all flow: %s" % ("all ok" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        code = main()
    except Exception as e:
        print("FAIL %s" % e)
        code = 1
    sys.stdout.flush()
    os._exit(code)
