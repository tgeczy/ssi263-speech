"""Build the MAME release libraries and contract tests into nvda/dist/blazie-lib/.

bl_live.exe and bl_live_mame.exe use MAME's Z180, as do x86/bl.dll and x64/bl.dll.
pc86.dll contains MAME's 8086 for the Accent-mini. Builds need w64devkit for both
architectures, but no z180emu checkout. --legacy-tests additionally builds the
explicitly named development references bl_live_legacy.exe/test_bl_board_legacy.exe
and test_z180_legacy.exe; none is copied into a release package.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

OUT = os.path.join(REPO, "nvda", "dist", "blazie-lib")
FLAGS = ["-O3", "-fcommon", "-std=gnu89", "-static", "-s"]


# bl.dll: the board, the Z180 core and the host (bl_host.c), in-process; the chip comes from ssi263.dll (linked
# against it, so Windows reuses the copy native.py already loaded: one chip, the one SSI263C made).  Floating point
# as ssi263.dll's: SSE2 doubles on 32-bit, no fused multiply-adds -- the host's time arithmetic must be Python's.
DLL_FLAGS = ["-O3", "-fcommon", "-std=gnu89", "-ffp-contract=off", "-shared", "-static", "-static-libgcc", "-s"]
ARCHES = {"x86": ("W64DEVKIT_X86", ["-msse2", "-mfpmath=sse"]), "x64": ("W64DEVKIT", [])}


# MAME's Z180 core: C++17 without exceptions or RTTI, libstdc++ and libgcc linked statically (one .exe, no DLLs)
MAME_CXX = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-msse2", "-mfpmath=sse", "-Wall"]
MAME_LINK = ["-static", "-static-libstdc++", "-static-libgcc", "-s"]


def build_mame(gcc, env):
    gxx = os.path.join(os.path.dirname(gcc), "g++.exe")
    cpu = os.path.join(os.path.dirname(HERE), "cpu")
    obj = os.path.join(OUT, "obj_mame")
    os.makedirs(obj, exist_ok=True)
    inc = ["-I" + HERE, "-I" + cpu, "-I" + os.path.dirname(HERE)]
    objs = {}
    for name, src, cc, flags in (
            ("z180_mame", os.path.join(cpu, "z180_mame.cpp"), gxx, MAME_CXX),
            ("z180_asci", os.path.join(cpu, "z180_asci.cpp"), gxx, MAME_CXX),
            ("bl_board", os.path.join(HERE, "bl_board.c"), gcc, ["-O3", "-std=gnu89", "-DBL_Z180_MAME"]),
            ("flash29", os.path.join(HERE, "flash29.c"), gcc, ["-O3", "-std=gnu89"]),
            ("bl_clock", os.path.join(HERE, "bl_clock.c"), gcc, ["-O3", "-std=gnu89", "-I" + cpu]),
            ("bl_rtc6242", os.path.join(HERE, "bl_rtc6242.c"), gcc, ["-O3", "-std=gnu89"]),
            ("bl_serial", os.path.join(HERE, "bl_serial.c"), gcc, ["-O3", "-std=gnu89"]),
            ("bl_live", os.path.join(HERE, "bl_live.c"), gcc, ["-O3", "-std=gnu89"]),
            ("test_bl_board", os.path.join(HERE, "test_bl_board.c"), gcc, ["-O3", "-std=gnu89"]),
            ("test_z180_contract", os.path.join(cpu, "test_z180_contract.c"), gcc, ["-O2", "-std=gnu89"]),
            ("i8085_mame", os.path.join(cpu, "i8085_mame.cpp"), gxx, MAME_CXX + ["-Wno-sign-compare"]),
            ("test_i8085_contract", os.path.join(cpu, "test_i8085_contract.c"), gcc, ["-O2", "-std=gnu89"]),
            ("i86_mame", os.path.join(cpu, "i86_mame.cpp"), gxx, MAME_CXX + ["-Wno-sign-compare"]),
            ("test_i86_contract", os.path.join(cpu, "test_i86_contract.c"), gcc, ["-O2", "-std=gnu89"]),
            ("m6809_mame", os.path.join(cpu, "m6809_mame.cpp"), gxx, MAME_CXX),
            ("test_m6809_contract", os.path.join(cpu, "test_m6809_contract.c"), gcc, ["-O2", "-std=gnu89"])):
        objs[name] = os.path.join(obj, name + ".o")
        subprocess.run([cc] + flags + inc + ["-c", "-o", objs[name], src], env=env, check=True)
    core = [objs["bl_board"], objs["flash29"], objs["bl_clock"], objs["bl_rtc6242"], objs["bl_serial"], objs["z180_mame"], objs["z180_asci"]]
    for exe, main_o in (("bl_live_mame.exe", "bl_live"), ("test_bl_board_mame.exe", "test_bl_board")):
        subprocess.run([gxx] + MAME_LINK + ["-o", os.path.join(OUT, exe), objs[main_o]] + core, env=env, check=True)
    # the CPU contract's tests (no board): ../cpu/test_z180_contract.c
    subprocess.run([gxx] + MAME_LINK + ["-o", os.path.join(OUT, "test_z180_contract.exe"), objs["test_z180_contract"],
                                        objs["z180_mame"], objs["z180_asci"]], env=env, check=True)
    # the white-box tests (they include the core): ../cpu/test_z180_whitebox.cpp
    subprocess.run([gxx] + MAME_CXX + MAME_LINK + inc + ["-o", os.path.join(OUT, "test_z180_whitebox.exe"),
                                                         os.path.join(cpu, "test_z180_whitebox.cpp"), objs["z180_asci"]],
                   env=env, check=True)
    # MAME's 8085 core (the Accent SA's; no board yet): the CPU contract's tests, ../cpu/test_i8085_contract.c
    subprocess.run([gxx] + MAME_LINK + ["-o", os.path.join(OUT, "test_i8085_contract.exe"),
                                        objs["test_i8085_contract"], objs["i8085_mame"]], env=env, check=True)
    # MAME's 8086 core (the Accent-mini's PC): the CPU contract's tests, ../cpu/test_i86_contract.c
    subprocess.run([gxx] + MAME_LINK + ["-o", os.path.join(OUT, "test_i86_contract.exe"),
                                        objs["test_i86_contract"], objs["i86_mame"]], env=env, check=True)
    # MAME's 6809 core (no board yet): the CPU contract's tests, ../cpu/test_m6809_contract.c
    subprocess.run([gxx] + MAME_LINK + ["-o", os.path.join(OUT, "test_m6809_contract.exe"),
                                        objs["test_m6809_contract"], objs["m6809_mame"]], env=env, check=True)


def build_pc86(arch, bindir, env, extra):
    """pc86.dll (../pc86: MAME's 8086 with 1 MB of flat memory) for src/hosts/pc86.py -- the Accent-mini host's
    release CPU for Accent-mini.  Static: KERNEL32 and msvcrt only, as the other DLLs."""
    cpu = os.path.join(os.path.dirname(HERE), "cpu")
    pc86 = os.path.join(os.path.dirname(HERE), "pc86")
    obj = os.path.join(OUT, "obj_pc86_" + arch)
    os.makedirs(obj, exist_ok=True)
    core_o, shim_o = os.path.join(obj, "i86_mame.o"), os.path.join(obj, "pc86.o")
    subprocess.run([os.path.join(bindir, "g++.exe"), "-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti"] + extra
                   + ["-Wall", "-Wno-sign-compare", "-I" + cpu, "-c", "-o", core_o, os.path.join(cpu, "i86_mame.cpp")],
                   env=env, check=True)
    subprocess.run([os.path.join(bindir, "gcc.exe"), "-O3", "-std=gnu89", "-Wall", "-I" + cpu] + extra
                   + ["-c", "-o", shim_o, os.path.join(pc86, "pc86.c")], env=env, check=True)
    subprocess.run([os.path.join(bindir, "g++.exe"), "-shared"] + MAME_LINK
                   + ["-o", os.path.join(OUT, arch, "pc86.dll"), shim_o, core_o], env=env, check=True)


def build_legacy_reference(gcc, env):
    """Development comparisons only. Never copied into release packages."""
    z180 = repo_paths.Z180_CORE
    # z180.c keeps __FILE__ (its CPUINFO source-file string): map the checkout's path to "." so no machine path is
    # built into the binaries (tools/check_binary_paths.py checks them)
    inc = ["-I" + HERE, "-I" + z180, "-I" + os.path.join(z180, "z180"), "-fmacro-prefix-map=%s=." % z180]
    for exe, main_c in (("bl_live_legacy.exe", "bl_live.c"), ("test_bl_board_legacy.exe", "test_bl_board.c")):
        subprocess.run([gcc] + FLAGS + inc + ["-o", os.path.join(OUT, exe), os.path.join(HERE, "bl_unity.c"),
                                               os.path.join(HERE, main_c)], env=env, check=True)
    # the legacy path's exceptions (CONTRACT.md 3) on z180emu: ../cpu/test_z180_legacy.c
    cpu = os.path.join(os.path.dirname(HERE), "cpu")
    subprocess.run([gcc] + FLAGS + inc + ["-I" + cpu, "-w", "-o", os.path.join(OUT, "test_z180_legacy.exe"),
                                           os.path.join(cpu, "test_z180_legacy.c"), os.path.join(cpu, "z180_legacy.c")],
                   env=env, check=True)


def build_mame_dll(arch, bindir, env, extra, chip_dll):
    """Release board/host on MAME only; no unity file containing z180emu."""
    cpu = os.path.join(os.path.dirname(HERE), "cpu")
    obj = os.path.join(OUT, "obj_release_" + arch)
    os.makedirs(obj, exist_ok=True)
    inc = ["-I" + HERE, "-I" + cpu, "-I" + os.path.dirname(HERE)]
    objects = []
    for name in ("z180_mame", "z180_asci", "bl_board", "flash29", "bl_serial", "bl_idle", "bl_clock", "bl_rtc6242",
                 "bl_host", "bl_voice", "bl_firmware", "bl_state"):
        cpp = name.startswith("z180_")
        src = os.path.join(cpu if cpp else HERE, name + (".cpp" if cpp else ".c"))
        tool = "g++.exe" if cpp else "gcc.exe"
        flags = (["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti"] if cpp else
                 ["-O3", "-std=gnu89", "-DBL_Z180_MAME"])
        out = os.path.join(obj, name + ".o")
        subprocess.run([os.path.join(bindir, tool)] + flags + extra + ["-ffp-contract=off"] + inc
                       + ["-c", src, "-o", out], env=env, check=True)
        objects.append(out)
    subprocess.run([os.path.join(bindir, "g++.exe"), "-shared"] + MAME_LINK
                   + ["-o", os.path.join(OUT, arch, "bl.dll")] + objects + [chip_dll], env=env, check=True)


def main():
    gcc = os.path.join(repo_paths.bin_dir("W64DEVKIT_X86", path_fallback=False), "gcc.exe")
    env = dict(os.environ, PATH=os.path.dirname(gcc) + os.pathsep + os.environ["PATH"])
    os.makedirs(OUT, exist_ok=True)
    if "--legacy-tests" in sys.argv:
        build_legacy_reference(gcc, env)
    subprocess.run([gcc] + FLAGS + ["-Wall", "-I" + HERE, "-o", os.path.join(OUT, "test_run_ahead.exe"),
                                    os.path.join(HERE, "test_run_ahead.c")], env=env, check=True)
    # the Braille 'n Speak 640's clock chip on its own: test_rtc6242.c
    subprocess.run([gcc] + FLAGS + ["-Wall", "-I" + HERE, "-o", os.path.join(OUT, "test_rtc6242.exe"),
                                    os.path.join(HERE, "test_rtc6242.c"), os.path.join(HERE, "bl_rtc6242.c")],
                   env=env, check=True)
    build_mame(gcc, env)
    # Public names are the release backend. Explicitly named old-core artifacts stay development-only.
    shutil.copy2(os.path.join(OUT, "bl_live_mame.exe"), os.path.join(OUT, "bl_live.exe"))
    shutil.copy2(os.path.join(OUT, "test_bl_board_mame.exe"), os.path.join(OUT, "test_bl_board.exe"))
    engine = os.path.dirname(os.path.dirname(HERE))
    for arch, (key, extra) in ARCHES.items():
        bindir = repo_paths.bin_dir(key, path_fallback=(arch == "x64"))
        env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
        out_dir = os.path.join(OUT, arch)
        os.makedirs(out_dir, exist_ok=True)
        chip_dll = os.path.join(engine, "ssi263", "_bin", arch, "ssi263.dll")
        build_mame_dll(arch, bindir, env, extra, chip_dll)
        build_pc86(arch, bindir, env, extra)
    print("built %s" % OUT)


if __name__ == "__main__":
    main()
