"""Does the Braille Lite driver keep the unit's channel open after speaking, as the real unit does?

After speech the real unit's channel stays open -- its hiss or whine goes on -- until the firmware clicks it off
(R3 = 00, ~9.95 s after the last phoneme on Tomi's unit).  With "keep the channel open" and the hiss or whine on, the
driver keeps the emulated unit running in real time after "done" (listeners, 2026-09-29).  The real driver under
stand-in NVDA; the tail's clock is sped up SPEED times.

  A  hiss, keep open: 8.5-11 s of audible hiss after "done", ending with the firmware's click-off (R3 = 00)
  A2 ... and the unit's click there (Tomi, 0.7: the click-off, with keep open only; blazie_idle.CLICK)
  B  new speech 2 s into the tail: the tail stops (the idle audio before the new speech is ~2 s, not ~10)
  C  keep open OFF (the control): nothing after "done"
  D  hiss off, keep open on: nothing after "done"

  E  the first speech after the click-off starts no later than with keep open off
  F  speech interrupting the open channel (cancel, then speak) starts no later than with keep open off

    python keep_open_test.py            KEEP_OPEN_BREAK=1: case A runs with keep open off -- the test must fail
                                        KEEP_OPEN_BREAK=mute: every fed sample silent -- passes only if A, B and E/F each fail
                                        KEEP_OPEN_BREAK=noclick: the click left out -- A2 must fail
                                        KEEP_OPEN_BREAK=reclick: a click-off clicked again (#70) -- G must fail

  G  after the click-off, an utterance the unit is given nothing for (an index alone): no second click (#70)
"""
import os
import sys
import time

sys.argv = [sys.argv[0], "blazie"]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(2.0)

SPEED = 20.0
_t0 = time.perf_counter()
drv_mod._now = lambda: _t0 + (time.perf_counter() - _t0) * SPEED
BREAK = os.environ.get("KEEP_OPEN_BREAK") == "1"
if os.environ.get("KEEP_OPEN_BREAK") == "noclick":
    drv_mod.NO_CLICK = True
if os.environ.get("KEEP_OPEN_BREAK") == "reclick":          # (#70) R3 = 00 taken for a click-off, seen or not
    drv_mod.CLICK_SEEN = False
if os.environ.get("KEEP_OPEN_BREAK") == "mute":     # the guards' control: silent audio must fail B, E and F
    _feed = d._player.feed
    d._player.feed = lambda data, onDone=None: _feed(bytes(len(data)), onDone)


def fed():
    return sum(len(c) for c in list(d._player.chunks))


done_at = []
_notify = sdh.synthDoneSpeaking.notify


def notify_done(**kw):
    done_at.append(fed())            # samples fed when "done" fired (in the tail case: the speech's own end)
    _notify(**kw)


sdh.synthDoneSpeaking.notify = notify_done


def utter(text, whine, keep_open, wait_tail=True):
    d._set_whine(whine)
    d._set_keepOpen(keep_open)
    n_done = len(done_at)
    d.speak([text])
    t = time.time()
    while len(done_at) == n_done and time.time() - t < 30:
        time.sleep(0.02)
    if len(done_at) == n_done:         # no NEW completion: never measure against an older utterance's end
        print("FAIL %r never completed (30 s)" % text)
        sys.stdout.flush()
        os._exit(1)
    if not wait_tail:
        return done_at[-1]
    t = time.time()                  # the tail: until R3 = 00 and the feeding stops, or 1.5 s of real time
    last = -1
    while time.time() - t < 30:
        time.sleep(0.2)
        now = fed()
        if d._unit.chip.regs[3] == 0 and now == last:
            break
        if not (keep_open and whine != "off") and time.time() - t > 1.5:
            break
        last = now
    return done_at[-1]


rate = None
bad = 0
failed = []


def check(name, ok, detail):
    global bad
    bad += not ok
    if not ok:
        failed.append(name)
    print("%-4s %s: %s" % ("ok" if ok else "FAIL", name, detail))


import numpy as np   # noqa: E402

# A: the tail
utter("Warming up.", "hiss", True)          # the first utterance after a setting change reboots the unit
rate = float(d._unit.chip.out_rate)
d0 = utter("Hello there.", "hiss", not BREAK)
with d._player.lock:
    y = np.concatenate(d._player.chunks)
tail = y[d0:]
tail_s = len(tail) / rate
rms = float(np.sqrt(np.mean(tail[:int(5 * rate)] ** 2))) if len(tail) else 0.0
check("A hiss, keep open", 8.5 <= tail_s <= 11.0 and d._unit.chip.regs[3] == 0 and rms > 1e-6,
      "%.2f s after done, R3 %02X at the end, rms %.2e over its first 5 s" % (tail_s, d._unit.chip.regs[3], rms))
# A2: the click at the click-off -- its first sample is the measured step (CLICK x REF_RMS, at the driver's gain)
want = abs(drv_mod.blazie_idle.CLICK[0] * drv_mod.blazie_idle.REF_RMS) * drv_mod.MAKEUP * d._volume / 100.0
end = tail[-int((drv_mod.CLICK_S + 0.1) * rate):] if len(tail) else tail
body = tail[:-int((drv_mod.CLICK_S + 0.1) * rate)] if len(tail) else tail
low = float(end.min()) if len(end) else 0.0
check("A2 the click at the click-off", low < -0.7 * want and (not len(body) or float(np.abs(body).max()) < 0.3 * want),
      "lowest %.4f in the tail's last %.2f s (the click's step %.4f); the hiss before it peaks at %.4f"
      % (low, drv_mod.CLICK_S + 0.1, -want, float(np.abs(body).max()) if len(body) else 0.0))
# G: the channel clicked off (A), then an utterance the unit is given nothing for (an index alone, as NVDA sends): no
# second click.  The tail took R3 = 00 left from the click-off before for the firmware clicking off just then (#70)
n_before = fed()
d.speak([cmds.IndexCommand(7)])
t = time.time()
while time.time() - t < 3.0 and ("index", 7) not in list(notified):
    time.sleep(0.02)
time.sleep(1.0)
with d._player.lock:
    y = np.concatenate(d._player.chunks) if d._player.chunks else np.zeros(0)
after = y[n_before:]
low_g = float(after.min()) if len(after) else 0.0
check("G no click after an utterance that said nothing", low_g > -0.3 * want,
      "%d samples fed after it, the lowest %.4f (a click's step: %.4f)" % (len(after), low_g, -want))

# B: interrupted by new speech 2 s (tail time) in
d1 = utter("Hello there.", "hiss", True, wait_tail=False)
time.sleep(2.0 / SPEED)
n_speak = fed()
utter("OK.", "hiss", True, wait_tail=False)
d.cancel()                                  # end the second tail
with d._player.lock:
    y = np.concatenate(d._player.chunks)
seg = y[n_speak:]
loud = np.abs(seg) > 0.02
first_loud = int(np.argmax(loud)) if np.any(loud) else None     # no speech at all must fail, not pass as "no idle"
# what matters is after the new speak(): the tail stops and the speech follows the idle already fed (at most
# IDLE_AHEAD_S, plus a block).  The tail fed before it depends on how much CPU the tail thread got (the full
# run_tests load gave 0.76 s where an idle machine gives ~1.5 s), so it is only required to have been running.
tail_before = (n_speak - d1) / rate          # captured tail audio, in audio seconds (not wall time)
if first_loud is None:
    check("B new speech stops the tail", False, "no audible speech after the new speak()")
else:
    idle_after = first_loud / rate
    speech_s = float(np.count_nonzero(loud)) / rate
    check("B new speech stops the tail", tail_before > 0.1 and idle_after <= 0.4 and speech_s > 0.05,
          "%.2f s of tail audio captured; %.3f s of idle after the new speak() before its speech (%.2f s audible)"
          % (tail_before, idle_after, speech_s))
time.sleep(0.5)

# C: the control -- keep open off
d2 = utter("Hello there.", "hiss", False)
check("C keep open off", fed() - d2 < int(0.05 * rate), "%.3f s after done" % ((fed() - d2) / rate))

# D: hiss off
utter("Warming up.", "off", True)
d3 = utter("Hello there.", "off", True)
check("D hiss off", fed() - d3 < int(0.05 * rate), "%.3f s after done" % ((fed() - d3) / rate))

# E: the first speech after the firmware clicked the channel off starts no later than with keep open off
#    (Tomi: keeping it open must not cost response time)
def first_sound_ms(text, keep_open, runs=5):
    """Median wall time from speak() to the first audible sample fed, over a few runs; None if any run stays silent
    for 5 s (a silent run must fail, not count as 5000 ms beside an equally silent baseline)."""
    out = []
    for _ in range(runs):
        if keep_open == "open":
            utter("Warm.", "hiss", True, wait_tail=False)   # interrupt the open channel, as NVDA does:
            time.sleep(0.05)
            d.cancel()                                      # cancel, then speak
        elif keep_open:
            utter("Warm.", "hiss", True)                 # ... and wait out the tail to the click-off
            assert d._unit.chip.regs[3] == 0
        else:
            utter("Warm.", "hiss", False)
        n = fed()
        t = time.perf_counter()
        d.speak([text])
        heard = False
        while time.perf_counter() - t < 5:
            with d._player.lock:
                y = np.concatenate(d._player.chunks) if d._player.chunks else np.zeros(0)
            if np.any(np.abs(y[n:]) > 0.02):
                heard = True
                break
            time.sleep(0.002)
        if not heard:
            d.cancel()
            return None
        out.append((time.perf_counter() - t) * 1e3)
        d.cancel()
        time.sleep(0.2)
    return sorted(out)[len(out) // 2]


after_off = first_sound_ms("Hello there.", True)
during = first_sound_ms("Hello there.", "open")
baseline = first_sound_ms("Hello there.", False)
if None in (after_off, during, baseline):
    check("E/F first sound", False, "no sound within 5 s (after the click-off %s, open %s, keep open off %s)"
          % (after_off, during, baseline))
else:
    check("E first speech after the click-off", after_off <= baseline + 15.0,
          "%.1f ms after the click-off, %.1f ms with keep open off (median of 5)" % (after_off, baseline))
    check("F speech interrupting the open channel", during <= baseline + 15.0,
          "%.1f ms with the channel open (cancel, then speak), %.1f ms with keep open off" % (during, baseline))

print("keep open: %s" % ("PASS" if not bad else "%d FAILED" % bad))
if os.environ.get("KEEP_OPEN_BREAK") == "mute":     # the guards' control: each audio check must have failed
    need = ("A hiss, keep open", "A2 the click at the click-off", "B new speech stops the tail", "E/F first sound")
    missed = [n for n in need if n not in failed]
    print("mute control: %s" % ("every audio check failed, as it must" if not missed else
                                "NOT rejected: %s" % ", ".join(missed)))
    bad = 1 if missed else 0
sys.stdout.flush()
d.terminate()
os._exit(1 if bad else 0)
