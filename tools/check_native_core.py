"""Hold the C chip (ssi263/native.py -> csrc/ssi263.c) to the Python reference (chip.py).

Both run the same seeded random host sessions -- register writes at every address,
power and mode changes, phoneme loads, run / run_until_request / skip, snap_pitch --
under the default parameters and under every switch flipped to its alternative.  The
Python chip uses the C host-rate FIR too, so only the chip loop is compared.

After every operation: A/R request, mode and registers must be equal, chip time and the
timer state EXACT (the firmware hosts are driven by them); the other internal values and
the audio within float noise; the 16-bit PCM identical.  Exit status 1 on any failure.

    python tools/check_native_core.py            (quick: the build step runs this)
    python tools/check_native_core.py --long     (more seeds and operations)

Runs on Python 3.7 (NVDA 2021-2023's) as well as 3.13: no numpy, no csv.
"""
import os
import random
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src"))
from ssi263 import SSI263                 # noqa: E402
from ssi263.chip import FIELDS            # noqa: E402
from ssi263.native import SSI263C         # noqa: E402

AUDIO_TOL = 1e-9          # absolute, on samples of order 0.1-1
STATE_TOL = 1e-9          # relative, on continuous internal values
EXACT = ("elapsed", "duration", "time", "timer_done", "mode", "lfsr", "trans_target",
         "released", "early_req", "apply_due", "npending", "onto_silence")

SWITCHES = [
    {},
    {"latch_quantize": "floor"},
    {"section_numerator": "allpole"},
    {"closure_timing": "frames"},
    {"closure_floor_db": -26.0},
    {"closure_target": "va"},
    {"closure_reopen": True},
    {"closure_enable": False},
    {"closure_release_b01": False},
    {"closure_noise_at_release": False},
    {"burst_hold_frames": None},
    {"pulse_place": "nearest"},
    {"noise_shaper": "flat"},
    {"noise_shaper": (0.3, 0.2)},
    {"noise_route": "fixed"},
    {"noise_route_mode": "switch"},
    {"noise_f5_point": "output"},
    {"carrier_when_powered_down": True},
    {"output_oversample": 2},
    {"f1_w": (0.0, 2.0, 4.0, 8.0), "f2_w": (0.96, 2.27, 3.77, 7.96), "f3_w": (0.89, 2.0, 3.96, 8.02)},
    {"noise_clock_ratio": 0.7, "lfsr_bits": 17, "amp_slew_per_phoneme": 0.5},
    {"release_lookahead": False},
    {"late_release_burst": False},
    {"fricative_precharge": False},
    {"lookahead_lead_frames": 1.5},
    {"closure_onto_silence": False},
]


def py_state(c):
    s = {"elapsed": c.elapsed, "duration": c.duration, "trans": c.trans, "trans_target": c.trans_target,
         "amp_cur": c.amp_cur, "clo": c.clo, "g2": c.g2, "g5": c.g5, "phase": c.phase,
         "pending_pulse": c.pending_pulse, "noise_phase": c.noise_phase, "lfsr": c.lfsr,
         "timer_done": int(c.timer_done), "mode": -1 if c.mode is None else c.mode, "time": c._time}
    for f in FIELDS:
        s["cur_" + f] = c.cur[f]
    for f in FIELDS:
        s["latch_" + f] = c.latch[f]
    s.update(released=int(c.released), early_req=int(c.early_req), apply_due=int(c.apply_due),
             npending=len(c.pending), onto_silence=int(c.onto_silence))
    return s


def c_state(c):
    v = c.state()
    names = ["elapsed", "duration", "trans", "trans_target", "amp_cur", "clo", "g2", "g5", "phase",
             "pending_pulse", "noise_phase", "lfsr", "timer_done", "mode", "time"]
    names += ["cur_" + f for f in FIELDS] + ["latch_" + f for f in FIELDS]
    names += ["released", "early_req", "apply_due", "npending", "onto_silence"]
    return dict(zip(names, v))


def ops_for(rng, n):
    """A random host session: power-up, then a mix of everything a host can do."""
    ops = [("w", 4, rng.choice([0xE7, 0xD0, 0xC8, 0xE0])), ("w", 2, rng.randrange(256)),
           ("w", 1, rng.randrange(256)), ("w", 3, 0x80 | rng.randrange(128)),
           ("w", 0, rng.randrange(4) << 6), ("w", 3, rng.randrange(128))]
    for _ in range(n):
        r = rng.random()
        if r < 0.30:
            ops.append(("w", 0, rng.randrange(256)))
            ops.append(("rur", rng.uniform(0.0, 0.25)))
        elif r < 0.45:
            addr = rng.choice([1, 1, 2, 2, 3, 4, 5, 6, 7])
            v = rng.randrange(256)
            if addr >= 4:
                v = rng.choice([rng.randrange(0x90, 0xF4), rng.randrange(0x90, 0xF4), 0x00, 0xFF])
            elif addr == 3:
                v &= 0x7F                              # stay powered here; power cycles below
            ops.append(("w", addr, v))
        elif r < 0.55:
            ops.append(("run", rng.uniform(0.0, 0.03)))
        elif r < 0.65:
            ops.append(("rur", rng.uniform(0.0, 0.12)))
        elif r < 0.73:
            ops.append(("skip", rng.choice([0.0, rng.uniform(0.0, 0.06), rng.uniform(0.0, 0.3)])))
        elif r < 0.80:
            ops.append(("snap", rng.random() < 0.6))
        elif r < 0.86:
            # power down, maybe run a little, power up in a (new) mode: CTL 1 -> 0
            ops.append(("w", 3, 0x80 | rng.randrange(128)))
            ops.append(("run", rng.uniform(0.0, 0.01)))
            ops.append(("w", 0, (rng.randrange(4) << 6) | rng.randrange(64)))
            ops.append(("w", 3, rng.randrange(128)))
        else:
            ops.append(("run", rng.uniform(0.0, 0.004)))    # host-sized blocks
    return ops


def compare(params, seed, n_ops, report):
    rng = random.Random(seed)
    ops = ops_for(rng, n_ops)
    a = SSI263(params, dsp="c")
    b = SSI263C(params)
    worst_audio, worst_state, samples, pcm_diff = 0.0, 0.0, 0, 0
    failures = []
    for i, op in enumerate(ops):
        kind = op[0]
        if kind == "w":
            a.write(op[1], op[2])
            b.write(op[1], op[2])
        elif kind == "snap":
            a.snap_pitch = op[1]
            b.snap_pitch = op[1]
        elif kind == "skip":
            a.skip(op[1])
            b.skip(op[1])
        else:
            fn = "run" if kind == "run" else "run_until_request"
            # extreme filter clocks make long runs slow in Python; keep them short
            dur = op[1] if a.regs[4] < 0xF8 else min(op[1], 0.005)
            ya, yb = getattr(a, fn)(dur), getattr(b, fn)(dur)
            if len(ya) != len(yb):
                failures.append("op %d %s: %d vs %d samples" % (i, op, len(ya), len(yb)))
                break
            samples += len(ya)
            if len(ya):
                d = max(abs(x - y) for x, y in zip(ya, yb))
                worst_audio = max(worst_audio, d)
                pa, pb = a.dsp.pcm16(ya, 1.0), b.dsp.pcm16(yb, 1.0)
                if pa != pb:
                    ia = struct.unpack("<%dh" % len(ya), pa)
                    ib = struct.unpack("<%dh" % len(yb), pb)
                    pcm_diff = max(pcm_diff, max(abs(x - y) for x, y in zip(ia, ib)))
        if (a.request != b.request or list(a.regs) != list(b.regs) or a.mode != b.mode
                or a.time != b.time or a.snap_pitch != b.snap_pitch):
            failures.append("op %d %s: request %s/%s mode %s/%s regs %s/%s time %r/%r snap %s/%s"
                            % (i, op, a.request, b.request, a.mode, b.mode, list(a.regs), list(b.regs),
                               a.time, b.time, a.snap_pitch, b.snap_pitch))
            break
        sa, sb = py_state(a), c_state(b)
        bad = None
        for k in sa:
            x, y = float(sa[k]), float(sb[k])
            if k in EXACT or k.startswith("latch_"):
                if x != y:
                    bad = "%s %r vs %r" % (k, x, y)
                    break
            else:
                rel = abs(x - y) / max(1.0, abs(x))
                worst_state = max(worst_state, rel)
                if rel > STATE_TOL:
                    bad = "%s %r vs %r" % (k, x, y)
                    break
        if bad:
            failures.append("op %d %s: %s" % (i, op, bad))
            break
    ok = not failures and worst_audio <= AUDIO_TOL and pcm_diff <= 1
    report.append((ok, params, seed, len(ops), samples, worst_audio, worst_state, pcm_diff, failures))
    return ok


def main():
    long_run = "--long" in sys.argv
    seeds = range(6) if long_run else range(2)
    n_ops = 160 if long_run else 60
    t0 = time.perf_counter()
    report = []
    for sw in SWITCHES:
        for seed in seeds:
            compare(sw, 1000 + seed, n_ops, report)
    bad = [r for r in report if not r[0]]
    total = sum(r[4] for r in report)
    print("check_native_core: python %s %d-bit, %d sessions, %.1f s of audio, %.1f s"
          % (sys.version.split()[0], 8 * struct.calcsize("P"), len(report), total / 44100.0,
             time.perf_counter() - t0))
    print("  worst audio |diff| %.3g, worst state rel diff %.3g, worst PCM diff %d LSB"
          % (max(r[5] for r in report), max(r[6] for r in report), max(r[7] for r in report)))
    for ok, sw, seed, nops, samples, wa, ws, pd, fails in bad:
        print("  FAIL %r seed %d: audio %.3g, PCM %d LSB, %s" % (sw, seed, wa, pd, "; ".join(fails)[:400]))
    if bad:
        sys.exit(1)
    print("  all equal")


if __name__ == "__main__":
    main()
