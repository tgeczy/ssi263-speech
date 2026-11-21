"""The Windows emulator in folders named beyond the system's code page (issue #87).

The program took its own folder and APPDATA through the ANSI calls and opened files with the C library, so a Windows
profile named with letters the system's code page cannot hold (Polish, Czech, Cyrillic, Greek, Hebrew, Arabic on an
English Windows) broke it: the save folder could not be made and every save failed ("what you wrote this time is
lost", again from the autosave each minute), and a program unzipped under such a profile found no firmware.  Its
manifest now makes UTF-8 its code page (Windows 10 1903 and later).

The real program, a private copy (its blazie_emu.ini beside it, its own APPDATA), all three in folders named
"...Жук...": the English Braille Lite chosen, the program started and closed by window messages.
  started     the unit starts: no "Could not start", the main window up
  saved       closed, the unit's memory is saved in the APPDATA folder, no "Could not save" box

    python test_win_unicode.py <blazie_emu.exe> <firmware folder: BL2ENG.BNS, bl2_2003_warm.state>     Windows only
    TEST_WIN_UNICODE_BREAK=1   runs blazie_emu_ansi.exe beside it, the program without its manifest -- must fail
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_win_factory as w  # noqa: E402  (the window helpers: message boxes, the main window)

NAME = "Жук"          # Zhuk, outside every Western code page


def main():
    exe, fw = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    if os.environ.get("TEST_WIN_UNICODE_BREAK"):
        exe = os.path.join(os.path.dirname(exe), "blazie_emu_ansi.exe")
    tmp = tempfile.mkdtemp(prefix="emu_uni_")
    proc = None
    try:
        app = os.path.join(tmp, "app_" + NAME)
        fwdir = os.path.join(tmp, "firmware_" + NAME)
        appdata = os.path.join(tmp, "appdata_" + NAME)
        os.makedirs(app)
        os.makedirs(fwdir)
        os.makedirs(appdata)
        shutil.copy(exe, os.path.join(app, "blazie_emu.exe"))
        for f in ("BL2ENG.BNS", "bl2_2003_warm.state"):
            shutil.copy(os.path.join(fw, f), fwdir)
        with open(os.path.join(app, "blazie_emu.ini"), "w", encoding="utf-8") as f:
            f.write("[unit]\nkind=english\nfirmware_dir=%s\n" % fwdir)
        proc = subprocess.Popen([os.path.join(app, "blazie_emu.exe")], env=dict(os.environ, APPDATA=appdata))
        main_w = w.main_window(proc.pid)
        time.sleep(3.0)                                       # the unit started, or its message shown
        w.answer(proc.pid, "\x00(none expected)", w.IDOK, timeout=1.0)
        failed = [m for _b, m in w.others if "Could not start" in m or "could not" in m.lower()]
        w.check("started", main_w is not None and not failed,
                "the English unit from a folder named %s: %s" % (ascii(NAME), failed[0][:90] if failed else "started"))
        del w.others[:]
        if main_w is not None:
            w.u32.PostMessageW(main_w, w.WM_CLOSE, 0, 0)
        t0 = time.monotonic()
        while proc.poll() is None and time.monotonic() - t0 < 20:
            w.answer(proc.pid, "\x00(none expected)", w.IDOK, timeout=0.5)
        if proc.poll() is None:
            proc.kill()
        proc = None
        saves = os.path.join(appdata, "ssi263-speech", "blazie-emu")
        states = [f for f in (os.listdir(saves) if os.path.isdir(saves) else []) if f.endswith(".state")]
        lost = [m for _b, m in w.others if "save" in m.lower()]
        w.check("saved", bool(states) and not lost, "closed: %s%s" % (
            "saved as %s" % states[0] if states else "NOTHING saved in the APPDATA folder",
            "; it said %r" % lost[0][:90] if lost else ""))
    finally:
        if proc is not None:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    print("folders beyond the code page: %s" % ("%d FAILED" % w.bad if w.bad else "all ok"))
    return 1 if w.bad else 0


if __name__ == "__main__":
    sys.exit(main())
