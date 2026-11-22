"""Build the Accent-PC board on MAME's 8085 core (not yet accepted; opt-in only), into nvda/dist/accentpc-lib/:

  test_as_board.exe    the board's own rules on small programs (test_as_board.c); no firmware needed
  as_render.exe        the C API alone: the ROMs from a folder, a text into a WAV (as_render.c; the chip built in)
  x64/accent_pc.dll    the board and host for src/hosts/accent_pc_c.py (the Python host's SSI263_ACCENT_PC_CORE=c)
  x86/accent_pc.dll    the same for 32-bit Python (NVDA 2021-2023)

accent_pc.dll links src/ssi263/_bin/<arch>/ssi263.dll (as bl.dll does), so the process uses the one chip library
ssi263/native.py loaded, and the SSI263C the caller made is the chip the host drives.  Floating point as ssi263.dll's:
SSE2 doubles on 32-bit, no fused multiply-adds -- the host's time arithmetic must be Python's.

w64devkit gcc/g++ (paths.local W64DEVKIT, W64DEVKIT_X86); C++17 without exceptions or RTTI for the core, libstdc++
and libgcc linked statically (no DLLs beside KERNEL32, msvcrt and ssi263.dll).  python src/csrc/accentpc/build_board.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
CPU = os.path.join(CSRC, "cpu")
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

OUT = os.path.join(REPO, "nvda", "dist", "accentpc-lib")
CXX = ["-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-Wall", "-Wno-sign-compare"]
CC = ["-O2", "-std=gnu89", "-ffp-contract=off", "-Wall"]
CHIP = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter"]
LINK = ["-static", "-static-libstdc++", "-static-libgcc", "-s"]
BOARD = ["as_board.c", "as_usart.c"]
ARCHES = {"x64": ("W64DEVKIT", []), "x86": ("W64DEVKIT_X86", ["-msse2", "-mfpmath=sse"])}


def compile_all(bindir, extra, obj, sources):
    """[(source, tool, flags)] into obj/; returns the objects' paths."""
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    os.makedirs(obj, exist_ok=True)
    inc = ["-I" + CPU, "-I" + HERE, "-I" + CSRC]
    out = []
    for src, tool, flags in sources:
        o = os.path.join(obj, os.path.splitext(os.path.basename(src))[0] + ".o")
        subprocess.run([os.path.join(bindir, tool)] + flags + extra + inc + ["-c", "-o", o, src], env=env, check=True)
        out.append(o)
    return out, env


def board_sources(host=True):
    s = [(os.path.join(CPU, "i8085_mame.cpp"), "g++", CXX)] + [(os.path.join(HERE, f), "gcc", CC) for f in BOARD]
    if host:
        s.append((os.path.join(HERE, "as_host.c"), "gcc", CC))
    return s


def main():
    os.makedirs(OUT, exist_ok=True)
    # the test programs (64-bit)
    bindir = repo_paths.bin_dir("W64DEVKIT", path_fallback=True)
    gxx = os.path.join(bindir, "g++")
    objs, env = compile_all(bindir, [], os.path.join(OUT, "obj"), board_sources(host=False) + [
        (os.path.join(HERE, "test_as_board.c"), "gcc", CC)])
    subprocess.run([gxx] + LINK + ["-o", os.path.join(OUT, "test_as_board.exe")] + objs, env=env, check=True)
    objs, env = compile_all(bindir, [], os.path.join(OUT, "obj_render"), board_sources() + [
        (os.path.join(HERE, "as_render.c"), "gcc", CC), (os.path.join(CSRC, "ssi263.c"), "gcc", CHIP),
        (os.path.join(CSRC, "ssi263dsp.c"), "gcc", CHIP)])
    subprocess.run([gxx] + LINK + ["-o", os.path.join(OUT, "as_render.exe")] + objs, env=env, check=True)
    # the board and host as a DLL, per architecture, importing ssi263.dll
    for arch, (key, extra) in ARCHES.items():
        bindir = repo_paths.bin_dir(key, path_fallback=(arch == "x64"))
        objs, env = compile_all(bindir, extra, os.path.join(OUT, "obj_" + arch), board_sources())
        chip_dll = os.path.join(REPO, "src", "ssi263", "_bin", arch, "ssi263.dll")
        if not os.path.isfile(chip_dll):
            sys.exit("ssi263.dll not found for %s: build it first (python src/csrc/build_native.py)" % arch)
        os.makedirs(os.path.join(OUT, arch), exist_ok=True)
        subprocess.run([os.path.join(bindir, "g++"), "-shared"] + LINK
                       + ["-o", os.path.join(OUT, arch, "accent_pc.dll")] + objs + [chip_dll], env=env, check=True)
    print("built %s" % OUT)


if __name__ == "__main__":
    main()
