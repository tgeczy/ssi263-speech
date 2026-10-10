"""The 0.8 add-on's archive check (nvda/build_ssi263.py verify_archive, Astra's Reply 164) sees what it must: the
built archive clean, and copies of it changed one way each caught by name -- a byte of one ssi263speech.dll, the
Mockingboard's firmware left out, a unit's driver changed, a disk image added.

    python addon_archive_test.py        # after a release build (nvda/build_ssi263.py); exit 0 when all hold
"""
import os
import shutil
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import build_ssi263 as b  # noqa: E402

results = []


def check(name, ok, detail):
    results.append(ok)
    print("%s %-14s %s" % ("ok  " if ok else "FAIL", name, detail))


def variant(tmp, name, change, add=None):
    out = os.path.join(tmp, name + ".nvda-addon")
    with zipfile.ZipFile(b.OUT) as zi, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zo:
        for item in zi.infolist():
            data = change(item.filename, zi.read(item.filename))
            if data is not None:
                zo.writestr(item, data)
        if add:
            zo.writestr(add, b"a disk")
    return b.verify_archive(out)


def main():
    if not os.path.isfile(b.OUT):
        print("no %s: run nvda/build_ssi263.py first" % b.OUT)
        return 1
    tmp = tempfile.mkdtemp(prefix="addon-archive-")
    try:
        clean = b.verify_archive(b.OUT)
        check("as_built", not clean, "; ".join(clean) or "every file as its source")
        dll = b.U + "_ssi263_speakout/bin/x86/ssi263speech.dll"
        got = variant(tmp, "dll", lambda n, d: d[:-1] + bytes([d[-1] ^ 1]) if n == dll else d)
        check("dll_byte", got == ["differs from its source: " + dll], "; ".join(got) or "NOT caught")
        mb = b.U + "_ssi263_mockingboard/mockingboard-tts-1.1.bin"
        got = variant(tmp, "nomb", lambda n, d: None if n == mb else d)
        check("no_mockingboard", got == ["missing: " + mb], "; ".join(got) or "NOT caught")
        drv = b.U + "blazie.py"
        got = variant(tmp, "drv", lambda n, d: d + b"\n# changed\n" if n == drv else d)
        check("driver_changed", got == ["differs from its source: " + drv], "; ".join(got) or "NOT caught")
        got = variant(tmp, "dsk", lambda n, d: d, add=b.U + "_ssi263_mockingboard/toolkit.dsk")
        check("disk_image", any("disk image" in g for g in got), "; ".join(got) or "NOT caught")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("addon archive: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
