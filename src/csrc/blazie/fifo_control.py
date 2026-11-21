"""The must-fail control for test_bl_fifo.c (issue #36): bl_board.c with its serial input queue's growth taken away --
the fixed 64 KB ring it was -- built into the test in a scratch folder and run.  It must fail "sizes" (and the rest).

    python src/csrc/blazie/fifo_control.py <firmware .bns>        exit 0 when the control failed as it must

The board's other objects are the build's: Windows nvda/dist/blazie-lib/obj_mame (32-bit, as build_board.py makes the
board's test programs), Linux build/linux/obj.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
GROW = "    if (used + (unsigned)n > u->fcap && !fifo_grow(u, used + (unsigned)n))\n"
WRAP = "    if (0)\n"
DEPS = ("flash29", "bl_clock", "bl_rtc6242", "bl_serial", "z180_mame", "z180_asci")


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: fifo_control.py <firmware .bns>")
    if os.name == "nt":
        from tools import repo_paths
        bindir = repo_paths.bin_dir("W64DEVKIT_X86", path_fallback=False)
        gcc, gxx = os.path.join(bindir, "gcc.exe"), os.path.join(bindir, "g++.exe")
        objdir = os.path.join(REPO, "nvda", "dist", "blazie-lib", "obj_mame")
        flags, link = ["-msse2", "-mfpmath=sse"], ["-static", "-static-libstdc++", "-static-libgcc"]
        env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    else:
        gcc, gxx, objdir, flags, link, env = "gcc", "g++", os.path.join(REPO, "build", "linux", "obj"), [], [], None
    deps = [os.path.join(objdir, d + ".o") for d in DEPS]
    missing = [d for d in deps if not os.path.isfile(d)]
    if missing:
        print("FAIL  fifo control: the build's objects are not there (%s)" % ", ".join(os.path.basename(m) for m in missing))
        return 1
    tmp = tempfile.mkdtemp(prefix="bl_fifo_control_")
    try:
        board = open(os.path.join(HERE, "bl_board.c"), encoding="utf-8").read().replace("\r\n", "\n")
        if board.count(GROW) != 1:
            print("FAIL  fifo control: the growth's line moved (bl_board.c)")
            return 1
        with open(os.path.join(tmp, "bl_board.c"), "w", encoding="utf-8") as f:
            f.write(board.replace(GROW, WRAP))
        shutil.copy(os.path.join(HERE, "test_bl_fifo.c"), tmp)
        o = os.path.join(tmp, "test_bl_fifo.o")
        exe = os.path.join(tmp, "test_bl_fifo")
        inc = ["-I" + tmp, "-I" + HERE, "-I" + os.path.join(CSRC, "cpu"), "-I" + CSRC]
        subprocess.run([gcc, "-O2", "-std=gnu89", "-w", "-DBL_Z180_MAME"] + flags + inc +
                       ["-c", "-o", o, os.path.join(tmp, "test_bl_fifo.c")], check=True, env=env)
        subprocess.run([gxx, "-o", exe, o] + deps + link + ["-lm"], check=True, env=env)
        r = subprocess.run([exe, sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = r.stdout.decode(errors="replace")
        sys.stdout.write(out)
        failed_sizes = any(l.startswith("FAIL sizes ") for l in out.splitlines())
        print("fifo control: %s" % ("failed as it must (the 64 KB ring)" if r.returncode == 1 and failed_sizes
                                    else "did NOT fail as it must"))
        return 0 if (r.returncode == 1 and failed_sizes) else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
