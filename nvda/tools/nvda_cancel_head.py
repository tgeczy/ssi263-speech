"""After a cancel, the next utterance through the NVDA driver is only itself (tools/check_cancel_head.py, as NVDA drives
the units).  Through the real driver under the stand-in NVDA, its player paced as a real device is, so a cancel lands
mid-speech.

A unit that keeps part of a cut text says it before the next one: the Braille Lite's kept line (#30; in lockstep too,
0.9.5: 3-4 s of the cut text, at most cuts, on several languages), a cut phoneme's fade (#91).  For each voice of the
driver: WORD said NATURAL times to its end, the most audio of them its own spread; then LONG cut by a cancel at each of
CUTS_MS, WORD said at once, and the audio fed for WORD may not run more than SLACK_MS past that most.

    python nvda_cancel_head.py speakout|accent|blazie|mockingboard|smartalk|synphonix|supervoice [voice,...]
    NVDA_CANCEL_HEAD_BREAK=1   the control: the unit never told to cancel (its binding's cancel a no-op), so the cut
                               text plays on into the next utterance -- must fail on every voice it runs

Not its control: BL_KEPT_LINE_BREAK=1 (bl_host.c, tools/check_cancel_head.py's).  The NVDA driver sends a text in
lines of its own, and at these cut points its unit kept no line even with the lockstep release taken out (0.9.5: all
22 Braille Lite voices passed so) -- the voice table's flow is the one that met the kept line.
"""
import os
import sys
import time

import shutil
import tempfile

WHICH = sys.argv[1]
ONLY = sys.argv[2].split(",") if len(sys.argv) > 2 else None
sys.argv = sys.argv[:2]
HERE = os.path.dirname(os.path.abspath(__file__))
_appdata = None
if WHICH == "supervoice":
    # its program from NVDA's configuration folder (ssi263_userfw): the stand-in has no configPath, so %APPDATA%\nvda --
    # a temporary one here, never the person's (userdata_test.py's lesson), with the repository's drop-in copy
    _bin = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
    if not os.path.isfile(_bin):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        sys.exit(0)
    _appdata = tempfile.mkdtemp(prefix="ssi263-cancel-head-")
    _to = os.path.join(_appdata, "nvda", "ssi263-speech", "firmware", "speech-systems-super-voice")
    os.makedirs(_to)
    shutil.copy(_bin, _to)
    os.environ["APPDATA"] = _appdata
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.5)

WORD = "Down arrow."
LONG = ("This is a long line of speech that will be cut off somewhere in the middle, and then it goes on and on for a "
        "good while longer, so that there is always more of it left to say when the cut comes.")
CUTS_MS = (150, 450, 900, 1600)
NATURAL = 5
SLACK_MS = 300
bad = 0


def fed_since(n0):
    with d._player.lock:
        return sum(len(c) for c in d._player.chunks[n0:])


def say_whole(text):
    n0 = len(d._player.chunks)
    d.speak([text])
    wait_idle()
    return fed_since(n0)


voices = list(d._get_availableVoices().keys())     # (the stand-in has no autoproperty: availableVoices raises)
if ONLY:
    voices = [v for v in voices if v in ONLY]
if not voices:                                     # never "all 0 voices ok": a name that matches nothing is a failure
    print("FAIL  no voice of %s to run (asked: %s; the driver has: %s)" % (WHICH, ONLY, list(d._get_availableVoices())))
    sys.stdout.flush()
    os._exit(1)
d._player.pace = True
for voice in voices:
    if voice:
        d._set_voice(voice)
    d.speak(["Settle."])
    wait_idle()
    if os.environ.get("NVDA_CANCEL_HEAD_BREAK") and getattr(d, "_unit", None) is not None:
        type(d._unit).cancel = lambda self, *a, **k: None          # the control: the unit never cancelled
    sr = float(getattr(d, "_out_rate", 22050))
    ms = lambda n: 1000.0 * n / sr           # noqa: E731
    natural = [ms(say_whole(WORD)) for _ in range(NATURAL)]
    limit = max(natural) + SLACK_MS
    rows, good = [], True
    for cut in CUTS_MS:
        d.speak([LONG])
        time.sleep(cut / 1000.0)
        d.cancel()
        time.sleep(0.03)
        got = ms(say_whole(WORD))
        ok = 0.0 < got <= limit
        good = good and ok
        rows.append("%d: %.0f%s" % (cut, got, "" if ok else " LEAK"))
    bad += not good
    print("%-5s %-14s ms of %r after a cut at %s ms (natural ends %.0f-%.0f)" % (
        "ok" if good else "FAIL", voice or WHICH, WORD, "; ".join(rows), min(natural), max(natural)))
d._player.pace = False
print("nvda cancel head %s: %s" % (WHICH, "%d FAILED" % bad if bad else "all %d voices ok" % len(voices)))
d.terminate()
if _appdata:
    shutil.rmtree(_appdata, ignore_errors=True)
sys.stdout.flush()
os._exit(1 if bad else 0)
