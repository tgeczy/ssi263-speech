"""The hard G (params hold_release, 0.7.6): watched in the chip's own state, not fitted on its sound.

The Braille Lite writes G as K HVC (the Speak-Out as KV HVC), with its PA prime before every phoneme.  0.6.0-0.7.5 held
the K and kept HVC's gate shut to its end, so nothing of the G was heard ("guess" said "ess").  Since 0.7.6 an HVC
after K / KV asks early for the next phoneme (release_lookahead's machinery: A/R ahead, the writes held to its end) and
opens its gate hold_release_frames before its end, with its own hold_release_ramp_ms ramp -- only when the held writes
load an open phoneme.  This test watches, at every output sample, the chip's own state (Python: chip.py; C: ssi263.c
through ssi263_state) for:

  OPEN cases (K/KV, prime, HVC, an open phoneme): exactly one hold opens per HVC; it opens hold_release_frames before
  HVC's end (within a sample and a tick); HVC's body is shut until then; from then to HVC's end the gate stays open and
  rises to 1 within the ramp; the voice is on (VA latch > 0) and every noise source is off (g2 = g5 = 0) -- the G is
  HVC's own stored voice, not noise or a click; and the output is silent before the opening and audible after it.
  SHUT cases: HVC before D, before PA, after a real pause, HVC HVC, HVC with no K, an answer too late or only the
  prime in time, a power-down while the writes are held, a K from before a power-down: no hold opens and HVC's gate
  stays shut to its end.
  OUT OF SCOPE (hold_release_min_frames, Astra's Reply 151): a hold shorter than 2 frames -- HVC'3, the Accents'
  KV KV HVC'3 HF, any HVC in mode 1 -- asks nothing early and renders sample for sample as with hold_release off.
  That is the bounded scope of our modelled fix, not a duration rule established for the silicon.

Sessions: a scripted host answering requests as the firmware does (registers, the PA prime, the phoneme 0.5 ms later),
on the Python reference and on the C core; then, with the firmware and nvda/dist/blazie-lib/bl_live.exe present, the
live Braille Lite (English) speaking G words and controls through the pipe host on the Python reference.

    python tools/check_hard_g.py            exit 1 on any failure

Controls (HARD_G_BREAK), each of which must fail: off (hold_release False: the 0.7.5 suppression restored), noise (the
gate opens but with the K's noise and no voice: Reply 146's sustained-noise adversary), click (the gate opens for 1 ms
then shuts), early (it opens a whole frame before the end).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "src"))
from ssi263 import SSI263                      # noqa: E402
from ssi263.native import SSI263C              # noqa: E402

BREAK = os.environ.get("HARD_G_BREAK", "")
RATE = 22050
K, KV, HVC, PA, EH, OU, D, S, T, HF = 0x29, 0x26, 0x2B, 0x00, 0x0A, 0x12, 0x25, 0x30, 0x28, 0x2C
DRIVER = {"closure_noise_lead_ms": 10.0}       # the Braille Lite voice's setting (bl_voice.c)
# the listening choice this implements (Tomi's "C_050_sharp", Astra's Reply 150): what the chip must do, whatever its
# parameters say -- a changed default fails here on purpose
OPEN_FRAMES = 0.5            # the gate opens this many frames (scaled as the stops' release) before HVC's end
OPEN_RAMP_MS = 2.0           # and rises over this
PARAMS = dict(DRIVER)
if BREAK == "off":
    PARAMS["hold_release"] = False
elif BREAK == "early":
    PARAMS["hold_release_frames"] = 1.0


class Adversary(SSI263):
    """The must-fail chips: 'noise' opens the gate on the K's noise with the voice off (the sustained-noise adversary of
    Astra's Reply 146), 'click' lets the gate open for 1 ms and shuts it again."""

    def _latch_all(self, force=False):
        SSI263._latch_all(self, force)
        if not self.hold_open:
            self._t_open = None
            return
        if BREAK == "noise":
            self.latch["VA"] = 0
            self.g2 = self.g5 = 2.0
        elif BREAK == "click":
            if getattr(self, "_t_open", None) is None:
                self._t_open = self._time
            elif self._time - self._t_open > 0.001:
                self.hold_open = False
                self.hold_ok = False


def py_chip():
    return (Adversary if BREAK in ("noise", "click") else SSI263)(dict(PARAMS), out_rate=RATE, dsp="c")


def c_chip():
    return SSI263C(dict(PARAMS), out_rate=RATE)


# ---- observation ------------------------------------------------------------------------------------------------
def observe(chip):
    """One state sample: the same fields from either core."""
    if isinstance(chip, SSI263C):
        v = chip.state()
        s = dict(elapsed=v[0], duration=v[1], clo=v[5], g2=v[6], g5=v[7], timer_done=v[12], time=v[14],
                 va=v[15 + 6 + 4], early_req=v[28], hold_ok=v[32], hold_open=v[33])
        s["phoneme"] = chip.regs[0] & 0x3F if chip.powered else -1
    else:
        s = dict(elapsed=chip.elapsed, duration=chip.duration, clo=chip.clo, g2=chip.g2, g5=chip.g5,
                 timer_done=int(chip.timer_done), time=chip._time, va=chip.latch["VA"], hold_ok=int(chip.hold_ok),
                 hold_open=int(chip.hold_open), early_req=int(chip.early_req))
        s["phoneme"] = chip.phoneme if chip.powered else -1
    s["frame"] = 4096.0 * (16 - (chip.regs[2] >> 4)) / 1e6
    return s


class Recorder:
    """Runs the chip one output sample at a time, keeping its state and output after each."""

    def __init__(self, chip):
        self.chip = chip
        self.rows = []

    def run(self, seconds, until_request=False):
        n = int(round(seconds * RATE))
        for _ in range(n):
            if until_request and self.chip.request:
                return True
            y = self.chip.run_until_request(1.0 / RATE) if until_request else self.chip.run(1.0 / RATE)
            st = observe(self.chip)
            st["y"] = y[-1] if len(y) else 0.0
            self.rows.append(st)
        return until_request and self.chip.request


def scripted(chip, phonemes, answer=0.00016, prime=0.0005, late=None, prime_only=None, powerdown=None, pause=None,
             mode=3):
    """A firmware-like host: power up in `mode` (3), then at each request write R3 R2 R1 R4 and the PA prime (C0h) `answer`
    s after it and the phoneme `prime` s later.  late = index: that answer comes only after the request's phoneme has
    ended; prime_only = index: the prime is written but the phoneme only after its end; powerdown = index: the chip is
    powered down while that answer is held, and back up; pause = index: a real PA (its own timer runs out) before it."""
    rec = Recorder(chip)
    for a, v in ((4, 0xE7), (2, 0xA8), (1, 0x80), (3, 0xFC), (0, mode << 6), (3, 0x7C)):
        chip.write(a, v)
    for i, ph in enumerate(phonemes):
        rec.run(2.0, until_request=True)
        if late == i:
            rec.run(chip_left(chip) + 0.004)
        rec.run(answer)
        for a, v in ((3, 0x7C), (2, 0xA8), (1, 0x80), (4, 0xE7)):
            chip.write(a, v)
        if pause == i:
            chip.write(0, 0xC0)
            rec.run(0.03)                               # longer than the prime's frame: a real pause
            rec.run(2.0, until_request=True)
        chip.write(0, 0xC0)
        if powerdown == i:
            rec.run(0.0002)
            chip.write(3, 0xFC)                         # power down: what is held lands at once
            rec.run(0.002)
            chip.write(0, 0xC0)
            chip.write(3, 0x7C)
        if prime_only == i:
            rec.run(chip_left(chip) + 0.002)
        else:
            rec.run(prime)
        chip.write(0, ph)
    rec.run(2.0, until_request=True)
    rec.run(0.01)
    return rec.rows


def powerdown_context(chip, new_k):
    """Astra's sequence (Reply 151): power up; load K; run 4 ms; CTL power-down; wait 10 ms; the mode-3 PA; power up;
    run 0.5 ms; [new_k: a genuinely new K, answered as the firmware does;] a four-frame HVC; its request answered with
    PA then EH."""
    rec = Recorder(chip)
    for a, v in ((4, 0xE7), (2, 0xA8), (1, 0x80), (3, 0xFC), (0, 0xC0), (3, 0x7C)):
        chip.write(a, v)
    chip.write(0, K)
    rec.run(0.004)
    chip.write(3, 0xFC)
    rec.run(0.010)
    chip.write(0, 0xC0)
    chip.write(3, 0x7C)
    rec.run(0.0005)
    if new_k:
        chip.write(0, K)
        rec.run(2.0, until_request=True)
        chip.write(0, 0xC0)
        rec.run(0.0005)
    chip.write(0, HVC)
    rec.run(2.0, until_request=True)
    chip.write(0, 0xC0)
    rec.run(0.0005)
    chip.write(0, EH)
    rec.run(2.0, until_request=True)
    rec.run(0.01)
    return rec.rows


def chip_left(chip):
    s = observe(chip)
    return max(0.0, s["duration"] - s["elapsed"])


# ---- the judgement --------------------------------------------------------------------------------------------------
def hvc_spans(rows):
    """[(i0, i1)]: the rows of each HVC (a run of samples with phoneme HVC and the same load time)."""
    out, i = [], 0
    while i < len(rows):
        if rows[i]["phoneme"] == HVC:
            j, t0 = i, rows[i]["time"] - rows[i]["elapsed"]
            while (j + 1 < len(rows) and rows[j + 1]["phoneme"] == HVC
                   and abs(rows[j + 1]["time"] - rows[j + 1]["elapsed"] - t0) < 1e-9):
                j += 1
            out.append((i, j))
            i = j + 1
        else:
            i += 1
    return out


def rms(ys):
    return math.sqrt(sum(y * y for y in ys) / len(ys)) if ys else 0.0


def gate_opening(rows, a, b):
    """The row where HVC's gate first rises again after it has shut (clo > 0 after clo == 0), or None: watched on the
    gate itself, whatever the chip's own flags say."""
    k_shut = next((k for k in range(a, b + 1) if rows[k]["clo"] == 0.0), None)
    if k_shut is None:
        return None
    return next((k for k in range(k_shut + 1, b + 1) if rows[k]["clo"] > 0.0), None)


def judge(rows, want_open, label, p):
    """Failures for one session: want_open = how many HVCs must open (each in its own span), the rest stay shut."""
    bad = []
    spans = hvc_spans(rows)
    opened = [(a, b) for a, b in spans if gate_opening(rows, a, b) is not None]
    if len(opened) != want_open:
        bad.append("%d of %d HVCs opened, want %d" % (len(opened), len(spans), want_open))
    tick = 2.0 * (256 - 0xE7) / 1e6
    for a, b in spans:
        r0 = rows[a]
        t_load, dur, frame = r0["time"] - r0["elapsed"], r0["duration"], r0["frame"]
        t_end = t_load + dur
        k_open = gate_opening(rows, a, b)
        if any(rows[k]["hold_open"] for k in range(a, b + 1)) != (k_open is not None):
            bad.append("HVC at %.4f s: the chip's hold flag and its gate disagree" % t_load)
        if k_open is None or (a, b) not in opened[:want_open]:
            continue
        t_open = rows[k_open - 1]["time"]          # the gate rose inside the sample after this one
        lead = t_end - t_open
        want = OPEN_FRAMES * frame * (dur / (4.0 * frame))
        if abs(lead - want) > 1.0 / RATE + tick + 1e-9:
            bad.append("HVC at %.4f s opened %.2f ms before its end, want %.2f ms" % (t_load, lead * 1e3, want * 1e3))
        body = rows[a:k_open - 1] if k_open - 1 > a else []
        if body and max(r["clo"] for r in body[len(body) // 2:]) > 0.0:
            bad.append("HVC at %.4f s: its body was not shut before the opening" % t_load)
        expo = [rows[k] for k in range(k_open, b + 1) if rows[k]["time"] <= t_end]
        if any(r["clo"] <= 0.0 for r in expo):
            bad.append("HVC at %.4f s: the gate shut again before HVC's end" % t_load)
        if any(r["va"] <= 0 for r in expo):
            bad.append("HVC at %.4f s: the voice is off while the gate is open" % t_load)
        if any(r["g2"] != 0.0 or r["g5"] != 0.0 for r in expo):
            bad.append("HVC at %.4f s: noise while the gate is open (g2 %.3g g5 %.3g)" % (
                t_load, max(r["g2"] for r in expo), max(r["g5"] for r in expo)))
        ramp = OPEN_RAMP_MS / 1000.0
        full = next((r["time"] for r in expo if r["clo"] >= 1.0), None)
        if full is None or full - t_open > ramp + 2 * tick + 1.0 / RATE:
            bad.append("HVC at %.4f s: the gate did not reach 1 within %.1f ms" % (t_load, ramp * 1e3))
        clos = [r["clo"] for r in expo]
        if any(y < x for x, y in zip(clos, clos[1:])):
            bad.append("HVC at %.4f s: the gate fell while open" % t_load)
        # silent before, audible after: closed vs open, not a level fitted on any candidate
        before = [r["y"] for r in rows[max(a, k_open - int(0.008 * RATE)):k_open]]
        after = [r["y"] for r in expo[len(expo) // 2:]]          # the later half of the exposure
        vowel = [r["y"] for r in rows[b + 1:b + 1 + int(0.03 * RATE)]]
        ref = rms(vowel) or 1.0
        if before and rms(before) > ref * 10 ** (-50 / 20.0):
            bad.append("HVC at %.4f s: sound before the opening (%.1f dB re the vowel)" % (
                t_load, 20 * math.log10(rms(before) / ref)))
        if not after or rms(after) < ref * 10 ** (-40 / 20.0):
            bad.append("HVC at %.4f s: silent after the opening (%.1f dB re the vowel)" % (
                t_load, 20 * math.log10(max(rms(after), 1e-12) / ref)))
        INFO.append("%.2f ms before HVC's end, %.0f / %.0f dB re the vowel before / after" % (
            lead * 1e3, 20 * math.log10(max(rms(before), 1e-12) / ref), 20 * math.log10(max(rms(after), 1e-12) / ref)))
    return bad


INFO = []


SCRIPTED = [
    # label, phonemes, kwargs, HVCs that must open
    ("K HVC EH S (guess)", [K, HVC, EH, S], {}, 1),
    ("K HVC OU (go)", [K, HVC, OU], {}, 1),
    ("KV HVC EH (the Speak-Out's G)", [KV, HVC, EH], {}, 1),
    ("K HVC EH, at rate 15", [K, HVC, EH], {"rate": 0xF8}, 1),
    ("K HVC EH, K HVC OU (twice)", [K, HVC, EH, K, HVC, OU], {}, 2),
    ("K HVC D (big dog)", [K, HVC, D], {}, 0),
    ("K HVC PA (egg)", [K, HVC, PA], {}, 0),
    ("K HVC S (closed by noise)", [K, HVC, S], {}, 0),
    ("K PA-pause HVC EH (context broken)", [K, HVC, EH], {"pause": 1}, 0),
    ("HVC EH (no K)", [EH, HVC, EH], {}, 0),
    ("T HVC EH", [T, HVC, EH], {}, 0),
    ("K HVC HVC EH (repeated)", [K, HVC, HVC, EH], {}, 0),
    ("K HVC EH, answered too late", [K, HVC, EH], {"late": 2}, 0),
    ("K HVC EH, only the prime in time", [K, HVC, EH], {"prime_only": 2}, 0),
    ("K HVC EH, power-down while held", [K, HVC, EH], {"powerdown": 2}, 0),
    ("K, power-down, HVC EH (stale K)", None, {"context": False}, 0),
    ("K, power-down, a new K, HVC EH", None, {"context": True}, 1),
    # out of scope: shorter than hold_release_min_frames -- no early request, and 0.7.5's output exactly
    ("K HVC'3 EH (one frame)", [K, 0xC0 | HVC, EH], {"same_as_off": True}, 0),
    ("KV KV HVC'3 HF (the Accents' G)", [KV, KV, 0xC0 | HVC, HF], {"same_as_off": True}, 0),
    ("K HVC EH in mode 1 (one frame)", [K, HVC, EH], {"mode": 1, "same_as_off": True}, 0),
]


def run_scripted(make, core):
    fails = 0
    for label, phs, kw, want in SCRIPTED:
        kw = dict(kw)
        rate = kw.pop("rate", None)
        context = kw.pop("context", None)
        same_as_off = kw.pop("same_as_off", False)
        chip = make()
        if rate is not None:
            chip.write(2, rate)
        if context is not None:
            rows = powerdown_context(chip, context)
        else:
            rows = scripted(chip, phs, **kw) if rate is None else scripted_rate(chip, phs, rate, **kw)
        del INFO[:]
        bad = judge(rows, want, label, chip.p)
        if same_as_off:
            if any(rows[k]["early_req"] for a, b in hvc_spans(rows) for k in range(a, b + 1)):
                bad.append("a short hold asked early")
            off = (SSI263C(dict(PARAMS, hold_release=False), out_rate=RATE) if core == "C" else
                   SSI263(dict(PARAMS, hold_release=False), out_rate=RATE, dsp="c"))
            ref = scripted(off, phs, **kw)
            if [r["y"] for r in rows] != [r["y"] for r in ref]:
                bad.append("differs from hold_release off")
        fails += bool(bad)
        print("%s  %-4s %-40s %s" % ("FAIL" if bad else "ok  ", core, label, "; ".join(bad) if bad else
                                     "opened %s" % ", ".join(INFO) if want else
                                     "no early request, = hold_release off" if same_as_off else "stayed shut"))
    return fails


def scripted_rate(chip, phs, rate, **kw):
    orig = chip.write

    def write(a, v):
        orig(a, rate if a == 2 else v)
    chip.write = write
    try:
        return scripted(chip, phs, **kw)
    finally:
        chip.write = orig


# ---- the live Braille Lite ------------------------------------------------------------------------------------------
LIVE_OPEN = ["guess", "guest", "go", "get", "give", "good", "global"]
LIVE_SHUT = ["big dog", "egg", "kiss", "again", "status"]


def run_live():
    exe = os.path.join(REPO, "nvda", "dist", "blazie-lib", "bl_live.exe")
    fw = os.path.join(REPO, "firmware", "blazie", "BL2ENG.BNS")
    st = os.path.join(REPO, "firmware", "blazie", "bl2_2003_warm.state")
    if not all(os.path.isfile(p) for p in (exe, fw, st)):
        print("skip the live Braille Lite: %s not found" % next(p for p in (exe, fw, st) if not os.path.isfile(p)))
        return 0
    from hosts.blazie import Blazie
    fails = 0
    for w in LIVE_OPEN + LIVE_SHUT:
        chip = py_chip()
        rec = Recorder(chip)
        u = Blazie(exe, fw, st, chip=chip, out_rate=RATE)
        u.send(b"\x18")
        u.send(b"\r\x06")
        u.run(0.3)
        u.send(b"\x056V")
        u.run(0.05)
        # the pipe host's lockstep, one output sample per step, with the chip's state kept after each
        u.say(w)
        t = 0.0
        while t < 4.0 and (t < 0.2 or u.busy()):
            if chip.request != u.ar:
                u.ar = chip.request
                u._cmd("A %d" % (1 if u.ar else 0))
            before = chip.time
            rec.run(1.0 / RATE)
            dt = max(chip.time - before, 1e-5)
            u._cmd("R %d" % max(1, int(6144000.0 * dt)))
            t += dt
        u.close()
        want = 1 if w in LIVE_OPEN else 0
        del INFO[:]
        bad = judge(rec.rows, want, w, chip.p)
        fails += bool(bad)
        print("%s  live %-40s %s" % ("FAIL" if bad else "ok  ", w, "; ".join(bad) if bad else
                                     "opened %s" % ", ".join(INFO) if want else "stayed shut"))
    return fails


def main():
    fails = run_scripted(py_chip, "py")
    if BREAK not in ("noise", "click"):                 # the adversaries are Python subclasses
        fails += run_scripted(c_chip, "C")
    if "--no-live" not in sys.argv:
        fails += run_live()
    print("hard G: %s" % ("%d FAILED" % fails if fails else "all ok"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
