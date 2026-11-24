"""The SAPI engine speaks from an install folder whose name is outside the system's code page (Panthera's audit, P27).

The engine hands its voices the firmware folder (its install folder's firmware\\) as an ANSI path: where the volume
keeps 8.3 short names those are ANSI-safe, but a folder named in another script (Жук, on a Western Windows) on a
volume without them has no ANSI form, and every voice was marked failed and silent ("the firmware folder cannot be
opened").

The second name is in the code page, but its ANSI bytes are also valid UTF-8 -- the characters the code page has at
C3 A9 ("Ã©" on Windows-1252) -- and bl_fopen.h, which takes a path with a high byte as UTF-8 where it is valid,
opened "é" instead: the ANSI form, handed on wherever the name had one, was misread (a review of P27's fix).

The case: the development stage (nvda/dist/sapi-dev: both engine DLLs, the harness, the firmware) copied into a folder
of each name on a volume where 8.3 names are off, and "Hello there." asked of it: the audio must be the same as from
the stage where it is built.  Without such a volume here, skipped, and said so.

    python sapi/test_install_folder.py [--arch x64|x86|both]
    SSI263_SAPI_PATH_BREAK=ansi        the folder as an ANSI path alone, as it was -- must FAIL Жук (the development
                                       DLL's SSI263_SAPI_TEST_BREAK)
    SSI263_SAPI_PATH_BREAK=ansi-first  the ANSI form wherever there is one, as P27 left it -- must FAIL the second name
"""
import ctypes
import os
import shutil
import string
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_sapi_requests as tsr  # noqa: E402

ARCHES = ["x64", "x86"]
if "--arch" in sys.argv and sys.argv[sys.argv.index("--arch") + 1] != "both":
    ARCHES = [sys.argv[sys.argv.index("--arch") + 1]]
BREAK = os.environ.get("SSI263_SAPI_PATH_BREAK")


def no_short_names():
    """a fixed drive where 8.3 names are off (fsutil says so), writable; None when there is none"""
    for d in string.ascii_uppercase[2:]:
        root = d + ":\\"
        if not os.path.isdir(root):
            continue
        r = subprocess.run(["fsutil", "8dot3name", "query", d + ":"], capture_output=True, text=True, errors="replace")
        if "DISABLED" in r.stdout:
            try:
                return tempfile.mkdtemp(prefix="ssi263_sapi_", dir=root)
            except OSError:
                continue
    return None


def main():
    base = no_short_names()
    if not base:
        print("skip  no volume here keeps 8.3 names off: the short path would make any folder ANSI-safe")
        print("install folder: SKIPPED")
        return 0
    names = ["Жук"]                                         # Жук: outside the code page
    cp = "cp%d" % ctypes.windll.kernel32.GetACP()
    try:
        names.append(b"\xc3\xa9".decode(cp) + "voices")     # in it, its ANSI bytes valid UTF-8 ("é")
    except (UnicodeDecodeError, LookupError):
        print("skip  the code page (%s) has no characters at C3 A9: the second name is not made" % cp)
    bad = 0
    try:
        for name in names:
            shutil.copytree(tsr.STAGE, os.path.join(base, name))
        if BREAK:
            tsr.BREAK = "ansi-path" if BREAK == "ansi" else BREAK           # the request lines' break field
        tmp = tempfile.mkdtemp(prefix="ssi263_sapi_out_")
        try:
            # every voice the stage has: each engine opens its own firmware files (bl_fopen.h); a voice no package
            # carries (voices.txt's "data:" rows) speaks from its data folder instead: test_sapi_datapath.py, a name
            # outside the code page included
            voices = [ln.split("\t")[0] for ln in open(os.path.join(tsr.STAGE, "voices.txt"), encoding="utf-8")
                      .read().splitlines() if ln.strip() and ln.count("\t") == 2]
            for arch in ARCHES:
                for voice in voices:
                    lines = [tsr.req([tsr.S("Hello there.")], voice=voice)]
                    home = tsr.harness(arch, "home", lines, tmp)
                    a = home[0]["pcm"] if home else b""
                    for name in names:
                        stage, tsr.STAGE = tsr.STAGE, os.path.join(base, name)
                        try:
                            moved = tsr.harness(arch, "moved", lines, tmp)
                        finally:
                            tsr.STAGE = stage
                        b = moved[0]["pcm"] if moved else b""
                        ok = len(a) > 0 and a == b
                        print("%-4s %s %-28s from a folder named %s, no 8.3 names: %s" % (
                            "ok" if ok else "FAIL", arch, voice, ascii(name),
                            "as from the stage (%s)" % tsr.secs(len(a)) if ok else
                            "%s against %s from the stage" % (tsr.secs(len(b)), tsr.secs(len(a)))))
                        bad += not ok
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("install folder: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
