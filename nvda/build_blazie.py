"""Assemble the Blazie (Braille Lite 2000 + SSI-263) NVDA add-on.

Carries the Braille Lite's firmware and a RAM snapshot containing it, which are not in the
repository: put your own copies in firmware/blazie/.  z180emu (GPLv2, with this project's
bns.c front end; set Z180 below) ships with its complete source in the add-on.
Output: nvda/dist/blazie-ssi263-<version>.nvda-addon

bns_live.exe is built here, 32-bit (i686, static), so the one binary runs on 32- and
64-bit Windows 7 and later; its output is identical to the x64 build's (verified).
"""
import os
import shutil
import subprocess
import sys
import zipfile

from build_common import NVDA_RANGE, check_native, copy_engine, rm, zip_build

VERSION = "0.5.0"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ENGINE = os.path.join(REPO, "src")
Z180 = r"C:\git\z180emu"
FIRMWARE = os.path.join(REPO, "firmware", "blazie", "BL2ENG.BNS")
STATE = os.path.join(REPO, "firmware", "blazie", "bl2_2003_warm.state")
GCC32 = r"C:\w64devkit-x86\w64devkit\bin"
CORE = ("z180", "z180dasm", "z80daisy", "z80scc", "z180asci")
CFLAGS = ["-O3", "-fcommon", "-DSOCKETCONSOLE", "-std=gnu89"]
LINK = ["-O3", "-fcommon", "-std=gnu89", "-static", "-s"]

BUILD = os.path.join(HERE, "dist", "blazie-build")
OBJ = os.path.join(HERE, "dist", "bns32-obj")
OUT = os.path.join(HERE, "dist", "blazie-ssi263-%s.nvda-addon" % VERSION)

MANIFEST = f'''name = blazie_ssi263
summary = "Braille Lite / Braille 'n Speak (SSI-263 emulation)"
description = """A Blazie Braille Lite 2000 in speech-box mode, emulated: the unit's own June 2003 firmware runs in z180emu and drives a register-level model of the Silicon Systems SSI-263 speech chip, whose A/R request drives the firmware back. The rules, number reading and inflection are the firmware's own, live. Nothing is recorded or concatenated.

Settings map to the unit's own: rate 1-16, pitch 1-63, tone 1-25 as the variant; the defaults are the unit's factory settings (rate 11, pitch 16, tone 7). Volume is applied digitally.

Runs on NVDA 2021.1 and later, 32- or 64-bit, Windows 7 and later; no numpy and no Visual C++ runtime needed.

This add-on carries the Braille Lite's own firmware (Blazie Engineering). It is not ours; it is here so the unit can speak again, and it will be removed if its rights holders ask. z180emu is GPLv2; its complete source for this build is included as z180emu-source.zip. Source: https://github.com/tgeczy/ssi263-speech"""
author = "tgeczy (SSI-263 chip engine and driver, with Claude)"
url = "https://github.com/tgeczy/ssi263-speech"
version = {VERSION}
''' + NVDA_RANGE


def build_bns32(out_exe):
    """i686 static bns_live.exe from the z180emu tree; objects go to dist/bns32-obj."""
    if os.path.isdir(OBJ):
        rm(OBJ)
    os.makedirs(OBJ)
    env = dict(os.environ, PATH=GCC32 + os.pathsep + os.environ["PATH"])
    gcc = os.path.join(GCC32, "gcc.exe")
    objs = []
    for name in CORE:
        o = os.path.join(OBJ, name + ".o")
        subprocess.run([gcc] + CFLAGS + ["-o", o, "-c", name + ".c"], cwd=os.path.join(Z180, "z180"),
                       env=env, check=True, stderr=subprocess.DEVNULL)
        objs.append(o)
    subprocess.run([gcc] + LINK + ["-o", out_exe, "bns.c"] + objs, cwd=Z180, env=env, check=True,
                   stderr=subprocess.DEVNULL)
    check_native(out_exe, "x86")


def main():
    for p in (FIRMWARE, STATE, os.path.join(Z180, "bns.c")):
        if not os.path.isfile(p):
            sys.exit("missing: %s" % p)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_blazie")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "blazie", "synthDrivers", "blazie.py"), sd)
    copy_engine(ENGINE, eng)
    shutil.copy2(os.path.join(ENGINE, "hosts", "blazie.py"), os.path.join(eng, "blazie_host.py"))
    shutil.copy2(os.path.join(HERE, "shared", "ssi263_numwords.py"), eng)
    build_bns32(os.path.join(eng, "bns_live.exe"))
    shutil.copy2(FIRMWARE, os.path.join(eng, "BL2ENG.BNS"))
    shutil.copy2(STATE, eng)
    # GPLv2: the complete corresponding source of bns_live.exe
    with zipfile.ZipFile(os.path.join(eng, "z180emu-source.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for fn in ("bns.c", "COPYING", "README.md", "Makefile", "sconsole.h", "z180dbg.h"):
            p = os.path.join(Z180, fn)
            if os.path.isfile(p):
                z.write(p, fn)
        for root, _, files in os.walk(os.path.join(Z180, "z180")):
            for fn in files:
                if fn.endswith((".c", ".h")):
                    p = os.path.join(root, fn)
                    z.write(p, os.path.relpath(p, Z180))
        z.writestr("BUILD.txt", "Built with w64devkit GCC 16.2, i686 (32-bit):\n"
                   "  cd z180; for each of %s:\n"
                   "    gcc %s -o ../<name>.o -c <name>.c\n"
                   "  gcc %s -o bns_live.exe bns.c %s\n"
                   % (", ".join(CORE), " ".join(CFLAGS), " ".join(LINK), " ".join(n + ".o" for n in CORE)))
    shutil.copy2(os.path.join(Z180, "COPYING"), os.path.join(eng, "COPYING.z180emu"))
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)
    zip_build(BUILD, OUT)


if __name__ == "__main__":
    main()
