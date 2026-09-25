"""Just enough of Unicorn for the Speak-Out host, straight from its C API with ctypes.

The official binding needs importlib_resources on Python < 3.9, which NVDA 2021-2023
(32-bit Python 3.7) do not have, and its DLL needs the VC runtime.  This one runs on
Python 3.7-3.13, 32- and 64-bit, against any Unicorn 2 DLL: the add-ons ship a static
x86-core-only build (bin/x64, bin/x86: KERNEL32 + msvcrt only).

The DLL is looked for in: bin/<arch>/ beside this file, LIBUNICORN_PATH (a file or a
directory; another add-on in NVDA's process may have set it, so it comes second), then the
lib/ folder of an installed unicorn package (research machines).
"""
import ctypes
import importlib.util
import os
import struct
from ctypes import byref, c_int, c_int64, c_size_t, c_uint32, c_uint64, c_void_p

UC_ARCH_X86 = 4
UC_MODE_16 = 2
UC_PROT_ALL = 7
UC_HOOK_INTR = 1
UC_HOOK_INSN = 2
UC_HOOK_CODE = 4
UC_HOOK_BLOCK = 8
UC_HOOK_MEM_WRITE = 2048
UC_X86_REG_AH = 1
UC_X86_REG_AL = 2
UC_X86_REG_AX = 3
UC_X86_REG_BP = 6
UC_X86_REG_BX = 8
UC_X86_REG_CX = 12
UC_X86_REG_DI = 14
UC_X86_REG_DS = 17
UC_X86_REG_DX = 18
UC_X86_REG_CS = 11
UC_X86_REG_EFLAGS = 25
UC_X86_REG_ES = 28
UC_X86_REG_IP = 34
UC_X86_REG_SI = 45
UC_X86_REG_SP = 47
UC_X86_REG_SS = 49
UC_X86_INS_IN = 218
UC_X86_INS_OUT = 500

_IN_CB = ctypes.CFUNCTYPE(c_uint32, c_void_p, c_uint32, c_int, c_void_p)
_OUT_CB = ctypes.CFUNCTYPE(None, c_void_p, c_uint32, c_int, c_uint32, c_void_p)
_MEM_CB = ctypes.CFUNCTYPE(None, c_void_p, c_int, c_uint64, c_int, c_int64, c_void_p)
_INTR_CB = ctypes.CFUNCTYPE(None, c_void_p, c_uint32, c_void_p)
_CODE_CB = ctypes.CFUNCTYPE(None, c_void_p, c_uint64, c_uint32, c_void_p)

_lib = None


class UcError(Exception):
    pass


def _candidates():
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    yield os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin", arch, "unicorn.dll")
    env = os.environ.get("LIBUNICORN_PATH")
    if env:
        yield env if env.lower().endswith(".dll") else os.path.join(env, "unicorn.dll")
    try:
        spec = importlib.util.find_spec("unicorn")
    except (ImportError, ValueError):
        spec = None
    if spec and spec.submodule_search_locations:
        for d in spec.submodule_search_locations:
            yield os.path.join(d, "lib", "unicorn.dll")


def _load():
    global _lib
    if _lib is None:
        tried = []
        for path in _candidates():
            if os.path.isfile(path):
                _lib = ctypes.CDLL(path)
                break
            tried.append(path)
        else:
            raise UcError("unicorn.dll not found; tried %s" % tried)
        _lib.uc_open.argtypes = [c_int, c_int, ctypes.POINTER(c_void_p)]
        _lib.uc_close.argtypes = [c_void_p]
        _lib.uc_mem_map.argtypes = [c_void_p, c_uint64, c_uint64, c_uint32]
        _lib.uc_mem_write.argtypes = [c_void_p, c_uint64, ctypes.c_char_p, c_uint64]
        _lib.uc_mem_read.argtypes = [c_void_p, c_uint64, c_void_p, c_uint64]
        _lib.uc_reg_read.argtypes = [c_void_p, c_int, c_void_p]
        _lib.uc_reg_write.argtypes = [c_void_p, c_int, c_void_p]
        _lib.uc_emu_start.argtypes = [c_void_p, c_uint64, c_uint64, c_uint64, c_size_t]
        _lib.uc_emu_stop.argtypes = [c_void_p]
        _lib.uc_strerror.argtypes = [c_int]
        _lib.uc_strerror.restype = ctypes.c_char_p
        # uc_hook_add is variadic: left without argtypes, every argument passed typed
    return _lib


def _check(err):
    if err:
        raise UcError("unicorn error %d: %s" % (err, _lib.uc_strerror(err).decode("ascii", "replace")))


class Uc:
    def __init__(self, arch, mode):
        lib = _load()
        self._uc = c_void_p()
        _check(lib.uc_open(arch, mode, byref(self._uc)))
        self._callbacks = []          # ctypes thunks must outlive the engine
        self._pending = None          # an exception raised inside a hook

    def close(self):
        if self._uc:
            _lib.uc_close(self._uc)
            self._uc = c_void_p()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def mem_map(self, address, size, perms=UC_PROT_ALL):
        _check(_lib.uc_mem_map(self._uc, address, size, perms))

    def mem_write(self, address, data):
        data = bytes(data)
        _check(_lib.uc_mem_write(self._uc, address, data, len(data)))

    def mem_read(self, address, size):
        buf = ctypes.create_string_buffer(size)
        _check(_lib.uc_mem_read(self._uc, address, buf, size))
        return bytearray(buf.raw)

    def reg_read(self, reg):
        v = c_uint64(0)               # x86 registers are written little-endian, <= 8 bytes
        _check(_lib.uc_reg_read(self._uc, reg, byref(v)))
        return v.value

    def reg_write(self, reg, value):
        v = c_uint64(value & 0xFFFFFFFFFFFFFFFF)
        _check(_lib.uc_reg_write(self._uc, reg, byref(v)))

    def emu_start(self, begin, until, timeout=0, count=0):
        self._pending = None
        _check(_lib.uc_emu_start(self._uc, begin, until, timeout, count))
        if self._pending is not None:
            e, self._pending = self._pending, None
            raise e

    def emu_stop(self):
        _check(_lib.uc_emu_stop(self._uc))

    def _guard(self, fn, default):
        def call(*args):
            try:
                r = fn(*args)
            except Exception as e:     # stop, and re-raise from emu_start
                self._pending = e
                _lib.uc_emu_stop(self._uc)
                return default
            return default if r is None else r
        return call

    def hook_add(self, htype, callback, user_data=None, begin=1, end=0, arg1=0):
        h = c_size_t()
        if htype == UC_HOOK_INSN and arg1 == UC_X86_INS_IN:
            f = self._guard(lambda uc, port, size, ud: callback(self, port, size, user_data), 0)
            thunk = _IN_CB(f)
        elif htype == UC_HOOK_INSN and arg1 == UC_X86_INS_OUT:
            f = self._guard(lambda uc, port, size, value, ud: callback(self, port, size, value, user_data), None)
            thunk = _OUT_CB(f)
        elif htype == UC_HOOK_MEM_WRITE:
            f = self._guard(lambda uc, access, address, size, value, ud:
                            callback(self, access, address, size, value, user_data), None)
            thunk = _MEM_CB(f)
        elif htype == UC_HOOK_INTR:
            f = self._guard(lambda uc, intno, ud: callback(self, intno, user_data), None)
            thunk = _INTR_CB(f)
        elif htype in (UC_HOOK_CODE, UC_HOOK_BLOCK):
            f = self._guard(lambda uc, address, size, ud: callback(self, address, size, user_data), None)
            thunk = _CODE_CB(f)
        else:
            raise UcError("ucmini supports IN/OUT, memory-write, interrupt and code/block hooks only")
        args = [self._uc, byref(h), c_int(htype), ctypes.cast(thunk, c_void_p), c_void_p(0),
                c_uint64(begin), c_uint64(end)]
        if htype == UC_HOOK_INSN:
            args.append(c_int(arg1))
        _check(_lib.uc_hook_add(*args))
        self._callbacks.append(thunk)
        return h.value
