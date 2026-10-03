"""Issue #8: is an Accent SA text done when as_voice says so?  Through the library every front end shares
(ssi263speech.dll / libssi263speech.so: the NVDA add-on's job API, and asv_speak, which the SAPI engine, the
speech-dispatcher module and Android reach through voices.c).

Between two clauses the firmware empties its phoneme queue, drops its speaking flag and reads the next clause before
it speaks again; ash_busy() is false there.  0.7.5 and earlier called that done (most dialog labels end in a colon:
"Rate: slider 55 alt+r" from NVDA's rate 60 up, and longer multi-clause texts at every rate).  as_voice now holds a
quiet block in which the 8085 ran any of its own code (outside its waiting loops: as_board.c's WAITING), and gives
the held blocks out only if speech follows; asv_set_settle caps the wait (6 s), and a text called done at that cap
says so (asv_limit).  The checks, each its own line of output:

  done      every text ends with a real *done: every API call succeeded, no fault, inside the render budget
            (NOT DONE / API / FAULT otherwise: an exhausted loop is never taken for a completion)
  complete  after the done, the host run on 4 s with nothing new says no phoneme (EARLY otherwise)
  audio     a text that never paused keeps its audio sample for sample against the settle off (0.7.5's done); one
            that paused begins with that audio (AUDIO otherwise)
  latency   a text that never paused is done at the same chip time as with the settle off: no wait added at a true
            end (LATENCY otherwise)
  phase     paused texts again after 0-29 ms of idle first, so the 30 ms blocks fall at every point of the firmware's
            work: a block in which it only just began its next clause must count (EARLY ... phase otherwise)
  replace   a cancel or a new text while blocks are held: no held audio and no pending done reach the next utterance,
            and after a new text no rest of the old one, through the job API and asv_speak (HELD otherwise); after a
            cancel, the flush's known residue (a phoneme or two the chip had latched, 0.7.5 too) is counted apart
  limit     with the cap made tiny, a text it stops is never a plain done: asv_limit says so (SILENT LIMIT), and the
            next text on that unit is its own, whole and really done (RECOVERY); and the longest wait the texts needed

    python test_as_complete.py [library]          exit 1 on any failure
    AS_COMPLETE_BREAK=1        the settle off (0.7.5's done)                    -- must fail: EARLY
    AS_COMPLETE_BREAK=never    a render budget too small to reach any done     -- must fail: NOT DONE
    AS_COMPLETE_BREAK=held     a cancel / new text keeps the wait (break 1)    -- must fail: HELD
    AS_COMPLETE_BREAK=limit    the cap: a plain done, no flush (break 2)       -- must fail: SILENT LIMIT, RECOVERY
Default library: $SSI263_LIB (Linux: libssi263speech.so), else the built NVDA add-on's ssi263speech.dll.
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
BREAK = os.environ.get("AS_COMPLETE_BREAK", "")
RUN_ON_S = 4.0
RATE = 22050                    # the add-ons' default sample rate
BUDGET = 3 if BREAK == "never" else 4000     # render calls a text may take (4000 = 2 minutes of chip time)
LOUD = 0.003 * 32767            # the lead trim's threshold, in PCM
PREROLL = 220                   # the lead trim keeps this much before the first sound


def library():
    if len(sys.argv) > 1:
        return sys.argv[1]
    if os.environ.get("SSI263_LIB") and not os.environ["SSI263_LIB"].endswith("ssi263.dll"):
        return os.environ["SSI263_LIB"]
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    return os.path.join(REPO, "nvda", "dist", "accent-build", "synthDrivers", "_ssi263_accent", "bin", arch,
                        "ssi263speech.dll")


P, I, D = ctypes.c_void_p, ctypes.c_int, ctypes.c_double
PCM = ctypes.POINTER(ctypes.c_short)


class Write(ctypes.Structure):
    _fields_ = [("t", D), ("reg", I), ("val", I)]


lib = ctypes.CDLL(library())
for name, res, args in (
        ("asv_create", P, [ctypes.c_char_p, ctypes.c_size_t] * 3 + [D, ctypes.c_char_p, I]),
        ("asv_destroy", None, [P]), ("asv_set", None, [P, I, I, I, I, I]), ("asv_set_settle", None, [P, D]),
        ("asv_set_test_break", None, [P, I]), ("asv_limit", I, [P]), ("asv_fault", I, [P]),
        ("asv_speak", I, [P, ctypes.c_char_p, I]), ("asv_render", I, [P, ctypes.POINTER(PCM), ctypes.POINTER(I)]),
        ("asv_cancel", None, [P]), ("asv_begin", I, [P]), ("asv_text", I, [P, ctypes.c_char_p, I]),
        ("asv_end", I, [P]), ("asv_flush", I, [P]),
        ("asv_say_bytes", I, [ctypes.c_char_p, I, ctypes.c_char_p, I]), ("asv_host", P, [P]),
        ("ash_run", I, [P, D, D, ctypes.POINTER(ctypes.POINTER(D))]), ("ash_set_int", None, [P, ctypes.c_char_p, I]),
        ("ash_get_double", D, [P, ctypes.c_char_p]), ("ash_busy", I, [P, D, D]), ("ash_get_int", I, [P, ctypes.c_char_p]), ("ash_speaking", I, [P]),
        ("ash_writes", I, [P, ctypes.POINTER(ctypes.POINTER(Write))]), ("ash_clear_writes", None, [P])):
    f = getattr(lib, name)
    f.restype, f.argtypes = res, args


class Problem(Exception):
    pass


def unit(settle=None, test_break=0):
    roms = [open(os.path.join(ROMS, n), "rb").read() for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
    err = ctypes.create_string_buffer(256)
    v = lib.asv_create(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]), float(RATE), err, 256)
    if not v:
        sys.exit("asv_create: %s" % err.value.decode("latin-1"))
    if settle is not None:
        lib.asv_set_settle(v, settle)
    if test_break:
        lib.asv_set_test_break(v, test_break)
    lib.ash_set_int(lib.asv_host(v), b"log_writes", 1)
    return v


def fixed_unit():
    """the unit under test: the shipping behaviour, or the control's break"""
    return unit(0.0 if BREAK == "1" else None, {"held": 1, "limit": 2, "threshold": 3}.get(BREAK, 0))


def phonemes(v):
    """the phonemes written since the last call (PA, 00, is not one)"""
    w = ctypes.POINTER(Write)()
    h = lib.asv_host(v)
    n = lib.ash_writes(h, ctypes.byref(w))
    out = sum(1 for i in range(n) if w[i].reg == 0 and (w[i].val & 0x3F))
    lib.ash_clear_writes(h)
    return out


def prepared(text):
    data = text.encode("utf-8")
    n = lib.asv_say_bytes(data, 1, None, 0)
    buf = ctypes.create_string_buffer(n + 1)
    lib.asv_say_bytes(data, 1, buf, n + 1)
    return buf.raw[:n] + b"\r"


def chip_time(v):
    return lib.ash_get_double(lib.asv_host(v), b"time")


def idle(v, seconds):
    y = ctypes.POINTER(D)()
    t = 0.0
    while t < seconds - 1e-9:
        lib.ash_run(lib.asv_host(v), min(0.03, seconds - t), 0.0005, ctypes.byref(y))
        t += 0.03


def start(v, text, rate, job):
    lib.asv_set(v, rate, 50, 100, 100, 1)
    if job:                                   # the NVDA driver: begin, the text with its carriage return
        if lib.asv_begin(v) != 0:
            raise Problem("API    asv_begin failed")
        line = prepared(text)
        if lib.asv_text(v, line, len(line)) != 1:
            raise Problem("API    asv_text failed")
    elif lib.asv_speak(v, text.encode("utf-8"), 0) != 1:   # asv_speak: SAPI, speech-dispatcher, Android
        raise Problem("API    asv_speak said nothing")


def render_to_done(v, job):
    """(audio, render calls) up to a real done; Problem when none comes in the budget, or on a fault"""
    pcm, done, audio, calls, quiet = PCM(), I(0), [], 0, False
    while not done.value:
        if calls >= BUDGET:
            raise Problem("NOT DONE after %d render calls" % calls)
        n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        audio.append(ctypes.string_at(pcm, 2 * n) if n > 0 else b"")
        calls += 1
        busy = lib.ash_busy(lib.asv_host(v), 0.03, 1.5)
        if not busy and not quiet and any(audio):     # a first quiet block after speech: the firmware's work in it
            FIRST_QUIET.append(lib.ash_get_int(lib.asv_host(v), b"work"))
        quiet = not busy
    if lib.asv_fault(v):
        raise Problem("FAULT  the unit faulted")
    if job and lib.asv_end(v) != 0:
        raise Problem("API    asv_end failed")
    return b"".join(audio), calls


def say(v, text, rate, job):
    """one text as a front end says it: (audio, phonemes before done, phonemes after, chip time at done, limit)"""
    start(v, text, rate, job)
    audio, calls = render_to_done(v, job)
    before, t_done, limit = phonemes(v), chip_time(v), lib.asv_limit(v)
    return audio, before, run_on(v), t_done, limit


def run_on(v):
    """the host on its own for RUN_ON_S: the phonemes it still SPEAKS -- written while its speaking flag is up.  Left
    idle long enough (at rate H from about 2.3 s) the firmware also resets the chip on its own, one write of phoneme
    19h and a PA with the flag down, as after a power-up or a Ctrl-X: that is not speech left over, not counted."""
    y, h, late, t = ctypes.POINTER(D)(), lib.asv_host(v), 0, 0.0
    while t < RUN_ON_S:
        lib.ash_run(h, 0.03, 0.0005, ctypes.byref(y))
        n = phonemes(v)
        if n and lib.ash_speaking(h):
            late += n
        t += 0.03
    return late


def name(text, rate, job):
    return "rate %3d %-4s %r" % (rate, "job" if job else "say", text)


DIALOG = ["Rate: slider %d alt+r", "Pitch: slider 50 alt+p", "Volume: slider 100 alt+o",
          "Inflection: slider 100 alt+i", "Variant: combo box Voice 5 (default) alt+a", "Voice: combo box Accent SA alt+v"]
WORDS = ("the quick brown fox jumps over a lazy dog extraordinary international communication representative "
         "1234 567 89 2026 $30.50 3.14 NVDA synthesizer settings dialog check box checked button OK").split()
rng = random.Random(8)
CORPUS = ["".join(" ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 9))) + rng.choice(":,;.?!")
                  + " " for _ in range(rng.randint(2, 4))).strip() for _ in range(6)]
CASES = [(t % r if "%d" in t else t, r, j) for r in (50, 60, 75, 100) for t in DIALOG for j in (True, False)
         if r in (60, 100) or t.startswith("Rate")]
CASES += [(t, r, j) for t in CORPUS for r in (0, 50, 100) for j in (True, False)]
FIRST_QUIET = []                # instructions the firmware ran outside its waiting loops in each first quiet block
fails = {k: 0 for k in ("done", "complete", "audio", "latency", "phase", "replace", "limit")}


def fail(check, line):
    fails[check] += 1
    print(line)


# ---- done, complete, audio, latency: each case on fresh units, the shipping one and one with the settle off ---------
paused_cases = []
for text, rate, job in CASES:
    fixed, old = fixed_unit(), unit(0.0)
    try:
        audio, before, late, t_done, limit = say(fixed, text, rate, job)
        ref, ref_before, ref_late, ref_t, _ = say(old, text, rate, job)
    except Problem as e:
        fail("done", "%s  %s" % (e, name(text, rate, job)))
        continue
    finally:
        lib.asv_destroy(fixed)
        lib.asv_destroy(old)
    if late:
        fail("complete", "EARLY  %s: %d phonemes before done, %d after" % (name(text, rate, job), before, late))
    if limit:
        fail("complete", "LIMIT  %s: done at the safety limit" % name(text, rate, job))
    if ref_late:                              # 0.7.5's done was early here: the audio goes on now, by design --
        paused_cases.append((text, rate, job))
        if audio[:len(ref)] != ref:           # after the same samples
            fail("audio", "AUDIO  %s: does not begin with the settle-off audio" % name(text, rate, job))
    else:
        if audio != ref:
            fail("audio", "AUDIO  %s: %d samples, %d with the settle off" % (name(text, rate, job), len(audio) // 2,
                                                                            len(ref) // 2))
        if abs(t_done - ref_t) > 1e-9:
            fail("latency", "LATENCY  %s: done at chip time %.4f, %.4f with the settle off" % (
                name(text, rate, job), t_done, ref_t))
print("done/complete/audio/latency: %d texts, %d paused (0.7.5 ended them early)" % (len(CASES), len(paused_cases)))

# ---- phase: the paused dialog texts and two corpus texts with the blocks shifted 0-29 ms ----------------------------
FIRST_QUIET[:] = []
PHASED = [c for c in paused_cases if c[2]][:6] + [(CORPUS[0], 100, False)]
n_phase = 0
for text, rate, job in PHASED:
    for ms in range(0, 30, int(os.environ.get("AS_COMPLETE_PHASE_STEP", "3"))):
        v = fixed_unit()
        try:
            idle(v, ms / 1000.0)
            audio, before, late, t_done, limit = say(v, text, rate, job)
        except Problem as e:
            fail("done", "%s  %s, +%d ms" % (e, name(text, rate, job), ms))
            continue
        finally:
            lib.asv_destroy(v)
        n_phase += 1
        if late or limit:
            fail("phase", "EARLY  %s, blocks shifted %d ms: %d phonemes before done, %d after%s" % (
                name(text, rate, job), ms, before, late, " (limit)" if limit else ""))
gaps = [w for w in FIRST_QUIET if w > 0]
print("phase: %d runs, blocks shifted 0-%d ms; a quiet block before more speech held at least %s instructions of "
      "work (at work from 1; the first revision needed 2000)" % (n_phase, max(range(0, 30, int(os.environ.get(
          "AS_COMPLETE_PHASE_STEP", "3")))), min(gaps) if gaps else "-"))

# ---- replace: a cancel or a new text while blocks are held ---------------------------------------------------------
NEXT = "OK button."


def held(v):
    """render until blocks are being held (a quiet block after audio, not done); True when that happened"""
    pcm, done, seen, calls = PCM(), I(0), False, 0
    while calls < BUDGET:
        n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        calls += 1
        if done.value:
            return False
        seen = seen or n > 0
        if seen and n == 0 and not lib.ash_busy(lib.asv_host(v), 0.03, 1.5):
            return True
    return False


def next_text(v, job, what):
    """NEXT on v, right after `what`: (audio, its phonemes); a Problem names a done before any audio of its own"""
    phonemes(v)
    start(v, NEXT, 50, job)
    pcm, done = PCM(), I(0)
    n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
    if done.value and n <= 0:
        raise Problem("the next text was done before any audio of its own (%s)" % what)
    audio = ctypes.string_at(pcm, 2 * n) if n > 0 else b""
    if not done.value:
        audio += render_to_done(v, job)[0]
    elif job and lib.asv_end(v) != 0:
        raise Problem("API    asv_end failed")
    return audio, phonemes(v)


def lead(audio):
    ys = [int.from_bytes(audio[i:i + 2], "little", signed=True) for i in range(0, len(audio), 2)]
    return next((i for i, y in enumerate(ys) if abs(y) > LOUD), len(ys))


def next_alone(job):
    v = unit()
    try:
        return next_text(v, job, "alone")[1]
    except Problem as e:
        fail("done", "%s  %r alone" % (e, NEXT))
        return None
    finally:
        lib.asv_destroy(v)


NEXT_PH = {job: next_alone(job) for job in (True, False)}
n_replace, residue = 0, []
for text, rate, job in [c for c in paused_cases if c[1] >= 60 and NEXT_PH[c[2]] is not None][:6]:
    for how in ("cancel", "new text"):
        v = fixed_unit()
        try:
            start(v, text, rate, job)
            if not held(v):
                continue
            if how == "cancel":               # as the drivers cancel: a job's end and flush, or asv_cancel
                if job:
                    lib.asv_end(v)
                    lib.asv_flush(v)
                else:
                    lib.asv_cancel(v)
            elif job:                         # a new job without a cancel: its end, then the next begin
                lib.asv_end(v)
            audio, ph = next_text(v, job, how + " while held")
            n_replace += 1
            # held audio given out into the next text would come as quiet samples ahead of its speech
            if lead(audio) > PREROLL + 1:
                fail("replace", "HELD   %s, %s while held: %d quiet samples before the next text's speech (%d at "
                     "most: held audio given out)" % (name(text, rate, job), how, lead(audio), PREROLL))
            # phonemes beyond the next text's own: what the unit still had of the old one.  After a cancel, the
            # flush's known residue (a phoneme or two the chip had latched, 0.7.5 as well) is reported, not failed;
            # after a new text without a cancel the old text's rest is the issue itself
            extra = ph - NEXT_PH[job]
            if how == "cancel":
                residue.append(extra)
                if extra > 2:
                    fail("replace", "HELD   %s, cancel while held: %d phonemes beyond the next text's %d" % (
                        name(text, rate, job), extra, NEXT_PH[job]))
        except Problem as e:
            fail("replace", "HELD   %s, %s while held: %s" % (name(text, rate, job), how, e))
        finally:
            lib.asv_destroy(v)
print("replace: %d cancels and new texts while blocks were held; after a cancel the next text had %s phonemes beyond "
      "its own (the flush's known residue, at most 2 allowed)" % (n_replace, sorted(set(residue)) or "-"))

# ---- limit: the cap made tiny -- a stop, reported, never a plain done; and the next text recovers ------------------
n_limit = cut = 0
for text, rate, job in [c for c in paused_cases if NEXT_PH[c[2]] is not None][:10]:
    v = unit(0.06, 2 if BREAK == "limit" else 0)
    full = unit()
    try:
        start(v, text, rate, job)
        audio, calls = render_to_done(v, job)
        limit, before = lib.asv_limit(v), phonemes(v)
        f_audio, f_before, f_late, f_t, f_limit = say(full, text, rate, job)
        n_limit += 1
        if len(audio) < len(f_audio):         # stopped short of the whole text: that must be said
            cut += 1
            if not limit:
                fail("limit", "SILENT LIMIT  %s: done with %d of its %d samples, and no limit reported" % (
                    name(text, rate, job), len(audio) // 2, len(f_audio) // 2))
        elif limit:
            fail("limit", "LIMIT  %s: a limit reported for a whole text" % name(text, rate, job))
        # recovery: the next text on the same unit is its own, whole, and a real completion
        r_audio, r_ph = next_text(v, job, "after the limit")
        late = run_on(v)
        if r_ph != NEXT_PH[job] or late or lead(r_audio) > PREROLL + 1 or lib.asv_limit(v):
            fail("limit", "RECOVERY  %s: the next text after the limit had %d phonemes (%d alone), %d after its "
                 "done, %d quiet samples first%s" % (name(text, rate, job), r_ph, NEXT_PH[job], late, lead(r_audio),
                                                     ", a limit again" if lib.asv_limit(v) else ""))
    except Problem as e:
        fail("done", "%s  %s (tiny cap)" % (e, name(text, rate, job)))
    finally:
        lib.asv_destroy(v)
        lib.asv_destroy(full)
print("limit: %d texts with a 0.06 s cap, %d stopped by it; the next text after each" % (n_limit, cut))
longest = 0.0
for text, rate, job in paused_cases:
    v = unit()
    try:
        start(v, text, rate, job)
        pcm, done, quiet, calls = PCM(), I(0), 0, 0
        while not done.value and calls < BUDGET:
            n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            calls += 1
            quiet = quiet + 1 if n == 0 and not done.value else 0
            longest = max(longest, quiet * 0.03)
    finally:
        lib.asv_destroy(v)
print("limit: the longest wait the paused texts needed was %.2f s of the 6 s cap" % longest)

bad = sum(fails.values())
print("checks: " + ", ".join("%s %s" % (k, "ok" if not n else "%d FAILED" % n) for k, n in fails.items()))
print("accent sa completion: %s" % ("FAILED" if bad else "ok"))
sys.exit(1 if bad else 0)
