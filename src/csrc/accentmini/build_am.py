"""Build the Accent-mini voice in C (am_voice on am_host, Aicom's SPKEMS.DVC on MAME's 8086) into
nvda/dist/accentmini-lib/, for the tests; the NVDA add-on does not carry it (its Accent-mini is still the Python host
on pc86.dll):

  am_render.exe        the C API alone: the driver from a file, a text into a WAV (am_render.c; the chip built in)
  x64/accent_mini.dll  am_voice + am_host + the shared text rules + pc86 + the 8086, for nvda/tools/am_voice_equiv.py
  x86/accent_mini.dll  the same for 32-bit Python

accent_mini.dll links src/ssi263/_bin/<arch>/ssi263.dll (as bl.dll and accent_pc.dll do), so a Python process that
loaded the chip library uses the one copy.  Floating point as ssi263.dll's: SSE2 doubles on 32-bit, no fused
multiply-adds -- the host's time arithmetic must be Python's.

w64devkit gcc/g++ (paths.local W64DEVKIT, W64DEVKIT_X86); C++17 without exceptions or RTTI for the core, libstdc++
and libgcc linked statically.  python src/csrc/accentmini/build_am.py [--x64]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
CPU = os.path.join(CSRC, "cpu")
PC86 = os.path.join(CSRC, "pc86")
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

OUT = os.path.join(REPO, "nvda", "dist", "accentmini-lib")
CXX = ["-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-Wall", "-Wno-sign-compare"]
CC = ["-O2", "-std=gnu99", "-ffp-contract=off", "-Wall"]
CHIP = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter"]
LINK = ["-static", "-static-libstdc++", "-static-libgcc", "-s"]
ARCHES = {"x64": ("W64DEVKIT", []), "x86": ("W64DEVKIT_X86", ["-msse2", "-mfpmath=sse"])}

# every source the voice is made of (the chip apart): what am_voice_equiv.py checks the DLL is newer than
SOURCES = [(os.path.join(CPU, "i86_mame.cpp"), "g++", CXX), (os.path.join(PC86, "pc86.c"), "gcc", CC),
           (os.path.join(HERE, "am_host.c"), "gcc", CC), (os.path.join(HERE, "am_voice.c"), "gcc", CC),
           (os.path.join(CSRC, "accent_text.c"), "gcc", CC), (os.path.join(CSRC, "numwords.c"), "gcc", CC)]
HEADERS = [os.path.join(HERE, "am_host.h"), os.path.join(HERE, "am_voice.h"), os.path.join(CSRC, "accent_text.h"),
           os.path.join(CSRC, "numwords.h"), os.path.join(PC86, "pc86.h"), os.path.join(CPU, "cpu.h")]


def compile_all(bindir, extra, obj, sources):
    """[(source, tool, flags)] into obj/; returns the objects' paths."""
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    os.makedirs(obj, exist_ok=True)
    inc = ["-I" + CPU, "-I" + PC86, "-I" + HERE, "-I" + CSRC]
    out = []
    for src, tool, flags in sources:
        o = os.path.join(obj, os.path.splitext(os.path.basename(src))[0] + ".o")
        subprocess.run([os.path.join(bindir, tool)] + flags + extra + inc + ["-c", "-o", o, src], env=env, check=True)
        out.append(o)
    return out, env


def main():
    os.makedirs(OUT, exist_ok=True)
    bindir = repo_paths.bin_dir("W64DEVKIT", path_fallback=True)
    objs, env = compile_all(bindir, [], os.path.join(OUT, "obj_render"), SOURCES + [
        (os.path.join(HERE, "am_render.c"), "gcc", CC), (os.path.join(CSRC, "ssi263.c"), "gcc", CHIP),
        (os.path.join(CSRC, "ssi263dsp.c"), "gcc", CHIP)])
    subprocess.run([os.path.join(bindir, "g++")] + LINK + ["-o", os.path.join(OUT, "am_render.exe")] + objs + [
        "-lm"], env=env, check=True)
    for arch, (key, extra) in ARCHES.items():
        if arch != "x64" and "--x64" in sys.argv:
            continue
        bindir = repo_paths.bin_dir(key, path_fallback=(arch == "x64"))
        objs, env = compile_all(bindir, extra, os.path.join(OUT, "obj_" + arch), SOURCES)
        chip_dll = os.path.join(REPO, "src", "ssi263", "_bin", arch, "ssi263.dll")
        if not os.path.isfile(chip_dll):
            sys.exit("ssi263.dll not found for %s: build it first (python src/csrc/build_native.py)" % arch)
        os.makedirs(os.path.join(OUT, arch), exist_ok=True)
        subprocess.run([os.path.join(bindir, "g++"), "-shared"] + LINK
                       + ["-o", os.path.join(OUT, arch, "accent_mini.dll")] + objs + [chip_dll], env=env, check=True)
    print("built %s" % os.path.relpath(OUT, REPO))


if __name__ == "__main__":
    main()
