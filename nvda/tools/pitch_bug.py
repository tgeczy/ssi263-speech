"""Capital pitch left high: a capital cancelled mid-speech, then plain text.  python pitch_bug.py speakout|blazie|accent"""
import os
import sys
import time

sys.argv = [sys.argv[0], sys.argv[1]]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(2.0)


def say(seq, cancel_after=None):
    global mark
    mark = len(notified)
    n0 = len(d._player.chunks)
    d.speak(seq)
    if cancel_after is not None:
        time.sleep(cancel_after)
        d.cancel()
        time.sleep(0.2)
        return None
    wait_idle()
    return f0(audio_since(n0))


d._player.pace = True        # real-time playback: the cancel lands while the capital speaks
base = say(["a"])
print("plain a first            F0 %.1f" % base)
results = []
for trial, delay in enumerate((0.01, 0.03, 0.06, 0.1, 0.15)):
    # a capital (pitch up, the letter, pitch back) cut off by NVDA while it speaks
    say([PitchCommand(30), "A capital word said slowly", PitchCommand()], cancel_after=delay)
    after = say(["a"])
    again = say(["a"])
    bad = after > 1.15 * base or again > 1.15 * base
    results.append(bad)
    print("cancel after %3.0f ms -> next a F0 %.1f, then %.1f %s"
          % (delay * 1e3, after, again, "STUCK HIGH" if bad else "ok"))
# and the uncancelled path, capital then plain
say([PitchCommand(30), "A", PitchCommand()])
print("capital A then a          F0 %.1f" % say(["a"]))
print("stuck in %d of %d cancelled capitals" % (sum(results), len(results)))
d.terminate()
