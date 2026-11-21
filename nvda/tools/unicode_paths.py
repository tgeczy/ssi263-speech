"""The Braille Lite from a folder whose name is not ASCII (issues #62, #63).

NVDA's add-on folder is under the user's own folder, and a Windows account may be named José, or in Cyrillic, Greek,
Hebrew...  The firmware and its state are copied into a scratch folder named "ssi263_José_Жук" (Жук is outside every
Western code page) and used from there as the drivers use them:

  release   blv_release_of on the copy's UTF-8 path (blazie.py's commas, trillions and own-marks guards) finds the
            same release as on the original.  It found none (-1), and the three guards switched off unseen (#62).

    python unicode_paths.py <ssi263speech.dll> <BL2ENG.BNS> <its state>        exit 1 on any failure
    BL_FOPEN_BREAK=1   the C library opens every path with fopen, as it did   -- must fail: release
"""
import ctypes
import os
import shutil
import sys
import tempfile


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    dll, bns, state = (os.path.abspath(a) for a in sys.argv[1:])
    if not (os.path.isfile(bns) and os.path.isfile(state)):
        print("skipped: the Braille Lite's firmware or state is not there")
        return 0
    lib = ctypes.CDLL(dll)
    lib.blv_release_of.argtypes = [ctypes.c_char_p]
    tmp = tempfile.mkdtemp(prefix="ssi263_José_Жук_")
    bad = 0
    try:
        bns2 = os.path.join(tmp, os.path.basename(bns))
        shutil.copyfile(bns, bns2)
        shutil.copyfile(state, os.path.join(tmp, os.path.basename(state)))
        want, got = lib.blv_release_of(os.fsencode(bns)), lib.blv_release_of(os.fsencode(bns2))
        ok = want >= 0 and got == want
        bad += not ok
        print("%-4s release   from %s: release %d (from its own folder: %d)" % ("ok" if ok else "FAIL", ascii(tmp),
                                                                                   got, want))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("unicode paths: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
