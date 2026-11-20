"""An utterance's first audio, as NVDA's player gets it from a driver (nvda/shared/ssi263_feed.py; a listener,
2026-11-20: "every so often, there's a pop at the beginning").  Through the real driver under the stand-in NVDA, whose
player keeps every feed as it came.

NVDA 2025 and later drop everything before the first sample over 1/1024 of full scale (their own trim of leading
silence), and the stream starts on what is left of that feed.  So for each utterance: the first feed with such a
sample in it must hold at least NEED_MS of sound from that sample on -- or be all the utterance has.  Before the
fix the Sounding Board gave 0 to 3 ms there in 33 of 60 utterances of "blank": the blip before the word.

  starts      each utterance of the words, said one after another to their ends
  after cut   the same, each one cut by a cancel 120 ms in, and the next begun at once
  short       a single letter: an utterance shorter than the hold still comes out whole, and is done

    python first_feed.py speakout|accent|blazie|mockingboard|smartalk [voice]      exit 1 on any failure
    FIRST_FEED_BREAK=1     the hold off (the drivers before it)       -- must fail: starts
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
NEED_MS = 25.0                    # of the module's 30: a block less the rounding of where its sound begins
THRESHOLD = 32.0 / 32768
WORDS = ["blank", "OK", "testing", "desktop", "blank", "seven"]
bad = 0

feed_mod = getattr(drv_mod, "_first", None)
if feed_mod is None:
    print("FAIL the driver has no ssi263_feed module (an add-on built before it?)")
    sys.exit(1)
if os.environ.get("FIRST_FEED_BREAK"):
    feed_mod.HOLD = False
if VOICE:
    d._set_voice(VOICE)
d._set_rate("80")
d.speak(["Settle."])
wait_idle()
sr = float(getattr(d, "_out_rate", 44100))


def check(name, ok, detail):
    global bad
    print("%-4s %-9s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def first_sound(n0):
    """(ms of sound in the first feed that has any, is it the utterance's last feed with audio, ms of audio in all)"""
    with d._player.lock:
        chunks = [np.asarray(c, float) for c in d._player.chunks[n0:]]
    total = sum(len(c) for c in chunks)
    for i, c in enumerate(chunks):
        loud = np.nonzero(np.abs(c) > THRESHOLD)[0]
        if len(loud):
            return 1000.0 * (len(c) - int(loud[0])) / sr, not any(len(x) for x in chunks[i + 1:]), 1000.0 * total / sr
    return None, True, 1000.0 * total / sr


def round_of(cut_ms):
    got = []
    for k in range(4 * len(WORDS)):
        n0 = len(d._player.chunks)
        d.speak([WORDS[k % len(WORDS)]])
        if cut_ms:
            time.sleep(cut_ms / 1000.0)
            d.cancel()
            time.sleep(0.03)
        else:
            wait_idle()
        ms_, last, _total = first_sound(n0)
        if ms_ is not None and not (cut_ms and last):      # (a cut utterance's only feed is what the cancel left of it)
            got.append(ms_)
    return got


got = round_of(0)
slivers = [g for g in got if g < NEED_MS]
check("starts", len(got) >= 3 * len(WORDS) and not slivers,
      "%d utterances: the first feed's sound is %.0f to %.0f ms; under %.0f ms: %d" % (
          len(got), min(got or [0]), max(got or [0]), NEED_MS, len(slivers)))
got = round_of(120)
slivers = [g for g in got if g < NEED_MS]
check("after cut", not slivers, "%d utterances begun after a cancel: under %.0f ms: %d" % (len(got), NEED_MS, len(slivers)))
n0 = len(d._player.chunks)
d.speak(["a"])
wait_idle()
ms_, last, total = first_sound(n0)
check("short", ms_ is not None and total > 40.0, "one letter: %.0f ms of audio came out, the first sound in a feed of %s ms" % (
    total, "-" if ms_ is None else "%.0f" % ms_))
print("first feed: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
os._exit(1 if bad else 0)
