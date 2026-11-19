"""The Speak-Out board on MAME's V40 core, in-process: src/csrc/speakout (speakout_v40.dll) through ctypes.

The default backend of hosts/speakout.py's SpeakOut. The board holds the CPU, the memory, the reduced ICU and SCU; the chip stays the
caller's, which applies the board's writes (so_board.h).

The DLL is looked for in: bin/<arch>/ beside this file; SSI263_SPEAKOUT_V40_DLL (the file); then the research tree's
nvda/dist/speakout-lib/<arch>/ (built by src/csrc/speakout/build_board.py), found upwards from this file, from
src/hosts or from a built add-on's engine folder.  On Linux, libspeakout_v40.so from build/linux (build_linux.sh).
"""
import ctypes
import os
import struct
from ctypes import c_char_p, c_int, c_long, c_size_t, c_uint8, c_uint32, c_uint64, c_void_p

_lib = None
_P = c_void_p


class _Write(ctypes.Structure):
    _fields_ = [("reg", c_uint8), ("val", c_uint8)]


def _candidates():
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    name = "speakout_v40.dll" if os.name == "nt" else "libspeakout_v40.so"
    here = os.path.dirname(os.path.abspath(__file__))
    yield os.path.join(here, "bin", arch, name)
    env = os.environ.get("SSI263_SPEAKOUT_V40_DLL")
    if env:
        yield env
    d = here
    for _ in range(6):
        yield os.path.join(d, "nvda", "dist", "speakout-lib", arch, name)
        yield os.path.join(d, "speakout-lib", arch, name)
        yield os.path.join(d, "build", "linux", name)          # build_linux.sh
        d = os.path.dirname(d)


def _load():
    global _lib
    if _lib is None:
        tried = []
        for path in _candidates():
            if os.path.isfile(path):
                lib = ctypes.CDLL(os.path.abspath(path))
                break
            tried.append(path)
        else:
            raise OSError("the Speak-Out board library not found (build it: src/csrc/speakout/build_board.py); "
                          "tried %s" % tried)
        lib.so_create.restype = _P
        lib.so_create.argtypes = []
        lib.so_destroy.argtypes = [_P]
        lib.so_load_hex.argtypes = [_P, c_char_p, c_size_t]
        lib.so_load_hex.restype = c_long
        lib.so_power_on.argtypes = [_P]
        lib.so_offer.argtypes = [_P, c_int]
        lib.so_offer.restype = c_int
        for fn in (lib.so_run_steps, lib.so_run_cycles, lib.so_run_steps_unicorn):
            fn.argtypes = [_P, c_uint64]
            fn.restype = c_uint64
        lib.so_send.argtypes = [_P, c_char_p, c_int]
        lib.so_drop_input.argtypes = [_P]
        lib.so_input_queued.argtypes = [_P]
        lib.so_input_queued.restype = c_int
        lib.so_input_count.argtypes = [_P]
        lib.so_input_count.restype = c_int
        lib.so_writes.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(_Write))]
        lib.so_writes.restype = c_int
        lib.so_clear_writes.argtypes = [_P]
        lib.so_tx.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
        lib.so_tx.restype = c_int
        lib.so_clear_tx.argtypes = [_P]
        lib.so_read.argtypes = [_P, c_uint32, ctypes.c_char_p, c_int]
        for fn in (lib.so_cycles, lib.so_steps):
            fn.argtypes = [_P]
            fn.restype = c_uint64
        lib.so_cpu_state.argtypes = [_P, ctypes.POINTER(c_uint32)]
        lib.so_icu_state.argtypes = [_P, ctypes.POINTER(c_int)]
        _lib = lib
    return _lib


class Board:
    """One Speak-Out: the firmware (Intel HEX) loaded and powered on."""

    def __init__(self, hex_path):
        self._lib = lib = _load()
        self._h = lib.so_create()
        if not self._h:
            raise MemoryError("so_create failed")
        with open(hex_path, "rb") as f:
            text = f.read()
        if lib.so_load_hex(self._h, text, len(text)) < 0:
            raise ValueError("malformed Intel HEX: %s" % hex_path)
        lib.so_power_on(self._h)
        self._wp = ctypes.POINTER(_Write)()

    def close(self):
        if self._h:
            self._lib.so_destroy(self._h)
            self._h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def offer(self, chip_request):
        """speakout.py's slice start (so_offer): the IR taken, or -1."""
        return self._lib.so_offer(self._h, 1 if chip_request else 0)

    def run_steps(self, n):
        return self._lib.so_run_steps(self._h, n)

    def run_cycles(self, n):
        return self._lib.so_run_cycles(self._h, n)

    def run_steps_unicorn(self, n):
        """n instructions as Unicorn counts them (so_board.h): for comparing the cores."""
        return self._lib.so_run_steps_unicorn(self._h, n)

    def send(self, data):
        self._lib.so_send(self._h, bytes(data), len(data))

    def drop_input(self):
        self._lib.so_drop_input(self._h)

    def input_queued(self):
        return bool(self._lib.so_input_queued(self._h))

    def input_count(self):
        return self._lib.so_input_count(self._h)

    def take_writes(self):
        """The chip writes since the last call, in order: [(reg, value)]."""
        n = self._lib.so_writes(self._h, ctypes.byref(self._wp))
        out = [(self._wp[i].reg, self._wp[i].val) for i in range(n)]
        self._lib.so_clear_writes(self._h)
        return out

    def take_tx(self):
        """The bytes the box sent on its serial line since the last call, in order (so_board.h so_tx)."""
        p = ctypes.POINTER(ctypes.c_ubyte)()
        n = self._lib.so_tx(self._h, ctypes.byref(p))
        out = [p[i] for i in range(n)]
        self._lib.so_clear_tx(self._h)
        return out

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        self._lib.so_read(self._h, addr, buf, n)
        return buf.raw

    def cycles(self):
        return self._lib.so_cycles(self._h)

    def steps(self):
        return self._lib.so_steps(self._h)

    def cpu_state(self):
        """{ps, ip, psw, halted, fault, undefined, undefined_at}"""
        a = (c_uint32 * 7)()
        self._lib.so_cpu_state(self._h, a)
        return dict(zip(("ps", "ip", "psw", "halted", "fault", "undefined", "undefined_at"), a))

    def icu_state(self):
        a = (c_int * 2)()
        self._lib.so_icu_state(self._h, a)
        return {"imr": a[0], "isr": a[1]}
