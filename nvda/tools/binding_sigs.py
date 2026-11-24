"""Every C function the NVDA binding calls has its signature declared (nvda/shared/ssi263speech.py).

A function called through ctypes with no argtypes gets each Python int as a 32-bit C int: a unit's handle, a pointer,
fits only while the heap happens to sit below 2 GB.  amv_end_dropped (2c3da41, the Accent-mini's cancel) was called so
on 0.9.5's main: on 64-bit Python with a handle above 2^31, every cancelled Accent-mini utterance raised
ctypes.ArgumentError and the driver restarted the card (found by PR 91's author; here the heap was low and it passed).

The names are read from the binding's own source -- self._call("x") and getattr(self._lib, self._api + "_x") for each
unit class's _api, self._lib.x(...) and lib.x(...) -- and each one the library exports must have argtypes after
load().  The library's own exports the binding never calls are not checked.

    python nvda/tools/binding_sigs.py [library]      (default build/win/<x64|x86>/ssi263speech.dll)
    BINDING_SIGS_BREAK=1   amv_end_dropped's declaration taken back after load(), as it was: must fail on it alone
"""
import ctypes
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(REPO, "nvda", "shared", "ssi263speech.py")
sys.path.insert(0, os.path.dirname(SRC))
import ssi263speech  # noqa: E402


def called_names(src):
    apis = set(re.findall(r'_api(?:, _host)? = "(\w+)"', src))
    names = set(re.findall(r'(?:self\._lib|\blib)\.(\w+)\(', src))
    for part in set(re.findall(r'_call\("(\w+)"', src)) | set(re.findall(r'self\._api \+ "_(\w+)"', src)):
        names |= {"%s_%s" % (a, part) for a in apis}
    return sorted(n for n in names if not n.startswith("_")), sorted(apis)


def main():
    arch = "x64" if sys.maxsize > 2 ** 32 else "x86"
    dll = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build", "win", arch,
                                                                               "ssi263speech.dll"))
    lib = ssi263speech.load(dll)
    if os.environ.get("BINDING_SIGS_BREAK") and hasattr(lib, "amv_end_dropped"):
        lib.amv_end_dropped.argtypes = None
    names, apis = called_names(open(SRC, encoding="utf-8").read())
    exported = [n for n in names if hasattr(lib, n)]
    missing = [n for n in exported if getattr(lib, n).argtypes is None]
    print("%d C functions called by the binding (units %s), %d of them in this library" % (
        len(names), ", ".join(apis), len(exported)))
    if "amv_end_dropped" not in exported:
        print("FAIL  amv_end_dropped not found among the binding's calls: the reading of the source is broken")
        return 1
    for n in missing:
        print("FAIL  %s: called with no signature declared" % n)
    if not missing:
        print("ok    every one has its signature declared")
    print("binding signatures: %s" % ("all declared" if not missing else "%d FAILED" % len(missing)))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
