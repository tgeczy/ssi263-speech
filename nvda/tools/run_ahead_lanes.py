"""Run ahead's timing in two lanes (Astra, Reply 107), through bl.dll with the driver's boot.

Emulated time only: Z180 cycles at 6.144 MHz and the chip model's time.  Nothing here is a measurement of a real
unit's chip bus; a finer lockstep converging on run ahead is evidence about our scheduler, not about the hardware.

Lane 1 -- capture and replay, with the timing held fixed.  The lockstep's own schedule (every write's chip time) is
  given to run ahead (bh_pace) and the session replayed from the capture.  Required: every write at the reference's
  time, to the sample, with its value; each captured write's segment = the A/R rises the lockstep unit was given
  before it (the capture's stand-in acknowledged the same requests); and the session's audio identical, sample for
  sample.  A failure here is a capture or replay bug, not a timing choice.

Lane 2 -- the deliberate retiming (run_ahead.h), against a progressively finer lockstep (step 0.5, 0.25, 0.125 ms,
  and 0.0625 without --quick).  Per segment (the writes answering one request), the answer latency: request -> first
  write, run ahead against the lockstep.  Classes, each bounded; anything outside them FAILS:
    answer    opened at the chip's request: run ahead answers at the unit's CPU distance, the lockstep up to one of its
              request slices (step/4) later.  -1 sample <= delta <= slice + 1 sample, and the worst must shrink with
              the step.  Two sub-classes, counted: "at x4" (quicker: the lockstep's x4 CPU while a line is prepared)
              and "+ interrupt" (slower by at most 0.3 ms: an interrupt served inside the answer in the lockstep).
    head      the utterance's first segment: the unit reading its line (the lockstep at x4 turbo): moved up.
    reading   a later segment opened after a pause whose first write came > 5 ms after its acknowledgement: moved
              up to the unit's last answer (the pause removed: delta >= 0).
    pause cut ... and not waiting for that pause to end: the pause's played length shortened (>= 0, <= the pause).
  Deliberate changes are classified and bounded, not required to be identical.

    python run_ahead_lanes.py [--quick] [--es] [--lane=1|2]
    RUN_AHEAD_LANES_BREAK=pace     lane 1's control: the schedule shifted one sample from the 6th write (must fail:
                                   TIME and AUDIO)
    RUN_AHEAD_LANES_BREAK=segment  lane 1's control: one captured write in the speech moved to the next segment
                                   (must fail: SEGMENT boundary)
    RUN_AHEAD_LANES_BREAK=class    lane 2's control: every answer judged a slice and a sample quicker (must fail:
                                   UNCLASSIFIED)
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [REPO, os.path.join(REPO, "src")]
from ssi263.native import SSI263C                 # noqa: E402
from hosts.native_blazie import NativeBlazie       # noqa: E402

ENG = os.path.join(REPO, "nvda", "dist", "blazie-build", "synthDrivers", "_ssi263_blazie")
DLL = os.path.join(REPO, "nvda", "dist", "blazie-lib", "x64" if sys.maxsize > 2 ** 32 else "x86", "bl.dll")
SPANISH = "--es" in sys.argv
QUICK = "--quick" in sys.argv
LANES = [int(a[7:]) for a in sys.argv if a.startswith("--lane=")] or [1, 2]
# --hard-g-off: the chip with 0.7.6's hard G off (params hold_release False: 0.7.5's chip, bit for bit), so the writes
# keep the times they had before it.  A test fixture only: the Spanish known failure of the paced replay's rounding
# (run_ahead.c, `if (r->pace)`) lives at those times, and the hard G moved them off it
HARD_G_OFF = "--hard-g-off" in sys.argv
BREAK = os.environ.get("RUN_AHEAD_LANES_BREAK", "")
RATE = 22050
BLOCK = 0.03
IDLE = 0.3
CLOCK = 6144000.0
READ_S = 0.005
# lane 2 renders at 44.1 kHz so that the finer lockstep can resolve its answers: its request slice (step / 4,
# rounded to samples) is 6, 3 and 1 samples (136, 68, 23 us); finer steps round to the same one sample
RATE2 = 44100
ISR_MS = 0.3                            # an interrupt's service inside an answer (see classes)
STEPS = [0.0005, 0.00025] + ([] if QUICK else [0.000125])

EN = [["Hello.", "OK button", "Select synthesizer dialog"],
      ["Custom number processing check box checked", "Is this a question?"],
      [["Settings.", "Voice settings. Synthesizer: combo box. Blazie. Collapsed."]],
      [["The quick brown fox jumps over the lazy dog,", "and then it runs far away into the forest today."]],
      ["A B C D E F G", "www.example.com slash index dot html"]]
ES = [["Hola.", "Aceptar botón", "Velocidad 50"],
      [["El rápido zorro marrón salta sobre el perro perezoso,", "y luego corre muy lejos hacia el bosque hoy."]],
      ["¡Qué sorpresa! Mañana llueve.", "Archivo, nuevo, abrir, guardar como."]]
CASES = ES if SPANISH else EN
if QUICK:
    CASES = CASES[:2]


def unit(ahead, log):
    if SPANISH:
        fw, st, enc = os.path.join(ENG, "BL2SPA.BNS"), os.path.join(ENG, "bl2spa_fresh.state"), "cp850"
    else:
        fw, st, enc = os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state"), "latin-1"
    chip = SSI263C(params=dict({"closure_noise_lead_ms": 10.0}, **({"hold_release": False} if HARD_G_OFF else {})),
                   out_rate=RATE)
    u = NativeBlazie(DLL, fw, st, chip=chip, out_rate=RATE, menu=("punct_none", "numbers_toggle"), key_start=3000000,
                     key_gap=1500000, board_lowpass_hz=5000.0, on_write=lambda t, r, v: log.append((t, r, v)))
    u.encoding = enc
    u.log_ar = 1
    audio = []
    u.send(b"\x18")
    u.send(b"\r\x06")
    audio.extend(u.run(0.3))
    u.send(b"\x056V")
    audio.extend(u.run(0.05))
    u.turbo_between_lines = True
    u.run_ahead = 1 if ahead else 0
    return u, chip, audio


def writes(log):
    return [(t, r, v) for t, r, v in log if r < 8]


def reference(case, step):
    """the lockstep at `step`: its log, audio, and per utterance (log index at say, say time, done time)"""
    log = []
    u, chip, audio = unit(False, log)
    marks = []
    t0 = time.perf_counter()
    for text in case:
        u._drain()
        n0, t_say = len(log), chip.time
        u.say(text)
        while True:
            audio.extend(u.run(BLOCK, step))
            u._drain()
            if not u.busy():
                break
        marks.append((n0, t_say, chip.time))
        audio.extend(u.run(IDLE, step))
        u._drain()
    cost = time.perf_counter() - t0
    u.close()
    return log, audio, marks, cost


def lane1(case):
    ref, ref_audio, rmarks, _ = reference(case, 0.0005)
    ref_w = writes(ref)
    log = []
    u, chip, audio = unit(True, log)
    segs = []
    for (n0, t_say, t_done), text in zip(rmarks, case):
        u._drain()
        g = len(writes(log))                           # this utterance's first write, in both logs
        times = [t for t, _, _ in ref_w[g:]]
        if BREAK == "pace" and len(times) > 5:
            times[5:] = [t + 1.0 / RATE for t in times[5:]]
        u.pace(times)
        u.say(text)
        while chip.time < t_done - 1e-9:
            audio.extend(u.run(min(BLOCK, t_done - chip.time)))
            u._drain()
        audio.extend(u.run(IDLE))
        u._drain()
        segs.append((g, [(c, s) for c, s, _, _ in u.script()]))
        if BREAK == "segment" and len(segs) == 1:  # a write in the speech moved into the next segment
            c, sg = segs[0][1][20]
            segs[0][1][20] = (c, sg + 1)
    u.close()
    got_w = writes(log)
    problems = []
    n = min(len(ref_w), len(got_w))
    bad_v = next((i for i in range(n) if ref_w[i][1:] != got_w[i][1:]), None)
    bad_t = next((i for i in range(n) if abs(ref_w[i][0] - got_w[i][0]) * RATE >= 0.5), None)
    if len(ref_w) != len(got_w):
        problems.append("%d writes in the lockstep, %d replayed" % (len(ref_w), len(got_w)))
    if bad_v is not None:
        problems.append("VALUE differs at write %d" % bad_v)
    if bad_t is not None:
        problems.append("TIME differs at write %d: %.6f -> %.6f s" % (bad_t, ref_w[bad_t][0], got_w[bad_t][0]))
    # the segments: where a write answers a new request.  Lockstep: the first write after an A/R rise it gave the
    # unit; capture: the first write of a segment (after a stand-in acknowledgement).  Through the speech they must
    # agree, with two classified exceptions, counted: the capture has an extra boundary where the unit read its next
    # line (> 5 ms of CPU since its last write: the lockstep's unit read it during the pauses before, at x4, and
    # needed no request -- the retimed rule moves such a segment up anyway); and anything after the last spoken load
    # (the idle tail: timer-driven pauses on either side of a request)
    bad_s, checked, tail, reading = None, 0, 0, 0
    for (n0, _, _), (g, seg) in zip(rmarks, segs):
        rises, k, r3, last_spoken, bref = 0, 0, 0, -1, set()
        for i, (_, r, v) in enumerate(writes(ref[n0:])[:len(seg)]):
            if r == 3:
                r3 = v
            if r == 0 and not r3 & 0x80 and v & 0x3F:
                last_spoken = i
        fresh = False
        for t, r, v in ref[n0:]:
            if k >= len(seg):
                break
            if r == 8 and v == 1:
                fresh = True
            elif r < 8:
                if fresh and k:
                    bref.add(k)
                fresh = False
                k += 1
        bcap = {i for i in range(1, len(seg)) if seg[i][1] != seg[i - 1][1]}
        checked += len(seg)
        for i in sorted(bref ^ bcap):
            if i > last_spoken:
                tail += 1
            elif i in bcap and seg[i][0] - seg[i - 1][0] > READ_S * CLOCK:
                reading += 1
            elif bad_s is None:
                bad_s = (g + i, "capture" if i in bcap else "lockstep")
    if bad_s is not None:
        problems.append("SEGMENT boundary at write %d in the %s only" % bad_s)
    m = min(len(ref_audio), len(audio))
    bad_a = next((i for i in range(m) if ref_audio[i] != audio[i]), None)
    if bad_a is not None:
        problems.append("AUDIO differs from sample %d (%.3f s)" % (bad_a, bad_a / RATE))
    if abs(len(ref_audio) - len(audio)) > RATE * BLOCK:
        problems.append("AUDIO length %d -> %d samples" % (len(ref_audio), len(audio)))
    return problems, len(got_w), checked, (tail, reading), m


def events(log, a, b, ahead):
    """the writes in log[a:b], each (t, reg, val, opened): opened = (time, how) when it is the first write after a
    request -- the lockstep's A/R rise (how 9), run ahead's segment opening (9 at the request, 10 moved up, 11 moved
    up without waiting for a pause) -- else None"""
    out, pend = [], None
    for t, r, v in log[a:b]:
        if not ahead and r == 8 and v == 1:
            pend = (t, 9)
        elif ahead and r in (9, 10, 11):
            pend = (t, r)
        elif r < 8:
            out.append((t, r, v, pend))
            pend = None
    return out


def classes(ref_log, ahd_log, win_r, win_a, t_say_r, t_say_a, step):
    """one utterance's answers to requests, write by write (the two logs' writes are identical in value), through its
    last spoken load: (class, delta ms, detail).  delta: the lockstep's latency minus run ahead's."""
    wr, wa = events(ref_log, win_r[0], win_r[1], False), events(ahd_log, win_a[0], win_a[1], True)
    sl = round(step / 4 * RATE2) / RATE2           # the lockstep's slice while the chip requests
    samp = 1.0 / RATE2
    r3, last = r3_at(ahd_log, win_a[0]), -1
    for i, (_, r, v, _) in enumerate(wa):
        if r == 3:
            r3 = v
        if r == 0 and not r3 & 0x80 and v & 0x3F:
            last = i
    out = []
    for i in range(min(len(wr), len(wa), last + 1)):
        tr, oa = wr[i][0], wa[i][3]
        ta, orf = wa[i][0], wr[i][3]
        if i == 0:
            out.append(("head", ((tr - t_say_r) - (ta - t_say_a)) * 1e3, "the line read before its first write"))
            continue
        if oa is None and orf is None:
            continue
        if oa is None:
            out.append(("UNMATCHED", 0.0, "write %d: a request in the lockstep that the capture did not see" % i))
            continue
        how = oa[1]
        if orf is None:                    # the lockstep's unit needed no request here (it had read ahead)
            d = ((tr - wr[i - 1][0]) - (ta - wa[i - 1][0])) * 1e3
            out.append(("reading" if how != 9 else "UNCLASSIFIED", d,
                        "write %d: no request in the lockstep; opened %d" % (i, how)))
            continue
        d = ((tr - orf[0]) - (ta - oa[0])) * 1e3
        if how == 9:
            if BREAK == "class":
                d -= (sl + samp) * 1e3
            ok = -samp * 1e3 - 1e-9 <= d <= (sl + samp) * 1e3 + 1e-9
            # ... or quicker: the lockstep runs its CPU x4 while the unit prepares a line (until its first spoken
            # phoneme, and between lines with "short pauses"): the same CPU distance in a quarter of the chip time
            x4 = not ok and -(0.75 * (ta - oa[0]) + samp) * 1e3 - 1e-9 <= d < 0
            # ... or slower by up to ISR_MS: the unit served an interrupt (its timer, the serial port) between the
            # request and its answer in the lockstep, where the capture met it elsewhere (A/R at 1 ms moves the
            # unit's work against its timer: Astra, Reply 107).  Inferred from the size (~1000 cycles), not traced
            isr = not ok and 0 < d <= (sl + samp) * 1e3 + ISR_MS
            out.append(("answer" if ok else "answer at x4" if x4 else "answer + interrupt" if isr else "UNCLASSIFIED",
                        d,
                        "write %d: %.3f -> %.3f ms after the request" % (i, (tr - orf[0]) * 1e3, (ta - oa[0]) * 1e3)))
        elif how == 10:
            out.append(("reading" if d >= -samp * 1e3 else "UNCLASSIFIED", d, "write %d" % i))
        else:                              # moved up without waiting: the pause before it, its played length
            j = max((k for k in range(i) if wa[k][1] == 0), default=None)
            if j is None or wa[j][2] & 0x3F:
                out.append(("UNCLASSIFIED", d, "write %d: moved up, no pause before it" % i))
                continue
            played_r, played_a = orf[0] - wr[j][0], oa[0] - wa[j][0]
            cut = (played_r - played_a) * 1e3
            out.append(("pause cut" if 0.0 <= cut <= played_r * 1e3 + 1e-6 else "UNCLASSIFIED", cut,
                        "write %d: the pause played %.1f -> %.1f ms" % (i, played_r * 1e3, played_a * 1e3)))
    return out


def r3_at(log, n):
    r3 = 0
    for _, r, v in log[:n]:
        if r == 3:
            r3 = v
    return r3


def lane2(case):
    global RATE
    keep, RATE = RATE, RATE2
    try:
        log_a = []
        u, chip, _ = unit(True, log_a)
        am = []
        for text in case:
            u._drain()
            n0, t_say = len(log_a), chip.time
            u.say(text)
            while True:
                u.run(BLOCK)
                u._drain()
                if not u.busy():
                    break
            am.append((n0, t_say))
            u.run(IDLE)
            u._drain()
        u.close()
        am_w = [(n0, (am[k + 1][0] if k + 1 < len(am) else len(log_a))) for k, (n0, _) in enumerate(am)]
        rows = []
        for step in STEPS:
            ref, _, rm, cost = reference(case, step)
            rm_w = [(n0, (rm[k + 1][0] if k + 1 < len(rm) else len(ref))) for k, (n0, _, _) in enumerate(rm)]
            per = []
            for wr_, wa_, (_, tr, _), (_, ta) in zip(rm_w, am_w, rm, am):
                per.extend(classes(ref, log_a, wr_, wa_, tr, ta, step))
            rows.append((step, per, cost))
        return rows
    finally:
        RATE = keep


failures = 0
lang = "Spanish" if SPANISH else "English"
print("emulated time (Z180 cycles, the chip model): not a measurement of a real unit's chip bus")
if 1 in LANES:
    for ci, case in enumerate(CASES):
        problems, n, segs, tail, samples = lane1(case)
        failures += bool(problems)
        print("%-4s lane 1 case %d: %s" % ("FAIL" if problems else "ok", ci + 1, "; ".join(problems) if problems else
              "%d writes at the lockstep's times and values; request boundaries = its A/R rises through the speech "
              "(%d writes; classified: %d at the unit reading its next line, %d in the idle tail); %d samples "
              "identical" % (n, segs, tail[1], tail[0], samples)))
if 2 in LANES:
    worst = {}
    for ci, case in enumerate(CASES):
        rows = lane2(case)
        for step, per, cost in rows:
            by = {}
            for cls, d, _ in per:
                by.setdefault(cls, []).append(d)
            bad = [(cls, d, why) for cls, d, why in per if cls in ("UNCLASSIFIED", "UNMATCHED")]
            failures += bool(bad)
            worst.setdefault(step, []).extend(by.get("answer", []))
            summary = ", ".join("%s %d (%.2f..%.2f ms)" % (c, len(v), min(v), max(v)) for c, v in sorted(by.items()))
            print("%-4s lane 2 case %d step %.4f ms: %s | lockstep %.2f s CPU%s" % (
                "FAIL" if bad else "ok", ci + 1, step * 1e3, summary, cost,
                "" if not bad else " | %s %.3f ms %s" % bad[0]))
    prev = None
    for step in STEPS:
        v = worst.get(step, [0.0])
        w = max(v) if v else 0.0
        mean = sum(v) / len(v) if v else 0.0
        conv = prev is None or w <= prev + 1.0 / RATE * 1e3 + 1e-9
        failures += not conv
        print("%-4s lane 2 answers, step %.4f ms: worst %.3f ms, mean %.3f ms over %d%s" % (
            "ok" if conv else "FAIL", step * 1e3, w, mean, len(v), "" if conv else " (did not shrink)"))
        prev = w
print("run ahead lanes (%s): %s" % (lang, "all ok" if not failures else "%d FAILED" % failures))
sys.exit(1 if failures else 0)
