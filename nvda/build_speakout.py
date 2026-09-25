"""Assemble the Speak-Out (SSI-263) NVDA add-on.

The result carries the Speak-Out's firmware, which is not in the repository: put your own
copy in firmware/gw-micro-speakout/.  Output: nvda/dist/speakout-ssi263-<version>.nvda-addon

The V40 runs in Unicorn through src/hosts/ucmini.py and a static x86-core-only
Unicorn 2.1.4 build (src/hosts/bin/{x64,x86}/unicorn.dll; see UNICORN_BUILD below).
"""
import os
import shutil
import sys

from build_common import IGN, NVDA_RANGE, check_native, copy_engine, copy_unicorn_license, rm, zip_build

VERSION = "0.5.0"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ENGINE = os.path.join(REPO, "src")
FIRMWARE = os.path.join(REPO, "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")

BUILD = os.path.join(HERE, "dist", "speakout-build")
OUT = os.path.join(HERE, "dist", "speakout-ssi263-%s.nvda-addon" % VERSION)

MANIFEST = f'''name = speakout_ssi263
summary = "Speak-Out (SSI-263 emulation)"
description = """The 1995 Speak-Out talking box, emulated: its own firmware (letter-to-sound rules, numbers, settings) runs inside NVDA and drives a register-level model of the Silicon Systems SSI-263 speech chip. Nothing is recorded or concatenated; the chip model generates the sound.

Settings map to the box's own: rate 0-9, pitch 0-9, and tone A-Z as the variant (default I). Volume is applied digitally, with the box at full volume.

Runs on NVDA 2021.1 and later, 32- or 64-bit, Windows 7 and later; no numpy and no Visual C++ runtime needed.

This add-on carries the Speak-Out's own firmware (hardware Daniel Weirich, software Douglas Geoffray, GW Micro). It is not ours; it is here so the box can speak again, and it will be removed if its rights holders ask. The firmware runs in Unicorn (GPLv2; see UNICORN-BUILD.txt). Source: https://github.com/tgeczy/ssi263-speech"""
author = "tgeczy (SSI-263 chip engine and driver, with Claude)"
url = "https://github.com/tgeczy/ssi263-speech"
version = {VERSION}
''' + NVDA_RANGE

UNICORN_BUILD = """unicorn.dll (bin/x64, bin/x86): Unicorn 2.1.4, GPLv2, x86 core only, static.

Source: https://pypi.org/project/unicorn/2.1.4/ (unicorn-2.1.4.tar.gz,
sha256 00567a70e323f749b419cd86bee4f9115beab7ebba32194581c090cbb7c59cff), src/ folder,
with one change: in src/qemu/configure, line 201, the path check "[[:space:]:]" becomes
"[[:space:]]" (Windows paths contain a colon).  The complete source is available on
request from the add-on's author.

bin/x86 (i686), built with w64devkit GCC 16.2:
  cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=x86
        -DUNICORN_BUILD_TESTS=OFF -DUNICORN_INSTALL=OFF -DUNICORN_LEGACY_STATIC_ARCHIVE=OFF
        -DBUILD_SHARED_LIBS=ON -DCMAKE_C_COMPILER=gcc
        "-DCMAKE_SHARED_LINKER_FLAGS=-static -static-libgcc -s"
  cmake --build build

bin/x64, the Windows-on-ARM fix (2026-09-25, sha256
e138d444388e0d73ac6c42fde2e462faa6660b7f7eb0d116beab1a52bdbe7c71): under x64 emulation on
ARM64 Windows the stock DLL died with 0xC00000FF on its first execution, when the Windows CRT's
longjmp tried to unwind through TCG-generated code.  unicorn-2.1.4-no-crt-unwind.patch (included)
makes Unicorn's paired internal sigsetjmp/siglongjmp use GCC's __builtin_setjmp/__builtin_longjmp
when UC_NO_CRT_UNWIND is defined.  Built as above with x86_64 GCC plus
-DCMAKE_C_FLAGS=-DUC_NO_CRT_UNWIND -DUNICORN_BUILD_TESTS=ON and without -s; the recipe is
build_unicorn_candidate.py (included).  Verified on an ARM64 Windows 11 machine (build 26200)
and on x64, where Accent and Speak-Out speech is byte-identical to the stock DLL's.
The license text is COPYING.unicorn.
"""


def main():
    if not os.path.isfile(FIRMWARE):
        sys.exit("firmware not found: %s" % FIRMWARE)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_speakout")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "speakout", "synthDrivers", "speakout.py"), sd)
    copy_engine(ENGINE, eng)
    shutil.copy2(os.path.join(ENGINE, "hosts", "speakout.py"), os.path.join(eng, "speakout_host.py"))
    shutil.copy2(os.path.join(ENGINE, "hosts", "ucmini.py"), eng)
    shutil.copytree(os.path.join(ENGINE, "hosts", "bin"), os.path.join(eng, "bin"), ignore=IGN)
    for arch in ("x64", "x86"):
        check_native(os.path.join(eng, "bin", arch, "unicorn.dll"), arch)
    shutil.copy2(FIRMWARE, os.path.join(eng, "SPEAKOUT.HEX"))
    copy_unicorn_license(eng)
    for name in ("unicorn-2.1.4-no-crt-unwind.patch", "build_unicorn_candidate.py"):
        shutil.copy2(os.path.join(ENGINE, "csrc", name), eng)
    with open(os.path.join(eng, "UNICORN-BUILD.txt"), "w", encoding="utf-8") as f:
        f.write(UNICORN_BUILD)
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)
    zip_build(BUILD, OUT)


if __name__ == "__main__":
    main()
