"""Build Blazie on MAME's Z180: the unit build_ssi263.py takes from dist/blazie-build (no add-on of its own since 0.8).
Runtime code is MIT + BSD; firmware has its own terms."""
import os
import shutil
import sys
from build_common import BLAZIE_UNITS, read_manifest, check_native, copy_engine, copy_native_voices, build_board, rm

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST, _ = read_manifest(os.path.join(HERE, "blazie"))
REPO = os.path.dirname(HERE)
ENGINE = os.path.join(REPO, "src")
FIRMWARE = os.path.join(REPO, "firmware", "blazie", "BL2ENG.BNS")
STATE = os.path.join(REPO, "firmware", "blazie", "bl2_2003_warm.state")
BUILD = os.path.join(HERE, "dist", "blazie-build")


def main():
    for p in (FIRMWARE, STATE):
        if not os.path.isfile(p):
            sys.exit("missing: %s" % p)
    build_board("blazie")
    if os.path.isdir(BUILD):
        rm(BUILD)
    sd = os.path.join(BUILD, "synthDrivers")
    eng = os.path.join(sd, "_ssi263_blazie")
    os.makedirs(eng)
    shutil.copy2(os.path.join(HERE, "blazie", "synthDrivers", "blazie.py"), sd)
    copy_engine(ENGINE, eng)
    for src, dst in (("blazie.py", "blazie_host.py"), ("native_blazie.py", "native_blazie.py"),
                     ("blazie_idle.py", "blazie_idle.py")):
        shutil.copy2(os.path.join(ENGINE, "hosts", src), os.path.join(eng, dst))
    native = os.path.join(HERE, "dist", "blazie-lib")
    copy_native_voices(eng)
    exe = os.path.join(native, "bl_live_mame.exe")
    check_native(exe, "x86")
    shutil.copy2(exe, os.path.join(eng, "bns_live.exe"))
    for name in ("ssi263_numwords.py", "ssi263_rates.py"):
        shutil.copy2(os.path.join(HERE, "shared", name), eng)
    shutil.copy2(FIRMWARE, eng)
    shutil.copy2(STATE, eng)
    for name, src in BLAZIE_UNITS:           # the other languages' units, each when its files are here
        src = os.path.join(REPO, "firmware", "blazie", *src.split("/"))
        if os.path.isfile(src):
            os.makedirs(os.path.dirname(os.path.join(eng, name)), exist_ok=True)
            shutil.copy2(src, os.path.join(eng, name))
    shutil.copytree(os.path.join(HERE, "blazie", "locale"), os.path.join(BUILD, "locale"))
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)


if __name__ == "__main__":
    main()
