"""Assemble the Accent SA and Mini (SSI-263) NVDA add-on.

The result carries Aicom's driver and firmware (firmware/, with firmware/AICOM.txt).
Output: nvda/dist/accent-ssi263-<version>.nvda-addon

The Mini's driver runs in Unicorn through src/hosts/ucmini.py and the same static
x86-core-only Unicorn 2.1.4 build as the Speak-Out add-on (see UNICORN-BUILD.txt there).
The SA's firmware (firmware/aicom-accent-sa, in the repository with its notice) runs on
src/hosts/i8085.py, plain Python.
"""
import os
import shutil
import sys

from build_common import IGN, NVDA_RANGE, check_native, copy_engine, copy_unicorn_license, rm, zip_build
from build_speakout import UNICORN_BUILD

VERSION = "0.5.0"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ENGINE = os.path.join(REPO, "src")
DRIVER = os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
SA_ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")

BUILD = os.path.join(HERE, "dist", "accent-build")
OUT = os.path.join(HERE, "dist", "accent-ssi263-%s.nvda-addon" % VERSION)

MANIFEST = f'''name = accent_ssi263
summary = "Accent SA and Mini (SSI-263 emulation)"
description = """The Aicom Accent, emulated, in two voices. Accent-mini: Aicom's own DOS device driver (SPKEMS.DVC, "Accent-EMS" V4.5, 1986-1993) runs inside NVDA as DOS would have loaded it, with its rules in emulated expanded memory. Accent SA: the stand-alone box's own 8085 firmware and dictionary ROMs (1986-1989) run on an emulated 8085, with text arriving on its serial port. Both drive a register-level model of the SSI-263 (Aicom's AI901). The pronunciation, intonation and timing are Aicom's own. Nothing is recorded or concatenated.

Settings map to the Accent's own commands: rate R0-H, pitch P0-9, voice characteristic V0-9 as the variant, inflection M1 (monotone) to M0 (full intonation). The defaults are the Accent's. Volume is applied digitally.

Runs on NVDA 2021.1 and later, 32- or 64-bit, Windows 7 and later; no numpy and no Visual C++ runtime needed.

This add-on carries Aicom Corporation's own driver and firmware. They are not ours; see AICOM.txt for where they came from, and why we believe there is no one left to ask. They will be removed if a rights holder asks. The Mini's driver runs in Unicorn (GPLv2; see UNICORN-BUILD.txt). Source: https://github.com/tgeczy/ssi263-speech"""
author = "tgeczy (SSI-263 chip engine and driver, with Claude)"
url = "https://github.com/tgeczy/ssi263-speech"
version = {VERSION}
''' + NVDA_RANGE


def write_state(path):
    """The emulated machine after the driver's INIT (0.58 s at every launch otherwise), for
    Accent.boot(state=...).  Stamped with the driver's sha256: the add-on ignores a state
    made from another file and runs INIT instead."""
    import hashlib
    import pickle
    sys.path.insert(0, ENGINE)
    from hosts.accent import Accent
    from ssi263.native import SSI263C
    state = Accent(DRIVER, chip=SSI263C()).init_state()
    state["dvc_sha256"] = hashlib.sha256(open(DRIVER, "rb").read()).hexdigest()
    with open(path, "wb") as f:
        pickle.dump(state, f, protocol=4)          # protocol 4: Python 3.7 (NVDA 2021) reads it


def main():
    if not os.path.isfile(DRIVER):
        sys.exit("driver not found: %s" % DRIVER)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_accent")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "accent", "synthDrivers", "accentmini.py"), sd)
    copy_engine(ENGINE, eng)
    shutil.copy2(os.path.join(ENGINE, "hosts", "accent.py"), os.path.join(eng, "accent_host.py"))
    shutil.copy2(os.path.join(ENGINE, "hosts", "ucmini.py"), eng)
    shutil.copy2(os.path.join(ENGINE, "hosts", "accent_sa.py"), os.path.join(eng, "accent_sa_host.py"))
    shutil.copy2(os.path.join(ENGINE, "hosts", "i8085.py"), eng)
    os.makedirs(os.path.join(eng, "accent-sa"))
    for name in ("u2.BIN", "u3.BIN", "u4.BIN"):
        shutil.copy2(os.path.join(SA_ROMS, name), os.path.join(eng, "accent-sa", name))
    shutil.copy2(os.path.join(REPO, "firmware", "AICOM.txt"), eng)
    shutil.copy2(os.path.join(HERE, "shared", "ssi263_numwords.py"), eng)
    shutil.copytree(os.path.join(ENGINE, "hosts", "bin"), os.path.join(eng, "bin"), ignore=IGN)
    for arch in ("x64", "x86"):
        check_native(os.path.join(eng, "bin", arch, "unicorn.dll"), arch)
    shutil.copy2(DRIVER, os.path.join(eng, "SPKEMS.DVC"))
    write_state(os.path.join(eng, "SPKEMS.state"))
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
