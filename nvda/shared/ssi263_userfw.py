# -*- coding: utf-8 -*-
"""Firmware the user brings, for a voice no package carries (src/csrc/voices.h SSV_USER_DATA): Speech Systems' Super
Voice, whose disk the Color Computer Archive keeps.  It lives in NVDA's own configuration folder,

    <NVDA's configuration folder>\\ssi263-speech\\firmware\\speech-systems-super-voice\\SUPERTOK.BIN

(%APPDATA%\\nvda\\ssi263-speech for an installed NVDA; Tomi, 2026-11-23), never in the add-on's folder, which every
update replaces.  The folder is NVDA's configPath, read on every call, never %APPDATA% by name: a portable copy keeps
its own configuration, and NVDA copies that folder to the sign-in screen's (outSPOKEN's lesson: its extractor wrote to
%APPDATA%\\nvda while its driver read configPath, so a portable copy never found what it had installed).  Below it,
the repository's firmware\\ layout: the voice table's own relative path, as the SAPI engine reads it too.

The download is by hand only (a button; never at start-up), from two places that keep the same disk, each checked
against its pinned size and sha256 before anything is read from it; it fails closed.  Whatever the source -- the
download, or a file the user picks (the program itself, the disk image, a zip holding either) -- only SUPERTOK.BIN is
ever written, and only once the library has taken it off the disk and found it the known program by its sha256
(sv_dsk.h sv_firmware_from, through `extract`).  It is written to a temporary file and moved into place.

No wx and no NVDA here (globalVars is optional): the dialog (globalPlugins/ssi263Speech/data.py) runs these on a worker
thread, and the tests (nvda/tools/userdata_test.py) run them as they are.  Python 3.7 to 3.13.

The tests' controls (SSI263_USERDATA_BREAK; never set in NVDA): `appdata` names %APPDATA%\\nvda instead of NVDA's
configPath; `hash` takes a download whatever its sha256; `nomirror` stops at the first source that fails; `noverify`
writes the user's file as it is, without the library's check.
"""
import hashlib
import io
import os
import urllib.request
import zipfile

DIRNAME = "ssi263-speech"
SUPERVOICE = ("speech-systems-super-voice", "SUPERTOK.BIN")    # under firmware\, as voices.c's files[0]
SUPERVOICE_SHA256 = "4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c"   # sv_host.h SV_SHA256
DISK_SHA256 = "7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784"         # SUPERV.DSK (sv_dsk.h)
DISK_SIZE = 161280
# (where, url, size, sha256, kind): the same disk, byte for byte, in both (docs/super-voice.md, checked 2026-11-23)
SOURCES = (
    ("the Color Computer Archive",
     "https://colorcomputerarchive.com/repo/Disks/Applications/Super%20Voice%20%28Speech%20Systems%29.zip",
     28456, "46a37e083a002dfda269dc69be4f06f3b87ef4272a444a78c670a98e888b81fa", "zip"),
    ("apps.irata.online",
     "https://apps.irata.online/COCO/Applications/Super%20Voice%20%28Speech%20Systems%29/SUPERV.DSK",
     DISK_SIZE, DISK_SHA256, "dsk"),
)
MAX_DOWNLOAD = 1 << 20
MAX_FILE = 64 << 20                       # a file the user picks: a zip of disks at most
TIMEOUT = 30
USER_AGENT = "ssi263-speech-nvda-addon"   # generic: nothing of the user's goes with the request


class FirmwareError(Exception):
    """Why nothing was installed, in words for the user."""


def _break():
    return os.environ.get("SSI263_USERDATA_BREAK", "")


def config_base():
    """NVDA's configuration folder, read now: configPath when NVDA gives an absolute one, else %APPDATA%\\nvda (a
    test or a tool outside NVDA)"""
    if _break() != "appdata":
        try:
            import globalVars
            p = getattr(getattr(globalVars, "appArgs", None), "configPath", None)
            if p and os.path.isabs(p):
                return p
        except ImportError:
            pass
    return os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "nvda")


def firmware_dir():
    """this add-on's user firmware folder (it may not exist yet)"""
    return os.path.join(config_base(), DIRNAME, "firmware")


def supervoice_path():
    return os.path.join(firmware_dir(), *SUPERVOICE)


def has_supervoice():
    return os.path.isfile(supervoice_path())


def _get(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        data = r.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise FirmwareError("the download is larger than the disk can be")
    return data


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _disk_from_zip(data):
    """the Super Voice's disk inside a zip, by its content; None when there is none"""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for info in z.infolist():
            if info.file_size == DISK_SIZE:
                inner = z.read(info)
                if _sha(inner) == DISK_SHA256:
                    return inner
    return None


def download_disk(fetch=_get):
    """(the disk's bytes, where from): the first source whose download is exactly its pinned file; FirmwareError
    naming what went wrong with each one otherwise"""
    problems = []
    for where, url, size, sha, kind in SOURCES:
        try:
            data = fetch(url)
            if (len(data) != size or _sha(data) != sha) and _break() != "hash":
                raise FirmwareError("the download is not the file it should be (%d bytes, sha256 %s...)"
                                    % (len(data), _sha(data)[:12]))
            disk = _disk_from_zip(data) if kind == "zip" else data
            if disk is None:
                raise FirmwareError("the zip holds no Super Voice disk")
            return disk, where
        except Exception as e:
            problems.append("%s: %s" % (where, e))
            if _break() == "nomirror":
                break
    raise FirmwareError("The Super Voice's disk could not be downloaded. " + " ".join(problems))


MAX_MEMBERS = 256                 # a zip's members tried at most: the archive's holds one disk


def candidates(data):
    """the byte strings in a file the user picked worth asking the library about: the file itself, and each member of
    a zip (one level, as the phones' import) small enough to be the disk or the program -- one at a time, so the caller
    stops at the first that is the Super Voice, and a member that cannot be read (damaged, encrypted, an unknown method)
    skipped, not the whole zip.  Every member used to be read first, under one try: a 64 MB zip of small members asked
    for gigabytes inside NVDA, and one unreadable member lost a disk that was readable (the 0.9.5 audit).
    SSI263_USERDATA_BREAK=zipall, the tests' control: as it was"""
    yield data
    if data[:4] != b"PK\x03\x04":
        return
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
    except (zipfile.BadZipFile, ValueError, OSError):
        return
    with z:
        members = [i for i in z.infolist() if not i.is_dir() and i.file_size <= 4 * DISK_SIZE][:MAX_MEMBERS]
        if _break() == "zipall":
            try:
                for b in [z.read(i) for i in members]:
                    yield b
            except zipfile.BadZipFile:
                pass
            return
        for i in members:
            try:
                b = z.read(i)
            except Exception:                 # BadZipFile (a CRC), RuntimeError (encrypted), NotImplementedError, zlib
                continue
            yield b


def install_supervoice(data, extract):
    """SUPERTOK.BIN from `data` (the program, the disk, or a zip holding either) through `extract` (the library's
    sv_firmware_from: bytes -> the program, raising with the reason), written into place.  'installed', or 'same' when
    the very same file is there already (nothing written); FirmwareError when it holds no known Super Voice."""
    prog, why = None, []
    for c in candidates(data):
        if _break() == "noverify":
            prog = c
            break
        try:
            prog = extract(c)
            break
        except Exception as e:
            why.append(str(e))
    if prog is None:
        raise FirmwareError("No Super Voice program was found in it: %s." % (why[0] if why else "nothing to read"))
    path = supervoice_path()
    try:
        with open(path, "rb") as f:
            if f.read() == prog:
                return "same"
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(prog)
    os.replace(tmp, path)
    return "installed"


def read_file(path):
    """a file the user picked, refused when it is too large to be any of the three"""
    if os.path.getsize(path) > MAX_FILE:
        raise FirmwareError("%s is too large to be the Super Voice's disk or a zip holding it."
                            % os.path.basename(path))
    with open(path, "rb") as f:
        return f.read()
