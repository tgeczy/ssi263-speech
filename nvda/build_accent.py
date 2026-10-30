"""Build Accent Mini/SA with MAME's 8086/8085 (MIT + BSD; Aicom's firmware notice retained): the unit build_ssi263.py
takes from dist/accent-build (no add-on of its own since 0.8)."""
import os
import shutil
import sys
from build_common import read_manifest, copy_native_binding, rm

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST, _ = read_manifest(os.path.join(HERE, "accent"))
REPO = os.path.dirname(HERE)
DRIVER = os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
SA_ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
BUILD = os.path.join(HERE, "dist", "accent-build")


def main():
    if not os.path.isfile(DRIVER):
        sys.exit("driver not found: %s" % DRIVER)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_accent")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "accent", "synthDrivers", "accentmini.py"), sd)
    copy_native_binding(eng)
    os.makedirs(os.path.join(eng, "accent-sa"))
    for name in ("u2.BIN", "u3.BIN", "u4.BIN"):
        shutil.copy2(os.path.join(SA_ROMS, name), os.path.join(eng, "accent-sa", name))
    shutil.copy2(os.path.join(REPO, "firmware", "AICOM.txt"), eng)
    shutil.copy2(DRIVER, eng)
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)


if __name__ == "__main__":
    main()
