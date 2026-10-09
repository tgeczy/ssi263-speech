"""Build ssi263speech.dll, the release voice library for Windows, 32- and 64-bit,
and the SAPI engine's native test host beside it.

    python src/csrc/build_ssi263speech.py [x64|x86 ...]      # both by default

Into build/win/<arch>/:

  ssi263speech.dll   the chip, the Braille Lite (board on MAME's Z180, host, voice, number words), the Accent SA
                     (board on MAME's 8085, host, voice), the Speak-Out (MAME's
                     V40) and the Accent-mini (MAME's 8086) -- behind voices.h, the voice table.  It exports the ssv_
                     table and every voice API (blv_, asv_, sov_, amv_, ssi263_); the SAPI engine (sapi/ssi263_sapi.cpp)
                     loads it with LoadLibrary from its own folder.
  ssi263_serve.exe   sapi/ssi_serve.c: the SAPI pipe protocol (OSP4) over that DLL, loaded the way the SAPI engine
                     loads it -- sapi/ssi_serve.py ported, so sapi/test_native.py can hold the two to each other over
                     the wire.  A test and build tool (its --list makes the installer's voices.txt); never installed.

w64devkit's gcc and g++ (paths.local W64DEVKIT, W64DEVKIT_X86), each unit with the flags of the libraries the NVDA
add-ons, Linux and Android ship (build_linux.sh, test_android_native.py): -ffp-contract=off throughout, so the chip's
arithmetic is the reference's; -msse2 -mfpmath=sse on x86, so the 32-bit library computes as the 64-bit one; C++17
without exceptions or RTTI for MAME's cores; -static -static-libstdc++ -static-libgcc, so the DLL imports only KERNEL32
and the system's msvcrt (checked here with objdump).  No firmware is built in.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

SRC = HERE
SAPI = os.path.join(REPO, "sapi")
OUT = os.path.join(REPO, "build", "win")
ARCHES = {"x64": ("W64DEVKIT", []), "x86": ("W64DEVKIT_X86", ["-msse2", "-mfpmath=sse"])}
ALLOWED_IMPORTS = {"kernel32.dll", "msvcrt.dll"}


def inc(*dirs):
    return ["-I" + os.path.join(SRC, d) if d else "-I" + SRC for d in dirs]


CHIP = ["-O2", "-std=c99", "-ffp-contract=off"]
MAME = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-w"] + inc("cpu", "")
BOARD = ["-O3", "-std=gnu89", "-ffp-contract=off", "-w", "-DBL_Z180_MAME"] + inc("blazie", "cpu", "")
ACCENT = ["-O2", "-std=gnu89", "-ffp-contract=off", "-w"] + inc("cpu", "accentsa", "")
FRONT = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall"] + inc("")


def units():
    """(flags, source) for all release voices; a missing voice is a build failure."""
    for source in ("speakout/so_voice.c", "accentmini/am_voice.c", "mockingboard/mb_voice.c"):
        if not os.path.isfile(os.path.join(SRC, source)):
            raise SystemExit("Required release voice source missing: " + source)
    u = [(CHIP, "ssi263.c"), (CHIP, "ssi263dsp.c"), (MAME, "cpu/z180_mame.cpp"), (MAME, "cpu/z180_asci.cpp")]
    u += [(BOARD, "blazie/%s.c" % n) for n in ("bl_board", "flash29", "bl_serial", "bl_idle", "bl_clock", "bl_host",
                                                "bl_voice", "bl_firmware", "bl_state")]
    u += [(FRONT + inc("blazie"), "blazie/bl_numbers.c"), (FRONT, "numwords.c"), (FRONT, "numwords_es.c")]
    u += [(MAME, "cpu/i8085_mame.cpp")] + [(ACCENT, "accentsa/%s.c" % n) for n in ("as_board", "as_usart", "as_host")]
    u += [(FRONT + inc("accentsa"), "accentsa/as_voice.c"), (FRONT, "accent_text.c")]   # both Accents' text rules
    have = []
    if os.path.isfile(os.path.join(SRC, "speakout", "so_voice.c")):          # so-voice's sources (so_voice.h)
        so = FRONT + inc("cpu", "speakout")
        u += [(MAME, "cpu/v40_mame.cpp")] + [(so, "speakout/%s.c" % n) for n in ("so_board", "so_icu", "so_scu",
                                                                                   "so_hex", "so_host", "so_voice")]
        have.append("-DSSV_HAVE_SPEAKOUT")
    if os.path.isfile(os.path.join(SRC, "accentmini", "am_voice.c")):        # am-voice's sources (am_voice.h)
        am = FRONT + inc("cpu", "pc86", "accentmini")
        u += [(MAME, "cpu/i86_mame.cpp"), (am, "pc86/pc86.c")]
        u += [(am, "accentmini/%s.c" % n) for n in ("am_host", "am_voice")]
        have.append("-DSSV_HAVE_ACCENTMINI")
    if os.path.isfile(os.path.join(SRC, "mockingboard", "mb_voice.c")):      # mb-voice's sources (mb_voice.h, 0.8)
        mb = ["-O2", "-std=gnu99", "-ffp-contract=off", "-w"] + inc("cpu", "mockingboard", "")
        u += [(mb, "cpu/m6502.c")] + [(mb, "mockingboard/%s.c" % n) for n in ("mb_board", "mb_host", "mb_voice")]
        have.append("-DSSV_HAVE_MOCKINGBOARD")
    u.append((FRONT + have, "voices.c"))
    return u, have


def run(cmd, env):
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode:
        sys.exit("FAILED: %s\n%s" % (" ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd)),
                                     (r.stdout + r.stderr)[-3000:]))
    return r.stdout


def imports(objdump, path, env):
    out = run([objdump, "-p", path], env)
    return sorted({m.lower() for m in re.findall(r"DLL Name: (\S+)", out)})


def build(arch):
    key, extra = ARCHES[arch]
    bindir = repo_paths.bin_dir(key, path_fallback=False)
    gcc, gxx, objdump = (os.path.join(bindir, t + ".exe") for t in ("gcc", "g++", "objdump"))
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    out = os.path.join(OUT, arch)
    obj = os.path.join(out, "obj")
    os.makedirs(obj, exist_ok=True)
    for f in os.listdir(obj):                              # linked from an explicit list: nothing left over
        os.remove(os.path.join(obj, f))
    u, have = units()
    objs = []
    for flags, src in u:
        o = os.path.join(obj, src.replace("/", "_").rsplit(".", 1)[0] + ".o")
        run([gxx if src.endswith(".cpp") else gcc] + flags + extra + ["-c", "-o", o, os.path.join(SRC, src)], env)
        objs.append(o)
    dll = os.path.join(out, "ssi263speech.dll")
    run([gxx, "-shared", "-o", dll] + objs + ["-static", "-static-libstdc++", "-static-libgcc", "-s"], env)
    # the native serve host: sapi/ssi_serve.c and the loader the SAPI engine uses (sapi/ssi_native.c), no library linked
    exe = os.path.join(out, "ssi263_serve.exe")
    run([gcc, "-O2", "-std=gnu99", "-Wall", "-municode"] + extra + inc("") +
        ["-o", exe, os.path.join(SAPI, "ssi_serve.c"), os.path.join(SAPI, "ssi_native.c"),
         "-static", "-static-libgcc", "-s"], env)
    for path in (dll, exe):
        got = imports(objdump, path, env)
        bad = [d for d in got if d not in ALLOWED_IMPORTS]
        if bad:
            sys.exit("FAILED: %s imports %s (only %s allowed)" % (path, ", ".join(bad), ", ".join(sorted(ALLOWED_IMPORTS))))
    print("%s: %s (%s), %s; imports %s" % (arch, os.path.relpath(dll, REPO), " ".join(have) or "Braille Lite and Accent SA",
                                           os.path.relpath(exe, REPO), ", ".join(imports(objdump, dll, env))))


def main():
    for arch in (sys.argv[1:] or ["x64", "x86"]):
        if arch not in ARCHES:
            sys.exit("arch: x64 or x86")
        build(arch)


if __name__ == "__main__":
    main()
