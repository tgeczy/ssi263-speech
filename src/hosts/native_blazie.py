"""The Braille Lite host in-process: hosts/blazie.py's Blazie, with the lockstep, the emulator and the board in C
(src/csrc/blazie: bl.dll), and the same constructor, methods and attributes.  The chip stays the SSI263C the
caller made (bl.dll imports ssi263.dll, so both use the one copy loaded).  nvda/tools/golden/blazie_*.txt gate it:
bns_equiv.py --native.

No pipe and no child process: every emulated step is a function call instead of a round trip.

SSI263_BLAZIE_RECORD=<file>: log every call into the unit (JSON lines, floats exact) so that
nvda/tools/bl_replay.py can play a session back offline, on this host or the Python one.
"""
import ctypes
import json
import os
import sys
from array import array

try:
    from .blazie_host import boot_keys    # packaged in the add-on (synthDrivers/_ssi263_blazie)
except ImportError:
    try:
        from .blazie import boot_keys     # src/hosts as a package
    except ImportError:                   # the research tree: src/ on sys.path
        boot_keys = None
if boot_keys is None:
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from hosts.blazie import boot_keys  # noqa: E402

_D, _I, _P, _S = ctypes.c_double, ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p
WHINES = {None: 0, "hiss": 1, "whine": 2}
_libs = {}


class _Write(ctypes.Structure):
    _fields_ = [("t", _D), ("reg", _I), ("val", _I)]


class RaWrite(ctypes.Structure):
    """run_ahead.h's ra_write: a captured write, its CPU cycle and its segment (acknowledgements before it)"""
    _fields_ = [("cyc", ctypes.c_ulonglong), ("seg", _I), ("reg", ctypes.c_ubyte), ("val", ctypes.c_ubyte)]


class Probe(ctypes.Structure):
    """bl_board.h's bl_probe: the board as the firmware sees it"""
    _fields_ = [("cycles", ctypes.c_ulonglong)] + [(n, _I) for n in (
        "pc", "sp", "iff1", "iff2", "im", "halted", "sleeping", "ssi0", "ssi1", "ssi2", "ssi3", "ssi4", "ssi_ar",
        "ssi_mode", "int1", "key_latched", "int2", "n_live_keys", "queued", "urgent", "host_xoff", "port_a0",
        "port_e0", "asci_cntla", "asci_cntlb", "asci_stat", "asci_asext", "asci_astc")]


class BlazieHostError(RuntimeError):
    """The host lost something the unit did (out of memory): a chip write or serial byte the board could not store, a
    transmitted byte or a logged write the host could not keep (Astra, Reply 112: never silently).  A fault stops the
    host -- busy() and new input raise -- until cancel() abandons the utterance."""


class RunAheadError(BlazieHostError):
    """A run-ahead utterance failed (out of memory: its script is incomplete), or input was refused.  Sticky: busy(),
    say() and send() raise until cancel(); input held for the unit is not delivered over it."""


# run_ahead.h's RA_* states, by number
RA_STATES = ("idle", "capturing", "replaying", "trailing", "final load", "complete", "limit", "error", "cancelled")
FAULT_RUN_AHEAD, FAULT_EVENT = 1, 2     # bl_host.c BH_FAULT_*


def _load(path):
    path = os.path.abspath(path)
    if path in _libs:
        return _libs[path]
    lib = ctypes.CDLL(path)
    lib.bh_create.restype = _P
    lib.bh_create.argtypes = [_S, _S, _P, _D, _D, ctypes.POINTER(ctypes.c_ulonglong), ctypes.POINTER(ctypes.c_ubyte),
                              _I, ctypes.c_ulonglong, _I, ctypes.c_char_p, _I]
    lib.bh_writes.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(_Write))]
    lib.bh_writes.restype = _I
    lib.bh_clear_writes.argtypes = [_P]
    lib.bh_destroy.argtypes = [_P]
    for fn in (lib.bh_send, lib.bh_say):
        fn.argtypes = [_P, ctypes.c_char_p, _I]
        fn.restype = _I
    lib.bh_script.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(RaWrite))]
    lib.bh_script.restype = _I
    lib.bh_pace.argtypes = [_P, ctypes.POINTER(_D), _I]
    lib.bh_pace.restype = _I
    lib.bh_probe.argtypes = [_P, ctypes.POINTER(Probe)]
    lib.bh_memory.argtypes = [_P, _I, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
    lib.bh_memory.restype = _I
    lib.bh_owed.argtypes = [_P]
    lib.bh_owed.restype = _I
    lib.bh_busy.argtypes = [_P, _D, _D]
    lib.bh_busy.restype = _I
    lib.bh_cancel.argtypes = [_P, _D, _D, _D]
    lib.bh_cancel.restype = _D
    lib.bh_skip.argtypes = [_P, _D]
    lib.bh_skip.restype = _D
    lib.bh_run.argtypes = [_P, _D, _D, ctypes.POINTER(ctypes.POINTER(_D))]
    lib.bh_run.restype = _I
    lib.bh_set_whine.argtypes = [_P, _I]
    lib.bh_get_whine.argtypes = [_P]
    lib.bh_get_whine.restype = _I
    lib.bh_get_int.argtypes = [_P, _S]
    lib.bh_get_int.restype = _I
    lib.bh_set_int.argtypes = [_P, _S, _I]
    lib.bh_get_double.argtypes = [_P, _S]
    lib.bh_get_double.restype = _D
    lib.bh_set_double.argtypes = [_P, _S, _D]
    lib.bh_tx.argtypes = [_P, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
    lib.bh_tx.restype = _I
    _libs[path] = lib
    return lib


def _int_attr(name):
    def get(self):
        return self._lib.bh_get_int(self._h, name.encode())

    def put(self, v):
        self._record("set", name, int(v))
        self._lib.bh_set_int(self._h, name.encode(), int(v))
    return property(get, put)


def _float_attr(name, none_is_zero=False):
    def get(self):
        v = self._lib.bh_get_double(self._h, name.encode())
        return None if (none_is_zero and v == 0.0) else v

    def put(self, v):
        self._record("setf", name, 0.0 if v is None else float(v))
        self._lib.bh_set_double(self._h, name.encode(), 0.0 if v is None else float(v))
    return property(get, put)


class NativeBlazie:
    def __init__(self, dll, firmware, state, chip=None, out_rate=44100, menu=(), key_start=None, key_gap=None,
                 board_lowpass_hz=None, status=(), on_write=None):
        """`on_write(t, reg, val)`: every SSI-263 write, with the chip time it was applied at (tests: the C host
        writes the chip directly, so a spy on chip.write sees nothing).  It can be set or cleared at any time."""
        self._on_write = on_write
        rec = os.environ.get("SSI263_BLAZIE_RECORD")
        self._rec = open(rec, "a") if rec else None
        self._record("init", firmware=firmware, state=state, out_rate=out_rate, menu=list(menu), key_start=key_start,
                     key_gap=key_gap, board_lowpass_hz=board_lowpass_hz, status=list(status))
        if chip is None:
            from ssi263.native import SSI263C
            chip = SSI263C(out_rate=out_rate)
        self.chip = chip
        self._record("chip", dict(chip.p), chip.out_rate)
        self.encoding = "latin-1"
        self._lib = _load(dll)
        keys, boot_instr = boot_keys(menu, key_start, key_gap, status)
        at = (ctypes.c_ulonglong * max(1, len(keys)))(*[int(k.split("=")[0]) for k in keys])
        val = (ctypes.c_ubyte * max(1, len(keys)))(*[int(k.split("=")[1], 16) for k in keys])
        err = ctypes.create_string_buffer(256)
        self._h = self._lib.bh_create(firmware.encode("mbcs" if os.name == "nt" else "utf-8"),
                                      state.encode("mbcs" if os.name == "nt" else "utf-8"), chip._c,
                                      float(chip.out_rate), float(board_lowpass_hz or 0.0), at, val, len(keys),
                                      boot_instr, 1 if on_write else 0, err, 256)
        if not self._h:
            raise RuntimeError("bl.dll: %s" % err.value.decode("latin-1", "replace"))
        # Regression-test override; the native host enables both cancel protections by default.
        # A value of 0 deliberately restores the old lockstep cancel race (bl_host.h "cancel_settle").
        settle = os.environ.get("SSI263_BLAZIE_CANCEL_SETTLE")
        if settle:
            self._record("set", "cancel_settle", int(settle))
            self._lib.bh_set_int(self._h, b"cancel_settle", int(settle))
        self._drain()

    def _record(self, *call, **kw):
        if self._rec:
            self._rec.write(json.dumps(list(call) + ([kw] if kw else [])) + "\n")
            self._rec.flush()

    @property
    def on_write(self):
        return self._on_write

    @on_write.setter
    def on_write(self, fn):
        self._drain()
        self._on_write = fn
        if getattr(self, "_h", None):
            self._lib.bh_set_int(self._h, b"log_writes", 1 if fn else 0)

    def _drain(self):
        if self._on_write is None or not getattr(self, "_h", None):
            return
        p = ctypes.POINTER(_Write)()
        n = self._lib.bh_writes(self._h, ctypes.byref(p))
        for i in range(n):
            self._on_write(p[i].t, p[i].reg, p[i].val)
        self._lib.bh_clear_writes(self._h)
        lost = self._lib.bh_get_int(self._h, b"writes_lost")
        if lost:
            self._lib.bh_set_int(self._h, b"writes_lost", 0)
            raise BlazieHostError("the write log lost %d write(s): out of memory, the log is incomplete" % lost)

    @property
    def fault(self):
        """bl_host.c's BH_FAULT_* bits now (0: none)"""
        return self._lib.bh_get_int(self._h, b"fault")

    def _raise_fault(self, what):
        f = self.fault
        if f & FAULT_EVENT:
            raise BlazieHostError("%s: the board lost a chip write or serial byte (out of memory); cancel() to "
                                  "abandon the utterance" % what)
        raise RunAheadError("%s: the run-ahead utterance failed (out of memory, its script is incomplete); cancel() to "
                            "abandon it" % what)

    def close(self):
        h = getattr(self, "_h", None)
        if h:
            self._tx_closed = self.tx          # blazie.py's tx outlives close()
            self._h = None
            self._lib.bh_destroy(h)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    # ---- blazie.py's attributes ----------------------------------------------------------
    sent_f = _int_attr("sent_f")
    echo_f = _int_attr("echo_f")
    _stale_f = _int_attr("stale_f")
    turbo = _float_attr("turbo")
    prep_step = _float_attr("prep_step", none_is_zero=True)
    last_speech = _float_attr("last_speech")
    say_time = _float_attr("say_time")
    cancel_cut = _float_attr("cancel_cut")
    cancel_quiet = _float_attr("cancel_quiet")
    # the unit run ahead of the chip (src/csrc/blazie/run_ahead.h; EXPERIMENTAL, opt-in): 0 = today's lockstep (the
    # default), 1 = on from the next say.  The Python pipe host has no such mode.  run_ahead_break: the tests' controls.
    run_ahead = _int_attr("run_ahead")
    # note-taking mode's line-start lift for the next line said (bl_host.c; the NVDA driver's lineLift): 1 arms
    # it, the line's second pitch marker ends it, a cancel drops it
    line_lift = _int_attr("line_lift")
    run_ahead_break = _int_attr("run_ahead_break")
    log_ar = _int_attr("log_ar")

    @property
    def run_ahead_state(self):
        return RA_STATES[self._lib.bh_get_int(self._h, b"run_ahead_state")]

    def geti(self, name):
        """a host integer by name (bl_host.h: run_ahead_end, run_ahead_played, held, port_a0 ...)"""
        return self._lib.bh_get_int(self._h, name.encode())

    def script(self):
        """the current (or last) run-ahead script: [(cycle, segment, reg, val)]"""
        p = ctypes.POINTER(RaWrite)()
        n = self._lib.bh_script(self._h, ctypes.byref(p))
        return [(p[i].cyc, p[i].seg, p[i].reg, p[i].val) for i in range(n)]

    def pace(self, times):
        """lane 1: the next run-ahead utterance's writes at these chip times"""
        arr = (_D * max(1, len(times)))(*times)
        if not self._lib.bh_pace(self._h, arr, len(times)):
            raise RunAheadError("bh_pace: out of memory")

    def probe(self):
        p = Probe()
        self._lib.bh_probe(self._h, ctypes.byref(p))
        return {name: getattr(p, name) for name, _ in Probe._fields_}

    def memory(self, which):
        """0: the RAM (1 MB address space), 1: the file flash, as bytes"""
        p = ctypes.POINTER(ctypes.c_ubyte)()
        n = self._lib.bh_memory(self._h, which, ctypes.byref(p))
        return ctypes.string_at(p, n)

    @property
    def preparing(self):
        return bool(self._lib.bh_get_int(self._h, b"preparing"))

    @preparing.setter
    def preparing(self, v):
        self._record("set", "preparing", 1 if v else 0)
        self._lib.bh_set_int(self._h, b"preparing", 1 if v else 0)

    @property
    def turbo_between_lines(self):
        return bool(self._lib.bh_get_int(self._h, b"turbo_between_lines"))

    @turbo_between_lines.setter
    def turbo_between_lines(self, v):
        self._record("set", "turbo_between_lines", 1 if v else 0)
        self._lib.bh_set_int(self._h, b"turbo_between_lines", 1 if v else 0)

    @property
    def ar(self):
        v = self._lib.bh_get_int(self._h, b"ar")
        return None if v < 0 else bool(v)

    @property
    def whine(self):
        return {0: None, 1: "hiss", 2: "whine"}[self._lib.bh_get_whine(self._h)]

    @whine.setter
    def whine(self, mode):
        self._record("whine", mode)
        self._lib.bh_set_whine(self._h, WHINES[mode])

    @property
    def tx(self):
        if not getattr(self, "_h", None):
            return list(getattr(self, "_tx_closed", []))
        lost = self._lib.bh_get_int(self._h, b"tx_lost")
        if lost:
            self._lib.bh_set_int(self._h, b"tx_lost", 0)
            raise BlazieHostError("the transmit record lost %d byte(s): out of memory, it is incomplete" % lost)
        p = ctypes.POINTER(ctypes.c_ubyte)()
        n = self._lib.bh_tx(self._h, ctypes.byref(p))
        return list(p[:n]) if n else []

    # ---- blazie.py's methods ---------------------------------------------------------------
    def send(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1", "replace")
        if data:
            self._record("send", data.decode("latin-1"))
            ok = self._lib.bh_send(self._h, data, len(data))
            self._drain()
            if ok < 0:
                self._raise_fault("send refused")
            if not ok:
                raise RunAheadError("bh_send: out of memory holding input for the run-ahead utterance")

    def say(self, text):
        lines = [text] if isinstance(text, str) else list(text)
        data = b"".join(ln.encode(self.encoding, "replace") + b"\r\x06" for ln in lines) + b"\r\x06"
        self._record("say", data.decode("latin-1"))
        ok = self._lib.bh_say(self._h, data, len(data))
        self._drain()
        if ok < 0:
            self._raise_fault("say refused")
        if not ok:
            raise RunAheadError("bh_say: out of memory holding input for the run-ahead utterance")

    def owed(self):
        self._record("owed")
        return self._lib.bh_owed(self._h)

    def busy(self, quiet=0.1, patience=3.0):
        self._record("busy", quiet, patience)
        v = self._lib.bh_busy(self._h, quiet, patience)
        if v < 0:
            self._raise_fault("busy")
        return bool(v)

    def cancel(self, limit=3.0, quiet=None, cut=None):
        self._record("cancel", limit, quiet, cut)
        t = self._lib.bh_cancel(self._h, limit, -1.0 if quiet is None else quiet, -1.0 if cut is None else cut)
        self._drain()
        return t

    def skip(self, seconds):
        self._record("skip", seconds)
        t = self._lib.bh_skip(self._h, seconds)
        self._drain()
        return t

    def run(self, seconds, step=0.0005):
        self._record("run", seconds, step)
        p = ctypes.POINTER(_D)()
        n = self._lib.bh_run(self._h, seconds, step, ctypes.byref(p))
        out = array("d", bytes(8 * n))
        if n:
            ctypes.memmove(out.buffer_info()[0], p, 8 * n)
        self._drain()
        return out
