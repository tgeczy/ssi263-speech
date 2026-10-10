"""The installed app's TTS service on the attached device, end to end: Android's own TextToSpeech client, bound to
this engine by package name (the system's default engine is left alone), synthesizes a sentence to a file at the
rate and pitch asked for (TtsSelfTest.kt); the file's PCM must equal the desktop's for the same text and the app's
default settings (its engine volume included).  That covers what the host-side test cannot: the Kotlin service, the
JNI bridge, the APK's Accent SA ROMs and the framework's hand-over.

- The Aicom Accent SA (the built-in voice): against the NVDA Accent driver itself (accent_reference.py), the request's
  rate and pitch mapped as ssa_engine.c maps them.  Then a capital, as TalkBack asks for one -- the same letter at
  pitch 1.0, 1.5, 1.2, 0.75 and 1.0 again, each its own request: the raised and lowered ones must differ from 1.0,
  each must equal the driver's (which jumps to the pitch with snap_pitch), and the last must be byte-identical to the
  first.
- The Braille Lite (when its English firmware is imported on the device): against bl_voice on the desktop, driven the
  way the speech-dispatcher module maps SSIP (test_android_native.py's reference); then with the EXPERIMENTAL run ahead
  on (SettingsActivity's setrunahead hook, as its check box), against bl_voice running ahead, and not the lockstep's.
- The GW Micro Speak-Out (when its SPEAKOUT.HEX is imported on the device): against so_voice driven directly on the
  desktop (test_android_native --so-direct) from the device's own HEX, at the app's defaults, the request's rate and
  pitch mapped as ssa_engine.c maps them; and the capital's pitches as the Accent SA's.
- The Aicom Accent-mini (built in, when this build carries it): against am_voice driven directly (--am-direct), the
  same way.
- The two Mockingboards (built in, or imported): against mb_voice driven directly (--mb-direct) from the file the
  device speaks from -- its imported copy, else the local copy the APK was built with -- the same way (--voice
  mockingboard runs both).

Each voice is chosen for the run through SettingsActivity's setvoice hook, as the Voice button would, and the choice
the device had before (or none) is put back afterwards, as is run ahead.

    python test_device_service.py [--rate 2.0] [--aloud]
                                  [--voice accent|braillelite|speakout|accentmini|mockingboard|both|all]
                                  (a debug build installed; ANDROID_SERIAL picks the device; both = the Accent SA and
                                  the Braille Lite, as before 0.7.5)

--aloud also speaks the sentence through the service on the device's speaker.  Options as test_android_native.py:
--firmware, --lib, --chip.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_android_native as T  # noqa: E402

PKG = "com.ssi263speech.tts"
PREFS = "/data/user_de/0/%s/shared_prefs/ssi263speech.xml" % PKG
TEXT = {"braillelite": "Hello there. This is the Braille Lite, speaking on a phone.",
        "accent": "Hello there. This is the Accent SA, speaking on a phone."}
TEXT["speakout"] = "Hello there. This is the Speak-Out, speaking on a phone."
TEXT["accentmini"] = "Hello there. This is the Accent-mini, speaking on a phone."
TEXT["mockingboard"] = "Hello there. This is the Mockingboard, speaking on a phone."
TEXT["mockingboardearly"] = "Hello there. This is the early Mockingboard, speaking on a phone."
VOICE = {"braillelite": 0, "accent": 2, "speakout": 3, "accentmini": 4, "mockingboard": 5,
         "mockingboardearly": 6}                                                     # SsiNative's indices
CAPITAL = "B"
SETTINGS = os.path.join(T.REPO, "src", "platforms", "android", "app", "src", "main", "kotlin", "com", "ssi263speech",
                        "tts", "SsiSettings.kt")


def adb(*args, **kw):
    exe = shutil.which("adb") or os.path.join(os.environ.get("ANDROID_HOME", ""), "platform-tools", "adb")
    return subprocess.run([exe] + list(args), capture_output=True, **kw)


def default_volume():
    return int(re.search(r"const val DEFAULT_VOLUME = (\d+)", open(SETTINGS, encoding="utf-8").read()).group(1))


def saved_voice():
    """The voice the device's settings hold now, or -1 when none was chosen."""
    r = adb("shell", "run-as", PKG, "cat", PREFS)
    m = re.search(rb'<int name="voice" value="(-?\d+)"', r.stdout or b"")
    return int(m.group(1)) if m else -1


def saved_run_ahead():
    """The run-ahead setting the device holds now: 1, 0, or -1 when never set."""
    r = adb("shell", "run-as", PKG, "cat", PREFS)
    m = re.search(rb'<boolean name="run_ahead" value="(true|false)"', r.stdout or b"")
    return -1 if not m else int(m.group(1) == b"true")


def set_run_ahead(v):
    adb("shell", "am", "force-stop", PKG, check=True)
    adb("shell", "am", "start", "-W", "-n", PKG + "/.SettingsActivity", "--ei", "setrunahead", str(v), check=True)
    time.sleep(1.0)


def set_voice(v):
    adb("shell", "am", "force-stop", PKG, check=True)
    adb("shell", "am", "start", "-W", "-n", PKG + "/.SettingsActivity", "--ei", "setvoice", str(v), check=True)
    time.sleep(1.0)


def render(text, rate, pitch, aloud=False):
    """The service's PCM for one request, in a fresh process."""
    adb("shell", "am", "force-stop", PKG, check=True)
    adb("shell", "run-as", PKG, "rm", "-f", "files/tts-test.wav")
    args = ["shell", "am", "start", "-n", PKG + "/.SettingsActivity", "--ez", "ttstest", "true",
            "--es", "text", "'%s'" % text, "--ef", "rate", str(rate), "--ef", "pitch", str(pitch)]
    if aloud:
        args += ["--ez", "aloud", "true"]
    adb(*args, check=True)                      # adb shell joins the arguments: the text is quoted for the device
    for _ in range(60):
        time.sleep(0.5)
        r = adb("exec-out", "run-as", PKG, "cat", "files/tts-test.wav")
        if r.returncode == 0 and len(r.stdout) > 44:
            time.sleep(1.0)                     # let the file be finished
            wav = adb("exec-out", "run-as", PKG, "cat", "files/tts-test.wav").stdout
            pcm = wav[44:]
            return len(pcm) // 2, T.fnv(pcm)
    sys.exit("no tts-test.wav from the device (is the debug build installed?)")


def accent_reference(cases):
    """(name, text, rate %, pitch %) -> the NVDA driver's (samples, hash), at the app's defaults"""
    level, volume = T.accent_level(), default_volume()
    speech = [dict(name=n, text=t, rate=T.on_top(50, r), offset=T.accent_pitch(50, p) - 50, inflection=100,
                   volume=volume * level // 100, sample_rate=22050, blocks=0) for n, t, r, p in cases]
    tmp = tempfile.mkdtemp(prefix="ssi263-device-accent-")
    try:
        path = os.path.join(tmp, "cases.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"speech": speech, "texts": []}, f)
        r = subprocess.run([sys.executable, os.path.join(HERE, "accent_reference.py"), path], capture_output=True,
                           text=True, env=dict(os.environ, SSI263_ACCENT_SA_CORE="c"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if r.returncode:
        sys.exit("accent_reference.py failed: %s" % (r.stdout + r.stderr).strip()[-1000:])
    return T.parse(r.stdout)


def line(ok, what, got, want=None):
    print("%-5s %s: %s" % ("ok" if ok else "FAIL", what, "%d samples %s" % got if ok or want is None
                            else "got %s, reference %s" % (got, want)))
    return not ok


def accent(a):
    pct = int(round(a.rate * 100))
    cases = [("sentence", TEXT["accent"], pct, 100), ("cap-100", CAPITAL, 100, 100), ("cap-150", CAPITAL, 100, 150),
             ("cap-120", CAPITAL, 100, 120), ("cap-75", CAPITAL, 100, 75)]
    want = accent_reference(cases)
    set_voice(VOICE["accent"])
    bad = 0
    got = render(TEXT["accent"], a.rate, 1.0, a.aloud)
    bad += line(got == want["sentence"], "Accent SA through the platform TTS, rate %.2f" % a.rate, got,
                want["sentence"])
    caps = {}
    for name, pitch in (("cap-100", 1.0), ("cap-150", 1.5), ("cap-120", 1.2), ("cap-75", 0.75), ("cap-100-again", 1.0)):
        caps[name] = render(CAPITAL, 1.0, pitch)
        ref = want[name if name != "cap-100-again" else "cap-100"]
        bad += line(caps[name] == ref, "Accent SA, %r at pitch %.2f (%s)" % (CAPITAL, pitch, name), caps[name], ref)
    bad += line(caps["cap-150"] != caps["cap-100"] and caps["cap-120"] != caps["cap-100"]
                and caps["cap-75"] != caps["cap-100"],
                "Accent SA: the capital's raised and lowered pitch sound different from 1.0", caps["cap-150"])
    bad += line(caps["cap-100-again"] == caps["cap-100"],
                "Accent SA: 1.0 again is byte-identical to the first (the pitch came back)", caps["cap-100-again"])
    return bad


_exe = []


def desktop_program():
    """test_android_native for the desktop (its --so-direct and --am-direct references), built once."""
    if not _exe:
        _exe.append(T.build_desktop())
    return _exe[0]


def direct(mode, firmware, lines):
    """One fresh unit per case (the service runs each request in a fresh process): the reference's (samples, hash)."""
    got = {}
    for line in lines:
        r = subprocess.run([desktop_program(), mode, firmware], input=line, capture_output=True, text=True)
        if r.returncode:
            sys.exit("test_android_native %s failed: %s" % (mode, r.stderr.strip()))
        got.update(T.parse(r.stdout))
    return got


def run_cases(a, voice, want, label):
    set_voice(VOICE[voice])
    bad = 0
    got = render(TEXT[voice], a.rate, 1.0, a.aloud)
    bad += line(got == want["sentence"], "%s through the platform TTS, rate %.2f" % (label, a.rate), got,
                want["sentence"])
    caps = {}
    for name, pitch in (("cap-100", 1.0), ("cap-150", 1.5), ("cap-120", 1.2), ("cap-75", 0.75), ("cap-100-again", 1.0)):
        caps[name] = render(CAPITAL, 1.0, pitch)
        ref = want[name if name != "cap-100-again" else "cap-100"]
        bad += line(caps[name] == ref, "%s, %r at pitch %.2f (%s)" % (label, CAPITAL, pitch, name), caps[name], ref)
    bad += line(caps["cap-150"] != caps["cap-100"] and caps["cap-120"] != caps["cap-100"]
                and caps["cap-75"] != caps["cap-100"],
                "%s: the capital's raised and lowered pitch sound different from 1.0" % label, caps["cap-150"])
    bad += line(caps["cap-100-again"] == caps["cap-100"],
                "%s: 1.0 again is byte-identical to the first (the pitch came back)" % label, caps["cap-100-again"])
    return bad


def pulled(path, tmp, name):
    r = adb("exec-out", "run-as", PKG, "cat", path)
    if r.returncode or not r.stdout:
        return None
    out = os.path.join(tmp, name)
    with open(out, "wb") as f:
        f.write(r.stdout)
    return out


def speakout(a):
    """The Speak-Out, against so_voice driven directly from the device's own HEX."""
    tmp = tempfile.mkdtemp(prefix="ssi263-device-so-")
    try:
        hexf = pulled("/data/user_de/0/%s/files/unit/SPEAKOUT.HEX" % PKG, tmp, "SPEAKOUT.HEX")
        if not hexf:
            print("skip  Speak-Out: no SPEAKOUT.HEX imported on the device")
            return 0
        volume, pct = default_volume(), int(round(a.rate * 100))
        lines = []
        for name, text, rate, pitch in (("sentence", TEXT["speakout"], pct, 100), ("cap-100", CAPITAL, 100, 100),
                                        ("cap-150", CAPITAL, 100, 150), ("cap-120", CAPITAL, 100, 120),
                                        ("cap-75", CAPITAL, 100, 75)):
            r, p, off = T.speakout_settings(50, 50, rate, pitch)
            lines.append("%s 22050 %d %d 8 %d 1 1 %d 0 %s\n" % (name, r, p, volume, off, text.encode("utf-8").hex()))
        want = direct("--so-direct", hexf, lines)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return run_cases(a, "speakout", want, "Speak-Out")


def accent_mini(a):
    """The Accent-mini (built in), against am_voice driven directly from the repository's SPKEMS.DVC (the APK's)."""
    if not os.path.isfile(T.MINI_DVC):
        print("skip  Accent-mini: no firmware/aicom-accent-mini/SPKEMS.DVC here")
        return 0
    level, volume, pct = T.accent_level(), default_volume(), int(round(a.rate * 100))
    lines = []
    for name, text, rate, pitch in (("sentence", TEXT["accentmini"], pct, 100), ("cap-100", CAPITAL, 100, 100),
                                    ("cap-150", CAPITAL, 100, 150), ("cap-120", CAPITAL, 100, 120),
                                    ("cap-75", CAPITAL, 100, 75)):
        lines.append("%s 1 22050 %d 50 100 %d 1 5 %d 0 %s\n" % (name, T.on_top(50, rate), volume * level // 100,
                                                               T.accent_pitch(50, pitch) - 50,
                                                               text.encode("utf-8").hex()))
    want = direct("--am-direct", T.MINI_DVC, lines)
    return run_cases(a, "accentmini", want, "Accent-mini")


def mockingboard(a, voice="mockingboard"):
    """A Mockingboard voice ("mockingboard", "mockingboardearly"), against mb_voice driven directly from the file the
    device speaks from: its imported copy when there is one, else the APK's built-in one (the repository's local copy,
    the one build_android.sh bundles)."""
    name_ = "mockingboard-tts-early.bin" if voice == "mockingboardearly" else "mockingboard-tts-1.1.bin"
    label = "Mockingboard, early" if voice == "mockingboardearly" else "Mockingboard"
    tmp = tempfile.mkdtemp(prefix="ssi263-device-mb-")
    try:
        fw = pulled("/data/user_de/0/%s/files/unit/%s" % (PKG, name_), tmp, name_)
        if not fw:
            fw = os.path.join(T.MB_BUILTIN, name_)
            if not os.path.isfile(fw):
                print("skip  %s: none imported on the device and no local copy of %s" % (label, name_))
                return 0
        volume, pct = default_volume(), int(round(a.rate * 100))
        lines = []
        for name, text, rate, pitch in (("sentence", TEXT[voice], pct, 100), ("cap-100", CAPITAL, 100, 100),
                                        ("cap-150", CAPITAL, 100, 150), ("cap-120", CAPITAL, 100, 120),
                                        ("cap-75", CAPITAL, 100, 75)):
            lines.append("%s 22050 %d 50 %d 1 %d 0 %s\n" % (name, T.on_top(50, rate), volume,
                                                            T.step_pitch(50, pitch, T.mb_step) - 50,
                                                            text.encode("utf-8").hex()))
        want = direct("--mb-direct", fw, lines)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return run_cases(a, voice, want, label)


def device_unit(tmp, spanish):
    """The unit's files from the device's storage (a debug build: run-as), into `tmp`."""
    names = list(T.FILES["en"]) + (list(T.FILES["es"]) if spanish else [])
    for name in names:
        r = adb("exec-out", "run-as", PKG, "cat", "/data/user_de/0/%s/files/unit/%s" % (PKG, name))
        if r.returncode or not r.stdout:
            sys.exit("could not read %s from the device: %s" % (name, (r.stderr or b"").decode(errors="replace")))
        with open(os.path.join(tmp, name), "wb") as f:
            f.write(r.stdout)
    return tmp


def braille_lite(a):
    arch = "x64" if T.struct.calcsize("P") == 8 else "x86"
    lib = a.lib or (os.path.join(T.REPO, "nvda", "dist", "blazie-lib", arch, "bl.dll") if T.WINDOWS
                    else os.path.join(T.REPO, "build", "linux", "libssi263speech.so"))
    chip = a.chip or (os.path.join(T.REPO, "src", "ssi263", "_bin", arch, "ssi263.dll") if T.WINDOWS else None)
    r = adb("shell", "run-as", PKG, "ls", "/data/user_de/0/%s/files/unit" % PKG)
    if b"BL2ENG.BNS" not in (r.stdout or b""):
        print("skip  Braille Lite: no English firmware imported on the device")
        return 0
    spanish = b"BL2SPA.BNS" in (r.stdout or b"")
    set_voice(VOICE["braillelite"])
    set_run_ahead(0)
    got = render(TEXT["braillelite"], a.rate, 1.0, a.aloud)    # a fresh process: the unit's first utterance
    set_run_ahead(1)                                            # EXPERIMENTAL run ahead, as its check box
    got_ra = render(TEXT["braillelite"], a.rate, 1.0)
    if spanish:
        set_voice(1)                                            # SsiNative.SPANISH
        got_es = render(T.SPANISH, 1.0, 1.0)
    tmp = tempfile.mkdtemp(prefix="ssi263-device-test-")
    bad = 0
    try:
        # The device's own unit files (its imported firmware and the state it made from it), so the desktop reference
        # runs exactly what the phone runs: a state made by a 0.7 import is MAME's (the list's hash), one from an
        # earlier import z180emu's -- both run, and they differ by a few samples, so the inputs must be the phone's.
        data = device_unit(tmp, spanish) if not a.firmware_from_repo else T.data_folder(a.firmware, tmp)
        lib = T.load_reference(lib, chip)
        lib.blv_state_check.argtypes = [T.ctypes.c_char_p, T.ctypes.c_char_p]
        for bns, state in T.FILES["en"], T.FILES["es"]:
            if os.path.isfile(os.path.join(data, bns)):
                listed = lib.blv_state_check(os.path.join(data, bns).encode(), os.path.join(data, state).encode())
                print("info  %s %s: %s" % ("the repository's" if a.firmware_from_repo else "the device's", state,
                                           "the list's (made on MAME's Z180, 0.7)" if listed else
                                           "not the list's (made on z180emu: shipped, or imported before 0.7)"))
        ref = T.Ref(lib, data, False)
        want_pcm = ref.say(TEXT["braillelite"], T.ssip_from_percent(int(round(a.rate * 100))), 0, None,
                           default_volume())
        want = (len(want_pcm) // 2, T.fnv(want_pcm))
        bad += line(got == want, "Braille Lite through the platform TTS, rate %.2f" % a.rate, got, want)
        ra = T.Ref(lib, data, False, run_ahead=1)
        pcm = ra.say(TEXT["braillelite"], T.ssip_from_percent(int(round(a.rate * 100))), 0, None, default_volume())
        want_ra = (len(pcm) // 2, T.fnv(pcm))
        bad += line(got_ra == want_ra, "Braille Lite with run ahead on, rate %.2f" % a.rate, got_ra, want_ra)
        bad += line(got_ra != got, "Braille Lite: run ahead's audio is not the lockstep's (the setting reached it)",
                    got_ra)
        if spanish:
            pcm = T.Ref(lib, data, True).say(T.SPANISH, 0, 0, None, default_volume())
            want = (len(pcm) // 2, T.fnv(pcm))
            bad += line(got_es == want, "Braille Lite (Spanish) through the platform TTS", got_es, want)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=float, default=2.0)
    ap.add_argument("--aloud", action="store_true")
    ap.add_argument("--voice", choices=("accent", "braillelite", "speakout", "accentmini", "mockingboard", "both", "all"),
                    default="both")
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(T.REPO, "firmware", "blazie"))
    ap.add_argument("--firmware-from-repo", action="store_true",
                    help="the Braille Lite reference from --firmware's shipped state, not the device's own unit files")
    ap.add_argument("--lib", default=None)
    ap.add_argument("--chip", default=None)
    a = ap.parse_args()
    before, before_ra = saved_voice(), saved_run_ahead()
    bad = 0
    try:
        if a.voice in ("accent", "both", "all"):
            bad += accent(a)
        if a.voice in ("braillelite", "both", "all"):
            bad += braille_lite(a)
        if a.voice in ("speakout", "all"):
            bad += speakout(a)
        if a.voice in ("accentmini", "all"):
            bad += accent_mini(a)
        if a.voice in ("mockingboard", "all"):
            bad += mockingboard(a) + mockingboard(a, "mockingboardearly")
    finally:
        set_run_ahead(before_ra)                # the device's own run ahead back (-1: never set)
        set_voice(before)                       # the device's own choice back (-1: none)
        adb("shell", "am", "force-stop", PKG)
    print("device service: %s" % ("FAILED" if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
