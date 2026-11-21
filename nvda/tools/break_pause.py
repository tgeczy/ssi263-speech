"""NVDA's BreakCommand is a pause of its length (issue #71; Tomi: honour it).

NVDA sends BreakCommand(ms) for a pause: "Delayed descriptions for characters on cursor movement" puts a second's
break before a letter's description, so it is heard only when the user stops on the letter (a move on cancels it).
Every driver dropped it: the description came at once, right behind the letter.  Now each driver feeds that much
silence where the break stands (NVDA keeps a sequence's leading silence when a BreakCommand puts it there).

Each driver through the stand-in NVDA:
  pause     "Hello." said plain, then after BreakCommand(1000): the second is a second longer, and that second is
            silence, at its start
  mid       "Hello.", BreakCommand(1000), "Hello.": the first said to its end, then the second's silence in one
            stretch, then the second
  cancel    BreakCommand(5000) before "Hello.", cancelled 0.4 s in (the player paced like a device): no more than
            the piece in hand is fed after the cancel -- a move on to the next letter is not held up by the rest of
            the pause

    python break_pause.py blazie|speakout|accent|mockingboard|smartalk       exit 1 on any failure
    BREAK_PAUSE_BREAK=1         ssi263_flow.pause feeds nothing: the break dropped, as it was -- must fail: pause, mid
    BREAK_PAUSE_BREAK=nocancel  the pause fed to its end through a cancel -- must fail: cancel
"""
import os
import sys
import time

WHICH = sys.argv[1]
sys.argv = sys.argv[:2]
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)
import numpy as np  # noqa: E402

TEXT = "Hello."
failures = 0


def report(name, ok, detail):
    global failures
    print("%-4s %-8s %s: %s" % ("ok" if ok else "FAIL", name, WHICH, detail))
    failures += not ok


def fed():
    with d._player.lock:
        return sum(len(c) for c in d._player.chunks)


def audio_from(n0):
    with d._player.lock:
        y = np.concatenate(d._player.chunks) if d._player.chunks else np.zeros(0)
    return y[n0:]


def said(seq):
    """the audio fed for one utterance, from the first feed after the speak"""
    n0 = fed()
    d.speak(seq)                                  # (the harness adds an index of its own, and waits for it)
    wait_done(timeout=30)
    time.sleep(0.3)
    return audio_from(n0)


def longest_quiet(y):
    """the longest run of silent samples, in seconds"""
    best = run = 0
    for v in np.abs(y) < 1e-4:
        run = run + 1 if v else 0
        best = max(best, run)
    return best / rate


rate = float(d._out_rate)
plain = said([TEXT])
paused = said([cmds.BreakCommand(1000), TEXT])
extra = (len(paused) - len(plain)) / rate
head = paused[:int(0.95 * rate)]
quiet = len(head) > 0 and float(np.abs(head).max()) < 1e-4
report("pause", 0.9 <= extra <= 1.1 and quiet, "%.2f s plain, %.2f s after a 1 s BreakCommand (%.2f s more); its "
       "first 0.95 s %s" % (len(plain) / rate, len(paused) / rate, extra, "silent" if quiet else
                            "NOT silent (peak %.4f)" % (float(np.abs(head).max()) if len(head) else 0.0)))

two = said([TEXT, cmds.BreakCommand(1000), TEXT])
gap = longest_quiet(two)
report("mid", gap >= 0.95, "text, a 1 s break, text: %.2f s in all, its longest silence %.2f s" % (len(two) / rate, gap))

# feed blocks as a device's does, so the cancel lands mid-pause.  The stand-in player under the drivers' FirstFeed
# (ssi263_feed.py): an attribute set on the wrapper never reaches it.  It paces as if at 44.1 kHz, so a driver at a
# lower rate is fed faster than it plays: what counts is what is fed after the cancel
player = getattr(d._player, "_p", d._player)
player.pace = True
d.speak([cmds.BreakCommand(5000), TEXT])
time.sleep(0.4)
at = fed()
d.cancel()
time.sleep(1.0)
after = (fed() - at) / rate
player.pace = False
# at most the piece being handed down as the cancel came: FirstFeed lets a quiet head go in pieces of ten times its
# least first feed (0.30 s), and the player's stop drops it; fed to its end (the control), 2 s and more
report("cancel", after < 0.5, "a 5 s break cancelled 0.4 s in: %.2f s fed after the cancel" % after)

print("break pause: %s" % ("all ok" if not failures else "%d FAILED" % failures))
sys.stdout.flush()
os._exit(1 if failures else 0)
