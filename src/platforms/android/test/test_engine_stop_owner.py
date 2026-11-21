"""One engine, two users: a stop from one must not end the other's utterance (#51), on the attached phone.

The engine had one stop flag (ssa_engine.c e->stop): whichever utterance owned the engine at its next pull was cut.
The settings screen's preview stop -- run by its Stop button, Remove firmware, and onDestroy (the screen turned, Back
from the system's Text-to-speech settings) whether a preview ran or not -- cut the screen reader's utterance and
reported it done; and a screen reader's stop of an utterance waiting behind a preview or a WAV export cut the render,
so an export wrote nothing and deleted its file.

  service   the service renders a long text (TtsSelfTest's synthesizeToFile, bound by package name: the system's
            default engine is left alone); the settings screen is rebuilt meanwhile (dark mode switched), so its
            onDestroy runs the preview's stop: the service must log its render stopped=false, all of it
  preview   the screen's Speak renders a long text (the autospeak hook) while a TTS utterance waits behind it and its
            own client stops it (TtsSelfTest's stop): the preview must render as many samples as undisturbed

    python test_engine_stop_owner.py           (a debug build installed; ANDROID_SERIAL picks the device)
    python test_engine_stop_owner.py --control the screen's test hook --ez stopbreak true (any stop ends whatever
                                               utterance owns the engine, as it was): must FAIL
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time

PKG = "com.ssi263speech.tts"
ACT = PKG + "/.SettingsActivity"
ADB = os.environ.get("ADB") or "adb"
LONG = ("This is a long sentence for the engine to render, so that there is time for something else to happen "
        "while it does: a screen turned, a stop from another client, a settings page left behind. ") * 3


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace")
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def run(extras, during=None, wait=8.0):
    adb("shell", "am", "force-stop", PKG)
    adb("logcat", "-c")
    # adb shell joins its arguments into one command line for the phone's shell: each quoted, or the text splits
    adb("shell", " ".join(shlex.quote(x) for x in ["am", "start", "-n", ACT] + list(extras)))
    if during:
        during()
    time.sleep(wait)
    return adb("logcat", "-d", "-v", "brief", "-s", "SsiTts:I", "Preview:I", "SsiSelfTest:I")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    brk = ["--ez", "stopbreak", "true"] if a.control else []
    night = adb("shell", "cmd", "uimode", "night").strip().split(":")[-1].strip()
    bad = 0
    try:
        # service: the render to a file, the settings screen rebuilt 1.5 s in
        def rebuild():
            time.sleep(1.5)                       # inside the render (the client binds first)
            adb("shell", "cmd", "uimode", "night", "no" if night == "yes" else "yes")
        log = run(["--ez", "ttstest", "true", "--es", "text", LONG] + brk, rebuild)
        adb("shell", "cmd", "uimode", "night", night)
        m = re.search(r"SsiTts.*?: (\d+) samples at \d+ Hz, stopped=(true|false)", log)
        ok = bool(m) and m.group(2) == "false" and "done: file" in log
        print("%-4s service  the settings screen rebuilt during the service's render: %s" % (
            "ok" if ok else "FAIL", "%s samples, stopped=%s" % m.groups() if m else "no render logged"))
        bad += not ok

        # preview: undisturbed, then with a TTS utterance stopped behind it
        def samples(log):
            m = re.findall(r"Preview.*?: (\d+) samples at", log)
            return int(m[-1]) if m else -1
        alone = samples(run(["--ez", "autospeak", "true", "--es", "text", LONG] + brk))
        log = run(["--ez", "autospeak", "true", "--es", "text", LONG, "--ez", "ttstest", "true", "--ei", "stop", "300",
                   "--ef", "rate", "1.0"] + brk)
        crowded = samples(log)
        ok = alone > 0 and crowded == alone
        print("%-4s preview  a TTS utterance stopped behind the preview's render: %d samples, alone %d" % (
            "ok" if ok else "FAIL", crowded, alone))
        bad += not ok
    finally:
        adb("shell", "cmd", "uimode", "night", night, check=False)
        adb("shell", "am", "force-stop", PKG, check=False)
    print("engine stop owner: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
