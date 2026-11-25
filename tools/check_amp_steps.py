"""The R3 amplitude reaches the output as its slew, not in whole-code steps (params amp_steps 'smooth', 2026-10-17).

The Accent writes a new amplitude with nearly every phoneme load (9 -> 12 -> 4 ...).  Through v0.18 the output volume
was the slew's nearest code, rint(amp) / 15: a gain step in the running waveform at every code, which Tomi heard as the
Accent voices' grainy ticking; his chip on the Accent's own writes has none.

  RAMP, Python reference and C core: AH at amplitude 9, then reloaded at 12 (the Accent's way), against AH held at 12
  throughout.  The volume acts after the tract on voice and noise alike, so where the held render sounds their ratio
  follows the volume (not exactly: the output resampling and the carrier come after it -- a controlled smoothness
  measure, Astra, Reply 244): during the reload's slew it must rise smoothly (no sample-to-sample change above STEP of
  the held level; the ideal whole-code step is 1/12 there, 0.067 as measured) and arrive at 1.
  HELD: with one amplitude written and settled, 'smooth' and 'code' render sample for sample alike (the Speak-Out and
  the Blazie hold one amplitude).
  PARITY: Python and C sample for sample (within 1e-9).

    python tools/check_amp_steps.py            exit 1 on any failure

Control (AMP_STEPS_BREAK=code): the v0.18 whole-code steps -- RAMP must fail, for Python and C, and HELD still pass.
Plain Python (no numpy): the Linux suite runs on boxes without it (the Pi).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from ssi263.chip import SSI263          # noqa: E402
from ssi263.native import SSI263C       # noqa: E402

FS = 44100
STEP = 0.02
MODE = "code" if os.environ.get("AMP_STEPS_BREAK") == "code" else "smooth"
SETUP = [(0, 0xC0), (3, 0xF0), (2, 0xE8), (3, 0x70), (4, 0xE7), (2, 0x77), (0, 0xC0)]   # the Accent-mini's start
failures = []


def ok(cond, name, detail):
    print("%-5s %-42s %s" % ("ok" if cond else "FAIL", name, detail))
    if not cond:
        failures.append(name)


def render(make, amps, mode):
    """PA, then AH loaded at amps[0] for 150 ms, reloaded at amps[1] for 250 ms (open loop)"""
    chip = make({"amp_steps": mode})
    ah = next(k for k, v in chip.rom.names.items() if v == "AH")
    for r, v in SETUP:
        chip.write(r, v)
    out = [chip.run(0.03)]
    for amp, dur in zip(amps, (0.15, 0.25)):
        for r, v in ((4, 0xE7), (3, 0x30 | amp), (2, 0xB8), (1, 0x77), (0, ah)):
            chip.write(r, v)
        out.append(chip.run(dur))
    return [float(v) for v in chip.dsp.concat(out)], int(round(0.18 * FS))


def max_abs_diff(a, b):
    """the largest |a - b|, sample for sample (unequal lengths: infinite, as they cannot match)"""
    return max((abs(x - y) for x, y in zip(a, b)), default=0.0) if len(a) == len(b) else float("inf")


def main():
    renders = {}
    for tag, make in (("py", lambda p: SSI263(p, out_rate=FS)), ("C", lambda p: SSI263C(p, out_rate=FS))):
        ramp, at = render(make, (9, 12), MODE)
        held, _ = render(make, (12, 12), MODE)
        renders[tag] = ramp
        seg = slice(at, len(held))
        h, r = held[seg], ramp[seg]
        loudest = max(abs(v) for v in h)
        idx = [i for i, v in enumerate(h) if abs(v) > 0.2 * loudest]
        ratio = [r[i] / h[i] for i in idx]
        jumps = [abs(ratio[k + 1] - ratio[k]) for k in range(len(idx) - 1)
                 if idx[k + 1] - idx[k] <= 2]                    # neighbouring sounding samples only
        end = sum(ratio[-200:]) / len(ratio[-200:])
        ok(max(jumps) < STEP, "RAMP %s: the volume rises smoothly" % tag,
           "largest change between neighbouring samples %.4f of the held level (want < %.2f; a code is %.3f)"
           % (max(jumps), STEP, 1 / 12))
        ok(abs(end - 1) < 0.005 and ratio[0] < 0.8, "RAMP %s: from 9 to 12" % tag,
           "starts at %.3f (9/12 = 0.750), ends at %.4f" % (ratio[0], end))
        a, _ = render(make, (12, 12), "smooth")
        b, _ = render(make, (12, 12), "code")
        settled = slice(int(0.2 * FS), len(a))
        ok(a[settled] == b[settled], "HELD %s: one amplitude, smooth = code" % tag,
           "max |diff| %.3g once settled" % max_abs_diff(a[settled], b[settled]))
    d = max_abs_diff(renders["py"], renders["C"])
    ok(d < 1e-9, "PARITY: Python = C", "max |diff| %.3g" % d)
    print("amp steps: %s" % ("all passed" if not failures else "%d FAILED" % len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
