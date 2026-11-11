"""The Apple apps carry no firmware at all -- checked in what is uploaded, before it leaves the Mac.

The store apps ship hollow (Tomi): not Blazie's, GW Micro's or Sweet Micro's firmware, and -- unlike the Android APK,
which carries Aicom's -- not Aicom's either, nor any Apple II disk image (the Mockingboard toolkit disk also carries
Apple's DOS 3.3).  Every file in an .xcarchive, an .app or an exported .ipa is judged by Android's
check_apk_no_firmware.scan -- a Braille Lite ROM image by its signature, an Intel HEX image, a unit's state and an
Apple II disk image by size and content, the Speak-Out's, the Sounding Board's two drivers (SBLOAD.COM, SB.COM: its
REFUSED) and the Mockingboards' files by sha256, firmware, state, ROM and disk names -- and, here, Aicom's four files
by sha256 as well, under any name.

    python src/platforms/apple/test/check_archive_no_firmware.py <.xcarchive | .app | .ipa> ...
    python ... --control <file> <archive>     the file added to a copy of the archive under a bland name: must FAIL
    python ... --synthetic                    a made-up app folder (run_tests's way): it must pass, and each control
                                              (a Braille Lite image, a state, a disk image, an Aicom ROM, an Intel
                                              HEX) put into it must fail

Exit 0 when nothing was found; 1 with every finding listed.
"""
import argparse
import hashlib
import os
import shutil
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "src", "platforms", "android", "test"))
import check_apk_no_firmware as apk  # noqa: E402

# Aicom's files: allowed into the APK (apk.ALLOWED), never into the Apple apps
AICOM = dict(apk.ALLOWED)
# the SmarTalk's S_TALK.EXE: the GitHub APK carries it (apk.SMARTALK, in assets/smartalk only), the Apple apps never
# (Tomi, 2026-10-24: import only there), wherever it sits
SMARTALK = dict(apk.SMARTALK)


def judge(label, data, name):
    """Findings for one file of the archive."""
    hits, allowed = [], []
    apk.scan(label, data, hits, allowed, depth=1, name=name)
    digest = hashlib.sha256(data).hexdigest()
    if digest in AICOM:
        hits.append("%s: Aicom's %s, by its sha256 (the Apple apps carry no firmware)" % (label, AICOM[digest]))
    if digest in SMARTALK:                 # (the APK's rule, "outside assets/smartalk/", is no advice for an Apple app)
        hits = [h for h in hits if not h.endswith(" outside " + apk.SMARTALK_DIR)]
        hits.append("%s: the SmarTalk's %s, by its sha256 (the Apple apps carry no firmware)" % (label,
                                                                                               SMARTALK[digest]))
    hits += ["%s: Aicom's %s inside it (the Apple apps carry no firmware)" % (label, a) for a in allowed
             if a not in [AICOM.get(digest)]]
    return hits


def check(path):
    """Every finding in an .xcarchive or .app folder, or an .ipa / .zip."""
    hits, files = [], 0
    if os.path.isdir(path):
        for root, _, names in os.walk(path):
            for n in names:
                full = os.path.join(root, n)
                if os.path.islink(full):
                    continue
                rel = os.path.relpath(full, path)
                with open(full, "rb") as f:
                    hits += judge(rel, f.read(), rel)
                files += 1
    else:
        with zipfile.ZipFile(path) as z:
            for n in z.namelist():
                if n.endswith("/"):
                    continue
                hits += judge(n, z.read(n), n)
                files += 1
    return hits, files


def report(path, expect_clean=True):
    hits, files = check(path)
    for h in hits:
        print("FOUND %s" % h)
    ok = not hits if expect_clean else bool(hits)
    print("%-5s %s: %d files, %s" % ("ok" if ok else "FAIL", os.path.basename(path.rstrip("/")), files,
                                    "no firmware" if not hits else "%d finding(s)" % len(hits)))
    return ok


def with_control(path, content, tmp):
    """A copy of the archive with `content` added under a bland name; its path."""
    if os.path.isdir(path):
        copy = os.path.join(tmp, os.path.basename(path.rstrip("/")))
        shutil.copytree(path, copy, symlinks=True)
        with open(os.path.join(copy, "data.dat"), "wb") as f:
            f.write(content)
        return copy
    copy = os.path.join(tmp, os.path.basename(path))
    shutil.copyfile(path, copy)
    with zipfile.ZipFile(copy, "a") as z:
        z.writestr("Payload/data.dat", content)
    return copy


def synthetic(tmp):
    """A made-up app folder must pass; each kind of firmware put into it must fail."""
    app = os.path.join(tmp, "Synthetic.app")
    os.makedirs(os.path.join(app, "Contents", "Resources", "licenses"))
    with open(os.path.join(app, "Contents", "Info.plist"), "wb") as f:
        f.write(b"<plist><dict/></plist>")
    with open(os.path.join(app, "Contents", "Resources", "licenses", "1 MIT.txt"), "wb") as f:
        f.write(b"MIT License")
    ok = report(app)
    controls = [
        ("a Braille Lite ROM image", b"\x18" * 0x3000 + b"\xF3\xC3\x32\x03\xFFCOPYRIGHT" + bytes(1000)),
        ("an Apple II disk image", bytes(143360)),
        ("an Intel HEX image", b":020000020000FC\r\n" * 4),
    ] + apk.state_samples()             # a unit's state, at every size it has, alone and with each clock's tail
    aicom = os.path.join(REPO, "firmware", "aicom-accent-sa", "u2.BIN")
    if os.path.isfile(aicom):
        controls.append(("Aicom's u2.BIN", open(aicom, "rb").read()))
    if os.path.isfile(apk.SMARTALK_EXE):
        controls.append(("the SmarTalk's S_TALK.EXE", open(apk.SMARTALK_EXE, "rb").read()))
    for label, content in controls:
        d = tempfile.mkdtemp(dir=tmp)
        print("control: %s added" % label)
        ok = report(with_control(app, content, d), expect_clean=False) and ok
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--control", metavar="FILE")
    ap.add_argument("--synthetic", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ssi263-apple-audit-")
    try:
        if a.synthetic:
            ok = synthetic(tmp)
        elif a.control:
            content = open(a.control, "rb").read()
            ok = all(report(with_control(p, content, tmp), expect_clean=False) for p in a.paths)
        else:
            ok = all([report(p) for p in a.paths]) and bool(a.paths)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("the Apple apps carry no firmware" if ok and not a.control and not a.synthetic
          else ("the controls are caught" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
