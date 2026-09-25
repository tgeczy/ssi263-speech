"""The chip's host-rate output stage and PCM conversion: numpy, or a small C library.

numpy is the engine's reference.  The C path (_bin/<arch>/ssi263.dll, source in
../csrc) is what the NVDA add-ons use: they ship no numpy, so they load in any NVDA from
2021.1 (32-bit Python 3.7) to 2026 (64-bit Python 3.13).  Same arithmetic; the FIR sums
run in a different order, so the two agree to ~1e-15, not bit for bit.

Blocks are numpy arrays on the numpy path and array("d") on the C path; both have len()
and the buffer protocol.  Pick with SSI263(dsp="c"|"numpy"), or SSI263_DSP in the
environment; the default is numpy when it imports, else C.
"""
import ctypes
import math
import os
import struct
from array import array

_HERE = os.path.dirname(os.path.abspath(__file__))
_backends = {}


def get(name=None):
    name = name or os.environ.get("SSI263_DSP") or ("numpy" if _numpy() is not None else "c")
    if name not in _backends:
        _backends[name] = {"numpy": _Numpy, "c": _C}[name]()
    return _backends[name]


def _numpy():
    try:
        import numpy
        return numpy
    except ImportError:
        return None


# ---- numpy: the reference ------------------------------------------------------------
class _Numpy:
    name = "numpy"

    def __init__(self):
        self.np = _numpy()
        if self.np is None:
            raise ImportError("numpy is not available")

    def firwin(self, numtaps, cutoff, fs):
        """Hamming-windowed sinc low-pass, unity DC gain (what scipy's firwin gives)."""
        np = self.np
        n = np.arange(numtaps) - (numtaps - 1) / 2.0
        h = np.sinc(2.0 * cutoff / fs * n) * np.hamming(numtaps)
        return h / h.sum()

    def fine(self, samples):
        return self.np.array(samples, dtype=self.np.float64)

    def decimator(self, fir, os_):
        return _NumpyDecimator(self.np, fir, os_)

    def zeros(self):
        return self.np.zeros(0)

    def concat(self, blocks):
        return self.np.concatenate(blocks) if blocks else self.np.zeros(0)

    def pcm16(self, y, gain):
        np = self.np
        return (np.clip(np.asarray(y) * gain, -1.0, 1.0) * 32767.0).astype("<i2").tobytes()


class _NumpyDecimator:
    def __init__(self, np, fir, os_):
        self.np, self.fir, self.os = np, fir, os_
        self.tail = np.zeros(len(fir) - 1)
        self.phase = 0

    def reset(self):
        self.tail[:] = 0.0

    def process(self, fine):
        np = self.np
        ext = np.concatenate([self.tail, fine])
        y = np.convolve(ext, self.fir, mode="valid")
        self.tail = ext[-(len(self.fir) - 1):]
        out = y[self.phase::self.os]
        self.phase = (self.phase - len(fine)) % self.os
        return out


# ---- C: the add-ons ----------------------------------------------------------------
def _ptr(buf, ctype):
    return ctypes.cast(buf.buffer_info()[0], ctypes.POINTER(ctype))


class _C:
    name = "c"

    def __init__(self):
        arch = "x64" if struct.calcsize("P") == 8 else "x86"
        lib = ctypes.CDLL(os.path.join(_HERE, "_bin", arch, "ssi263.dll"))
        lib.ssi_fir_decimate.restype = ctypes.c_int
        lib.ssi_fir_decimate.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int),
                                         ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
        lib.ssi_pcm16.restype = None
        lib.ssi_pcm16.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.c_double,
                                  ctypes.POINTER(ctypes.c_short)]
        self.lib = lib

    def firwin(self, numtaps, cutoff, fs):
        """numpy's firwin above, in plain Python."""
        c = (numtaps - 1) / 2.0
        h = []
        for i in range(numtaps):
            x = 2.0 * cutoff / fs * (i - c)
            s = 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)
            h.append(s * (0.54 - 0.46 * math.cos(2.0 * math.pi * i / (numtaps - 1))))
        total = sum(h)
        return array("d", [v / total for v in h])

    def fine(self, samples):
        return array("d", samples)

    def decimator(self, fir, os_):
        return _CDecimator(self.lib, fir, os_)

    def zeros(self):
        return array("d")

    def concat(self, blocks):
        out = array("d")
        for b in blocks:
            out.extend(b if isinstance(b, array) else array("d", b))
        return out

    def pcm16(self, y, gain):
        n = len(y)
        if not n:
            return b""
        src = y if isinstance(y, array) and y.typecode == "d" else array("d", y)
        dst = array("h", bytes(2 * n))
        self.lib.ssi_pcm16(_ptr(src, ctypes.c_double), n, gain, _ptr(dst, ctypes.c_short))
        return dst.tobytes()


class _CDecimator:
    def __init__(self, lib, fir, os_):
        self.lib, self.os = lib, os_
        self.taps = array("d", fir)
        self.tail = array("d", bytes(8 * (len(self.taps) - 1)))
        self.phase = ctypes.c_int(0)

    def reset(self):
        self.tail = array("d", bytes(8 * (len(self.taps) - 1)))

    def process(self, fine):
        n = len(fine)
        if not n:
            return array("d")
        if not (isinstance(fine, array) and fine.typecode == "d"):
            fine = array("d", fine)
        out = array("d", bytes(8 * (n // self.os + 1)))
        k = self.lib.ssi_fir_decimate(_ptr(fine, ctypes.c_double), n, _ptr(self.taps, ctypes.c_double),
                                      len(self.taps), _ptr(self.tail, ctypes.c_double),
                                      ctypes.byref(self.phase), self.os, _ptr(out, ctypes.c_double))
        del out[k:]
        return out
