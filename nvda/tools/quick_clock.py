"""The Braille Lite's clock keeps real time while the emulator's quick key response runs the CPU fast (issue #78).

Quick key response (the emulator's setting) runs the unit's CPU EMU_QUICK_TURBO (8) times faster after a key, until
the unit's first spoken phoneme (bl_host: turbo while preparing), so its answer comes sooner.  The clock controller
was stepped from the CPU's own cycle count, so every one of those fast cycles was clock time too: a key that brings
no speech moved the unit's time of day up to 8 s for each second, and the gain was saved with the unit for good.  On
a real unit the clock is a chip of its own, with its own time base.

  hurried   the clock set, then 5 s of the chip's time with the CPU 8 times fast and nothing spoken: the clock
            moves 5 s (within a second either way), not 40
  plain     the same 5 s at the CPU's own speed: 5 s too (the clock itself runs right)

    python quick_clock.py [<bl.dll>] [<BL2ENG.BNS> <its state>]          exit 1 on any failure
    QUICK_CLOCK_BREAK=1   bl_host.c bh_clock_hurry_break: the fast cycles counted as clock time, as they were
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [REPO, os.path.join(REPO, "src")]
ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
ENG = os.path.join(REPO, "nvda", "dist", "blazie-build", "synthDrivers", "_ssi263_blazie")
args = sys.argv[1:]
DLL = args[0] if args else os.path.join(REPO, "nvda", "dist", "blazie-lib", ARCH,
                                        "bl.dll" if os.name == "nt" else "libbl.so")
FW = args[1] if len(args) > 2 else os.path.join(ENG, "BL2ENG.BNS")
STATE = args[2] if len(args) > 2 else os.path.join(ENG, "bl2_2003_warm.state")


class BlcTime(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("year", "month", "day", "hour", "minute", "second")]


def seconds(t):
    return ((t.day * 24 + t.hour) * 60 + t.minute) * 60 + t.second


def main():
    if not (os.path.isfile(FW) and os.path.isfile(STATE)):
        print("skipped: the Braille Lite's firmware or state is not there")
        return 0
    from ssi263.native import SSI263C
    from hosts import native_blazie
    NativeBlazie = native_blazie.NativeBlazie
    SSI263C(out_rate=22050)                       # the chip's library first: bl.dll imports it
    lib = native_blazie._load(DLL)
    if os.environ.get("QUICK_CLOCK_BREAK"):
        ctypes.c_int.in_dll(lib, "bh_clock_hurry_break").value = 1
    bad = 0
    for label, turbo in (("plain", None), ("hurried", 8.0)):
        u = NativeBlazie(DLL, FW, STATE, chip=SSI263C(out_rate=22050), out_rate=22050)
        h = ctypes.c_void_p(u._h)
        lib.bh_clock_on.argtypes = [ctypes.c_void_p, ctypes.POINTER(BlcTime), ctypes.c_longlong]
        lib.bh_clock_time.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(BlcTime)]
        lib.bh_set_double.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_double]
        lib.bh_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        start = BlcTime(2026, 1, 15, 12, 0, 0)
        if not lib.bh_clock_on(h, ctypes.byref(start), 1768478400):
            print("FAIL %-9s the clock could not be set" % label)
            bad += 1
            u.close()
            continue
        u.run(1.0)                                # settled at its own speed
        t0, t1 = BlcTime(), BlcTime()
        lib.bh_clock_time(h, 0, ctypes.byref(t0))
        if turbo:
            lib.bh_set_double(h, b"turbo", turbo)
            lib.bh_set_int(h, b"preparing", 1)    # as emu_key does: fast until a spoken phoneme, and none comes
        u.run(5.0)
        lib.bh_clock_time(h, 0, ctypes.byref(t1))
        u.close()
        moved = seconds(t1) - seconds(t0)
        ok = 4 <= moved <= 6
        bad += not ok
        print("%-4s %-9s 5 s of the chip's time%s: the clock moved %d s (%02d:%02d:%02d -> %02d:%02d:%02d)" % (
            "ok" if ok else "FAIL", label, " with the CPU %g times fast" % turbo if turbo else "", moved,
            t0.hour, t0.minute, t0.second, t1.hour, t1.minute, t1.second))
    print("quick clock: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
