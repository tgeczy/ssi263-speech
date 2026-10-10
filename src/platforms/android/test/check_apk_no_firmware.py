"""An APK must carry no firmware but Aicom's (Tomi, 2026-09-30: store builds carry only Aicom's content; the Blazie
firmware and states, and GW Micro's Speak-Out firmware, never: their users import them, 0.7.5; nor Sweet Micro
Systems' Mockingboard firmware, treated as Blazie's: Tomi, 2026-10-09).

Allowed, by sha256 and nothing else -- exactly these files, the built-in voices (firmware/AICOM.txt): the Accent SA's
three ROMs, and the Accent-mini's driver (build_android.sh stages it when src/csrc/accentmini/am_voice.c is there):

    u2.BIN      8d6aa48880d1efd02f8dc759741c16b8902db33dcc2856d1f0db4776149992ad   (8085 program ROM, 64 KB)
    u3.BIN      248dd6fb6d08f6b4316ce38f83fd330e55dde80ead4b26a652ba228e6b46859a   (dictionary, part 1, 32 KB)
    u4.BIN      7c12a8cf56a573cd83c928d61cd49e90b3afc810279e8ee54852d7b90042d155   (dictionary, part 2, 32 KB)
    SPKEMS.DVC  6b99fea4cc5534ee1f693b141df9d734057f116d0a36972e3949d86db00cbe79   (the Accent-mini's DOS driver)

Refused, in any file, an archive's members included: a Braille Lite ROM image by content (F3 C3 xx xx FF
"COPYRIGHT"); a unit's state by content (the 786432 bytes every state has: it holds what the firmware wrote); the
Speak-Out's SPEAKOUT.HEX by its sha256, and any Intel HEX image by content; the Mockingboard's
mockingboard-tts-1.1.bin by its sha256 (and by name, .bin); firmware, state or ROM files by name
(.bns .tns .state .hex .bin .dvc .rom) unless the file is one of the three above; and anything in assets/aicom/ that
is not one of them.

    python check_apk_no_firmware.py <apk>...        prints what it looked at; exit 1 on any hit
    python check_apk_no_firmware.py --synthetic     an APK-like zip made here: the Aicom files where the app has them
                                                    (the three ROMs, and SPKEMS.DVC when the Accent-mini is in the
                                                    tree) and the licences build_android.sh stages (no APK build
                                                    needed); must pass
  the controls, each must FAIL (on an APK, or --synthetic in place of it):
    --control <BL2ENG.BNS> <apk>                    the firmware added under a bland name -- beside the Aicom ROMs
    --control <bl2_2003_warm.state> <apk>           ... a state under a bland name
    --control <SPEAKOUT.HEX> <apk>                  ... the Speak-Out's firmware under a bland name
    --control <mockingboard-tts-1.1.bin> <apk>      ... the Mockingboard's firmware under a bland name
    --control-aicom-flipped <apk>                   assets/aicom/u2.BIN with one byte changed: not an allowed file
    --control-gpl <apk>                             z180emu's GPL text added among the licences (0.6's APK had it)

And the licences (Tomi, 2026-09-30: all-MAME 0.7): the APK carries the project's and Casso's MIT licences, MAME's
BSD-3-Clause notices for the Z180, 8085, V40 and 8086 cores, Aicom's notice and the distribution notice, and -- with
the Mockingboard's sources in the tree -- its 6502's credits (Fake6502's PINNED.txt, EchoTalk's BSD-3-Clause), and
no z180emu or Unicorn engine and no GPL notice anywhere (tools/check_no_gpl.py's search, the native libraries
included).
"""
import hashlib
import io
import os
import re
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, REPO)
from tools import check_no_gpl  # noqa: E402
IMAGE = re.compile(rb"\xF3\xC3..\xFFCOPYRIGHT", re.S)
NAMES = re.compile(r"(?i)\.(bns|tns|state|hex|bin|dvc|rom)$")
INTEL_HEX = re.compile(rb"\A(?::[0-9A-Fa-f]{10,}\r?\n){4}")       # four Intel HEX records at the start
STATE_SIZE = 786432                         # bl_save_state's: battery-backed RAM + file flash
CLOCK_TAIL = 64                             # ... and the clock controller after them, when the emulator saved it
AICOM_DIR = "assets/aicom/"
ALLOWED = {
    "8d6aa48880d1efd02f8dc759741c16b8902db33dcc2856d1f0db4776149992ad": "u2.BIN",
    "248dd6fb6d08f6b4316ce38f83fd330e55dde80ead4b26a652ba228e6b46859a": "u3.BIN",
    "7c12a8cf56a573cd83c928d61cd49e90b3afc810279e8ee54852d7b90042d155": "u4.BIN",
    "6b99fea4cc5534ee1f693b141df9d734057f116d0a36972e3949d86db00cbe79": "SPKEMS.DVC",
}
# the licences the APK must carry: its name under assets/licenses/, and where build_android.sh takes it from
LICENSES = {"DISTRIBUTION.txt": os.path.join("src", "platforms", "android", "licenses", "DISTRIBUTION.txt"),
            "ssi263-speech-MIT.txt": "LICENSE",
            "Casso-MIT.txt": os.path.join("third_party", "casso", "LICENSE"),
            "MAME-Z180-core-BSD-3-Clause.txt": os.path.join("src", "csrc", "cpu", "mame_z180", "LICENSE-BSD-3-Clause.txt"),
            "MAME-8085-core-BSD-3-Clause.txt": os.path.join("src", "csrc", "cpu", "mame_i8085", "LICENSE-BSD-3-Clause.txt"),
            "MAME-NEC-V40-core-BSD-3-Clause.txt": os.path.join("src", "csrc", "cpu", "mame_nec", "LICENSE-BSD-3-Clause.txt"),
            "MAME-8086-core-BSD-3-Clause.txt": os.path.join("src", "csrc", "cpu", "mame_i86", "LICENSE-BSD-3-Clause.txt"),
            "Aicom-Accent-SA-notice.txt": os.path.join("firmware", "AICOM.txt")}
# the Mockingboard's 6502 (build_android.sh stages these when src/csrc/mockingboard/mb_voice.c is there)
MOCKINGBOARD = os.path.isfile(os.path.join(REPO, "src", "csrc", "mockingboard", "mb_voice.c"))
if MOCKINGBOARD:
    LICENSES.update({"Fake6502-6502-core.txt": os.path.join("src", "csrc", "cpu", "fake6502", "PINNED.txt"),
                     "EchoTalk-BSD-3-Clause.txt": os.path.join("src", "csrc", "cpu", "fake6502",
                                                               "LICENSE-EchoTalk-BSD-3-Clause.txt")})
REFUSED = {"1c6930c8c6aed0550bc267c14032f9195b450ed95de606f2fa9727e2b7eb1eb1": "the Speak-Out's SPEAKOUT.HEX",
           "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae": "the Mockingboard's "
           "mockingboard-tts-1.1.bin"}


def scan(label, data, hits, allowed, depth=0, name=""):
    digest = hashlib.sha256(data).hexdigest()
    if depth > 0 and digest in ALLOWED:
        allowed.append(ALLOWED[digest])
        return
    if depth == 1 and name.startswith(AICOM_DIR):
        hits.append("%s: in %s but not one of the Aicom ROMs (sha256 %s...)" % (label, AICOM_DIR, digest[:12]))
    if digest in REFUSED:
        hits.append("%s: %s" % (label, REFUSED[digest]))
    if name and NAMES.search(name):
        hits.append("%s: a firmware, state or ROM file by name" % label)
    if IMAGE.search(data):
        hits.append("%s: a Braille Lite ROM image" % label)
    if INTEL_HEX.match(data):
        hits.append("%s: an Intel HEX image" % label)
    if depth > 0 and (len(data) == STATE_SIZE or (len(data) == STATE_SIZE + CLOCK_TAIL
                                                  and data[STATE_SIZE:STATE_SIZE + 8] == b"BLCLOCK1")):
        hits.append("%s: a unit's state, by its size" % label)
    if depth < 2 and data[:4] == b"PK\x03\x04":
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for n in z.namelist():
                scan("%s!%s" % (label, n), z.read(n), hits, allowed, depth + 1, n)
    elif depth < 2 and data[:2] == b"\x1f\x8b":
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
            for m in t.getmembers():
                if m.isfile():
                    scan("%s!%s" % (label, m.name), t.extractfile(m).read(), hits, allowed, depth + 1, m.name)


def synthetic():
    """An APK-like zip: the Aicom ROMs where the app carries them, and the licences build_android.sh stages."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for n in ("u2.BIN", "u3.BIN", "u4.BIN"):
            z.writestr(AICOM_DIR + n, open(os.path.join(REPO, "firmware", "aicom-accent-sa", n), "rb").read())
        mini = os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
        if os.path.isfile(os.path.join(REPO, "src", "csrc", "accentmini", "am_voice.c")) and os.path.isfile(mini):
            z.writestr(AICOM_DIR + "SPKEMS.DVC", open(mini, "rb").read())
        for name, src in LICENSES.items():
            z.writestr("assets/licenses/" + name, open(os.path.join(REPO, src), "rb").read())
    return out.getvalue()


def licence_hits(label, data):
    """The licences missing from the APK, and every z180emu, GPL or Unicorn find (tools/check_no_gpl.py)."""
    names = set(zipfile.ZipFile(io.BytesIO(data)).namelist())
    hits = ["%s: no assets/licenses/%s" % (label, n) for n in LICENSES if "assets/licenses/" + n not in names]
    check_no_gpl.scan(label, data, hits, 0)
    return hits


def with_file(data, name, content, replace=False):
    """The APK's entries plus (or with `name` replaced by) a file."""
    out = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(data)) as src, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as dst:
        for n in src.namelist():
            if not (replace and n == name):
                dst.writestr(n, src.read(n))
        dst.writestr(name, content)
    return out.getvalue()


def main():
    args = sys.argv[1:]
    control = flipped = gpl = None
    if args[:1] == ["--control-gpl"]:        # z180emu's GPL text among the licences: this run must FAIL
        gpl, args = True, args[1:]
    elif args[:1] == ["--control"]:            # --control <a firmware or state file> <apk>: this run must FAIL
        control, args = args[1], args[2:]
    elif args[:1] == ["--control-aicom-flipped"]:
        flipped, args = True, args[1:]
    bad = 0
    for apk in args:
        hits, allowed = [], []
        if apk == "--synthetic":
            data, label = synthetic(), "synthetic.apk"
        else:
            data, label = open(apk, "rb").read(), apk.replace("\\", "/").split("/")[-1]
        if control:
            data = with_file(data, "assets/data/unit.dat", open(control, "rb").read())
        if flipped:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                u2 = bytearray(z.read(AICOM_DIR + "u2.BIN"))
            u2[0x100] ^= 0x01
            data = with_file(data, AICOM_DIR + "u2.BIN", bytes(u2), replace=True)
        if gpl:
            data = with_file(data, "assets/licenses/z180emu-GPL-2.0.txt",
                             open(os.path.join(REPO, "third_party", "z180emu", "COPYING"), "rb").read())
        scan(label, data, hits, allowed)
        lic = licence_hits(label, data)
        n = len(zipfile.ZipFile(io.BytesIO(data)).namelist())
        print("%s: %d entries, %s; Aicom ROMs allowed: %d%s" % (
            label, n, "no firmware" if not hits else "FIRMWARE: " + "; ".join(hits[:5]), len(allowed),
            "" if not allowed else " (%s)" % ", ".join(sorted(allowed))))
        print("%s: licences %s" % (label, "MIT (ours, Casso's), MAME's BSD-3-Clause (Z180, 8085, V40, 8086), Aicom's%s; "
                                   "no GPL or Unicorn" % (", Fake6502's and EchoTalk's" if MOCKINGBOARD else "")
                                   if not lic else "WRONG: " + "; ".join(lic[:5])))
        bad += bool(hits) or bool(lic)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
