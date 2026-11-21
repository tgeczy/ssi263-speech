"""An utterance that makes no sound must still report its indexes and its done (issue #32).

NVDA's WASAPI player calls an onDone only from a later feed() or sync(), and sync() does nothing unless the stream is
playing.  After a stop (every cancel), or once NVDA's 10 s idle check has closed the stream, the stream is stopped
until audio is fed.  A driver gives each index as feed(b"", onDone=...); for an utterance that feeds no audio at all
-- text NVDA left as spaces, an index alone -- nothing starts the stream, the indexes are never called, and NVDA,
which waits for an utterance's last index before it says anything more, goes quiet until the next key press.

Each driver, through the stand-in NVDA on FAKE_PLAYER=wasapi (wasapi_player.py: the player as NVDA 2026.2 is):
  sound         after a cancel, a word: completes (the test itself works)
  spaces        after a cancel, text that is only spaces, between two indexes: both indexes, the done
  index only    after a cancel, an utterance that is only an index
  after idle    after NVDA's idle check has closed the stream, spaces again
  then sound    a word after them still completes, and is heard

    python silent_indexes.py speakout|accent|blazie|mockingboard|smartalk [voice]      exit 1 on any failure
    SILENT_INDEXES_BREAK=1   the first-feed piece not starting a silent stream (ssi263_feed.KICK off)
                                                                        -- must fail: spaces, index only, after idle
"""
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
WAIT_S = 4.0
bad = 0

feed_mod = getattr(drv_mod, "_first", None)
if feed_mod is None:
    print("FAIL the driver has no ssi263_feed module (an add-on built before it?)")
    sys.exit(1)
if os.environ.get("SILENT_INDEXES_BREAK"):
    feed_mod.KICK = False
if VOICE:
    d._set_voice(VOICE)


def check(name, ok, detail):
    global bad
    print("%-4s %-11s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def say(seq):
    """speak seq; (completed, the indexes below the harness's own reported for it, ms taken)"""
    n0 = len(notified)
    t0 = time.monotonic()
    d.speak(seq)
    try:
        wait_done(timeout=WAIT_S)
        ok = True
    except CompletionError as e:
        ok = str(e).split(":")[0]
    got = [i for name, i in notified[n0:] if name == "index" and isinstance(i, int) and i < OWNED_BASE]
    return ok, got, 1000.0 * (time.monotonic() - t0)


def cancel_after_sound():
    d.speak(["Hello there, this is a longer sentence to be cut."])
    time.sleep(0.3)
    d.cancel()
    time.sleep(0.2)


d.speak(["Settle."])
wait_done(timeout=20)

cancel_after_sound()
ok, got, ms = say(["blank"])
check("sound", ok is True, "a word after a cancel: %s in %.0f ms" % ("completed" if ok is True else ok, ms))

cancel_after_sound()
ok, got, ms = say([IndexCommand(1), "   ", IndexCommand(2)])
check("spaces", ok is True and got == [1, 2], "spaces after a cancel: %s; indexes %s" % (
    "completed in %.0f ms" % ms if ok is True else ok, got))

cancel_after_sound()
ok, got, ms = say([IndexCommand(3)])
check("index only", ok is True and got == [3], "an index alone after a cancel: %s; indexes %s" % (
    "completed in %.0f ms" % ms if ok is True else ok, got))

d.speak(["Settle."])
wait_done(timeout=20)
d._player.idle_check()                        # NVDA's 10 s idle check: the stream closed
ok, got, ms = say([IndexCommand(4), "  ", IndexCommand(5)])
check("after idle", ok is True and got == [4, 5], "spaces after the idle check: %s; indexes %s" % (
    "completed in %.0f ms" % ms if ok is True else ok, got))

n_chunks = len(d._player.chunks)
ok, got, ms = say(["blank"])
heard = sum(len(c) for c in d._player.chunks[n_chunks:])
check("then sound", ok is True and heard > 1000, "a word after them: %s, %d samples fed" % (
    "completed" if ok is True else ok, heard))
print("silent indexes: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
os._exit(1 if bad else 0)
