"""The Setup page's "Speaking with" follows the voice chosen on Voice settings (#55), on the attached phone.

The Voice dialog saved the choice and changed its own button's text alone; the Setup page's status line still named
the previous voice -- while speech already used the new one -- until the screen was left and resumed.

The case, through the screen itself (uiautomator: the tabs, the Voice button, the dialog's list): Voice settings,
the Voice button, another voice from the list, then Setup: its status must say "Speaking with: <the voice chosen>".
The voice the phone had is put back afterwards (the screen's setvoice hook, with the number read from its settings).

    python test_voice_status.py            (a debug build installed; ANDROID_SERIAL picks the device)
    python test_voice_status.py --control  the screen's test hook --ez voicebreak true (the dialog changes its button's
                                           text alone, as it was): must FAIL
"""
import argparse
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

PKG = "com.ssi263speech.tts"
ADB = os.environ.get("ADB") or "adb"
PREFS = "/data/user_de/0/%s/shared_prefs/ssi263speech.xml" % PKG


def adb(*args, check=True):
    r = subprocess.run([ADB] + list(args), capture_output=True, text=True, errors="replace",
                       env=dict(os.environ, MSYS_NO_PATHCONV="1"))
    if check and r.returncode:
        raise RuntimeError("adb %s: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout


def nodes():
    adb("shell", "uiautomator", "dump", "/sdcard/ssi263_ui.xml")
    xml = adb("exec-out", "cat", "/sdcard/ssi263_ui.xml")
    return [n.attrib for n in ET.fromstring(xml[xml.index("<"):]).iter("node")]


def tap(pred, what):
    for n in nodes():
        if pred(n.get("text", "")):
            x1, y1, x2, y2 = map(int, re.findall(r"\d+", n["bounds"]))
            adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
            time.sleep(1.2)
            return n.get("text", "")
    raise RuntimeError("not on the screen: " + what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--control", action="store_true")
    a = ap.parse_args()
    m = re.search(r'name="voice" value="(-?\d+)"', adb("shell", "run-as", PKG, "cat", PREFS, check=False))
    before = int(m.group(1)) if m else -1
    bad = 1
    try:
        adb("shell", "am", "force-stop", PKG)
        adb("shell", "am", "start", "-W", "-n", PKG + "/.SettingsActivity",
            *(["--ez", "voicebreak", "true"] if a.control else []))
        time.sleep(1.5)
        tap(lambda t: t == "Voice settings", "the Voice settings tab")
        was = tap(lambda t: t.startswith("Voice: "), "the Voice button")[len("Voice: "):]
        listed = [n.get("text", "") for n in nodes() if n.get("class", "").endswith("CheckedTextView")]
        chosen = next((t for t in listed if t and t != was), None)
        if not chosen:
            print("skip  only one voice on this phone: nothing to choose")
            return 0
        tap(lambda t: t == chosen, "the voice " + chosen)
        tap(lambda t: t == "Setup", "the Setup tab")
        status = ""
        for _ in range(10):                      # the status line is below the page's first screen: scroll to it
            status = next((n.get("text", "") for n in nodes() if "Speaking with:" in n.get("text", "")), "")
            if status:
                break
            adb("shell", "input", "swipe", "600", "2000", "600", "900", "250")
            time.sleep(0.6)
        said = status.split("Speaking with:", 1)[1].strip().rstrip(".") if status else "(no status line)"
        ok = said == chosen
        print("%-4s status  %s chosen in the Voice dialog (was %s); Setup says Speaking with: %s" % (
            "ok" if ok else "FAIL", chosen, was, said))
        bad = not ok
    finally:
        adb("shell", "am", "force-stop", PKG, check=False)
        adb("shell", "am", "start", "-W", "-n", PKG + "/.SettingsActivity", "--ei", "setvoice", str(before), check=False)
        time.sleep(1.0)
        adb("shell", "am", "force-stop", PKG, check=False)
        adb("shell", "rm", "-f", "/sdcard/ssi263_ui.xml", check=False)
        m = re.search(r'name="voice" value="(-?\d+)"', adb("shell", "run-as", PKG, "cat", PREFS, check=False))
        print("      the phone's voice put back: %s (was %s)" % (m.group(1) if m else "the default", before))
    print("voice status: %s" % ("FAILED" if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
