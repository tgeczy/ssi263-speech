"""The Accent-PC's C host (src/hosts/accent_pc_c.py) speaks from a ROM folder named outside the system's code page, and
from one whose ANSI name is also valid UTF-8.

The host handed the C library its folder as strict "mbcs" on Windows: a folder named in another script (Жук on a
Western Windows) raised UnicodeEncodeError before the unit was made, and a name in the code page whose ANSI bytes are
also UTF-8 ("Ã©" on Windows-1252: C3 A9) was read by bl_fopen.h as another name ("é"), so the ROMs were not found --
the wheel's bug (df3ec8e), left in this research host.  The folder now goes as os.fsencode gives it: UTF-8 on Windows
(PEP 529), as the NVDA drivers and the wheel pass theirs.

The case: the three ROMs (firmware/aicom-accent-sa) copied into a folder named Жук and, on Windows, one named from the
code page's characters at C3 A9; the unit booted and "Hello there." said from each must make the chip writes the
original folder makes, value for value.

    python src/csrc/accentpc/test_host_paths.py
    ACCENT_PC_PATHS_BREAK=mbcs python ...   the control: the folder encoded as "mbcs", as it was -- both must FAIL
                                            (Windows; elsewhere the control has nothing to break)
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "src"))
from ssi263.native import SSI263C  # noqa: E402
import hosts.accent_pc_c as HC  # noqa: E402

ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
FILES = ("u2.BIN", "u3.BIN", "u4.BIN")

if os.environ.get("ACCENT_PC_PATHS_BREAK") == "mbcs" and os.name == "nt":
    # the folder as it was encoded: strict mbcs
    HC.os = type("os", (), {"name": "nt", "path": os.path, "environ": os.environ, "sep": os.sep,
                            "fsencode": staticmethod(lambda p: p.encode("mbcs"))})


def writes(folder):
    a = HC.AccentPCC(folder, chip=SSI263C(out_rate=22050))
    try:
        a.boot()
        a.writes.clear()
        a.say("Hello there.\r")
        a.run(0.05)
        while a.busy():
            a.run(0.05)
        return [(r, v) for _t, r, v in a.writes]
    finally:
        a.close()


def main():
    if not all(os.path.isfile(os.path.join(ROMS, f)) for f in FILES):
        print("skip accent pc host paths: the ROMs are not in firmware/aicom-accent-sa")
        return 0
    ref = writes(ROMS)
    names = ["Жук"]                                  # Жук
    if os.name == "nt":
        names.append(b"\xc3\xa9".decode("mbcs") + "roms")              # its ANSI bytes are UTF-8 for "é"
    bad = 0
    tmp = tempfile.mkdtemp()
    try:
        for name in names:
            d = os.path.join(tmp, name)
            os.makedirs(d)
            for f in FILES:
                shutil.copy2(os.path.join(ROMS, f), d)
            try:
                got = writes(d)
                ok = got == ref
                why = "%d chip writes, as the original folder's" % len(got) if ok else \
                    "%d chip writes, the original folder's %d differ" % (len(got), len(ref))
            except Exception as e:                                    # noqa: BLE001 -- the failure is the finding
                ok, why = False, "%s: %s" % (type(e).__name__, e)
            print("%-4s the Accent-PC host from a folder named %s: %s" % ("ok" if ok else "FAIL", ascii(name), why))
            bad += not ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("accent pc host paths: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
