"""Build Speak-Out with MAME's V40 and instruction-count compatibility timing: the unit build_ssi263.py takes from
dist/speakout-build (no add-on of its own since 0.8).
Runtime code is MIT + BSD; firmware remains private and separately licensed.
"""
import os
import shutil
import sys
from build_common import read_manifest, copy_native_binding, rm

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST, _ = read_manifest(os.path.join(HERE, "speakout"))
REPO = os.path.dirname(HERE)
FIRMWARE = os.path.join(REPO, "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
SB_DIR = os.path.join(REPO, "firmware", "gw-micro-sounding-board")
SOUNDINGBOARD = ("SBLOAD.COM", "SB.COM")
BUILD = os.path.join(HERE, "dist", "speakout-build")


def main():
    if not os.path.isfile(FIRMWARE):
        sys.exit("firmware not found: %s" % FIRMWARE)
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_speakout")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "speakout", "synthDrivers", "speakout.py"), sd)
    copy_native_binding(eng)
    shutil.copy2(FIRMWARE, eng)
    # GW Micro's Sounding Board (0.8.5): its two DOS drivers, when the local firmware folder has them -- the driver's
    # second voice; a release of the unified add-on requires them (build_ssi263.py), as it carries SPEAKOUT.HEX
    for name in SOUNDINGBOARD:
        if os.path.isfile(os.path.join(SB_DIR, name)):
            shutil.copy2(os.path.join(SB_DIR, name), eng)
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)


if __name__ == "__main__":
    main()
