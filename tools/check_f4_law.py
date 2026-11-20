"""The voice path's F4 on its own capacitor bank, tuned by the F3 code (params f4_tuning 'code', engine v0.21).

The SC-02 prototype latches one 4-bit code for F3 and F4 in the same scan slot; each drives a capacitor bank of its
own (Henri Asseily's reading of the prototype schematic, issue 31).  F4's bank has 1670 pF fixed and 200, 400, 820
and 1620 pF switched, low bit first, so its centre follows  (2 sin(pi f / fc))^2 = K (C0 + the set bits' weights).

  LAW, Python reference and C core, in the chip's own state: R, U, AH, E and IE held until the tract has settled;
  the F4 section's centre and width, read back from its coefficients, must be the law at the latched F3 code and
  the one width, to 1e-9.
  FOLLOWS: AH settled, then R (the code falls from 11 to 1), then PA, sampled every 0.25 ms: at every sample F4 is
  the law at the F3 code latched then -- F4 is retuned at the latch that retunes F3, through a transition and
  through PA -- and the transition passes through at least five codes.
  PARITY: Python and C have the same code at every sample, and the same coefficients to 1e-12.

    python tools/check_f4_law.py            exit 1 on any failure

Control (F4_LAW_BREAK=fixed): v0.20's F4 (f4_tuning 'fixed': f4_ratio 0.163, bw_ratio[3] 0.03) -- the law and the
following must fail in both engines.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from ssi263.chip import FIELDS, SSI263  # noqa: E402
from ssi263.native import SSI263C       # noqa: E402

FS = 44100
RATE = 4
STEP = 0.00025
TOL = 1e-9
K, C0, W, BW = 0.0495, 1670.0 / 200.0, (1.0, 2.0, 820.0 / 200.0, 1620.0 / 200.0), 0.0076
PARAMS = {"f4_tuning": "fixed"} if os.environ.get("F4_LAW_BREAK") == "fixed" else {}
LATCH_F3 = 15 + len(FIELDS) + FIELDS.index("F3")     # ssi263_state: 15 scalars, cur[], latch[], ...
failures = []


def ok(cond, name, detail):
    print("%-5s %-28s %s" % ("ok" if cond else "FAIL", name, detail))
    if not cond:
        failures.append(name)


def law(code):
    """F4's centre, f / fc, at an F3 code"""
    cap = C0 + sum(w for i, w in enumerate(W) if code >> i & 1)
    return math.asin(math.sqrt(K * cap) / 2.0) / math.pi


def f4(chip):
    """(the latched F3 code, F4's a1, a2)"""
    if isinstance(chip, SSI263C):
        s = chip.state()
        return int(s[LATCH_F3]), s[-2], s[-1]
    return chip.latch["F3"], chip.sec[3].a1, chip.sec[3].a2


def tuning(a1, a2):
    """(centre f / fc, width B / fc) of a section from its coefficients"""
    r = math.sqrt(a2)
    return math.acos(max(-1.0, min(1.0, a1 / (2.0 * r)))) / (2.0 * math.pi), -math.log(r) / math.pi


def start(make, name):
    chip = make()
    ph = next(k for k, v in chip.rom.names.items() if v == name)
    for r, v in ((3, 0x80), (0, 0xC0), (3, 0x70), (2, RATE << 4), (4, 0xE7), (1, (16 << 3) | 7), (3, 0x73), (0, ph)):
        chip.write(r, v)
    chip.run(0.6)
    return chip


def trace(chip, seconds):
    out = []
    for _ in range(int(round(seconds / STEP))):
        chip.run(STEP)
        out.append(f4(chip))
    return out


def main():
    traces = {}
    for tag, make in (("py", lambda: SSI263(PARAMS, out_rate=FS)), ("C", lambda: SSI263C(PARAMS, out_rate=FS))):
        codes = set()
        for name in ("R", "U", "AH", "E", "IE"):
            code, a1, a2 = f4(start(make, name))
            centre, width = tuning(a1, a2)
            codes.add(code)
            ok(abs(centre - law(code)) <= TOL and abs(width - BW) <= TOL, "LAW %s %s" % (tag, name),
               "F3 code %2d: F4 at %.4f of the clock, width %.4f; the law %.4f, %.4f" % (code, centre, width, law(code), BW))
        ok(len(codes) >= 4, "LAW %s codes" % tag, "the five phonemes latch %d different F3 codes" % len(codes))
        chip = start(make, "AH")
        names = {v: k for k, v in chip.rom.names.items()}
        chip.write(0, names["R"])
        tr = trace(chip, 0.4)
        n_r = len(tr)
        chip.write(0, names["PA"])
        tr += trace(chip, 0.3)
        traces[tag] = tr
        for part, seg in (("into R", tr[:n_r]), ("through PA", tr[n_r:])):
            off = [i for i, (code, a1, a2) in enumerate(seg)
                   if abs(tuning(a1, a2)[0] - law(code)) > TOL or abs(tuning(a1, a2)[1] - BW) > TOL]
            seen = sorted(set(code for code, _, _ in seg))
            ok(not off, "FOLLOWS %s %s" % (tag, part),
               "F4 is the law at the latched F3 code at all %d samples (codes %s)" % (len(seg), seen) if not off else
               "%d of %d samples off the law, the first at %.2f ms (code %d, F4 at %.4f, width %.4f)"
               % (len(off), len(seg), 1000 * STEP * (off[0] + 1), seg[off[0]][0], *tuning(*seg[off[0]][1:])))
            if part == "into R":
                ok(len(seen) >= 5, "FOLLOWS %s codes" % tag, "the transition passes through %d codes" % len(seen))
    # the codes exactly; the coefficients to an ulp or two (cos and asin are the C library's in the C core)
    diff = [i for i, (a, b) in enumerate(zip(traces["py"], traces["C"]))
            if a[0] != b[0] or abs(a[1] - b[1]) > 1e-12 or abs(a[2] - b[2]) > 1e-12]
    ok(not diff and len(traces["py"]) == len(traces["C"]), "PARITY: Python = C",
       "the same code, and coefficients within 1e-12, at all %d samples" % len(traces["py"]) if not diff else
       "they differ, the first at sample %d" % diff[0])
    print("F4 law: %s" % ("all passed" if not failures else "%d FAILED" % len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
