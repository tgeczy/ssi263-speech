"""The C core compiles with each optional engine left out, as the standalone builds do.

voices.c and the Speak-Out's so_voice.c take their optional engines (the Accent-mini, the Mockingboard, the SmarTalk,
the Sounding Board) behind #ifdef; a header the file needs whatever is built must not sit behind one.  bl_fopen.h did
(e214c0b: inside SO_HAVE_SOUNDINGBOARD and SSV_HAVE_SMARTALK), and speakout/build_board.py's Speak-Out, built without
the Sounding Board, no longer compiled -- found by an independent review, not by the full builds, which define them
all.  Each file is compiled here with none of its optional defines and with the full set.

    python tools/check_optional_engines.py           exit 1 on any failure
    OPTIONAL_ENGINES_BREAK=1   bl_fopen.h's include guard defined beforehand (the header then gives nothing, as a
                               guarded include does in a minimal build) -- must fail the minimal builds
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSRC = os.path.join(ROOT, "src", "csrc")
FULL_VOICES = ["-DSSV_HAVE_SPEAKOUT", "-DSSV_HAVE_ACCENTMINI", "-DSSV_HAVE_MOCKINGBOARD", "-DSSV_HAVE_SMARTALK",
               "-DSSV_HAVE_SOUNDINGBOARD", "-DSO_HAVE_SOUNDINGBOARD"]
CASES = [("voices.c", [], "minimal"), ("voices.c", FULL_VOICES, "full"),
         ("speakout/so_voice.c", [], "minimal"), ("speakout/so_voice.c", ["-DSO_HAVE_SOUNDINGBOARD"], "full")]


def compiler():
    """the build's gcc (paths.local W64DEVKIT, as build_ssi263speech.py finds it), else the PATH's gcc or cc"""
    sys.path.insert(0, ROOT)
    from tools import repo_paths
    try:
        return repo_paths.program("W64DEVKIT", "gcc")
    except SystemExit:
        return shutil.which("cc")


def main():
    cc = compiler()
    if not cc:
        print("FAIL  no C compiler: set W64DEVKIT in paths.local, or put gcc on the PATH")
        return 1
    brk = ["-DBL_FOPEN_H"] if os.environ.get("OPTIONAL_ENGINES_BREAK") else []
    tmp = tempfile.mkdtemp(prefix="ssi263_optional_")
    bad = 0
    try:
        for src, defs, what in CASES:
            env = dict(os.environ, PATH=os.path.dirname(cc) + os.pathsep + os.environ.get("PATH", ""))
            r = subprocess.run([cc, "-std=gnu99", "-c", "-Werror=implicit-function-declaration", "-I", CSRC,
                                "-I", os.path.join(CSRC, "cpu")] + defs + brk +
                               [os.path.join(CSRC, src), "-o", os.path.join(tmp, "out.o")],
                               capture_output=True, text=True, env=env)
            ok = r.returncode == 0
            first = next((ln for ln in r.stderr.splitlines() if "error" in ln), "")
            print("%-4s %-22s %-8s %s" % ("ok" if ok else "FAIL", src, what, "compiles" if ok else first[-110:]))
            bad += not ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("optional engines: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
