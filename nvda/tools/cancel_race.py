"""A cancel that lands just after an utterance was handed over must cancel it (issue #34).

NVDA can hand a driver an utterance and cancel it in the same pass of its main thread: a key pressed in the few ms
after say all's index pushed the next chunk, or a key's cancel already queued behind the previous key's speech.  Between
the two the main thread runs only Python, so the driver's worker, waiting on its queue, cannot run.  The drivers'
cancel() set the flag, then stopped the player, then emptied the queue -- and stopping NVDA's player is a call into
its C library that lets other threads run.  The worker woke there, took the utterance, cleared the flag, and spoke it
in full: say all read one more chunk after the key that should have silenced it.

Each driver through the stand-in NVDA, the player's stop() made to give the other threads 150 ms as the real one
can, and the main thread made to keep the interpreter otherwise (a long switch interval), as NVDA's does:
  cancelled     an utterance handed over and cancelled at once is never spoken: none of its indexes, no audio
  then sound    the next utterance is spoken and completes

    python cancel_race.py speakout|accent|blazie|mockingboard|smartalk [voice]      exit 1 on any failure
    CANCEL_RACE_BREAK=1   the drivers' cancel as it was (stop first, no utterance generations)   -- must fail: cancelled
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
bad = 0

if not hasattr(drv_mod, "CANCEL_FIRST"):
    print("FAIL the driver has no CANCEL_FIRST (an add-on built before the fix?)")
    sys.exit(1)
if os.environ.get("CANCEL_RACE_BREAK"):
    drv_mod.CANCEL_FIRST = False
if VOICE:
    d._set_voice(VOICE)


def check(name, ok, detail):
    global bad
    print("%-4s %-10s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


inner = d._player._p if hasattr(d._player, "_p") else d._player      # the player behind ssi263_feed.FirstFeed
real_stop = inner.stop


def slow_stop():
    """NVDA's stop() is a ctypes call into its C library: the other threads run meanwhile"""
    real_stop()
    time.sleep(0.15)


d.speak(["Settle."])
wait_done(timeout=20)
inner.stop = slow_stop
sys.setswitchinterval(1.0)                     # the main thread keeps the interpreter until it blocks
leaks = 0
for k in range(5):
    n0, c0 = len(notified), len(d._player.chunks)
    d.speak([IndexCommand(70 + k), "These words were cancelled before they began, and must never be heard."])
    d.cancel()
    sys.setswitchinterval(0.005)
    time.sleep(1.0)
    sys.setswitchinterval(1.0)
    got = [i for name, i in notified[n0:] if name == "index" and isinstance(i, int) and i < OWNED_BASE]
    audio = sum(len(c) for c in d._player.chunks[c0:])
    leaks += bool(got or audio > 2000)
sys.setswitchinterval(0.005)
inner.stop = real_stop
check("cancelled", leaks == 0, "5 utterances handed over and cancelled at once: %d spoken" % leaks)
c0 = len(d._player.chunks)
d.speak(["And this one is heard."])
try:
    wait_done(timeout=20)
    ok = True
except CompletionError as e:
    ok = str(e).split(":")[0]
heard = sum(len(c) for c in d._player.chunks[c0:])
check("then sound", ok is True and heard > 4000, "the next utterance: %s, %d samples" % (
    "completed" if ok is True else ok, heard))
print("cancel race: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
os._exit(1 if bad else 0)
