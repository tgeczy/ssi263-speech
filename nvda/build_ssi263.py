"""Build the 0.8 add-on, ssi263-speech-<version>.nvda-addon: every unit in one driver
(nvda/ssi263/synthDrivers/ssi263.py).

The units are the 0.7 add-ons' own: build_blazie.py, build_speakout.py and build_accent.py are run as they are, and
their staged synthDrivers (each driver and its engine package) are moved whole into this add-on's private package,
synthDrivers/_ssi263_unified -- so each unit here is that add-on's, file for file.  The Mockingboard's driver
(nvda/mockingboard) is staged beside them with its engine: the shared binding, the library, its firmware file when
the local firmware folder has it (firmware/sweet-micro-mockingboard, never in the repository; without it the voice is
not staged) and its notices.  So is the Artic SynPhonix's (nvda/synphonix), with VEST.COM when firmware/artic-synphonix
has it -- only the copy whose serial is replaced by 99999999 (SP_SHA256: the archive check refuses any other).  So is
the SmarTalk's (nvda/smartalk), with Automated Functions' S_TALK.EXE when
firmware/automated-functions-smartalk has it -- optional in a release too: whether a release carries it is decided for
the release, never by default here.  GW Micro's Sounding Board drivers (SBLOAD.COM, SB.COM 2.7) go with the Speak-Out's unit,
as its SPEAKOUT.HEX does (Tomi, 2026-10-18: bundled on the desktop, imported on the phones).  Speech Systems' Super
Voice's (nvda/supervoice) is staged always and never with its program: each user downloads or imports SUPERTOK.BIN
into NVDA's configuration folder (nvda/shared/ssi263_userfw.py), and the archive check refuses it by name and sha256.

    python nvda/build_ssi263.py                # a release: every unit built fresh, every voice required
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

from build_common import BLAZIE_UNITS, copy_native_binding, read_manifest, rm, zip_build

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MANIFEST, VERSION = read_manifest(os.path.join(HERE, "ssi263"))
BUILD = os.path.join(HERE, "dist", "ssi263-build")
OUT = os.path.join(HERE, "dist", "ssi263-speech-%s.nvda-addon" % VERSION)
UNITS = ("blazie", "speakout", "accent")
MB_FILE = os.path.join(REPO, "firmware", "sweet-micro-mockingboard", "mockingboard-tts-1.1.bin")
MB_FILE_EARLY = os.path.join(REPO, "firmware", "sweet-micro-mockingboard", "mockingboard-tts-early.bin")
SB_FILES = [os.path.join(REPO, "firmware", "gw-micro-sounding-board", n) for n in ("SBLOAD.COM", "SB.COM")]
ST_FILE = os.path.join(REPO, "firmware", "automated-functions-smartalk", "S_TALK.EXE")
SP_FILE = os.path.join(REPO, "firmware", "artic-synphonix", "VEST.COM")
# Apple II disk images are never shipped (Tomi, 2026-10-09): the toolkit's disk also carries Apple's DOS 3.3 and
# Sweet Micro's other programs; only the voice's own file is staged
DISK_IMAGES = (".dsk", ".do", ".po", ".nib", ".woz", ".2mg")
MB_NOTICE = """The Mockingboard's text-to-speech -- notice

This add-on carries Sweet Micro Systems' text-to-speech for the Mockingboard: mockingboard-tts-1.1.bin (TEXT TO SPEECH
and INFLECTION version 1.1, 11 March 1985, the IIe TTS driver and the MKB:RULE files, from the Mockingboard Developers
Toolkit disk) and mockingboard-tts-early.bin (the earlier TEXT TO SPEECH and its MKB:RULE files, from Mockingboard disk
1). They are not ours; they are here so the card can speak again, and they will be removed if their rights holders ask.
They are not covered by this add-on's MIT license.
"""


ST_NOTICE = """The SmarTalk's text-to-speech -- notice

This add-on carries Automated Functions' text-to-speech for the SmarTalk: S_TALK.EXE, version 1.21 (15 November 1995),
from the SmarTalk V1.21 disk (Automated Functions, Inc., Arlington, Virginia). The disk's README.TXT says: "Please feel
free to share the new version of the SmarTalk software with anyone who needs it." It is not ours; it is here so the
box can speak again, and it will be removed if its rights holders ask. It is not covered by this add-on's MIT license.
"""


SP_NOTICE = """The Artic SynPhonix's text-to-speech -- notice

This add-on carries Artic Technologies' text-to-speech for the SynPhonix cards: VEST.COM, version 1.60 English, the
"Special Release For Artic Visions" (copyright 1984-1992) from a Business Vision 3.08 disk. Each copy of VEST was sold
with one card and held that card's serial number; in this copy it is replaced by 99999999 (fourteen bytes: the
serial check's encoded copy and the banner's), so it names no one's card, and it speaks as the original does. It is not
ours; Artic Technologies appears to have closed long ago, and we have written to its founder. It is here so the card
can speak again, and it will be removed if its rights holders ask. It is not covered by this add-on's MIT license.
"""


def stage_synphonix(unified):
    """the SynPhonix's driver and engine, with VEST.COM and its notice; False (nothing staged) without it"""
    if not os.path.isfile(SP_FILE):
        print("note: no %s: the SynPhonix is not staged" % os.path.relpath(SP_FILE, REPO))
        return False
    shutil.copy2(os.path.join(HERE, "synphonix", "synthDrivers", "synphonix.py"), unified)
    eng = os.path.join(unified, "_ssi263_synphonix")
    os.makedirs(eng)
    copy_native_binding(eng)
    shutil.copy2(SP_FILE, eng)
    lic = os.path.join(eng, "licenses")
    os.makedirs(lic, exist_ok=True)
    with open(os.path.join(lic, "SynPhonix-software-notice.txt"), "w", encoding="utf-8") as f:
        f.write(SP_NOTICE)
    return True


def stage_supervoice(unified):
    """the Super Voice's driver and engine, always, with NO firmware: no package carries its program (SV_SHA256, the
    archive check refuses it); the user downloads or imports it into NVDA's configuration folder (ssi263_userfw.py)"""
    shutil.copy2(os.path.join(HERE, "supervoice", "synthDrivers", "supervoice.py"), unified)
    eng = os.path.join(unified, "_ssi263_supervoice")
    os.makedirs(eng)
    copy_native_binding(eng)
    shutil.copy2(os.path.join(HERE, "shared", "ssi263_userfw.py"), eng)


def stage_smartalk(unified):
    """the SmarTalk's driver and engine, with S_TALK.EXE and its notice; False (nothing staged) without it"""
    if not os.path.isfile(ST_FILE):
        print("note: no %s: the SmarTalk is not staged" % os.path.relpath(ST_FILE, REPO))
        return False
    shutil.copy2(os.path.join(HERE, "smartalk", "synthDrivers", "smartalk.py"), unified)
    eng = os.path.join(unified, "_ssi263_smartalk")
    os.makedirs(eng)
    copy_native_binding(eng)
    shutil.copy2(ST_FILE, eng)
    lic = os.path.join(eng, "licenses")
    os.makedirs(lic, exist_ok=True)
    with open(os.path.join(lic, "SmarTalk-software-notice.txt"), "w", encoding="utf-8") as f:
        f.write(ST_NOTICE)
    return True


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
    if os.path.isfile(MB_FILE) or os.path.isfile(MB_FILE_EARLY):
        for f in (MB_FILE, MB_FILE_EARLY):
            if os.path.isfile(f):
                shutil.copy2(f, eng)
            else:
                print("note: no %s: that Mockingboard voice is not staged" % os.path.relpath(f, REPO))
        with open(os.path.join(lic, "Mockingboard-firmware-notice.txt"), "w", encoding="utf-8") as f:
            f.write(MB_NOTICE)
        return True
    print("note: no %s: the Mockingboard is not staged" % os.path.relpath(MB_FILE, REPO))
    return False


U = "synthDrivers/_ssi263_unified/"
ST_SHA256 = "6cef7bc467959960505bfd771d07f387b7bdfad25bad2b51801ae56bb8d78e6c"
# VEST.COM with its card's serial replaced by 99999999 (SP_NOTICE): the only copy an add-on may carry -- one built with
# anyone's own copy fails its archive check
SP_SHA256 = "4d74b00d8541bbe1c8ca4ec21c8bb3632f054c28d6fead97ce452679db0cdb49"
SV_SHA256 = "4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c"   # never in the archive
MB_SHA256 = "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae"
MB_SHA256_EARLY = "c7c049b1b61792719e21e461a2a8c25fc32c12882c81305814a3dc67af6e5835"


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
        U + "_ssi263_speakout/SPEAKOUT.HEX": os.path.join(fw, "gw-micro-speakout", "SPEAKOUT.HEX"),
        U + "_ssi263_speakout/SBLOAD.COM": os.path.join(fw, "gw-micro-sounding-board", "SBLOAD.COM"),
        U + "_ssi263_speakout/SB.COM": os.path.join(fw, "gw-micro-sounding-board", "SB.COM"),
        U + "_ssi263_accent/SPKEMS.DVC": os.path.join(fw, "aicom-accent-mini", "SPKEMS.DVC"),
        U + "_ssi263_accent/AICOM.txt": os.path.join(fw, "AICOM.txt"),
        U + "_ssi263_mockingboard/mockingboard-tts-1.1.bin": MB_SHA256,
        U + "_ssi263_mockingboard/mockingboard-tts-early.bin": MB_SHA256_EARLY,
        U + "_ssi263_mockingboard/licenses/Mockingboard-firmware-notice.txt": None,
        U + "_ssi263_mockingboard/licenses/Fake6502-provenance.txt":
            os.path.join(REPO, "src", "csrc", "cpu", "fake6502", "PINNED.txt"),
        U + "_ssi263_mockingboard/licenses/LICENSE-EchoTalk-BSD-3-Clause.txt":
            os.path.join(REPO, "src", "csrc", "cpu", "fake6502", "LICENSE-EchoTalk-BSD-3-Clause.txt"),
    }
    for n in ("u2.BIN", "u3.BIN", "u4.BIN"):
        out[U + "_ssi263_accent/accent-sa/" + n] = os.path.join(fw, "aicom-accent-sa", n)
    units = ["speakout", "accent", "mockingboard"]
    if os.path.isfile(ST_FILE):              # the SmarTalk, staged when its S_TALK.EXE is here (stage_smartalk)
        units.append("smartalk")
        out[U + "smartalk.py"] = os.path.join(HERE, "smartalk", "synthDrivers", "smartalk.py")
        out[U + "_ssi263_smartalk/S_TALK.EXE"] = ST_SHA256
        out[U + "_ssi263_smartalk/licenses/SmarTalk-software-notice.txt"] = None
    if os.path.isfile(SP_FILE):              # the SynPhonix, staged when its VEST.COM is here (stage_synphonix)
        units.append("synphonix")
        out[U + "synphonix.py"] = os.path.join(HERE, "synphonix", "synthDrivers", "synphonix.py")
        out[U + "_ssi263_synphonix/VEST.COM"] = SP_SHA256
        out[U + "_ssi263_synphonix/licenses/SynPhonix-software-notice.txt"] = None
    units.append("supervoice")               # always, its program never (stage_supervoice)
    out[U + "supervoice.py"] = os.path.join(HERE, "supervoice", "synthDrivers", "supervoice.py")
    out[U + "_ssi263_supervoice/ssi263_userfw.py"] = os.path.join(HERE, "shared", "ssi263_userfw.py")
    for unit in units:
        for n in ("ssi263speech.py", "ssi263_rates.py", "ssi263_flow.py", "ssi263_feed.py"):
            out[U + "_ssi263_%s/%s" % (unit, n)] = os.path.join(HERE, "shared", n)
    for unit in ["blazie"] + units:
        for arch in ("x64", "x86"):
            out[U + "_ssi263_%s/bin/%s/ssi263speech.dll" % (unit, arch)] = os.path.join(
                REPO, "build", "win", arch, "ssi263speech.dll")
    for name, src in BLAZIE_UNITS:           # Spanish, German, Italian, Slovak: required in a release
        out[U + "_ssi263_blazie/" + name] = os.path.join(fw, "blazie", *src.split("/"))
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
        # the Super Voice's program: each user downloads or imports it (voices.h SSV_USER_DATA), by name and content
        problems += ["the Super Voice's program, which no package carries: %s" % n for n in names
                     if os.path.basename(n).upper() == "SUPERTOK.BIN"
                     or hashlib.sha256(z.read(n)).hexdigest() == SV_SHA256]
    return problems


def main():
    dev = "--dev" in sys.argv[1:]
    reuse = dev and "--reuse" in sys.argv[1:]
    if "--reuse" in sys.argv[1:] and not dev:
        sys.exit("--reuse is for development builds only (--dev --reuse): a release builds every unit fresh")
    for f in (MB_FILE, MB_FILE_EARLY):
        if not dev and not os.path.isfile(f):
            sys.exit("a release needs the Mockingboard's firmware file: %s (or --dev to build without it)"
                     % os.path.relpath(f, REPO))
    for f in SB_FILES:
        if not dev and not os.path.isfile(f):
            sys.exit("a release needs the Sounding Board's driver: %s (or --dev to build without it)"
                     % os.path.relpath(f, REPO))
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
    stage_smartalk(unified)
    stage_synphonix(unified)
    stage_supervoice(unified)
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
        print("checked: %s holds every voice's files, each as its source" % os.path.basename(OUT))


if __name__ == "__main__":
    main()
