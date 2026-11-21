"""A setting changed just before a cancel must still reach the unit (issue #35).

At the start of each utterance the voice sends the unit the settings that changed (rate, pitch, ...) and records them
as sent.  The unit reads its serial line only while the voice renders.  An utterance cancelled before its first block
had been rendered took the settings' bytes with it (the cancel drops what the unit has not read), but the record said
they were sent, so the next utterance sent nothing: NVDA showed the new rate and the unit kept the old one, until that
setting changed again.

Each driver through the stand-in NVDA: the time a fixed phrase takes is measured at a slow and a fast rate; then the
rate is moved from slow to fast and the next utterance is cancelled the moment its text has been handed to the unit,
before any audio -- and the phrase must take the fast time.
  baseline      the two rates do differ (the test itself works)
  after cancel  the phrase after the cancelled utterance is spoken at the new rate

    python cancel_settings.py speakout|accent|blazie|mockingboard|smartalk [voice]      exit 1 on any failure
    SSV_SETTINGS_BREAK=1   the C voice's record of what was sent kept across such a cancel (so_voice.c,
                           as_voice.c; read by the library, so set before this starts)   -- must fail: after cancel
"""
import os
import sys
import time

WHICH = sys.argv[1]
VOICE = sys.argv[2] if len(sys.argv) > 2 else None
sys.argv = sys.argv[:2]
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
PHRASE = "One two three four five six seven."
SLOW, FAST = "20", "80"
bad = 0
if VOICE:
    d._set_voice(VOICE)


def check(name, ok, detail):
    global bad
    print("%-4s %-12s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def phrase_ms():
    c0 = len(d._player.chunks)
    d.speak([PHRASE])
    wait_done(timeout=30)
    return 1000.0 * sum(len(c) for c in d._player.chunks[c0:]) / float(getattr(d, "_out_rate", 44100))


def unit():
    return getattr(d, "_unit", None) if WHICH == "blazie" else getattr(d, "_box", None)


d.speak(["Settle."])
wait_done(timeout=20)
d._set_rate(SLOW)
slow = phrase_ms()
d._set_rate(FAST)
fast = phrase_ms()
check("baseline", slow > 1.3 * fast, "the phrase at rate %s: %.0f ms, at rate %s: %.0f ms" % (SLOW, slow, FAST, fast))

d._set_rate(SLOW)
phrase_ms()                                        # the unit now at the slow rate, and the voice's record with it
u = unit()
real_say = u.say


def say_then_cancel(*a, **k):
    """the text handed over, then NVDA's cancel at once: before any of the utterance is rendered"""
    r = real_say(*a, **k)
    u.say = real_say
    d.cancel()
    return r


d._set_rate(FAST)
u.say = say_then_cancel
d.speak(["Rate eighty."])
time.sleep(1.0)
after = phrase_ms()
near = abs(after - fast) < 0.25 * fast
check("after cancel", near, "the phrase after a cancelled change to rate %s: %.0f ms (rate %s took %.0f ms, rate %s %.0f "
      "ms)" % (FAST, after, FAST, fast, SLOW, slow))
print("cancel settings: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
os._exit(1 if bad else 0)
