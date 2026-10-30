"""Every shipped Windows binary must load on Windows 7 SP1 (Tomi: Windows 7 support; the README promises Windows 7
and later).  No external tools: the PE headers and import tables are read here.

    python tools/check_win7_imports.py PATH [PATH ...] [--exclude GLOB] [--allow DLL!FUNC] [--require GLOB] [--list]
    python tools/check_win7_imports.py --regenerate [SDK_UM_INCLUDE_FOLDER]
    python tools/check_win7_imports.py --control win8|ucrt|subsystem

PATH is a file, a folder (searched recursively) or an archive (.zip, .nvda-addon, .whl: searched inside, archives
within archives too).  Every PE file in them (.dll, .exe, .pyd) is read, and fails on:

  1. a DLL Windows 7 SP1 does not have: an api-ms-win-* API set beyond the 34 that Windows 7 SP1 ships (WIN7_APISETS
     below; the UCRT's api-ms-win-crt-* sets are not among them -- they come only with KB2999226), ucrtbase.dll,
     vcruntime*.dll, msvcp*.dll (the Visual C++ runtime is not part of Windows 7);
  2. a function introduced after Windows 7, imported from a system DLL (tools/win7_missing_apis.txt);
  3. a PE header asking for more than Windows 7 (NT 6.1): MajorSubsystemVersion.MinorSubsystemVersion or
     MajorOperatingSystemVersion.MinorOperatingSystemVersion above 6.1 -- Windows 7 refuses to start an EXE whose
     subsystem version is higher ("not a valid Win32 application").

--require GLOB names a binary that must be among those read (matched against the report's labels, e.g.
*ssi263-speech-*.nvda-addon!*ssi263speech.dll): a check fed last year's build instead of what ships fails rather than
passing on the wrong files.

Delay-load imports count as imports.  A function looked up at run time (GetProcAddress) is not in the import table
and passes -- that is the remedy for a function Windows 7 lacks (or YY-Thunks, which does the same for MSVC builds).
An import known to be resolved only after an OS check (a delay-load behind a version test) goes on the allow-list:
tools/win7_allowed_imports.txt (one DLL!function per line, with the reason) or --allow DLL!FUNC.

How tools/win7_missing_apis.txt is made (--regenerate; the method, so it can be checked):
  * every header in the Windows SDK's Include\\<version>\\um and shared folders is read with its preprocessor
    conditions tracked line by line;
  * each condition is evaluated twice -- as Windows 7 SP1 (_WIN32_WINNT = WINVER = 0x0601, NTDDI_VERSION =
    NTDDI_WIN7 with SP1, 0x06010100) and as the newest Windows (0x0A00, the newest NTDDI) -- with the constants from sdkddkver.h and
    every other macro unknown (three-valued logic: an unknown never decides);
  * a function declared (NAME( after WINAPI, APIENTRY, NTAPI, WINAPIV or STDAPI) only inside blocks that are
    definitely off for Windows 7 and not off for the newest is "Windows 8 or later"; one declared anywhere that
    Windows 7 can see is not;
  * the list keeps the names that this machine's System32 exports from the DLLs in SYSTEM_DLLS (kernel32, user32,
    advapi32, winmm, ole32, shell32, setupapi, comdlg32, gdi32 and the others below), as DLL!function;
  * MANUAL adds the few the headers do not guard (each with its source), e.g. ProcessPrng (Rust's std since 1.78),
    WaitOnAddress and SetThreadDescription; DOCUMENTED_WIN7 takes out the few guarded for 8 but documented for 7;
  * --regenerate stops, writing nothing, unless SANITY_IN (Windows 8+ functions) are all listed and SANITY_OUT
    (Windows 7 and earlier) none.
Limits: msvcrt.dll's exports carry no version guards, so a newer-msvcrt-only import is not caught; a function newer
than 7 that its header declares without a guard is caught only once it is added to MANUAL.
Imports through an api-ms-win-core-* set are matched against every DLL's names (the sets forward to kernel32's).

Exit 0 when every binary passes, 1 when any fails (each failure named: the binary, the DLL, the function), 2 on a
usage error or when nothing was found to check.
"""
import argparse
import bisect
import fnmatch
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MISSING_FILE = os.path.join(HERE, "win7_missing_apis.txt")
ALLOW_FILE = os.path.join(HERE, "win7_allowed_imports.txt")
PE_EXT = (".dll", ".exe", ".pyd")
ARCHIVE_EXT = (".zip", ".nvda-addon", ".whl")

# The API sets Windows 7 SP1 ships in System32 (Windows 7 introduced API sets; these are its own, all -l1-1-0 but
# for service-management's l2).  Anything else -- api-ms-win-core-synch-l1-2-0 (WaitOnAddress), every
# api-ms-win-crt-* (the UCRT), the -l1-2-*/-l2-* versions Windows 8 added -- is not there.
WIN7_APISETS = frozenset("api-ms-win-" + s + ".dll" for s in (
    "core-console-l1-1-0", "core-datetime-l1-1-0", "core-debug-l1-1-0", "core-delayload-l1-1-0",
    "core-errorhandling-l1-1-0", "core-fibers-l1-1-0", "core-file-l1-1-0", "core-handle-l1-1-0", "core-heap-l1-1-0",
    "core-interlocked-l1-1-0", "core-io-l1-1-0", "core-libraryloader-l1-1-0", "core-localization-l1-1-0",
    "core-localregistry-l1-1-0", "core-memory-l1-1-0", "core-misc-l1-1-0", "core-namedpipe-l1-1-0",
    "core-processenvironment-l1-1-0", "core-processthreads-l1-1-0", "core-profile-l1-1-0",
    "core-rtlsupport-l1-1-0", "core-string-l1-1-0", "core-synch-l1-1-0", "core-sysinfo-l1-1-0",
    "core-threadpool-l1-1-0", "core-util-l1-1-0", "core-xstate-l1-1-0", "security-base-l1-1-0",
    "security-lsalookup-l1-1-0", "security-sddl-l1-1-0", "service-core-l1-1-0", "service-management-l1-1-0",
    "service-management-l2-1-0", "service-winsvc-l1-1-0"))
# runtimes Windows 7 SP1 does not include (the UCRT only arrives with KB2999226; the VC++ runtime never)
RUNTIME_DLLS = (("ucrtbase.dll", "the Universal CRT (Windows 10's; on 7 only with KB2999226)"),
                ("ucrtbased.dll", "the debug Universal CRT"),
                ("vcruntime*.dll", "the Visual C++ runtime (not part of Windows)"),
                ("msvcp*.dll", "the Visual C++ library (not part of Windows)"),
                ("concrt*.dll", "the Visual C++ concurrency runtime (not part of Windows)"),
                ("vccorlib*.dll", "the Visual C++ runtime (not part of Windows)"))

# the System32 DLLs whose Windows 8+ functions the list holds (--regenerate maps each header name to these exports)
SYSTEM_DLLS = ("kernel32", "user32", "advapi32", "winmm", "ole32", "shell32", "setupapi", "comdlg32", "gdi32",
               "oleaut32", "shlwapi", "comctl32", "ws2_32", "version", "dwmapi", "uxtheme", "psapi", "powrprof",
               "dbghelp", "bcrypt", "crypt32", "secur32", "userenv", "winhttp", "wininet", "iphlpapi", "shcore",
               "dsound", "avrt", "mmdevapi", "imm32", "ntdll")
# not guarded in the SDK headers, or not in them at all: name, the DLL, where it is documented
MANUAL = (("bcryptprimitives.dll", "ProcessPrng", "Windows 10; Rust's std imports it since 1.78, which dropped 7"),
          ("kernel32.dll", "SetThreadDescription", "Windows 10 1607 (processthreadsapi.h, no version guard)"),
          ("kernel32.dll", "GetThreadDescription", "Windows 10 1607 (processthreadsapi.h, no version guard)"),
          ("kernelbase.dll", "SetThreadDescription", "Windows 10 1607"),
          ("kernelbase.dll", "GetThreadDescription", "Windows 10 1607"),
          # synchapi.h declares these without a guard (Windows 8; WaitOnAddress lives in kernelbase and
          # api-ms-win-core-synch-l1-2-0, whose import alone already fails)
          ("kernelbase.dll", "WaitOnAddress", "Windows 8 (synchapi.h, no version guard)"),
          ("kernelbase.dll", "WakeByAddressSingle", "Windows 8 (synchapi.h, no version guard)"),
          ("kernelbase.dll", "WakeByAddressAll", "Windows 8 (synchapi.h, no version guard)"),
          ("kernel32.dll", "InitializeSynchronizationBarrier", "Windows 8 (synchapi.h, no version guard)"),
          ("kernel32.dll", "EnterSynchronizationBarrier", "Windows 8 (synchapi.h, no version guard)"),
          ("kernel32.dll", "DeleteSynchronizationBarrier", "Windows 8 (synchapi.h, no version guard)"),
          ("d2d1.dll", "D2D1CreateDevice", "Windows 8 (d2d1_1.h, a Windows 8 header without a version guard)"),
          ("d2d1.dll", "D2D1CreateDeviceContext", "Windows 8 (d2d1_1.h)"),
          ("dxgi.dll", "CreateDXGIFactory2", "Windows 8.1 (dxgi1_3.h, no version guard)"),
          ("dxgi.dll", "DXGIGetDebugInterface1", "Windows 8.1 (dxgi1_3.h, no version guard)"))
# guarded for Windows 8 in the headers, but documented as Windows 7 (learn.microsoft.com, "Minimum supported
# client"): left out of the list
DOCUMENTED_WIN7 = ("SystemTimeToTzSpecificLocalTimeEx", "TzSpecificLocalTimeToSystemTimeEx")
# DLLs Windows 7 SP1 does not have at all (each from its documentation's minimum client)
WIN8_DLLS = (("dcomp.dll", "DirectComposition (Windows 8)"), ("shcore.dll", "Windows 8.1"),
             ("combase.dll", "Windows 8"), ("xinput1_4.dll", "Windows 8"), ("ninput.dll", "Windows 8"),
             ("wintypes.dll", "Windows 8"), ("coremessaging.dll", "Windows 8"), ("d3d12.dll", "Windows 10"),
             ("dxcore.dll", "Windows 10"), ("icu.dll", "Windows 10 1703"), ("icuuc.dll", "Windows 10 1703"),
             ("icuin.dll", "Windows 10 1703"), ("windowsapp.dll", "Windows 10"), ("onecoreuap.dll", "Windows 10"))


# ---------------------------------------------------------------- the PE file

class PEError(Exception):
    pass


def _cstr(data, off):
    end = data.find(b"\0", off)
    if end < 0:
        raise PEError("unterminated string at 0x%x" % off)
    return data[off:end].decode("latin-1")


def pe_info(data):
    """{machine, pe32plus, os_version, subsystem_version, subsystem, imports: [(dll, [func or '#ordinal'])],
    delay: [...]} of a PE image held in data (bytes)."""
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise PEError("not an MZ file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise PEError("no PE signature")
    machine, nsect, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B):
        raise PEError("unknown optional header magic 0x%x" % magic)
    plus = magic == 0x20B
    image_base = struct.unpack_from("<Q", data, opt + 24)[0] if plus else struct.unpack_from("<I", data, opt + 28)[0]
    os_major, os_minor, _, _, ss_major, ss_minor = struct.unpack_from("<6H", data, opt + 40)
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    ndirs_off = opt + (108 if plus else 92)
    ndirs = struct.unpack_from("<I", data, ndirs_off)[0]
    dirs = [struct.unpack_from("<II", data, ndirs_off + 4 + 8 * i) for i in range(min(ndirs, 16))]
    sections = []
    sect = opt + opt_size
    for i in range(nsect):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, sect + 40 * i + 8)
        sections.append((va, max(vsize, rsize), raw, rsize))

    def off(rva):
        for va, size, raw, rsize in sections:
            if va <= rva < va + size:
                if rva - va >= rsize:
                    raise PEError("RVA 0x%x in a section's uninitialised part" % rva)
                return raw + rva - va
        if rva < len(data) and (not sections or rva < sections[0][0]):
            return rva                          # inside the headers
        raise PEError("RVA 0x%x outside every section" % rva)

    tsize, tfmt, ordflag = (8, "<Q", 1 << 63) if plus else (4, "<I", 1 << 31)

    def thunks(rva, va_based):
        out = []
        p = off(rva)
        while True:
            v = struct.unpack_from(tfmt, data, p)[0]
            if v == 0:
                return out
            if v & ordflag:
                out.append("#%d" % (v & 0xFFFF))
            else:
                v = (v - image_base) if va_based else v
                out.append(_cstr(data, off(v & 0x7FFFFFFF) + 2))
            p += tsize

    imports, delay = [], []
    if len(dirs) > 1 and dirs[1][0]:
        p = off(dirs[1][0])
        while True:
            oft, _, _, name, ft = struct.unpack_from("<IIIII", data, p)
            if not (oft or name or ft):
                break
            imports.append((_cstr(data, off(name)), thunks(oft or ft, False)))
            p += 20
    if len(dirs) > 13 and dirs[13][0]:
        p = off(dirs[13][0])
        while True:
            attrs, name, _, iat, int_, _, _, _ = struct.unpack_from("<8I", data, p)
            if not (name or iat or int_):
                break
            vab = not (attrs & 1)               # the old (VC6) form holds VAs, not RVAs
            fix = (lambda v: v - image_base) if vab else (lambda v: v)
            delay.append((_cstr(data, off(fix(name))), thunks(fix(int_), vab)))
            p += 32
    return dict(machine={0x14C: "x86", 0x8664: "x64", 0xAA64: "arm64", 0x1C4: "arm"}.get(machine, hex(machine)),
                pe32plus=plus, os_version=(os_major, os_minor), subsystem_version=(ss_major, ss_minor),
                subsystem=subsystem, imports=imports, delay=delay)


# ---------------------------------------------------------------- finding the binaries

def _excluded(rel, excludes):
    rel = rel.replace("\\", "/")
    return any(fnmatch.fnmatch(rel, g) or fnmatch.fnmatch(rel, g.rstrip("/") + "/*") or
               any(fnmatch.fnmatch(part, g) for part in rel.split("/")[:-1]) for g in excludes)


def is_pe(head):
    """An MZ header pointing at a PE signature: a .dll/.exe/.pyd, and a plug-in (.vst3, .clap) or any renamed one."""
    if head[:2] != b"MZ" or len(head) < 0x40:
        return False
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    return head[pe:pe + 4] == b"PE\0\0"


def _from_zip(label, blob, excludes):
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        for info in z.infolist():
            if info.is_dir() or _excluded(info.filename, excludes):
                continue
            low = info.filename.lower()
            if low.endswith(ARCHIVE_EXT):
                yield from _from_zip(label + "!" + info.filename, z.read(info), excludes)
                continue
            data = z.read(info)
            if low.endswith(PE_EXT) or is_pe(data):
                yield label + "!" + info.filename, data


def binaries(paths, excludes=()):
    """(label, bytes) for every PE file in paths: files, folders, archives."""
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                dirs.sort()
                rel_root = os.path.relpath(root, path)
                dirs[:] = [d for d in dirs if not _excluded(os.path.join(rel_root, d, "x"), excludes)]
                for f in sorted(files):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, path)
                    if _excluded(rel, excludes):
                        continue
                    if f.lower().endswith(ARCHIVE_EXT):
                        yield from _from_zip(_label(full), _read(full), excludes)
                    elif f.lower().endswith(PE_EXT) or _sniff(full):
                        yield _label(full), _read(full)
        elif os.path.isfile(path):
            if path.lower().endswith(ARCHIVE_EXT):
                yield from _from_zip(_label(path), _read(path), excludes)
            else:
                yield _label(path), _read(path)
        else:
            raise SystemExit("check_win7_imports: no such file or folder: %s" % _label(path))


def _read(p):
    with open(p, "rb") as f:
        return f.read()


def _sniff(p):
    with open(p, "rb") as f:
        head = f.read(4096)
    return is_pe(head)


def _label(p):
    """A path for the report: repo-relative inside the repo, else its last three parts (no machine paths)."""
    a = os.path.abspath(p)
    try:
        rel = os.path.relpath(a, REPO)
        if not rel.startswith(".."):
            return rel.replace("\\", "/")
    except ValueError:
        pass
    return "/".join(a.replace("\\", "/").split("/")[-3:])


# ---------------------------------------------------------------- the lists

def read_pairs(path):
    """DLL!function lines (lower-case DLL with .dll, the function as written); '#' starts a comment."""
    out = set()
    if not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(_pair(line))
    return out


def _pair(text):
    dll, _, func = text.partition("!")
    dll = dll.strip().lower()
    if not dll.endswith(".dll"):
        dll += ".dll"
    return dll, func.strip()


def dll_problem(dll):
    d = dll.lower()
    if d.startswith(("api-ms-win-", "ext-ms-win-")):
        if d in WIN7_APISETS:
            return None
        if d.startswith("api-ms-win-crt-"):
            return "an API set of the Universal CRT (on Windows 7 only with KB2999226)"
        return "an API set Windows 7 SP1 does not have"
    for pat, why in RUNTIME_DLLS + WIN8_DLLS:
        if fnmatch.fnmatch(d, pat):
            return "not in Windows 7: " + why
    return None


def check_binary(info, missing, allowed):
    """The failures of one binary, as text lines."""
    bad = []
    by_name = {}
    for dll, func in missing:
        by_name.setdefault(func, set()).add(dll)
    for kind, table in (("import", info["imports"]), ("delay-load import", info["delay"])):
        for dll, funcs in table:
            d = dll.lower()
            why = dll_problem(d)
            if why and (d, "*") not in allowed:     # a whole DLL is allowed only as DLL!*, never by one function
                bad.append("%s of %s: %s" % (kind, dll, why))
            for f in funcs:
                if (d, f) in allowed or (d, "*") in allowed:
                    continue
                hit = (d, f) in missing or (d.startswith(("api-ms-win-", "ext-ms-win-")) and f in by_name)
                if hit:
                    bad.append("%s %s!%s: not in Windows 7 (added in a later Windows)" % (kind, dll, f))
    for what, ver in (("subsystem version", info["subsystem_version"]), ("OS version", info["os_version"])):
        if ver > (6, 1):
            bad.append("PE header %s %d.%d: above Windows 7's 6.1 (Windows 7 refuses to start such an EXE)"
                       % (what, ver[0], ver[1]))
    return bad


def check(paths, excludes=(), extra_allow=(), listing=False, out=sys.stdout, required=()):
    missing = read_pairs(MISSING_FILE)
    if not missing:
        print("check_win7_imports: %s is missing or empty (run --regenerate)" % _label(MISSING_FILE), file=out)
        return 2
    allowed = read_pairs(ALLOW_FILE) | set(_pair(a) for a in extra_allow)
    n = nbad = 0
    labels = []
    for label, data in binaries(paths, excludes):
        n += 1
        labels.append(label.replace("\\", "/"))
        try:
            info = pe_info(data)
        except (PEError, struct.error) as e:
            nbad += 1
            print("FAIL %s: unreadable PE file (%s)" % (label, e), file=out)
            continue
        bad = check_binary(info, missing, allowed)
        nimp = sum(len(f) for _, f in info["imports"]) + sum(len(f) for _, f in info["delay"])
        dlls = [d for d, _ in info["imports"]] + ["%s (delay)" % d for d, _ in info["delay"]]
        head = "%s %s (%s, subsystem %d.%d, OS %d.%d, %d DLLs, %d imports)" % (
            "FAIL" if bad else "ok  ", label, info["machine"], info["subsystem_version"][0],
            info["subsystem_version"][1], info["os_version"][0], info["os_version"][1], len(dlls), nimp)
        print(head, file=out)
        for b in bad:
            print("     " + b, file=out)
        if listing:
            for kind, table in (("", info["imports"]), (" (delay)", info["delay"])):
                for dll, funcs in table:
                    print("       %s%s: %s" % (dll, kind, " ".join(funcs)), file=out)
        nbad += bool(bad)
    if not n:
        print("check_win7_imports: no PE files found in %s" % ", ".join(_label(p) for p in paths), file=out)
        return 2
    for g in required:
        if not any(fnmatch.fnmatch(lb, g) for lb in labels):
            nbad += 1
            print("FAIL required: no binary matching %s was read" % g, file=out)
    print("win7 imports: %d binaries, %s" % (n, "%d FAILED" % nbad if nbad else "all load on Windows 7"), file=out)
    return 1 if nbad else 0


# ---------------------------------------------------------------- --regenerate: the list from the SDK headers

def pe_exports(data):
    """The names a PE image exports (forwarders included)."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    _, nsect, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    plus = struct.unpack_from("<H", data, opt)[0] == 0x20B
    rva = struct.unpack_from("<I", data, opt + (112 if plus else 96))[0]
    if not rva:
        return set()
    secs = [struct.unpack_from("<IIII", data, opt + opt_size + 40 * i + 8) for i in range(nsect)]

    def off(r):
        for vsize, va, rsize, raw in secs:
            if va <= r < va + max(vsize, rsize):
                return raw + r - va
        raise PEError("export RVA 0x%x outside every section" % r)
    nnames = struct.unpack_from("<I", data, off(rva) + 24)[0]          # NumberOfNames
    p = off(struct.unpack_from("<I", data, off(rva) + 32)[0])          # AddressOfNames
    return set(_cstr(data, off(struct.unpack_from("<I", data, p + 4 * i)[0])) for i in range(nnames))


_TOKEN = re.compile(r"\s*(?:(0[xX][0-9a-fA-F]+|\d+)[uUlL]*|([A-Za-z_]\w*)|(&&|\|\||==|!=|>=|<=|<<|>>|[-+*/%()!<>&|^~?:,]))")


class Cond:
    """A preprocessor #if expression, three-valued: an int, or None for anything unknown (it never decides)."""

    def __init__(self, text, env):
        self.toks, self.i, self.env = [], 0, env
        pos = 0
        text = text.strip()
        while pos < len(text):
            m = _TOKEN.match(text, pos)
            if not m or m.end() == pos:
                raise ValueError("can't read %r" % text[pos:])
            if m.group(1):
                self.toks.append(("n", int(m.group(1), 0)))
            elif m.group(2):
                self.toks.append(("i", m.group(2)))
            else:
                self.toks.append(("o", m.group(3)))
            pos = m.end()
            while pos < len(text) and text[pos].isspace():
                pos += 1

    def value(self):
        v = self.ternary()
        if self.i != len(self.toks):
            raise ValueError("trailing tokens")
        return v

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self, op=None):
        t = self.peek()
        if op is not None and t != ("o", op):
            raise ValueError("expected %s" % op)
        self.i += 1
        return t

    def ternary(self):
        c = self.binary(0)
        if self.peek() == ("o", "?"):
            self.take()
            a = self.ternary()
            self.take(":")
            b = self.ternary()
            if c is None:
                return a if a == b else None
            return a if c else b
        return c

    PREC = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="), ("<", ">", "<=", ">="), ("<<", ">>"),
            ("+", "-"), ("*", "/", "%")]

    def binary(self, level):
        if level == len(self.PREC):
            return self.unary()
        a = self.binary(level + 1)
        while self.peek()[0] == "o" and self.peek()[1] in self.PREC[level]:
            op = self.take()[1]
            b = self.binary(level + 1)
            a = self.apply(op, a, b)
        return a

    @staticmethod
    def apply(op, a, b):
        if op == "&&":
            return 0 if (a == 0 or b == 0) else (None if a is None or b is None else 1)
        if op == "||":
            return 1 if (a not in (0, None) or b not in (0, None)) else (None if a is None or b is None else 0)
        if a is None or b is None:
            return None
        if op in ("/", "%") and b == 0:
            return None
        return {"|": lambda: a | b, "^": lambda: a ^ b, "&": lambda: a & b, "==": lambda: int(a == b),
                "!=": lambda: int(a != b), "<": lambda: int(a < b), ">": lambda: int(a > b),
                "<=": lambda: int(a <= b), ">=": lambda: int(a >= b), "<<": lambda: a << b, ">>": lambda: a >> b,
                "+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b, "/": lambda: a // b,
                "%": lambda: a % b}[op]()

    def unary(self):
        kind, v = self.take()
        if kind == "n":
            return v
        if kind == "o":
            if v == "(":
                x = self.ternary()
                self.take(")")
                return x
            x = self.unary()
            if x is None:
                return None
            return {"!": lambda: int(not x), "-": lambda: -x, "+": lambda: x, "~": lambda: ~x}[v]()
        if v == "defined":
            paren = self.peek() == ("o", "(")
            if paren:
                self.take()
            name = self.take()[1]
            if paren:
                self.take(")")
            return 1 if name in self.env else None
        if self.peek() == ("o", "("):              # a function-like macro: OSVER and friends known, others unknown
            self.take()
            depth, args, cur = 1, [], []
            while depth:
                t = self.take()
                if t[0] is None:
                    raise ValueError("unclosed call")
                if t == ("o", "("):
                    depth += 1
                elif t == ("o", ")"):
                    depth -= 1
                    if not depth:
                        break
                if t == ("o", ",") and depth == 1:
                    args.append(cur)
                    cur = []
                else:
                    cur.append(t)
            args.append(cur)
            fn = {"OSVER": lambda x: x & 0xFFFF0000, "SPVER": lambda x: (x & 0xFF00) >> 8,
                  "SUBVER": lambda x: x & 0xFF}.get(v)
            if fn and len(args) == 1:
                sub = Cond("", self.env)
                sub.toks = args[0]
                x = sub.value()
                return None if x is None else fn(x)
            return None
        return self.env.get(v)


def _sdk_constants(sdkddkver):
    """Every object-like #define in sdkddkver.h that comes to a number."""
    raw = {}
    with open(sdkddkver, encoding="latin-1") as f:
        for line in f:
            m = re.match(r"\s*#\s*define\s+(\w+)\s+([^/\n]+?)\s*(?://.*)?$", line)
            if m and "(" not in m.group(1):
                raw[m.group(1)] = m.group(2)
    env = {}
    for _ in range(4):                          # aliases of aliases
        for k, v in raw.items():
            try:
                x = Cond(v, env).value()
            except ValueError:
                continue
            if x is not None:
                env[k] = x
    return env


def _strip_comments(text):
    """Comments out (strings kept), newlines kept so line numbers hold."""
    def repl(m):
        s = m.group(0)
        return s if s[0] in "\"'" else (" " if s.startswith("/*") and "\n" not in s else
                                        "\n" * s.count("\n") if s.startswith("/*") else "")
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', repl, text, flags=re.S)


_DECL = re.compile(r"\b(?:\w*API|APIENTRY|WINAPIV|__stdcall|STDAPICALLTYPE)\s+(\w+)\s*\("
                   r"|\b\w*API_\s*\((?:[^()]|\([^()]*\))*\)\s+(\w+)\s*\(")
_KEYWORDS = frozenset("if while for return sizeof typedef struct union enum const volatile".split())


def header_declarations(path, env7, env_new):
    """[(name, on_for_7, on_for_newest, condition text)] for each function declared in one header."""
    with open(path, encoding="latin-1") as f:
        text = _strip_comments(f.read())
    lines = text.split("\n")
    # \ continuations joined onto their first line
    logical, i = [], 0
    while i < len(lines):
        line, j = lines[i], i
        while line.endswith("\\") and j + 1 < len(lines):
            j += 1
            line = line[:-1] + " " + lines[j]
        logical.append((i, line))
        logical.extend((k, "") for k in range(i + 1, j + 1))
        i = j + 1
    state7, state_new, body, conds = [], [], [], []      # per line: is it on? (three-valued), plus the guard text
    stack = []                                          # frames: [any7, any_new, cur7, cur_new, text]

    def ev(expr, env):
        try:
            return Cond(expr, env).value()
        except (ValueError, IndexError, KeyError, TypeError):
            return None

    def truth(x):
        return None if x is None else int(bool(x))

    for n, line in logical:
        m = re.match(r"\s*#\s*(\w+)\s*(.*)$", line)
        if m:
            d, arg = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef", "ifndef"):
                expr = arg if d == "if" else ("defined(%s)" % arg if d == "ifdef" else "!defined(%s)" % arg)
                c7, cn = truth(ev(expr, env7)), truth(ev(expr, env_new))
                stack.append([c7, cn, c7, cn, expr])
            elif d in ("elif", "else") and stack:
                fr = stack[-1]
                expr = arg if d == "elif" else "1"
                c7, cn = truth(ev(expr, env7)), truth(ev(expr, env_new))
                n7 = Cond.apply("&&", truth(None if fr[0] is None else int(not fr[0])), c7)
                nn = Cond.apply("&&", truth(None if fr[1] is None else int(not fr[1])), cn)
                fr[2], fr[3] = n7, nn
                fr[0], fr[1] = Cond.apply("||", fr[0], c7), Cond.apply("||", fr[1], cn)
                fr[4] = ("!(%s)" % fr[4]) + ("" if d == "else" else " && " + arg)
            elif d == "endif" and stack:
                stack.pop()
            body.append("")
        else:
            body.append(line)
        on7 = on_new = 1
        for fr in stack:
            on7, on_new = Cond.apply("&&", on7, fr[2]), Cond.apply("&&", on_new, fr[3])
        state7.append(on7)
        state_new.append(on_new)
        conds.append(" && ".join(fr[4] for fr in stack if fr[2] == 0 and fr[3] != 0))
    flat = "\n".join(body)
    starts = [0]
    for ln in body:
        starts.append(starts[-1] + len(ln) + 1)
    out = []
    for m in _DECL.finditer(flat):
        name = m.group(1) or m.group(2)
        if name in _KEYWORDS:
            continue
        ln = bisect.bisect_right(starts, m.start(1) if m.group(1) else m.start(2)) - 1
        out.append((name, state7[ln], state_new[ln], conds[ln]))
    return out


def default_sdk_include():
    base = os.path.join(os.environ.get("ProgramFiles(x86)", ""), "Windows Kits", "10", "Include")
    if not os.path.isdir(base):
        return None
    vers = sorted((v for v in os.listdir(base) if re.match(r"10\.\d+\.\d+\.\d+$", v)),
                  key=lambda v: tuple(int(x) for x in v.split(".")))
    return os.path.join(base, vers[-1]) if vers else None


SANITY_IN = ("SetProcessInformation", "GetProcessInformation", "GetSystemTimePreciseAsFileTime", "WaitOnAddress",
             "PrefetchVirtualMemory", "GetCurrentThreadStackLimits", "CreateFile2")
SANITY_OUT = ("CreateSymbolicLinkW", "GetTickCount64", "InitializeSRWLock", "TryAcquireSRWLockExclusive", "CancelIoEx",
              "SetThreadStackGuarantee", "GetFinalPathNameByHandleW", "QueryUnbiasedInterruptTime",
              "GetLogicalProcessorInformationEx", "SetThreadGroupAffinity", "CreateFileW", "Sleep",
              "WaitForSingleObject", "InitializeConditionVariable", "waveOutOpen", "SetupDiGetClassDevsW")


def regenerate(include):
    """Write MISSING_FILE from the SDK at include (Include\\<version>, holding um and shared)."""
    include = os.path.abspath(include)
    if os.path.basename(include).lower() in ("um", "shared"):
        include = os.path.dirname(include)
    sdkddkver = os.path.join(include, "shared", "sdkddkver.h")
    if not os.path.isfile(sdkddkver):
        raise SystemExit("--regenerate: no shared/sdkddkver.h under %s" % _label(include))
    consts = _sdk_constants(sdkddkver)
    newest_ntddi = max(v for k, v in consts.items() if k.startswith("NTDDI_WIN") and v >> 16 == 0x0A00)
    env7 = dict(consts, _WIN32_WINNT=0x0601, WINVER=0x0601, NTDDI_VERSION=consts["NTDDI_WIN7"] | 0x0100)   # SP1
    env_new = dict(consts, _WIN32_WINNT=0x0A00, WINVER=0x0A00, NTDDI_VERSION=newest_ntddi)
    seen7, new_only = set(), {}
    nheaders = 0
    for sub in ("um", "shared"):
        folder = os.path.join(include, sub)
        for f in sorted(os.listdir(folder)):
            if not f.lower().endswith(".h"):
                continue
            nheaders += 1
            for name, on7, on_new, cond in header_declarations(os.path.join(folder, f), env7, env_new):
                if on7 != 0:
                    seen7.add(name)
                elif on_new != 0:
                    new_only.setdefault(name, (f, cond))
    later = {k: v for k, v in new_only.items() if k not in seen7 and k not in DOCUMENTED_WIN7}
    system32 = os.path.join(os.environ["SystemRoot"], "System32")
    pairs = {}
    for dll in SYSTEM_DLLS + ("kernelbase",):
        p = os.path.join(system32, dll + ".dll")
        if not os.path.isfile(p):
            print("note: %s.dll not in System32, skipped" % dll)
            continue
        exports = pe_exports(_read(p))
        for name in exports & set(later):
            pairs[(dll + ".dll", name)] = "%s: %s" % later[name]
    for dll, name, why in MANUAL:
        pairs[(dll, name)] = "MANUAL: " + why
    names = set(n for _, n in pairs)
    wrong = [n for n in SANITY_IN if n not in names] + [n for n in SANITY_OUT if n in names]
    if wrong:
        raise SystemExit("--regenerate: sanity check failed on %s (the condition reader is off); nothing written"
                         % ", ".join(wrong))
    with open(MISSING_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Functions Windows 7 SP1 lacks, imported from a system DLL: DLL!function  # header: guard.\n"
                "# Generated by tools/check_win7_imports.py --regenerate from Windows SDK %s (%d headers in um and\n"
                "# shared): a function declared only under a guard that is off for Windows 7 (_WIN32_WINNT 0x0601,\n"
                "# NTDDI 0x06010100, SP1) and on for the newest Windows, kept where this machine's System32 DLL exports it;\n"
                "# MANUAL lines are the few the headers do not guard.  Do not edit; regenerate.\n"
                % (os.path.basename(include), nheaders))
        for (dll, name) in sorted(pairs):
            f.write("%s!%s  # %s\n" % (dll, name, pairs[(dll, name)][:160]))
    by_dll = {}
    for dll, _ in pairs:
        by_dll[dll] = by_dll.get(dll, 0) + 1
    print("%d headers read; %d functions declared only for Windows 8 or later; %d DLL!function lines written to %s"
          % (nheaders, len(later), len(pairs), _label(MISSING_FILE)))
    print("  " + ", ".join("%s %d" % kv for kv in sorted(by_dll.items())))
    print("sanity: %d known Windows 8+ functions listed, %d Windows 7 functions not" % (len(SANITY_IN), len(SANITY_OUT)))
    return 0


# ---------------------------------------------------------------- the must-fail controls

CONTROL_SRC = os.path.join(HERE, "win7_control.c")
CONTROLS = {
    # a DLL importing SetProcessInformation (Windows 8) from kernel32 directly
    "win8": (["-DWIN7_CONTROL_WIN8", "-shared", "-nostdlib", "-Wl,-e,DllMain", "-lkernel32"], "win8_import.dll"),
    # a DLL importing from ucrtbase.dll (the Universal CRT), which Windows 7 SP1 does not have
    "ucrt": (["-DWIN7_CONTROL_UCRT", "-shared", "-nostdlib", "-Wl,-e,DllMain", "-lucrtbase", "-lkernel32"],
             "ucrt_import.dll"),
    # an EXE whose PE header asks for subsystem 6.2 (Windows 8): Windows 7 will not start it
    "subsystem": (["-DWIN7_CONTROL_SUBSYSTEM", "-nostdlib", "-Wl,-e,start", "-Wl,--major-subsystem-version=6",
                   "-Wl,--minor-subsystem-version=2", "-Wl,--major-os-version=6", "-Wl,--minor-os-version=2",
                   "-lkernel32"], "subsystem_62.exe"),
}


def control(which):
    """Build one control with w64devkit's gcc and check it: it must fail, naming what Windows 7 lacks."""
    sys.path.insert(0, REPO)
    from tools import repo_paths
    gcc = repo_paths.program("W64DEVKIT", "gcc")
    flags, name = CONTROLS[which]
    with tempfile.TemporaryDirectory(prefix="win7-control-") as d:
        target = os.path.join(d, name)
        argv = [gcc, "-O1", "-s", CONTROL_SRC, "-o", target] + flags
        p = subprocess.run(argv, capture_output=True, text=True, cwd=d,
                           env=dict(os.environ, PATH=os.path.dirname(gcc) + os.pathsep + os.environ.get("PATH", "")))
        if p.returncode or not os.path.isfile(target):
            print("control %s: the build FAILED to link (%s)" % (which, (p.stderr or p.stdout).strip()[:300]))
            return 3                            # not the failure the control names: run_tests rejects it
        print("control %s: built %s" % (which, name))
        return check([target])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*", help="files, folders, .zip/.nvda-addon/.whl archives")
    ap.add_argument("--exclude", action="append", default=[], help="a glob of relative paths or folder names to skip")
    ap.add_argument("--allow", action="append", default=[], help="DLL!function resolved lazily (after an OS check)")
    ap.add_argument("--require", action="append", default=[],
                    help="a glob of a binary's label that must be among those read (what ships, not an old build)")
    ap.add_argument("--list", action="store_true", help="also list every import")
    ap.add_argument("--regenerate", nargs="?", const="", metavar="SDK_INCLUDE",
                    help="rebuild %s from the SDK headers (default: the newest Windows Kits 10 Include)"
                    % os.path.basename(MISSING_FILE))
    ap.add_argument("--control", choices=sorted(CONTROLS), help="build and check a must-fail control")
    a = ap.parse_args(argv)
    if a.regenerate is not None:
        inc = a.regenerate or default_sdk_include()
        if not inc:
            raise SystemExit("--regenerate: no Windows SDK found; give its Include\\<version> folder")
        return regenerate(inc)
    if a.control:
        return control(a.control)
    if not a.paths:
        ap.print_usage()
        return 2
    return check(a.paths, a.exclude, a.allow, a.list, required=a.require)


if __name__ == "__main__":
    sys.stdout.reconfigure(errors="replace")
    sys.exit(main())
