"""The settings screen's intent runs once, not again when the screen is rebuilt (Panthera's audit, P40), on the phone.

The settings screen is exported and took its intent's extras in onCreate on every start -- and a rebuild (the screen
turned, dark mode, a keyboard) starts it again with the same intent: an autospeak spoke again, a removefirmware
removed again, an import imported again.  Now its test hooks and the adb route run on a fresh start only, and in a
debug build alone (a release build ignores the hooks and asks before an import or a removal: TestHooksTest).

The case (harmless: the screen's Speak): autospeak a short text, dark mode switched 2 s in (the screen rebuilt), then
switched back (rebuilt again): the preview must have rendered once.  Dark mode is put back as it was.

    python test_hooks_rebuild.py            (a debug build installed; ANDROID_SERIAL picks the device)
    python test_hooks_rebuild.py --control  the screen's `--ez hooksbreak true` (a debug build's: every rebuild takes
                                            the intent again, as it was): must FAIL
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


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    night = adb("shell", "cmd", "uimode", "night").strip().split(":")[-1].strip()
    bad = 1
    try:
        adb("shell", "am", "force-stop", PKG)
        adb("logcat", "-c")
        args = ["am", "start", "-W", "-n", PKG + "/.SettingsActivity", "--ez", "autospeak", "true", "--es", "text",
                "One."] + (["--ez", "hooksbreak", "true"] if a.control else [])
        adb("shell", " ".join(shlex.quote(x) for x in args))
        time.sleep(2.0)
        adb("shell", "cmd", "uimode", "night", "no" if night == "yes" else "yes")
        time.sleep(2.5)
        adb("shell", "cmd", "uimode", "night", night)
        time.sleep(2.5)
        renders = len(re.findall(r"Preview.*: \d+ samples at", adb("logcat", "-d", "-v", "brief", "-s", "Preview:I")))
        ok = renders == 1
        print("%-4s rebuild  autospeak, then the screen rebuilt twice (dark mode): the preview rendered %d time%s" % (
            "ok" if ok else "FAIL", renders, "" if renders == 1 else "s"))
        bad = not ok
    finally:
        adb("shell", "cmd", "uimode", "night", night, check=False)
        adb("shell", "am", "force-stop", PKG, check=False)
    print("hooks rebuild: %s" % ("FAILED" if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
