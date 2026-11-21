"""NVDA's BreakCommand is a pause of its length (issue #71; Tomi: honour it).

NVDA sends BreakCommand(ms) for a pause: "Delayed descriptions for characters on cursor movement" puts a second's
break before a letter's description, so it is heard only when the user stops on the letter (a move on cancels it).
Every driver dropped it: the description came at once, right behind the letter.  Now each driver feeds that much
silence where the break stands (NVDA keeps a sequence's leading silence when a BreakCommand puts it there).

Each driver through the stand-in NVDA:
  pause     "Hello." said plain, then after BreakCommand(1000): the second is a second longer, and that second is
            silence, at its start

    python break_pause.py blazie|speakout|accent|mockingboard|smartalk       exit 1 on any failure
    BREAK_PAUSE_BREAK=1   ssi263_flow.pause feeds nothing: the break dropped, as it was -- must fail
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


def said(seq):
    """the audio fed for one utterance, from the first feed after the speak"""
    n0 = sum(len(c) for c in list(d._player.chunks))
    d.speak(seq)                                  # (the harness adds an index of its own, and waits for it)
    wait_done(timeout=30)
    time.sleep(0.3)
    with d._player.lock:
        y = np.concatenate(d._player.chunks) if d._player.chunks else np.zeros(0)
    return y[n0:]


rate = float(d._out_rate)
plain = said([TEXT])
paused = said([cmds.BreakCommand(1000), TEXT])
extra = (len(paused) - len(plain)) / rate
head = paused[:int(0.95 * rate)]
quiet = len(head) > 0 and float(np.abs(head).max()) < 1e-4
ok = 0.9 <= extra <= 1.1 and quiet
print("%-4s pause    %s: %.2f s plain, %.2f s after a 1 s BreakCommand (%.2f s more); its first 0.95 s %s" % (
    "ok" if ok else "FAIL", WHICH, len(plain) / rate, len(paused) / rate, extra,
    "silent" if quiet else "NOT silent (peak %.4f)" % (float(np.abs(head).max()) if len(head) else 0.0)))
print("break pause: %s" % ("all ok" if ok else "1 FAILED"))
sys.stdout.flush()
os._exit(0 if ok else 1)
