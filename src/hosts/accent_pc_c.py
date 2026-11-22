"""The Accent-PC host in C, in-process: accent_pc.py's AccentPC with its lockstep, board and 8085 (MAME's) in
src/csrc/accentpc (accent_pc.dll), and the same constructor, methods and attributes the add-on uses.  The chip stays
the SSI263C the caller made (accent_pc.dll imports ssi263.dll, so both use the one copy loaded).

The default backend returned by AccentPC() in accent_pc.py, packaged with its native libraries.
SSI263_ACCENT_PC_SLICES=chip turns off the Python host's counting (as_board.h: an acceptance that ends a slice, a TRAP
in EI's shadow) and runs the core's own semantics -- experimental, for comparison only.

The library is looked for in: SSI263_ACCENT_PC_DLL (the file); bin/<arch>/ beside this file; then the research tree's
nvda/dist/accentpc-lib/<arch>/ (built by src/csrc/accentpc/build_board.py), found upwards from this file, from
src/hosts or from a built add-on's engine folder.  On Linux, libaccent_pc.so from build/linux (build_linux.sh), which
takes the chip's functions from the libssi263speech.so ssi263/native.py loaded (made global here first).
"""
import ctypes
import os
import re
import struct
import sys
from array import array

_D, _I, _P = ctypes.c_double, ctypes.c_int, ctypes.c_void_p
_lib = None
_ESC = re.compile("\x1b(?:[=+\\-O][A-Za-z]|[A-Z][0-9A-Z]|\\|~[^~]*~)")   # as accent_pc.py


class _Write(ctypes.Structure):
    _fields_ = [("t", _D), ("reg", _I), ("val", _I)]


def _candidates():
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    name = "accent_pc.dll" if os.name == "nt" else "libaccent_pc.so"
    env = os.environ.get("SSI263_ACCENT_PC_DLL")
    if env:
        yield env
    here = os.path.dirname(os.path.abspath(__file__))
    yield os.path.join(here, "bin", arch, name)
    d = here
    for _ in range(6):
        yield os.path.join(d, "nvda", "dist", "accentpc-lib", arch, name)
        yield os.path.join(d, "accentpc-lib", arch, name)
        yield os.path.join(d, "build", "linux", name)          # build_linux.sh
        d = os.path.dirname(d)


def _load(chip):
    global _lib
    if _lib is not None:
        return _lib
    if os.name != "nt":
        # the chip's functions come from the library native.py loaded: made global so libaccent_pc.so binds to it
        for cls in type(chip).__mro__:                 # SSI263C, or a subclass of it
            chip_lib = getattr(sys.modules.get(cls.__module__), "_lib", None)
            if chip_lib is not None:
                ctypes.CDLL(chip_lib._name, mode=ctypes.RTLD_GLOBAL)
                break
    tried = []
    for path in _candidates():
        if os.path.isfile(path):
            lib = ctypes.CDLL(os.path.abspath(path))
            break
        tried.append(path)
    else:
        raise OSError("the Accent-PC library not found (build it: src/csrc/accentpc/build_board.py); tried %s" % tried)
    lib.ash_create_dir.restype = _P
    lib.ash_create_dir.argtypes = [ctypes.c_char_p, _P, _D, ctypes.c_char_p, _I]
    lib.ash_destroy.argtypes = [_P]
    lib.ash_say.argtypes = [_P, ctypes.c_char_p, _I, _I]
    lib.ash_say.restype = _I
    lib.ash_run.argtypes = [_P, _D, _D, ctypes.POINTER(ctypes.POINTER(_D))]
    lib.ash_run.restype = _I
    lib.ash_skip.argtypes = [_P, _D, _D]
    lib.ash_skip.restype = _D
    lib.ash_busy.argtypes = [_P, _D, _D]
    lib.ash_busy.restype = _I
    lib.ash_speaking.argtypes = [_P]
    lib.ash_speaking.restype = _I
    lib.ash_boot.argtypes = [_P, _D]
    lib.ash_boot.restype = _D
    lib.ash_cancel.argtypes = [_P, _D]
    lib.ash_cancel.restype = _D
    lib.ash_get_double.argtypes = [_P, ctypes.c_char_p]
    lib.ash_get_double.restype = _D
    lib.ash_set_double.argtypes = [_P, ctypes.c_char_p, _D]
    lib.ash_get_int.argtypes = [_P, ctypes.c_char_p]
    lib.ash_get_int.restype = _I
    lib.ash_set_int.argtypes = [_P, ctypes.c_char_p, _I]
    lib.ash_writes.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(_Write))]
    lib.ash_writes.restype = _I
    lib.ash_clear_writes.argtypes = [_P]
    lib.ash_tx.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
    lib.ash_tx.restype = _I
    lib.ash_is_speech.argtypes = [ctypes.c_char_p, _I]
    lib.ash_is_speech.restype = _I
    _lib = lib
    return lib


def _double(name):
    def get(self):
        return self._lib.ash_get_double(self._h, name.encode())

    def put(self, v):
        self._lib.ash_set_double(self._h, name.encode(), float(v))
    return property(get, put)


def _flag(name):
    def get(self):
        return bool(self._lib.ash_get_int(self._h, name.encode()))

    def put(self, v):
        self._lib.ash_set_int(self._h, name.encode(), 1 if v else 0)
    return property(get, put)


class AccentPCC:
    """accent_pc.py's AccentPC on the C host.  `writes`, as there, holds (round(chip time, 6), reg, value) while
    keep_writes is set; `on_write(t, reg, val)` sees each with its exact chip time (tests)."""

    def __init__(self, rom_dir, chip=None, out_rate=44100, cpu_hz=3_072_000, tick_hz=0.0, switches=0x00, turbo=8.0,
                 slices=None):
        if chip is None:
            try:
                from .ssi263.native import SSI263C      # inside an add-on: the engine is a sibling package
            except ImportError:                          # the research tree: src/ on sys.path
                from ssi263.native import SSI263C
            chip = SSI263C(out_rate=out_rate)
        self.chip = chip
        self._lib = _load(chip)
        err = ctypes.create_string_buffer(256)
        # the folder as os.fsencode gives it: UTF-8 on Windows (PEP 529), which bl_fopen.h opens through _wfopen.
        # "mbcs" raised on a name outside the code page and turned one whose ANSI bytes are UTF-8 into another name
        # (test_host_paths.py)
        self._h = self._lib.ash_create_dir(os.fsencode(rom_dir), chip._c, float(chip.out_rate), err, 256)
        if not self._h:
            raise RuntimeError("accent_pc: %s" % err.value.decode("latin-1", "replace"))
        self.cpu_hz = cpu_hz
        self.turbo = turbo
        self.tick_hz = tick_hz
        self.switches = switches
        slices = slices or os.environ.get("SSI263_ACCENT_PC_SLICES", "python")
        if slices not in ("python", "chip"):
            raise ValueError("SSI263_ACCENT_PC_SLICES: python or chip, not %r" % slices)
        self._lib.ash_set_int(self._h, b"python_slices", 1 if slices == "python" else 0)
        self.writes = []
        self._on_write = None
        self.trace = None
        self._keep = False
        self.keep_writes = True

    def close(self):
        h, self._h = getattr(self, "_h", None), None
        if h:
            self._lib.ash_destroy(h)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    # ---- accent_pc.py's attributes -----------------------------------------------------------
    cpu_hz = _double("cpu_hz")
    turbo = _double("turbo")
    tick_hz = _double("tick_hz")
    last_speech = _double("last_speech")
    say_time = _double("say_time")
    preparing = _flag("preparing")

    @property
    def switches(self):
        return self._lib.ash_get_int(self._h, b"switches")

    @switches.setter
    def switches(self, v):
        self._lib.ash_set_int(self._h, b"switches", int(v))

    @property
    def on_write(self):
        return self._on_write

    @on_write.setter
    def on_write(self, fn):
        self._drain()
        self._on_write = fn
        self._lib.ash_set_int(self._h, b"log_writes", 1 if (fn or self._keep) else 0)

    @property
    def keep_writes(self):
        return self._keep

    @keep_writes.setter
    def keep_writes(self, on):
        self._drain()
        self._keep = bool(on)
        self._lib.ash_set_int(self._h, b"log_writes", 1 if (on or self._on_write) else 0)

    @property
    def latch40(self):
        return self._lib.ash_get_int(self._h, b"latch40")

    @property
    def speaking(self):
        return bool(self._lib.ash_speaking(self._h))

    @property
    def tx(self):
        p = ctypes.POINTER(ctypes.c_ubyte)()
        n = self._lib.ash_tx(self._h, ctypes.byref(p))
        return bytearray(p[:n]) if n else bytearray()

    def get(self, name):
        """the board's and host's counters by name (as_host.h): "splits", "ei_traps", "pc", ..."""
        return self._lib.ash_get_int(self._h, name.encode())

    def _drain(self):
        if not getattr(self, "_h", None):
            return
        p = ctypes.POINTER(_Write)()
        n = self._lib.ash_writes(self._h, ctypes.byref(p))
        for i in range(n):
            w = p[i]
            if self._keep:
                self.writes.append((round(w.t, 6), w.reg, w.val))
            if self._on_write is not None:
                self._on_write(w.t, w.reg, w.val)
        self._lib.ash_clear_writes(self._h)

    # ---- the host interface (as accent_pc.py) --------------------------------------------------
    def say(self, text, speech=None, background=False):
        data = text.encode("latin-1", "replace")
        if not data:
            return
        if speech is None:
            speech = any(ch.isalnum() for ch in _ESC.sub("", text))
        if not self._lib.ash_say(self._h, data, len(data), 1 if speech else 0):
            raise MemoryError("accent_pc: the input was refused (out of memory)")

    def run(self, seconds, step=0.0005):
        p = ctypes.POINTER(_D)()
        n = self._lib.ash_run(self._h, seconds, step, ctypes.byref(p))
        if n < 0:
            raise MemoryError("accent_pc: out of memory")
        out = array("d", bytes(8 * n))
        if n:
            ctypes.memmove(out.buffer_info()[0], p, 8 * n)
        self._drain()
        return out

    def skip(self, seconds, step=0.002):
        t = self._lib.ash_skip(self._h, seconds, step)
        self._drain()
        return t

    def busy(self, quiet=0.03, patience=1.5):
        return bool(self._lib.ash_busy(self._h, quiet, patience))

    def boot(self, limit=4.0, state=None):
        self._lib.ash_boot(self._h, limit)
        self._drain()

    def cancel(self, limit=0.6):
        t = self._lib.ash_cancel(self._h, limit)
        self._drain()
        return t

    def state(self):
        g = self.get
        im = g("im")
        return ("pc %04X sp %04X ie %s masks %d latch40 %02X usart %02X rx %d/%s request %s t %.3f "
                "last_speech %.3f preparing %s [C host: splits %d, ei_traps %d]"
                % (g("pc"), g("sp"), bool(im & 8), im & 7, g("latch40"), g("usart_cmd"), g("rx"), bool(g("rx_ready")),
                   self.chip.request, self.chip.time, self.last_speech, self.preparing, g("splits"), g("ei_traps")))
