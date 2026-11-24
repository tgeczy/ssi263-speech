"""The add-on's user firmware (nvda/shared/ssi263_userfw.py): the Super Voice's program fetched or imported by hand
into NVDA's configuration folder.  No network: the fetch is a stand-in, and the pinned sources are swapped for a disk
laid out here around the local SUPERTOK.BIN (tools/check_sv_voice.py make_disk) -- so the download, the mirror and
the checksum logic run as they are, and the real library (sv_firmware_from) judges what is kept.

    python nvda/tools/userdata_test.py         (after src/csrc/build_ssi263speech.py); skips without SUPERTOK.BIN

Cases:
  portable    the folder follows NVDA's configPath (a portable copy's own), read on every call
  download    the zip from the first source: only SUPERTOK.BIN kept, nothing else left in the folder
  same        the same program again: "same", nothing written
  pinned      a download that is not its pinned file is refused, and nothing is written
  mirror      the first source failing, the second's disk is used
  verified    an imported program with one byte changed is refused by the library, and nothing is written
Controls (SSI263_USERDATA_BREAK, each must fail exactly these): appdata -> portable, hash -> pinned,
nomirror -> mirror, noverify -> verified and download (the disk itself is then written as the program).

Everything is written under one temporary folder, %APPDATA% too: the appdata control names %APPDATA%\\nvda, and up to
0.9.5's draft that was the person's REAL folder -- the test wrote and deleted SUPERTOK.BIN there, and the suite took
the copy a person had downloaded (found by PR 91's author; Tomi's too).  A path outside the temporary folder now
stops the test before anything is written.

    python nvda/tools/userdata_test.py --sentinel     the appdata control run in a child with %APPDATA% holding a
                                                      sentinel SUPERTOK.BIN where a person's would be: it must survive
    USERDATA_APPDATA_BREAK=1   %APPDATA% left as it is, as it was: --sentinel must fail (the sentinel deleted)
"""
import hashlib
import io
import os
import shutil
import sys
import tempfile
import types
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "nvda", "shared"))
sys.path.insert(0, os.path.join(REPO, "tools"))
import ssi263_userfw as userfw  # noqa: E402
import ssi263speech  # noqa: E402
from check_sv_voice import make_disk  # noqa: E402

BIN = os.path.join(REPO, "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
DLL = os.path.join(REPO, "build", "win", "x64" if sys.maxsize > 2 ** 32 else "x86", "ssi263speech.dll")
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%-5s %-10s %s" % ("ok" if ok else "FAIL", name, detail))


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main():
    if not os.path.isfile(BIN):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        return 0
    prog = open(BIN, "rb").read()
    disk = make_disk(prog, [33, 34, 5, 60, 41])
    z = io.BytesIO()
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("Super Voice (Speech Systems)/SUPERV.DSK", disk)
    zipped = z.getvalue()
    # the pinned sources, swapped for this disk (the real ones' values are checked by hand: docs/super-voice.md)
    userfw.DISK_SHA256, userfw.DISK_SIZE = sha(disk), len(disk)
    userfw.SOURCES = (("first", "https://first.invalid/sv.zip", len(zipped), sha(zipped), "zip"),
                      ("second", "https://second.invalid/SUPERV.DSK", len(disk), sha(disk), "dsk"))
    extract = lambda d: ssi263speech.supervoice_firmware(DLL, d)    # noqa: E731
    tmp = tempfile.mkdtemp(prefix="ssi263-userdata-")
    real_appdata = os.environ.get("APPDATA")
    if not os.environ.get("USERDATA_APPDATA_BREAK"):
        os.environ["APPDATA"] = os.path.join(tmp, "appdata")       # the appdata control's %APPDATA%\nvda: ours
    try:
        conf = os.path.join(tmp, "portable", "userConfig")
        sys.modules["globalVars"] = types.SimpleNamespace(appArgs=types.SimpleNamespace(configPath=conf))
        want = os.path.join(conf, "ssi263-speech", "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
        got = userfw.supervoice_path()
        sys.modules["globalVars"].appArgs.configPath = conf2 = os.path.join(tmp, "other")
        moved = userfw.supervoice_path()
        sys.modules["globalVars"].appArgs.configPath = conf
        check("portable", got == want and moved.startswith(conf2), got)
        here = os.path.normcase(os.path.abspath(userfw.supervoice_path()))
        if not here.startswith(os.path.normcase(os.path.abspath(tmp)) + os.sep) and not os.environ.get(
                "USERDATA_APPDATA_BREAK"):
            print("FAIL  stopped: the Super Voice's path is outside this test's folder: %s" % here)
            return 1

        def serve(table):
            def fetch(url):
                v = table[url.split("/")[2]]
                if isinstance(v, Exception):
                    raise v
                return v
            return fetch

        def folder_files():
            d = os.path.dirname(userfw.supervoice_path())
            return sorted(os.listdir(d)) if os.path.isdir(d) else []

        data, where = userfw.download_disk(serve({"first.invalid": zipped, "second.invalid": disk}))
        state = userfw.install_supervoice(data, extract)
        kept = open(userfw.supervoice_path(), "rb").read() if userfw.has_supervoice() else b""
        check("download", where == "first" and state == "installed" and kept == prog
              and folder_files() == ["SUPERTOK.BIN"], "%s from %s; the folder: %s" % (state, where, folder_files()))
        check("same", userfw.install_supervoice(data, extract) == "same", "installed again: same")

        os.remove(userfw.supervoice_path())
        z2 = io.BytesIO()
        with zipfile.ZipFile(z2, "w") as zf:
            zf.writestr("Super Voice (Speech Systems)/SUPERV.DSK", disk)
            zf.writestr("README.TXT", b"someone else's zip")
        try:
            data, where = userfw.download_disk(serve({"first.invalid": z2.getvalue(), "second.invalid": b"junk"}))
            userfw.install_supervoice(data, extract)
            check("pinned", False, "taken from %s" % where)
        except userfw.FirmwareError as e:
            check("pinned", not userfw.has_supervoice() and "not the file it should be" in str(e), str(e)[:110])

        os.remove(userfw.supervoice_path()) if userfw.has_supervoice() else None
        try:
            data, where = userfw.download_disk(serve({"first.invalid": OSError("no route to host"),
                                                      "second.invalid": disk}))
            check("mirror", where == "second" and userfw.install_supervoice(data, extract) == "installed",
                  "from %s" % where)
        except userfw.FirmwareError as e:
            check("mirror", False, str(e)[:110])

        os.remove(userfw.supervoice_path()) if userfw.has_supervoice() else None
        changed = bytearray(prog)
        changed[4000] ^= 1
        try:
            userfw.install_supervoice(bytes(changed), extract)
            check("verified", False, "a changed program was written")
        except userfw.FirmwareError as e:
            check("verified", not userfw.has_supervoice() and "sha256" in str(e), str(e)[:110])
    finally:
        sys.modules.pop("globalVars", None)
        if real_appdata is None:
            os.environ.pop("APPDATA", None)
        else:
            os.environ["APPDATA"] = real_appdata
        shutil.rmtree(tmp, ignore_errors=True)
    print("user data: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


def sentinel():
    """The appdata control in a child, %APPDATA% a folder holding a sentinel where a person's SUPERTOK.BIN would be."""
    import subprocess
    if not os.path.isfile(BIN):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        return 0
    fake = tempfile.mkdtemp(prefix="ssi263-appdata-")
    try:
        keep = os.path.join(fake, "nvda", "ssi263-speech", "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
        os.makedirs(os.path.dirname(keep))
        with open(keep, "wb") as f:
            f.write(b"a person's own copy")
        env = dict(os.environ, APPDATA=fake, SSI263_USERDATA_BREAK="appdata")
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, universal_newlines=True)
        survived = os.path.isfile(keep) and open(keep, "rb").read() == b"a person's own copy"
        print("%-5s %-10s %s" % ("ok" if survived else "FAIL", "sentinel",
                                 "the appdata control (exit %d) left %%APPDATA%%'s SUPERTOK.BIN %s" % (
                                     r.returncode, "as it was" if survived else "DELETED or changed")))
        print("user data sentinel: %s" % ("all passed" if survived else "1 FAILED"))
        return 0 if survived else 1
    finally:
        shutil.rmtree(fake, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(sentinel() if sys.argv[1:] == ["--sentinel"] else main())
