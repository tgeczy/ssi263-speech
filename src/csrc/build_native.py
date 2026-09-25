"""Build ssi263.dll (the chip core + host-rate stage), 64- and 32-bit, into ssi263/_bin/{x64,x86}/.

w64devkit gcc, -static: the DLLs import only KERNEL32 and msvcrt (no VC runtime, no
UCRT), so they load on Windows 7 as well as 11.  Floating point is kept to the Python
reference's arithmetic: -ffp-contract=off (no fused multiply-adds), and on 32-bit SSE2
doubles instead of the x87's 80-bit ones (32-bit Python 3.7 already requires SSE2).

The sources are plain C99; on Linux:  cc -O2 -std=c99 -ffp-contract=off -shared -fPIC
-o libssi263.so ssi263.c ssi263dsp.c -lm
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
SOURCES = [os.path.join(HERE, "ssi263.c"), os.path.join(HERE, "ssi263dsp.c")]
sys.path.insert(0, os.path.dirname(ENGINE))
from tools import repo_paths  # noqa: E402  (the compilers' folders come from paths.local)

# arch -> (paths.local key, extra flags); the i686 compiler is never taken from the PATH
TOOLCHAINS = {"x64": ("W64DEVKIT", []),
              "x86": ("W64DEVKIT_X86", ["-msse2", "-mfpmath=sse"])}
CFLAGS = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-parameter"]
ALLOWED = {"KERNEL32.dll", "msvcrt.dll"}


def main():
    objdump = repo_paths.program("W64DEVKIT", "objdump")
    for arch, (key, extra) in TOOLCHAINS.items():
        bindir = repo_paths.bin_dir(key, path_fallback=(arch == "x64"))
        out_dir = os.path.join(ENGINE, "ssi263", "_bin", arch)
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, "ssi263.dll")
        env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
        cmd = ([os.path.join(bindir, "gcc.exe")] + CFLAGS + extra
               + ["-shared", "-static", "-static-libgcc", "-s", "-o", out] + SOURCES)
        subprocess.run(cmd, check=True, env=env)
        dump = subprocess.run([objdump, "-p", out], capture_output=True, text=True, check=True).stdout
        dlls = {ln.split(":", 1)[1].strip() for ln in dump.splitlines() if "DLL Name:" in ln}
        if not dlls <= ALLOWED:
            sys.exit("%s imports %s" % (out, sorted(dlls - ALLOWED)))
        fmt = "pei-x86-64" if arch == "x64" else "pei-i386"
        if fmt not in dump:
            sys.exit("%s is not %s" % (out, fmt))
        old = os.path.join(out_dir, "ssi263dsp.dll")          # before 0.3: the FIR alone
        if os.path.exists(old):
            os.remove(old)
        print("%s: %d bytes, imports %s" % (out, os.path.getsize(out), sorted(dlls)))


if __name__ == "__main__":
    main()
