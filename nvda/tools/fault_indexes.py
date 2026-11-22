"""A unit that faults in the middle of an utterance must not leave NVDA waiting (issue #33).

When an emulated unit fails while speaking (the V40 faulting, the Accent's watchdog, the SmarTalk or the 6502 not
coming back, a native error), the driver reboots it and says it is done.  The indexes after the fault -- among them
the last one, which NVDA waits for before it says anything more -- were never reported, and the closing idle() was
skipped, so NVDA went quiet until the next key press (and other programs' audio stayed ducked).

Each driver through the stand-in NVDA on the WASAPI stand-in (FAKE_PLAYER=wasapi): the unit is made to fail at the
text of an utterance -- its say() raises once, as a fault would --
  fault         the utterance still completes: every index in it is reported, before its done
  recovers      the next utterance, on the rebooted unit, is spoken and completes
  cancelled     a cancel while the fault's recovery waits for the fed audio (idle()): none of that utterance's owed
                indexes reported after it -- NVDA wants none after a cancel (a review: the cancel was looked at only
                before the wait)

    python fault_indexes.py speakout|accent|blazie|mockingboard|smartalk [voice]      exit 1 on any failure
    FAULT_INDEXES_BREAK=1        the drivers' report of what a fault left owed taken away -- must fail: fault
    FAULT_INDEXES_BREAK=cancel   the cancel not looked at again after the wait             -- must fail: cancelled
"""
import threading
import os
import sys
import time

os.environ["FAKE_PLAYER"] = "wasapi"
WHICH = sys.argv[1]
VOICE = sys.argv[2] if len(sys.argv) > 2 else None
sys.argv = sys.argv[:2]
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
bad = 0

_error = lh.log.error


def _quiet_error(msg, exc_info=False):
    """the driver logs the fault it met with its traceback: the one this test made is named, not printed (a
    control's output with a traceback in it reads as a crash)"""
    kind, value = sys.exc_info()[:2]
    if exc_info and value is not None and "a fault made by the test" in str(value):
        print("LOG ERROR: %s (the fault this test made)" % msg)
        return
    _error(msg, exc_info=exc_info)


lh.log.error = _quiet_error
feed_mod = getattr(drv_mod, "_first", None)
if feed_mod is None or not hasattr(feed_mod, "owed"):
    print("FAIL the driver's ssi263_feed module has no owed() (an add-on built before it?)")
    sys.exit(1)
BREAK = os.environ.get("FAULT_INDEXES_BREAK", "")
if BREAK == "cancel":
    feed_mod.CANCEL_AFTER_IDLE = False
elif BREAK:
    feed_mod.REPORT_OWED = False
if VOICE:
    d._set_voice(VOICE)


def check(name, ok, detail):
    global bad
    print("%-4s %-9s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def unit():
    return getattr(d, "_unit", None) if WHICH == "blazie" else getattr(d, "_box", None)


def arm_fault():
    """the unit's next say() raises, once"""
    u = unit()
    real = u.say
    state = {"armed": True}

    def say(*a, **k):
        if state["armed"]:
            state["armed"] = False
            raise RuntimeError("a fault made by the test")
        return real(*a, **k)
    u.say = say
    return u


d.speak(["Settle."])
wait_done(timeout=20)
before = unit()
arm_fault()
n0 = len(notified)
d.speak([IndexCommand(1), "These words meet the fault.", IndexCommand(2)])
try:
    wait_done(timeout=5)
    ok = True
except CompletionError as e:
    ok = str(e).split(":")[0]
got = [i for name, i in notified[n0:] if name == "index" and isinstance(i, int) and i < OWNED_BASE]
check("fault", ok is True and got == [1, 2], "the utterance that met a fault: %s; its indexes %s" % (
    "completed" if ok is True else ok, got))
n_chunks = len(d._player.chunks)
d.speak(["The unit was rebooted, and speaks again."])
try:
    wait_done(timeout=20)
    ok = True
except CompletionError as e:
    ok = str(e).split(":")[0]
heard = sum(len(c) for c in d._player.chunks[n_chunks:])
check("recovers", ok is True and heard > 4000 and unit() is not before, "the next utterance: %s, %d samples; %s" % (
    "completed" if ok is True else ok, heard, "a new unit" if unit() is not before else "the SAME unit"))

# ---- cancelled: the recovery's idle() held until the test has cancelled; then nothing more of that utterance
# (not with the report taken away: there is no recovery to wait in, and the control names fault's failure alone)
if BREAK and BREAK != "cancel":
    print("skip cancelled  the owed indexes are not reported under this control")
    print("fault indexes: %s" % ("%d FAILED" % bad if bad else "all ok"))
    sys.stdout.flush()
    os._exit(1 if bad else 0)
time.sleep(0.5)
player = d._player
real_idle = player.idle
in_idle, release = threading.Event(), threading.Event()


def held_idle(*a, **k):
    in_idle.set()
    release.wait(10)
    return real_idle(*a, **k)


player.idle = held_idle
arm_fault()
n0 = len(notified)
d.speak([IndexCommand(3), "These words meet the fault, and then a cancel.", IndexCommand(4)])
waited = in_idle.wait(10)
n_cancel = len(notified)
d.cancel()
release.set()
time.sleep(1.5)
player.idle = real_idle
after = [i for name, i in notified[n_cancel:] if name == "index" and isinstance(i, int) and i < OWNED_BASE]
check("cancelled", waited and not after, "the recovery %s; indexes reported after the cancel: %s" % (
    "waited in idle()" if waited else "NEVER reached idle()", after or "none"))
print("fault indexes: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
os._exit(1 if bad else 0)
