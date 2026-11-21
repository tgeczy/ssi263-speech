"""The Braille Lite from a folder whose name is not ASCII (issues #62, #63).

NVDA's add-on folder is under the user's own folder, and a Windows account may be named José, or in Cyrillic, Greek,
Hebrew...  The firmware and its state are copied into a scratch folder named "ssi263_José_Жук" (Жук is outside every
Western code page) and used from there as the drivers use them:

  release   blv_release_of on the copy's UTF-8 path (blazie.py's commas, trillions and own-marks guards) finds the
            same release as on the original.  It found none (-1), and the three guards switched off unseen (#62).
  boot      the unit boots from the copy through the native host (blazie.py's _boot, bl.dll) and speaks.  The path
            was encoded as strict mbcs, which raised on a letter outside the code page: the Braille Lite could not
            boot, and the whole synthesizer, whose first type it is, failed to load (#63).

    python unicode_paths.py <ssi263speech.dll> <BL2ENG.BNS> <its state> [<bl.dll>]   exit 1 on any failure
    BL_FOPEN_BREAK=1   the C libraries open every path with fopen, as they did     -- must fail: release, boot
    UNICODE_PATHS_BREAK=mbcs   the host encodes the path as mbcs, as it did        -- must fail: boot
"""
import ctypes
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [REPO, os.path.join(REPO, "src")]


def boot(bl_dll, bns, state, folder):
    from ssi263.native import SSI263C
    from hosts import native_blazie
    if os.environ.get("UNICODE_PATHS_BREAK") == "mbcs":
        native_blazie.PATH_ENCODING = "mbcs"
    try:
        u = native_blazie.NativeBlazie(bl_dll, bns, state, chip=SSI263C(out_rate=22050), out_rate=22050)
    except Exception as e:                                # (not a traceback: the control is to FAIL, not crash)
        print("FAIL boot      from %s: %s: %s" % (ascii(folder), type(e).__name__, ascii(str(e))[:120]))
        return False
    try:
        u.say("Hello.")
        loud, t = 0, 0.0
        while t < 4.0:
            pcm = u.run(0.1)
            loud = max([loud] + [abs(x) for x in pcm])
            t += 0.1
            if t > 0.5 and not u.busy():
                break
    finally:
        u.close()
    ok = loud > 0.01
    print("%-4s boot      from %s: the unit booted and said \"Hello.\" (peak %.3f)" % ("ok" if ok else "FAIL",
                                                                                  ascii(folder), loud))
    return ok


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    dll, bns, state = (os.path.abspath(a) for a in sys.argv[1:4])
    bl_dll = os.path.abspath(sys.argv[4]) if len(sys.argv) == 5 else None
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
        if bl_dll:
            bad += not boot(bl_dll, bns2, os.path.join(tmp, os.path.basename(state)), tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("unicode paths: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
