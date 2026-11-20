"""Build the Accent SA's board on MAME's Z80 core, into nvda/dist/accentz80-lib/:

  test_az_board.exe    the board's own rules on small programs (test_az_board.c); no firmware needed
  az_trace.exe         the C API alone: the two ROMs from files, a text, every chip write with its time (az_trace.c;
                       the chip built in)
  az_waiting.exe       the tool that measures a firmware's waiting loops into az_waiting.inc (az_waiting.c)

The voice itself (az_voice.c) is built into ssi263speech.dll by ../build_ssi263speech.py, with the other voices.

w64devkit gcc/g++ (paths.local W64DEVKIT); C++17 without exceptions or RTTI for the cores, libstdc++ and libgcc linked
statically.  python src/csrc/accentz80/build_board.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
CPU = os.path.join(CSRC, "cpu")
PC = os.path.join(CSRC, "accentsa")
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

OUT = os.path.join(REPO, "nvda", "dist", "accentz80-lib")
CXX = ["-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-Wall", "-Wno-sign-compare"]
CC = ["-O2", "-std=gnu89", "-ffp-contract=off", "-Wall"]
CHIP = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter"]
LINK = ["-static", "-static-libstdc++", "-static-libgcc", "-s"]
BOARD = [(os.path.join(CPU, "z80_mame.cpp"), "g++", CXX), (os.path.join(HERE, "az_board.c"), "gcc", CC),
         (os.path.join(PC, "as_usart.c"), "gcc", CC)]
# the host is the Accent-PC's (as_host.c), which brings that card's board and its 8085 with it
HOST = [(os.path.join(HERE, "az_host.c"), "gcc", CC), (os.path.join(PC, "as_host.c"), "gcc", CC),
        (os.path.join(PC, "as_board.c"), "gcc", CC), (os.path.join(CPU, "i8085_mame.cpp"), "g++", CXX),
        (os.path.join(CSRC, "ssi263.c"), "gcc", CHIP), (os.path.join(CSRC, "ssi263dsp.c"), "gcc", CHIP)]


def compile_all(bindir, obj, sources):
    """[(source, tool, flags)] into obj/; returns the objects' paths."""
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    os.makedirs(obj, exist_ok=True)
    inc = ["-I" + CPU, "-I" + HERE, "-I" + PC, "-I" + CSRC]
    out = []
    for src, tool, flags in sources:
        o = os.path.join(obj, os.path.splitext(os.path.basename(src))[0] + ".o")
        subprocess.run([os.path.join(bindir, tool)] + flags + inc + ["-c", "-o", o, src], env=env, check=True)
        out.append(o)
    return out, env


def main():
    os.makedirs(OUT, exist_ok=True)
    bindir = repo_paths.bin_dir("W64DEVKIT", path_fallback=True)
    gxx = os.path.join(bindir, "g++")
    obj = os.path.join(OUT, "obj")
    shared, env = compile_all(bindir, obj, BOARD + HOST)
    board = shared[:len(BOARD)]
    for exe, objs in (("test_az_board", board), ("az_trace", shared), ("az_waiting", shared)):
        own, env = compile_all(bindir, obj, [(os.path.join(HERE, exe + ".c"), "gcc", CC)])
        subprocess.run([gxx] + LINK + ["-o", os.path.join(OUT, exe + ".exe")] + own + objs, env=env, check=True)
    print("built %s" % OUT)


if __name__ == "__main__":
    main()
