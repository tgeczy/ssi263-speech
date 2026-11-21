"""The settings screen's Speak stops when the screen is left (Panthera's audit, P54), on the attached phone.

The preview played to its end from the background after Home or a switch to another app: a long text kept talking
over whatever the person went to.  Now the screen's onStop stops a Speak (an export goes on: the person's file).

The case: Speak (the autospeak hook) a long text, Home 3 s into its playback: the preview must log that it stopped,
having played only part of the text, within 4 s.

    python test_preview_leave.py            (a debug build installed; ANDROID_SERIAL picks the device)
    python test_preview_leave.py --control  the screen's `--ez previewbreak true` (it plays on, as it was): must FAIL
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time

PKG = "com.ssi263speech.tts"
ADB = os.environ.get("ADB") or "adb"
LONG = "This is a long text for the preview to speak, so that there is time to leave the screen while it talks. " * 4


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    bad = 1
    try:
        adb("shell", "am", "force-stop", PKG)
        adb("logcat", "-c")
        args = ["am", "start", "-W", "-n", PKG + "/.SettingsActivity", "--ez", "autospeak", "true", "--es", "text",
                LONG] + (["--ez", "previewbreak", "true"] if a.control else [])
        adb("shell", " ".join(shlex.quote(x) for x in args))
        played = None
        for _ in range(40):                       # the render first, then its playback starts
            log = adb("logcat", "-d", "-v", "brief", "-s", "Preview:I")
            if re.search(r"Preview.*: \d+ samples at", log):
                break
            time.sleep(0.25)
        time.sleep(3.0)                           # 3 s into the playback
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        time.sleep(4.0)
        log = adb("logcat", "-d", "-v", "brief", "-s", "Preview:I")
        m = re.search(r"played (\d+) of (\d+) samples(, stopped)?", log)
        if m:
            played = (int(m.group(1)), int(m.group(2)), bool(m.group(3)))
        ok = bool(played) and played[2] and played[0] < played[1]
        print("%-4s leave    Home 3 s into a Speak: %s" % (
            "ok" if ok else "FAIL",
            "stopped after %d of %d samples" % played[:2] if ok else
            "still playing 4 s after Home (no end logged)" if not played else
            "played %d of %d samples, not stopped" % played[:2]))
        bad = not ok
    finally:
        adb("shell", "am", "force-stop", PKG, check=False)
    print("preview leave: %s" % ("FAILED" if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
