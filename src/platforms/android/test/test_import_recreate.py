"""The settings screen rebuilt while it checks a file to import (#49), on the attached phone.

An import shows "Checking the file" while a worker reads the file (SsiImport.inspect: seconds for a large or slow
one).  A configuration change in that time -- the screen turned, a keyboard connected, dark mode or font size changed
-- recreates the activity; the worker then dismissed the old activity's dialog, whose window the system had already
removed: IllegalArgumentException, and the app's process -- the TTS service with it -- died.  The new screen was told
nothing either.

The case: a large zip (many entries, so the check takes a while) pushed to the app's own external files folder,
the import started through the adb route (`am start ... --es import <path>`), dark mode switched while it checks, and
then: the app's process must be the one it was (no crash), the log must not show the window manager's exception, and
the rebuilt screen must show the check's outcome (its status line, read through the "import" log line).  Dark mode
is put back as it was.

    python test_import_recreate.py [--tries 3]       (a debug build installed; ANDROID_SERIAL picks the device)
    python test_import_recreate.py --control         the screen's test hook --ez recreatebreak true (the checking
                                                     dialog dismissed before the screen is looked at, the check never
                                                     handed to the rebuilt screen, as it was): must FAIL
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
import time
import zipfile

PKG = "com.ssi263speech.tts"
ACT = PKG + "/.SettingsActivity"
ADB = os.environ.get("ADB") or "adb"
DEVICE_DIR = "/sdcard/Android/data/%s/files" % PKG


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def pid():
    return adb("shell", "pidof", PKG, check=False).strip()


def big_zip(path, entries=3000):
    """a zip of many small entries, none of them firmware: the check reads and judges each one"""
    rng = random.Random(49)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for k in range(entries):
            z.writestr("folder%02d/file%04d.bin" % (k % 40, k), bytes(rng.getrandbits(8) for _ in range(4096)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tries", type=int, default=3)
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ssi263_recreate_")
    local = os.path.join(tmp, "not-firmware.zip")
    big_zip(local)
    adb("shell", "mkdir", "-p", DEVICE_DIR)
    remote = DEVICE_DIR + "/not-firmware.zip"
    adb("push", local, remote)
    night = adb("shell", "cmd", "uimode", "night").strip().split(":")[-1].strip()       # "no" or "yes"
    bad = 0
    try:
        for k in range(a.tries):
            adb("shell", "am", "force-stop", PKG)
            adb("logcat", "-c")
            # one start, the import in it: a second start onto the running screen's task is dropped by the system
            # ("its current task has been brought to the front"), so the screen is never open before
            adb("shell", "am", "start", "-W", "-n", ACT, "--es", "import", remote,
                *(["--ez", "recreatebreak", "true"] if a.control else []))
            before = pid()
            time.sleep(0.1)
            adb("shell", "cmd", "uimode", "night", "yes" if night == "no" else "no")      # the activity rebuilt
            time.sleep(8.0)
            after = pid()
            log = adb("logcat", "-d", "-v", "brief")
            crashed = "not attached to window manager" in log or "FATAL EXCEPTION" in log
            told = any("SsiImport" in ln and ("refused" in ln or "plan" in ln) for ln in log.splitlines())
            checking = sum(1 for ln in log.splitlines() if "SsiImport" in ln and "checking" in ln.lower())
            ok = before and after == before and not crashed and told
            print("%-4s try %d: process %s -> %s, %s, the rebuilt screen %s" % (
                "ok" if ok else "FAIL", k + 1, before or "-", after or "(gone)",
                "the window manager's exception in the log" if crashed else "no crash in the log",
                "told the check's outcome" if told else "NOT told the outcome"))
            bad += not ok
            if not ok or os.environ.get("RECREATE_VERBOSE"):          # what the app said, for the reader
                for ln in log.splitlines():
                    if (before and "(%s)" % before.rjust(5) in ln) or "AndroidRuntime" in ln:
                        print("     " + ln[:190])
            adb("shell", "cmd", "uimode", "night", night)
            time.sleep(1.0)
    finally:
        adb("shell", "cmd", "uimode", "night", night, check=False)
        adb("shell", "rm", "-f", remote, check=False)
        os.remove(local)
        os.rmdir(tmp)
    print("import recreate: %s" % ("%d of %d FAILED" % (bad, a.tries) if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
