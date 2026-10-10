"""Build the 0.8 add-on, ssi263-speech-<version>.nvda-addon: every unit in one driver (nvda/ssi263/synthDrivers/ssi263.py).

The units are the 0.7 add-ons' own: build_blazie.py, build_speakout.py and build_accent.py are run as they are, and
their staged synthDrivers (each driver and its engine package) are moved whole into this add-on's private package,
synthDrivers/_ssi263_unified -- so each unit here is that add-on's, file for file.  The Mockingboard's driver
(nvda/mockingboard) is staged beside them with its engine: the shared binding, the library, its firmware file when
the local firmware folder has it (firmware/sweet-micro-mockingboard, never in the repository; without it the voice is
not staged) and its notices.

    python nvda/build_ssi263.py [--reuse]      # --reuse: take dist/<name>-build as built, do not run the builders

Runtime code is MIT + BSD; the firmware has its own terms (the manifest, licenses/).
"""
import os
import shutil
import subprocess
import sys

from build_common import copy_native_binding, read_manifest, rm, zip_build

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MANIFEST, VERSION = read_manifest(os.path.join(HERE, "ssi263"))
BUILD = os.path.join(HERE, "dist", "ssi263-build")
OUT = os.path.join(HERE, "dist", "ssi263-speech-%s.nvda-addon" % VERSION)
UNITS = ("blazie", "speakout", "accent")
MB_FILE = os.path.join(REPO, "firmware", "sweet-micro-mockingboard", "mockingboard-tts-1.1.bin")
MB_NOTICE = """The Mockingboard's text-to-speech -- notice

This add-on carries Sweet Micro Systems' text-to-speech for the Mockingboard (mockingboard-tts-1.1.bin: TEXT TO SPEECH
and INFLECTION version 1.1, 11 March 1985, the IIe TTS driver and the MKB:RULE files, from the Mockingboard Developers
Toolkit disk). It is not ours; it is here so the card can speak again, and it will be removed if its rights holders
ask. It is not covered by this add-on's MIT license.
"""


def stage_mockingboard(unified):
    shutil.copy2(os.path.join(HERE, "mockingboard", "synthDrivers", "mockingboard.py"), unified)
    eng = os.path.join(unified, "_ssi263_mockingboard")
    os.makedirs(eng)
    copy_native_binding(eng)
    lic = os.path.join(eng, "licenses")
    os.makedirs(lic, exist_ok=True)
    fake = os.path.join(REPO, "src", "csrc", "cpu", "fake6502")
    shutil.copy2(os.path.join(fake, "PINNED.txt"), os.path.join(lic, "Fake6502-provenance.txt"))
    shutil.copy2(os.path.join(fake, "LICENSE-EchoTalk-BSD-3-Clause.txt"), lic)
    if os.path.isfile(MB_FILE):
        shutil.copy2(MB_FILE, eng)
        with open(os.path.join(lic, "Mockingboard-firmware-notice.txt"), "w", encoding="utf-8") as f:
            f.write(MB_NOTICE)
        return True
    print("note: no %s: the Mockingboard is not staged" % os.path.relpath(MB_FILE, REPO))
    return False


def main():
    reuse = "--reuse" in sys.argv[1:]
    for unit in UNITS:
        built = os.path.join(HERE, "dist", "%s-build" % unit, "synthDrivers")
        if not (reuse and os.path.isdir(built)):
            subprocess.run([sys.executable, os.path.join(HERE, "build_%s.py" % unit)], check=True)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    unified = os.path.join(sd, "_ssi263_unified")
    os.makedirs(unified)
    with open(os.path.join(unified, "__init__.py"), "w", encoding="utf-8") as f:
        f.write('"""The units of synthDrivers/ssi263.py: the 0.7 add-ons\' drivers with their engines."""\n')
    for unit in UNITS:
        src = os.path.join(HERE, "dist", "%s-build" % unit, "synthDrivers")
        for name in os.listdir(src):
            if name == "__pycache__":
                continue
            p = os.path.join(src, name)
            (shutil.copytree if os.path.isdir(p) else shutil.copy2)(
                p, os.path.join(unified, name), **({"ignore": shutil.ignore_patterns("__pycache__")}
                                                   if os.path.isdir(p) else {}))
    if not stage_mockingboard(unified):
        os.remove(os.path.join(unified, "mockingboard.py"))
        rm(os.path.join(unified, "_ssi263_mockingboard"))
    shutil.copy2(os.path.join(HERE, "ssi263", "synthDrivers", "ssi263.py"), sd)
    shutil.copytree(os.path.join(HERE, "blazie", "locale"), os.path.join(BUILD, "locale"))
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)
    zip_build(BUILD, OUT)


if __name__ == "__main__":
    main()
