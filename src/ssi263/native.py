"""The chip in C (../csrc/ssi263.c), with the same host interface as chip.SSI263.

chip.py stays the reference -- research, tuning and Astra's reviews happen there.  This
class runs the same model in C: the NVDA add-ons use it (Python 3.7 to 3.13, 32- and
64-bit, no numpy), and it is the core a SAPI or Linux front end would build on.
tools/check_native_core.py holds the two together: A/R timing and chip time exact,
audio within float noise, PCM identical.

Parameters and the ROM reading still come from params.py and rom.py, so an override
dict works here exactly as it does for chip.SSI263.  There is no event log.
"""
import ctypes
import struct
from array import array

from . import dsp as _dsp
from .chip import FIELDS
from .params import Params
from .rom import Rom

_c_double = ctypes.c_double
_lib = None


class _Params(ctypes.Structure):
    """Mirror of ssi263_params (csrc/ssi263.h): all doubles, in the header's order."""
    _fields_ = [
        ("xck_hz", _c_double), ("frame_xck_cycles", _c_double), ("pitch_xck_div", _c_double),
        ("glide_xck_cycles_per_count", _c_double), ("glide_field_mult", _c_double * 8),
        ("art_codes_per_frame", _c_double * 8), ("art_amp_mult", _c_double), ("latch_round", _c_double),
        ("amp_slew_per_phoneme", _c_double), ("f_K", _c_double * 3), ("f_C0", _c_double * 3),
        ("f_w", (_c_double * 4) * 3), ("f4_ratio", _c_double), ("f5_ratio", _c_double),
        ("section_bilinear", _c_double), ("bw_ratio", _c_double * 5), ("nas_f2_bw_gain", _c_double),
        ("closure_enable", _c_double), ("closure_delay_frames", _c_double),
        ("closure_release_frames", _c_double), ("closure_release_b01", _c_double),
        ("closure_hold_delay_frames", _c_double), ("has_burst_hold", _c_double),
        ("burst_hold_frames", _c_double), ("burst_tail_level", _c_double),
        ("closure_noise_at_release", _c_double), ("closure_fraction", _c_double),
        ("has_closure_floor", _c_double), ("closure_floor_db", _c_double), ("closure_va", _c_double),
        ("closure_ramp_ms", _c_double), ("closure_reopen", _c_double), ("pulse_fractional", _c_double),
        ("lfsr_bits", _c_double), ("noise_clock_ratio", _c_double), ("noise_shaper", _c_double),
        ("noise_shaper_ratio", _c_double), ("noise_shaper_bw", _c_double), ("noise_into_f2", _c_double),
        ("noise_into_f5", _c_double), ("noise_route_b02", _c_double),
        ("noise_route_b02_w", (_c_double * 2) * 2), ("noise_f5_output", _c_double),
        ("noise_per_path", _c_double), ("noise_gain", _c_double), ("hp_ratio", _c_double),
        ("carrier_rel_db", _c_double), ("carrier_h2_db", _c_double),
        ("carrier_when_powered_down", _c_double), ("output_oversample", _c_double),
        ("output_lowpass_hz", _c_double), ("output_gain", _c_double),
        ("release_lookahead", _c_double), ("lookahead_lead_frames", _c_double),
        ("late_release_burst", _c_double), ("fricative_precharge", _c_double),
        ("closure_onto_silence", _c_double),
    ]


def _choice(p, name, one, choices):
    v = p[name]
    if v not in choices:
        raise ValueError("%s = %r: the C core knows %s" % (name, v, choices))
    return 1.0 if v == one else 0.0


def params_struct(p):
    """params.py values -> ssi263_params.  Switches are strict: an unknown string raises."""
    s = _Params()
    for name in ("xck_hz", "frame_xck_cycles", "pitch_xck_div", "glide_xck_cycles_per_count",
                 "art_amp_mult", "amp_slew_per_phoneme", "f4_ratio", "f5_ratio", "nas_f2_bw_gain",
                 "closure_delay_frames", "closure_release_frames", "closure_hold_delay_frames",
                 "burst_tail_level", "closure_ramp_ms", "lfsr_bits", "noise_clock_ratio",
                 "noise_into_f2", "noise_into_f5", "noise_gain", "hp_ratio", "carrier_rel_db",
                 "carrier_h2_db", "output_oversample", "output_lowpass_hz", "output_gain",
                 "lookahead_lead_frames"):
        setattr(s, name, float(p[name]))
    for name in ("closure_enable", "closure_release_b01", "closure_noise_at_release", "closure_reopen",
                 "carrier_when_powered_down", "release_lookahead", "late_release_burst",
                 "fricative_precharge", "closure_onto_silence"):
        setattr(s, name, 1.0 if p[name] else 0.0)
    for i, v in enumerate(p["glide_field_mult"]):
        s.glide_field_mult[i] = v
    for i, v in enumerate(p["art_codes_per_frame"]):
        s.art_codes_per_frame[i] = v
    for i, v in enumerate(p["bw_ratio"]):
        s.bw_ratio[i] = v
    for fi, f in enumerate(("f1", "f2", "f3")):
        s.f_K[fi] = p[f + "_K"]
        s.f_C0[fi] = p[f + "_C0"]
        for i, v in enumerate(p[f + "_w"]):
            s.f_w[fi][i] = v
    s.latch_round = _choice(p, "latch_quantize", "round", ("round", "floor"))
    s.section_bilinear = _choice(p, "section_numerator", "bilinear", ("bilinear", "allpole"))
    s.closure_fraction = _choice(p, "closure_timing", "fraction", ("fraction", "frames"))
    s.closure_va = _choice(p, "closure_target", "va", ("vol", "va"))
    s.pulse_fractional = _choice(p, "pulse_place", "fractional", ("fractional", "nearest"))
    s.noise_route_b02 = _choice(p, "noise_route", "b02", ("b02", "fixed"))
    s.noise_f5_output = _choice(p, "noise_f5_point", "output", ("input", "output"))
    s.noise_per_path = _choice(p, "noise_route_mode", "per_path", ("per_path", "switch"))
    s.has_burst_hold = 0.0 if p["burst_hold_frames"] is None else 1.0
    s.burst_hold_frames = p["burst_hold_frames"] or 0.0
    s.has_closure_floor = 0.0 if p["closure_floor_db"] is None else 1.0
    s.closure_floor_db = p["closure_floor_db"] or 0.0
    shp = p["noise_shaper"]
    if shp == "flat":
        s.noise_shaper = 0.0
    elif shp[0] == "bp":
        s.noise_shaper, s.noise_shaper_ratio, s.noise_shaper_bw = 2.0, shp[1], shp[2]
    else:
        s.noise_shaper, s.noise_shaper_ratio, s.noise_shaper_bw = 1.0, shp[0], shp[1]
    for i in range(2):
        for j in range(2):
            s.noise_route_b02_w[i][j] = p["noise_route_b02"][i][j]
    return s


def rom_bytes(rom):
    """64 entries of F1 F2 F3 NAS VA FA closure_clear class1 class2 (ssi263.h)."""
    out = bytearray()
    for code in range(64):
        e = rom.entry(code)
        out += bytes([e[f] for f in FIELDS] + [e["closure_clear"], e["class1"], e["class2"]])
    return bytes(out)


def _load():
    global _lib
    if _lib is None:
        lib = _dsp.get("c").lib                    # the same ssi263.dll
        P = ctypes.c_void_p
        lib.ssi263_params_size.restype = ctypes.c_int
        lib.ssi263_new.restype = P
        lib.ssi263_new.argtypes = [ctypes.POINTER(_Params), ctypes.c_char_p, _c_double]
        lib.ssi263_free.argtypes = [P]
        lib.ssi263_write.argtypes = [P, ctypes.c_int, ctypes.c_int]
        for fn in (lib.ssi263_reg,):
            fn.argtypes = [P, ctypes.c_int]
            fn.restype = ctypes.c_int
        for fn in (lib.ssi263_request, lib.ssi263_mode, lib.ssi263_get_snap_pitch):
            fn.argtypes = [P]
            fn.restype = ctypes.c_int
        lib.ssi263_time.argtypes = [P]
        lib.ssi263_time.restype = _c_double
        lib.ssi263_set_snap_pitch.argtypes = [P, ctypes.c_int]
        for fn in (lib.ssi263_run, lib.ssi263_run_until_request):
            fn.argtypes = [P, ctypes.c_long, ctypes.POINTER(_c_double)]
            fn.restype = ctypes.c_long
        lib.ssi263_skip.argtypes = [P, _c_double]
        lib.ssi263_state.argtypes = [P, ctypes.POINTER(_c_double), ctypes.c_int]
        lib.ssi263_state.restype = ctypes.c_int
        if lib.ssi263_params_size() != ctypes.sizeof(_Params):
            raise RuntimeError("ssi263.dll and native.py disagree on ssi263_params (%d vs %d bytes)"
                               % (lib.ssi263_params_size(), ctypes.sizeof(_Params)))
        _lib = lib
    return _lib


class _Regs:
    def __init__(self, chip):
        self._chip = chip

    def __getitem__(self, i):
        if isinstance(i, slice):
            return [self[j] for j in range(5)[i]]
        return _lib.ssi263_reg(self._chip._c, i)

    def __len__(self):
        return 5

    def __iter__(self):
        return iter(self[:])


class SSI263C:
    def __init__(self, params=None, rom=None, out_rate=44100):
        lib = _load()
        self.p = params if isinstance(params, Params) else Params(params)
        self.rom = rom or Rom.load()
        self.out_rate = float(out_rate)
        self.os = int(self.p["output_oversample"])
        self.dsp = _dsp.get("c")
        self._params = params_struct(self.p)
        self._c = lib.ssi263_new(ctypes.byref(self._params), rom_bytes(self.rom), self.out_rate)
        if not self._c:
            raise MemoryError("ssi263_new failed")
        self.regs = _Regs(self)
        self.log = []                # chip.py's event log is not kept in C

    def __del__(self):
        c, self._c = getattr(self, "_c", None), None
        if c and _lib is not None:
            _lib.ssi263_free(c)

    # ---- host side (as chip.SSI263) ---------------------------------------------------
    @property
    def powered(self):
        return not (self.regs[3] & 0x80)

    @property
    def mode(self):
        m = _lib.ssi263_mode(self._c)
        return None if m < 0 else m

    @property
    def request(self):
        return bool(_lib.ssi263_request(self._c))

    @property
    def time(self):
        return _lib.ssi263_time(self._c)

    @property
    def snap_pitch(self):
        return bool(_lib.ssi263_get_snap_pitch(self._c))

    @snap_pitch.setter
    def snap_pitch(self, on):
        _lib.ssi263_set_snap_pitch(self._c, 1 if on else 0)

    def write(self, addr, value):
        _lib.ssi263_write(self._c, addr, value)

    def _out(self, fn, n):
        buf = array("d", bytes(8 * (n + 1)))
        k = fn(self._c, n, ctypes.cast(buf.buffer_info()[0], ctypes.POINTER(_c_double)))
        del buf[k:]
        return buf

    def run(self, seconds):
        # the sample count is decided here, by Python's round(), as chip.py decides it
        return self._out(_lib.ssi263_run, int(round(seconds * self.out_rate)))

    def run_until_request(self, max_seconds=5.0):
        return self._out(_lib.ssi263_run_until_request, int(max_seconds * self.out_rate))

    def skip(self, seconds):
        _lib.ssi263_skip(self._c, seconds)

    def state(self):
        buf = (_c_double * 64)()
        n = _lib.ssi263_state(self._c, buf, 64)
        return list(buf[:n])


def bits():
    return 8 * struct.calcsize("P")
