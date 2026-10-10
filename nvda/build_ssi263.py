"""Build the 0.8 add-on, ssi263-speech-<version>.nvda-addon: every unit in one driver
(nvda/ssi263/synthDrivers/ssi263.py).

The units are the 0.7 add-ons' own: build_blazie.py, build_speakout.py and build_accent.py are run as they are, and
their staged synthDrivers (each driver and its engine package) are moved whole into this add-on's private package,
synthDrivers/_ssi263_unified -- so each unit here is that add-on's, file for file.  The Mockingboard's driver
(nvda/mockingboard) is staged beside them with its engine: the shared binding, the library, its firmware file when
the local firmware folder has it (firmware/sweet-micro-mockingboard, never in the repository; without it the voice is
not staged) and its notices.

    python nvda/build_ssi263.py                # a release: every unit built fresh, all six voices required
    python nvda/build_ssi263.py --dev [--reuse] # development: a missing Mockingboard file allowed; --reuse takes
                                                # dist/<name>-build as built instead of running the builders

A release build ends by reading the finished archive back (Astra, Reply 164): every unit's driver, the shared
binding, this driver and the global plugin byte for byte against their sources; every copy of ssi263speech.dll
against the one build/win/<arch> made; every voice's firmware against the repository's firmware folder (the
Mockingboard's against its known sha256); the notices present; no disk image.  Any difference stops it.

Runtime code is MIT + BSD; the firmware has its own terms (the manifest, licenses/).
"""
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

from build_common import copy_native_binding, read_manifest, rm, zip_build

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MANIFEST, VERSION = read_manifest(os.path.join(HERE, "ssi263"))
BUILD = os.path.join(HERE, "dist", "ssi263-build")
OUT = os.path.join(HERE, "dist", "ssi263-speech-%s.nvda-addon" % VERSION)
UNITS = ("blazie", "speakout", "accent")
MB_FILE = os.path.join(REPO, "firmware", "sweet-micro-mockingboard", "mockingboard-tts-1.1.bin")
# Apple II disk images are never shipped (Tomi, 2026-10-09): the toolkit's disk also carries Apple's DOS 3.3 and
# Sweet Micro's other programs; only the voice's own file is staged
DISK_IMAGES = (".dsk", ".do", ".po", ".nib", ".woz", ".2mg")
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


U = "synthDrivers/_ssi263_unified/"
MB_SHA256 = "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae"


def expected_files():
    """archive path -> its source file (byte for byte), or a sha256 (the Mockingboard's firmware)"""
    fw = os.path.join(REPO, "firmware")
    out = {
        "synthDrivers/ssi263.py": os.path.join(HERE, "ssi263", "synthDrivers", "ssi263.py"),
        U + "blazie.py": os.path.join(HERE, "blazie", "synthDrivers", "blazie.py"),
        U + "speakout.py": os.path.join(HERE, "speakout", "synthDrivers", "speakout.py"),
        U + "accentmini.py": os.path.join(HERE, "accent", "synthDrivers", "accentmini.py"),
        U + "mockingboard.py": os.path.join(HERE, "mockingboard", "synthDrivers", "mockingboard.py"),
        U + "_ssi263_blazie/BL2ENG.BNS": os.path.join(fw, "blazie", "BL2ENG.BNS"),
        U + "_ssi263_blazie/bl2_2003_warm.state": os.path.join(fw, "blazie", "bl2_2003_warm.state"),
        U + "_ssi263_blazie/BL2SPA.BNS": os.path.join(fw, "blazie", "spanish", "BL2SPA.BNS"),
        U + "_ssi263_blazie/bl2spa_fresh.state": os.path.join(fw, "blazie", "spanish", "bl2spa_fresh.state"),
        U + "_ssi263_speakout/SPEAKOUT.HEX": os.path.join(fw, "gw-micro-speakout", "SPEAKOUT.HEX"),
        U + "_ssi263_accent/SPKEMS.DVC": os.path.join(fw, "aicom-accent-mini", "SPKEMS.DVC"),
        U + "_ssi263_accent/AICOM.txt": os.path.join(fw, "AICOM.txt"),
        U + "_ssi263_mockingboard/mockingboard-tts-1.1.bin": MB_SHA256,
        U + "_ssi263_mockingboard/licenses/Mockingboard-firmware-notice.txt": None,
        U + "_ssi263_mockingboard/licenses/Fake6502-provenance.txt":
            os.path.join(REPO, "src", "csrc", "cpu", "fake6502", "PINNED.txt"),
        U + "_ssi263_mockingboard/licenses/LICENSE-EchoTalk-BSD-3-Clause.txt":
            os.path.join(REPO, "src", "csrc", "cpu", "fake6502", "LICENSE-EchoTalk-BSD-3-Clause.txt"),
    }
    for n in ("u2.BIN", "u3.BIN", "u4.BIN"):
        out[U + "_ssi263_accent/accent-sa/" + n] = os.path.join(fw, "aicom-accent-sa", n)
    for unit in ("speakout", "accent", "mockingboard"):
        for n in ("ssi263speech.py", "ssi263_rates.py"):
            out[U + "_ssi263_%s/%s" % (unit, n)] = os.path.join(HERE, "shared", n)
    for unit in ("blazie", "speakout", "accent", "mockingboard"):
        for arch in ("x64", "x86"):
            out[U + "_ssi263_%s/bin/%s/ssi263speech.dll" % (unit, arch)] = os.path.join(
                REPO, "build", "win", arch, "ssi263speech.dll")
    plugin = os.path.join(HERE, "ssi263", "globalPlugins", "ssi263Speech")
    for n in os.listdir(plugin):
        if n.endswith(".py"):
            out["globalPlugins/ssi263Speech/" + n] = os.path.join(plugin, n)
    return out


def verify_archive(path):
    """the finished archive against its sources; the list of problems (empty: it is what was built)"""
    problems = []
    with zipfile.ZipFile(path) as z:
        names = set(z.namelist())
        for arc, want in expected_files().items():
            if arc not in names:
                problems.append("missing: %s" % arc)
                continue
            data = z.read(arc)
            if want is None:
                continue
            if os.path.isabs(want) or os.sep in want:
                with open(want, "rb") as f:
                    ok = f.read() == data
            else:
                ok = hashlib.sha256(data).hexdigest() == want
            if not ok:
                problems.append("differs from its source: %s" % arc)
        problems += ["a disk image: %s" % n for n in names if n.lower().endswith(DISK_IMAGES)]
    return problems


def main():
    dev = "--dev" in sys.argv[1:]
    reuse = dev and "--reuse" in sys.argv[1:]
    if "--reuse" in sys.argv[1:] and not dev:
        sys.exit("--reuse is for development builds only (--dev --reuse): a release builds every unit fresh")
    if not dev and not os.path.isfile(MB_FILE):
        sys.exit("a release needs the Mockingboard's firmware file: %s (or --dev to build without it)"
                 % os.path.relpath(MB_FILE, REPO))
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
    shutil.copytree(os.path.join(HERE, "ssi263", "globalPlugins"), os.path.join(BUILD, "globalPlugins"),
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(os.path.join(HERE, "blazie", "locale"), os.path.join(BUILD, "locale"))
    with open(os.path.join(BUILD, "manifest.ini"), "w", encoding="utf-8") as f:
        f.write(MANIFEST)
    disks = [os.path.join(r, f) for r, _d, fs in os.walk(BUILD) for f in fs if f.lower().endswith(DISK_IMAGES)]
    if disks:
        sys.exit("disk images must never ship: %s" % ", ".join(os.path.relpath(d, BUILD) for d in disks))
    zip_build(BUILD, OUT)
    if not dev:
        problems = verify_archive(OUT)
        if problems:
            os.remove(OUT)
            sys.exit("the archive is not what was built, removed:\n  " + "\n  ".join(problems))
        print("checked: %s holds the six voices' files, each as its source" % os.path.basename(OUT))


if __name__ == "__main__":
    main()
