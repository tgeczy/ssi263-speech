"""A unit whose start finishes after its screen was left must not come on unseen (#52), on the attached phone.

The unit screen switches a unit off when it is left (onStop), but only one already on: a start still running on its
thread (seconds; a release unit's first start makes its memory, up to a minute) finished later and switched the unit
on in the background -- its power-up speech and idle sound from nowhere, the emulation running in real time -- until
the screen was shown and left again; a system destroy meanwhile left it running unsaved.

The case: the Spanish unit started through the screen's test hook, held 4 s before the unit's own start (the
startdelay hook: a slow first start's window, every time), Home pressed 1 s in, the start left to finish.  The unit must not come on ("SsiUnit: on" with no "off" after
it, the screen's own debug log) -- the screen switches it off at once, its memory saved.  The memory file the first
start made is removed afterwards when there was none before, so the phone is left as it was.

    python test_unit_background.py               (a debug build installed, the Spanish firmware imported)
    python test_unit_background.py --control     the screen's test hook --ez bgbreak true (the start finished as if the
                                                 screen were shown, as it was): must FAIL
"""
import argparse
import os
import subprocess
import sys
import time

PKG = "com.ssi263speech.tts"
ADB = os.environ.get("ADB") or "adb"
SAVED = "files/units/spanish.state"


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def has_saved():
    return adb("shell", "run-as", PKG, "ls", SAVED, check=False).strip().endswith("spanish.state")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    had = has_saved()
    adb("shell", "am", "force-stop", PKG)
    adb("logcat", "-c")
    try:
        adb("shell", "am", "start", "-n", PKG + "/.UnitActivity", "--es", "unit", "bl-es", "--ez", "autostart", "true",
            "--ei", "startdelay", "4000",
            *(["--ez", "bgbreak", "true"] if a.control else []))
        time.sleep(1.0)
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        deadline = time.time() + 120
        log = ""
        while time.time() < deadline:                   # the start's outcome: on, refused, or switched off unseen
            time.sleep(2.0)
            log = adb("logcat", "-d", "-v", "brief", "-s", "SsiUnit:*")
            if "on bl-es" in log or "not shown" in log or "would not start" in log:
                time.sleep(3.0)
                log = adb("logcat", "-d", "-v", "brief", "-s", "SsiUnit:*")
                break
        on = "on bl-es" in log
        off_after = on and "off bl-es" in log.split("on bl-es", 1)[1]
        unseen = "not shown" in log
        # a pass needs the evidence: the screen's own "not shown" line, the start switched off as it came on.  No
        # outcome in the time, or a start that failed, proves nothing either way (it passed before: a review)
        ok = unseen and not on
        print("%-4s background  the screen left 1 s into the Spanish unit's first start: %s" % (
            "ok" if ok else "FAIL",
            "switched off unseen, its memory saved" if unseen and not on else
            "came ON in the background and kept running" if on and not off_after else
            "on, then off: the start finished while the screen was shown (the window missed)" if on else
            "the start failed ('would not start' in the log): nothing shown either way" if "would not start" in log else
            "no outcome logged in 120 s: nothing shown either way"))
        for ln in log.splitlines():
            if "SsiUnit" in ln:
                print("     " + ln[:170])
        bad = not ok
    finally:
        adb("shell", "am", "force-stop", PKG, check=False)
        if not had and has_saved():
            adb("shell", "run-as", PKG, "rm", "-f", SAVED, SAVED + ".before-import", check=False)
    print("unit background: %s" % ("FAILED" if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
