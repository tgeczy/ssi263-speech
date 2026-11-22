# -*- coding: utf-8 -*-
"""The Speak-Out, the Accents, the Mockingboard and the SmarTalk in ssi263speech.dll (src/csrc/voices.h: so_voice.h,
am_voice.h, as_voice.h, mockingboard/mb_voice.h, smartalk/st_voice.h), for their NVDA drivers (0.7.5: no Python host,
no Python front end; the Mockingboard since 0.8, the SmarTalk after it).  ctypes only, Python 3.7 to 3.13, 32- and
64-bit.

One class per unit, with the methods the drivers' 0.7.0 Python boxes had where the drivers call them -- say() (the
text as the unit is sent it), cancel() (the flush after a cancelled job), a chip with time and regs, on_write for the
tests -- and the voice's job (sov_begin ... sov_flush): the C front end and host speak a whole NVDA sequence exactly
as the 0.7.0 driver did (nvda/tools/native_driver_equiv.py holds the two byte for byte).  The driver keeps NVDA's
side: its worker thread, the player, index and done callbacks, the cancel flag.

Not thread-safe per unit: the driver's worker is the only thread that calls one.  The firmware is read here and
handed over in memory (no path reaches C: an add-on folder under a profile name outside the ANSI code page).
"""
import ctypes
import os
import struct

ARCH = "x64" if struct.calcsize("P") == 8 else "x86"
BLOCK_S = 0.03                    # a render() is 30 ms of the unit's time, as the drivers' box.run(BLOCK_S)

_P, _I, _D, _S = ctypes.c_void_p, ctypes.c_int, ctypes.c_double, ctypes.c_char_p
_SHORTS = ctypes.POINTER(ctypes.c_short)
_libs = {}


class NativeVoiceError(RuntimeError):
    """The unit failed (a host fault, the Accents' watchdog): the driver reboots it, as it did the Python box."""


class _Write(ctypes.Structure):
    _fields_ = [("t", _D), ("reg", _I), ("val", _I)]


class _Mark(ctypes.Structure):                         # src/csrc/marks.h's ssv_mark
    _fields_ = [("at", _I), ("id", _I)]


class _Reach(ctypes.Structure):                        # ... and its ssv_reach
    _fields_ = [("id", _I), ("sample", _I), ("exact", _I)]


def dll_path(engine_dir):
    """The library for this Python's bitness, in an add-on's engine folder: bin/x64 or bin/x86."""
    return os.path.join(engine_dir, "bin", ARCH, "ssi263speech.dll")


def _sig(lib, name, res, args):
    f = getattr(lib, name)
    f.restype = res
    f.argtypes = args


def load(path):
    path = os.path.abspath(path)
    if path in _libs:
        return _libs[path]
    lib = ctypes.CDLL(path)
    _sig(lib, "ssi263_reg", _I, [_P, _I])
    _sig(lib, "ssi263_time", _D, [_P])
    wl = ctypes.POINTER(ctypes.POINTER(_Write))
    for p in ("soh", "amh", "ash"):                       # the hosts: state by name and the write log
        _sig(lib, p + "_get_double", _D, [_P, _S])
        _sig(lib, p + "_set_double", None, [_P, _S, _D])
        _sig(lib, p + "_get_int", _I, [_P, _S])
        _sig(lib, p + "_set_int", None, [_P, _S, _I])
        _sig(lib, p + "_writes", _I, [_P, wl])
        _sig(lib, p + "_clear_writes", None, [_P])
    _sig(lib, "amh_chip", _P, [_P])
    _sig(lib, "ash_chip", _P, [_P])
    err = [_S, _I]
    _sig(lib, "sov_create_hex", _P, [_S, ctypes.c_size_t, _D] + err)
    _sig(lib, "sov_set", None, [_P] + [_I] * 6)
    _sig(lib, "sov_say_bytes", _I, [_S, _S, _I])
    _sig(lib, "sov_host", _P, [_P])
    _sig(lib, "sov_chip", _P, [_P])
    _sig(lib, "sov_fault", _I, [_P])
    for p in ("sov", "asv", "amv"):                       # the units' own index marks (so_voice.h, as_voice.h)
        if hasattr(lib, p + "_expect"):
            _sig(lib, p + "_marks_exact", _I, [_P])
            _sig(lib, p + "_expect", _I, [_P, ctypes.POINTER(_Mark), _I])
            _sig(lib, p + "_reached", _I, [_P, ctypes.POINTER(ctypes.POINTER(_Reach))])
    _sig(lib, "amv_create_mem", _P, [_S, ctypes.c_size_t, _D] + err)
    _sig(lib, "amv_set", None, [_P] + [_I] * 6)
    _sig(lib, "amv_say_bytes", _I, [_S, _I, _S, _I])
    _sig(lib, "amv_host", _P, [_P])
    _sig(lib, "amv_fault", _I, [_P])
    _sig(lib, "asv_create", _P, [_S, ctypes.c_size_t] * 3 + [_D] + err)
    _sig(lib, "asv_set", None, [_P] + [_I] * 5)
    _sig(lib, "asv_set_voice", None, [_P, _I])
    _sig(lib, "asv_say_bytes", _I, [_S, _I, _S, _I])
    _sig(lib, "asv_host", _P, [_P])
    _sig(lib, "asv_fault", _I, [_P])
    _sig(lib, "asv_limit", _I, [_P])
    for p in ("sov", "amv", "asv"):
        _sig(lib, p + "_destroy", None, [_P])
        _sig(lib, p + "_render", _I, [_P, ctypes.POINTER(_SHORTS), ctypes.POINTER(_I)])
        _sig(lib, p + "_begin", None if p == "sov" else _I, [_P])
        _sig(lib, p + "_pitch", None if p == "sov" else _I, [_P, _I])
        _sig(lib, p + "_text", _I, [_P, _S, _I])
        _sig(lib, p + "_end", None if p == "sov" else _I, [_P])
        _sig(lib, p + "_flush", None if p == "sov" else _I, [_P])
    if hasattr(lib, "sov_create_sb"):                     # the Sounding Board (0.8.5): so_voice on sb_host.h
        _sig(lib, "sov_create_sb", _P, [_S, ctypes.c_size_t, _S, ctypes.c_size_t, _D] + err)
        _sig(lib, "sov_sb_host", _P, [_P])
        _sig(lib, "sbh_get_double", _D, [_P, _S])
        _sig(lib, "sbh_set_double", None, [_P, _S, _D])
        _sig(lib, "sbh_get_int", _I, [_P, _S])
        _sig(lib, "sbh_set_int", None, [_P, _S, _I])
        _sig(lib, "sbh_writes", _I, [_P, wl])
        _sig(lib, "sbh_clear_writes", None, [_P])
    if hasattr(lib, "mbv_create"):                        # the Mockingboard (0.8): mb_voice.h's one-text API
        _sig(lib, "mbv_create", _P, [_S, ctypes.c_size_t, _D] + err)
        _sig(lib, "mbv_destroy", None, [_P])
        _sig(lib, "mbv_set", None, [_P] + [_I] * 4)
        _sig(lib, "mbv_speak", _I, [_P, _S, _I])
        _sig(lib, "mbv_render", _I, [_P, ctypes.POINTER(_SHORTS), ctypes.POINTER(_I)])
        _sig(lib, "mbv_cancel", None, [_P])
        _sig(lib, "mbv_host", _P, [_P])
        _sig(lib, "mbh_chip", _P, [_P])
        _sig(lib, "mbh_get_int", _I, [_P, _S])
    if hasattr(lib, "stv_create"):                        # the SmarTalk: st_voice.h's one-text API
        _sig(lib, "stv_create", _P, [_S, ctypes.c_size_t, _D] + err)
        _sig(lib, "stv_destroy", None, [_P])
        _sig(lib, "stv_set", None, [_P] + [_I] * 6)
        _sig(lib, "stv_speak", _I, [_P, _S, _I])
        _sig(lib, "stv_render", _I, [_P, ctypes.POINTER(_SHORTS), ctypes.POINTER(_I)])
        _sig(lib, "stv_cancel", None, [_P])
        _sig(lib, "stv_fault", _I, [_P])
        if hasattr(lib, "stv_speak_marked"):             # the TSR's own index marks (st_voice.h)
            _sig(lib, "stv_marks_exact", _I, [_P])
            _sig(lib, "stv_speak_marked", _I, [_P, _S, _I, ctypes.POINTER(_Mark), _I])
            _sig(lib, "stv_reached", _I, [_P, ctypes.POINTER(ctypes.POINTER(_Reach))])
        _sig(lib, "stv_host", _P, [_P])
        _sig(lib, "sth_chip", _P, [_P])
        _sig(lib, "sth_get_double", _D, [_P, _S])
        _sig(lib, "sth_get_int", _I, [_P, _S])
        _sig(lib, "sth_set_int", None, [_P, _S, _I])
        _sig(lib, "sth_writes", _I, [_P, wl])
        _sig(lib, "sth_clear_writes", None, [_P])
    _libs[path] = lib
    return lib


def _read(path):
    with open(path, "rb") as f:
        return f.read()


class _Regs:
    def __init__(self, unit):
        self._unit = unit

    def __getitem__(self, i):
        return self._unit._lib.ssi263_reg(self._unit._chip_ptr(), i)

    def __len__(self):
        return 5


class _Chip:
    """The unit's SSI-263 as the tests look at it: its time and its registers (read-only)."""

    def __init__(self, unit):
        self._unit = unit
        self.regs = _Regs(unit)

    @property
    def time(self):
        return self._unit._lib.ssi263_time(self._unit._chip_ptr())


class _Unit:
    """One voice of the library (a subclass names its API prefix and host)."""
    _api = _host = None
    encoding = "latin-1"

    def __init__(self, lib_path, out_rate):
        self._lib = load(lib_path)
        self.out_rate = out_rate
        self._on_write = None
        self.chip = _Chip(self)
        self._pcm = _SHORTS()
        self._done = _I(0)
        self._v = None

    # ---- the library's calls, with the write log drained after each ---------------------------------------------
    def _call(self, name, *args):
        if self._on_write is not None:
            getattr(self._lib, self._host + "_set_int")(self._host_ptr(), b"log_writes", 1)
        r = getattr(self._lib, self._api + "_" + name)(self._v, *args)
        if self._on_write is not None:
            self._drain()
        return r

    def _host_ptr(self):
        return getattr(self._lib, self._api + "_host")(self._v)

    def _drain(self):
        h = self._host_ptr()
        p = ctypes.POINTER(_Write)()
        n = getattr(self._lib, self._host + "_writes")(h, ctypes.byref(p))
        fn = self._on_write
        for i in range(n):
            fn(p[i].t, p[i].reg, p[i].val)
        getattr(self._lib, self._host + "_clear_writes")(h)

    @property
    def on_write(self):
        """fn(t, reg, val) on every chip write, with the chip time it was applied at (tests: nvda/tools/write_spy.py)"""
        return self._on_write

    @on_write.setter
    def on_write(self, fn):
        if self._v:
            if self._on_write is not None:
                self._drain()
            getattr(self._lib, self._host + "_set_int")(self._host_ptr(), b"log_writes", 1 if fn else 0)
            getattr(self._lib, self._host + "_clear_writes")(self._host_ptr())
        self._on_write = fn

    # ---- the driver's job --------------------------------------------------------------------------------------
    def begin(self):
        """_speakJob's start: the settings that changed sent, the lead trim armed"""
        if self._call("begin") == -1:
            raise NativeVoiceError("%s: the unit failed" % type(self).__name__)

    def pitch(self, offset):
        """a PitchCommand item: on the pitch set() last gave"""
        if self._call("pitch", int(offset or 0)) == -1:
            raise NativeVoiceError("%s: the unit failed" % type(self).__name__)

    def say(self, text):
        """a text item, as the unit is sent it (prepare()'s text and the carriage return)"""
        data = text.encode(self.encoding, "replace")
        if self._call("text", data, len(data)) == -1:
            raise NativeVoiceError("%s: the unit failed" % type(self).__name__)

    def render(self):
        """the next 30 ms block: (16-bit PCM bytes, done); empty while the unit is still silent (the lead trim)"""
        n = self._call("render", ctypes.byref(self._pcm), ctypes.byref(self._done))
        data = ctypes.string_at(self._pcm, 2 * n) if n > 0 else b""
        return data, bool(self._done.value)

    # ---- the unit's own index marks (src/csrc/marks.h): a mark in the text, the unit saying when speech reaches it ---
    def own_marks(self):
        """True when this unit's own index marks are used: its voice has them (<api>_expect) and says they are
        usable now (<api>_marks_exact)"""
        return (hasattr(self._lib, self._api + "_expect") and hasattr(self, "mark_text")
                and bool(getattr(self._lib, self._api + "_marks_exact")(self._v)))

    def expect(self, pairs):
        """the marks in the text about to be said, in its order: [(what the unit calls the mark -- mark_text's
        second value --, the caller's number for it)]; False when they are not used (the text is then said
        without them)"""
        arr = (_Mark * len(pairs))()
        for k, (name, number) in enumerate(pairs):
            arr[k].at, arr[k].id = int(name), int(number)
        return bool(self._call("expect", arr, len(pairs)))

    def reached(self):
        """the marks speech reached in the audio render() just returned: [(the caller's number, its sample in
        that audio)]"""
        p = ctypes.POINTER(_Reach)()
        return [(p[k].id, p[k].sample) for k in range(getattr(self._lib, self._api + "_reached")(self._v, ctypes.byref(p)))]

    def end(self, cancelled=False):
        """_speakJob's finally: the user's pitch again if a capital left it changed.  cancelled: a flush follows
        (cancel()), and a unit that has <api>_end_dropped leaves the pitch to it -- the Accent-mini's restore printed
        ESC P through its driver and ran the card through the rest of the text first (#74)"""
        if cancelled and hasattr(self._lib, self._api + "_end_dropped"):
            self._call("end_dropped")
        else:
            self._call("end")

    def cancel(self):
        """_run's flush after a cancelled job (after end()): the unit's flush, the pitch said again if dropped"""
        if self._call("flush") == -1:
            raise NativeVoiceError("%s: the unit failed" % type(self).__name__)

    @property
    def fault(self):
        return bool(getattr(self._lib, self._api + "_fault")(self._v))

    @property
    def last_speech(self):
        return getattr(self._lib, self._host + "_get_double")(self._host_ptr(), b"last_speech")

    def state(self):
        """a line for the drivers' diagnostics"""
        return "time %.3f last_speech %.3f fault %d" % (self.chip.time, self.last_speech, self.fault)

    def close(self):
        v, self._v = self._v, None
        if v:
            getattr(self._lib, self._api + "_destroy")(v)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _created(self, v, err):
        if not v:
            raise NativeVoiceError("%s: %s" % (type(self).__name__, err.value.decode("latin-1", "replace")))
        self._v = v


class SpeakOutC(_Unit):
    """GW Micro's Speak-Out: its firmware on MAME's V40 and the NVDA driver's front end (so_voice.h)."""
    _api, _host = "sov", "soh"

    def __init__(self, lib_path, firmware, out_rate):
        _Unit.__init__(self, lib_path, out_rate)
        hex_text = _read(firmware)
        err = ctypes.create_string_buffer(256)
        self._created(self._lib.sov_create_hex(hex_text, len(hex_text), float(out_rate), err, 256), err)

    def _chip_ptr(self):
        return self._lib.sov_chip(self._v)

    def set(self, rate, pitch, tone, volume, join, short_pauses):
        """NVDA's scales; tone 0-25 = A-Z"""
        self._lib.sov_set(self._v, int(rate), int(pitch), int(tone), int(volume), 1 if join else 0,
                          1 if short_pauses else 0)

    def prepare(self, text):
        """the text the box is sent for an item (currencies, the clean-up, strip), without the carriage return"""
        data = text.encode("utf-8", "replace")
        n = self._lib.sov_say_bytes(data, None, 0)
        if n < 0:
            raise MemoryError("sov_say_bytes")
        out = ctypes.create_string_buffer(n + 1)
        self._lib.sov_say_bytes(data, out, n + 1)
        return out.raw[:n].rstrip(b"\r").decode("latin-1")

    # ---- the box's own index marks (so_voice.h): 05h 5Bh xx in the text comes back as xx when speech is there ------
    MARK_FIRST, MARK_LAST = 0x30, 0x7A      # the bytes used (so_voice.c's: thirteen others do not come back as sent)
    _mark = 0

    def mark_text(self, k):
        """the k-th mark of a text: (its characters, the byte the box -- or the card's driver -- calls it)"""
        self._mark = self._mark + 1 if self.MARK_FIRST <= self._mark < self.MARK_LAST else self.MARK_FIRST
        return "\x05[" + chr(self._mark), self._mark


class SoundingBoardC(SpeakOutC):
    """GW Micro's Sounding Board: the card's own DOS drivers (SBLOAD.COM and SB.COM 2.7) on MAME's 8086, whose front
    end is the Speak-Out's byte for byte -- the same voice and settings (so_voice.h's sov_create_sb), its own host
    (sb_host.h)."""
    _host = "sbh"

    def __init__(self, lib_path, sbload, sbcom, out_rate):
        _Unit.__init__(self, lib_path, out_rate)
        if not hasattr(self._lib, "sov_create_sb"):
            raise NativeVoiceError("SoundingBoardC: this library has no Sounding Board")
        a, b = _read(sbload), _read(sbcom)
        err = ctypes.create_string_buffer(256)
        self._created(self._lib.sov_create_sb(a, len(a), b, len(b), float(out_rate), err, 256), err)

    def _host_ptr(self):
        return self._lib.sov_sb_host(self._v)


class _Accent(_Unit):
    encoding = "ascii"            # the Accents' clean-up leaves 7-bit text

    def prepare(self, text, numbers):
        """the text the card is sent for an item (currencies, the clean-up, strip, the number words when numbers)"""
        data = text.encode("utf-8", "replace")
        fn = getattr(self._lib, self._api + "_say_bytes")
        n = fn(data, 1 if numbers else 0, None, 0)
        if n < 0:
            raise MemoryError(self._api + "_say_bytes")
        out = ctypes.create_string_buffer(n + 1)
        fn(data, 1 if numbers else 0, out, n + 1)
        return out.raw[:n].decode("latin-1")


class _AccentMarks:
    """the Accents' own index mark (as_voice.h, am_voice.h): Ctrl-F, which the unit answers (the Accent-PC) or its
    driver counts (the Accent-mini) when the word before it has ended; all alike, so the k-th of a text is called k"""

    def mark_text(self, k):
        return "\x06", k


class AccentMiniC(_AccentMarks, _Accent):
    """Aicom's Accent-mini: its DOS driver SPKEMS.DVC on MAME's 8086 and the NVDA driver's front end (am_voice.h)."""
    _api, _host = "amv", "amh"
    mark_trails = True            # its driver counts a mark when the word BEFORE it has ended: the mark goes there

    def __init__(self, lib_path, dvc, out_rate):
        _Unit.__init__(self, lib_path, out_rate)
        data = _read(dvc)
        err = ctypes.create_string_buffer(256)
        self._created(self._lib.amv_create_mem(data, len(data), float(out_rate), err, 256), err)

    def _chip_ptr(self):
        return self._lib.amh_chip(self._host_ptr())

    def set(self, rate, pitch, inflection, volume, numbers, voice):
        self._lib.amv_set(self._v, int(rate), int(pitch), int(inflection), int(volume), 1 if numbers else 0,
                          int(voice))


class AccentPCC(_AccentMarks, _Accent):
    """Aicom's Accent-PC: its 8085 firmware and ROMs on MAME's 8085 and the NVDA driver's front end (as_voice.h)."""
    _api, _host = "asv", "ash"

    def __init__(self, lib_path, rom_dir, out_rate):
        _Unit.__init__(self, lib_path, out_rate)
        roms = [_read(os.path.join(rom_dir, n)) for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
        err = ctypes.create_string_buffer(256)
        self._created(self._lib.asv_create(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]),
                                           float(out_rate), err, 256), err)

    def _chip_ptr(self):
        return self._lib.ash_chip(self._host_ptr())

    def set(self, rate, pitch, inflection, volume, numbers, voice):
        self._lib.asv_set(self._v, int(rate), int(pitch), int(inflection), int(volume), 1 if numbers else 0)
        self._lib.asv_set_voice(self._v, int(voice))

    @property
    def limit(self):
        """the last text was called done at as_voice's safety limit, the firmware still at work (asv_limit)"""
        return bool(self._lib.asv_limit(self._v))


class MockingboardC:
    """Sweet Micro Systems' Mockingboard: its text-to-speech on Fake6502 and the board, and the voice's text path
    (mb_voice.h).  Not the job API of the others: one text at a time, said in parts the firmware can take, a capital
    as that text's pitch offset; render() until done."""

    def __init__(self, lib_path, firmware, out_rate):
        self._lib = load(lib_path)
        if not hasattr(self._lib, "mbv_create"):
            raise NativeVoiceError("MockingboardC: this ssi263speech.dll has no Mockingboard")
        self.out_rate = out_rate
        self._pcm = _SHORTS()
        self._done = _I(0)
        self._v = None
        data = _read(firmware)
        err = ctypes.create_string_buffer(256)
        v = self._lib.mbv_create(data, len(data), float(out_rate), err, 256)
        if not v:
            raise NativeVoiceError("MockingboardC: %s" % err.value.decode("latin-1", "replace"))
        self._v = v

    def set(self, rate, pitch, volume, numbers):
        """NVDA's scales (mb_voice.h); used from the next say()"""
        self._lib.mbv_set(self._v, int(rate), int(pitch), int(volume), 1 if numbers else 0)

    def say(self, text, pitch_offset=0):
        """one text item, with a capital's offset; False when there is nothing to say"""
        data = text.encode("utf-8", "replace")
        return bool(self._lib.mbv_speak(self._v, data, int(pitch_offset or 0)))

    def render(self):
        """the next 30 ms block: (16-bit PCM bytes, done)"""
        n = self._lib.mbv_render(self._v, ctypes.byref(self._pcm), ctypes.byref(self._done))
        data = ctypes.string_at(self._pcm, 2 * n) if n > 0 else b""
        return data, bool(self._done.value)

    def cancel(self):
        self._lib.mbv_cancel(self._v)

    @property
    def fault(self):
        h = self._lib.mbv_host(self._v)
        # MBH_FAULT_STUCK: the 6502 did not come back; a refusal (MBH_FAULT_LONG) the voice splits and says again
        return self._lib.mbh_get_int(h, b"fault") == 2 if h else False

    @property
    def regs(self):
        """the chip's five registers, for the tests"""
        chip = self._lib.mbh_chip(self._lib.mbv_host(self._v))
        return [self._lib.ssi263_reg(chip, i) for i in range(5)]

    def close(self):
        v, self._v = self._v, None
        if v:
            self._lib.mbv_destroy(v)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class SmarTalkC:
    """Automated Functions' SmarTalk: its DOS text-to-speech (S_TALK.EXE 1.21) on MAME's 8086 and the box on LPT1, and
    the voice's text path (smartalk/st_voice.h).  The Mockingboard's API: one text at a time (said in parts, the next
    while the box still talks), a capital as that text's pitch offset; render() until done."""

    def __init__(self, lib_path, firmware, out_rate):
        self._lib = load(lib_path)
        if not hasattr(self._lib, "stv_create"):
            raise NativeVoiceError("SmarTalkC: this ssi263speech.dll has no SmarTalk")
        self.out_rate = out_rate
        self._pcm = _SHORTS()
        self._done = _I(0)
        self._v = None
        data = _read(firmware)
        err = ctypes.create_string_buffer(256)
        v = self._lib.stv_create(data, len(data), float(out_rate), err, 256)
        if not v:
            raise NativeVoiceError("SmarTalkC: %s" % err.value.decode("latin-1", "replace"))
        self._v = v

    def set(self, rate, pitch, tone, volume, inflection, numbers):
        """NVDA's scales, tone 0-25 = A-Z (st_voice.h); used from the next say()"""
        self._lib.stv_set(self._v, int(rate), int(pitch), int(tone), int(volume), 1 if inflection else 0,
                          1 if numbers else 0)

    def say(self, text, pitch_offset=0):
        """one text item, with a capital's offset; False when there is nothing to say"""
        data = text.encode("utf-8", "replace")
        return bool(self._lib.stv_speak(self._v, data, int(pitch_offset or 0)))

    # ---- the TSR's own index marks (st_voice.h): a Ctrl-F in the text, counted by the TSR as speech passes it --------
    def own_marks(self):
        return hasattr(self._lib, "stv_speak_marked") and bool(self._lib.stv_marks_exact(self._v))

    def say_marked(self, text, marks, pitch_offset=0):
        """say() with index marks: [(the characters of text before the mark, the caller's number for it)], in the
        text's order; reached() gives them after each render()"""
        data = text.encode("utf-8", "replace")
        arr = (_Mark * max(1, len(marks)))()
        for k, (chars, number) in enumerate(marks):
            arr[k].at, arr[k].id = len(text[:chars].encode("utf-8", "replace")), int(number)
        return bool(self._lib.stv_speak_marked(self._v, data, int(pitch_offset or 0), arr, len(marks)))

    def reached(self):
        """the marks speech reached in the block render() just returned: [(the caller's number, its sample)]"""
        p = ctypes.POINTER(_Reach)()
        return [(p[k].id, p[k].sample) for k in range(self._lib.stv_reached(self._v, ctypes.byref(p)))]

    def render(self):
        """the next 30 ms block: (16-bit PCM bytes, done)"""
        n = self._lib.stv_render(self._v, ctypes.byref(self._pcm), ctypes.byref(self._done))
        data = ctypes.string_at(self._pcm, 2 * n) if n > 0 else b""
        return data, bool(self._done.value)

    def cancel(self):
        self._lib.stv_cancel(self._v)

    @property
    def fault(self):
        """the host faulted or the box stalled: the driver boots a new one"""
        return bool(self._lib.stv_fault(self._v))

    @property
    def regs(self):
        """the chip's five registers, for the tests"""
        chip = self._lib.sth_chip(self._lib.stv_host(self._v))
        return [self._lib.ssi263_reg(chip, i) for i in range(5)]

    def log_writes(self, on):
        """keep every chip write for writes() (the tests)"""
        self._lib.sth_set_int(self._lib.stv_host(self._v), b"log_writes", 1 if on else 0)

    def writes(self):
        """the chip writes kept since the last call, as (chip time, register, value)"""
        h = self._lib.stv_host(self._v)
        p = ctypes.POINTER(_Write)()
        n = self._lib.sth_writes(h, ctypes.byref(p))
        out = [(p[i].t, p[i].reg, p[i].val) for i in range(n)]
        self._lib.sth_clear_writes(h)
        return out

    def close(self):
        v, self._v = self._v, None
        if v:
            self._lib.stv_destroy(v)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
