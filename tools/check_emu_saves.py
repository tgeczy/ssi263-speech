"""Every emulator app saves a unit's state whole or not at all (issue #37).

A unit's state holds its RAM and file flash: the person's files.  Saved straight to its file, fopen(path, "wb")
truncates the last good state before the first byte is written, and a save cut short (the app killed, a crash, a
full disk, the power lost) leaves a file that will not load -- every start then fails, every file in the unit gone.
The Windows app saved that way at all three of its saves (the minute's, on leaving, before files are imported or
exported), and the Linux and GTK apps at theirs before files change.  emu_save_whole (src/apps/blazie/emu_unit.h)
writes PATH.new and puts it in place in one step; test_emu_unit checks it keeps the last state when it cannot finish.

This check reads the three apps: none may call emu_save() itself, only emu_save_whole().

    python tools/check_emu_saves.py [folder]       exit 1 on any failure (folder: the apps' sources)
    EMU_SAVES_BREAK=1   the Windows app's first save read as it was, emu_save()   -- must fail: main_win.c, only
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS = ("main_win.c", "main_linux.c", "main_gtk.c")


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "src", "apps", "blazie")
    bad = 0
    for name in APPS:
        text = open(os.path.join(folder, name), encoding="utf-8", errors="replace").read()
        if os.environ.get("EMU_SAVES_BREAK") and name == "main_win.c":
            text = text.replace("emu_save_whole(", "emu_save(", 1)
        plain =[text.count("\n", 0, m.start()) + 1 for m in re.finditer(r"\bemu_save\s*\(", text)]
        whole = len(re.findall(r"\bemu_save_whole\s*\(", text))
        ok = not plain and whole > 0
        bad += not ok
        print("%-4s %-13s %s" % ("ok" if ok else "FAIL", name, "%d saves, all whole (emu_save_whole)" % whole if ok
                                 else "saves in place with emu_save() at line%s %s" % (
                                     "s" if len(plain) > 1 else "", ", ".join(str(p) for p in plain))
                                 if plain else "no save found"))
    print("emulator saves: %s" % ("%d FAILED" % bad if bad else "all whole"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
