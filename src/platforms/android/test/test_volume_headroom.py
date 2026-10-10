"""The Android engine's default volume leaves headroom: no clipped sample, the loudest peak under HEADROOM_DBFS, for
each voice.

Tomi (0.7, on the phone): the Blazie voice sat under TalkBack's own sounds.  At the desktop level (volume 100: bl_voice's
MAKEUP x 1) the voice peaks near -5 dBFS at most with its voiced RMS near -22.5 dBFS, so Android's engine volume now
runs 0-200 with a default of SsiSettings.DEFAULT_VOLUME (read from the app's source, one place): louder, still short
of clipping on these English and Spanish lines (some of them the loudest found in a 67-line sweep).

The Aicom Accent SA (the built-in voice, 2026-09-30) shares that slider: at the NVDA driver's full volume (the gain 1,
ssa_engine.h's SSA_ACCENT_LEVEL 100) it peaks at -6.0 dBFS on these lines with its speech RMS at -21.5 dBFS, within a
decibel of the Braille Lite's (-5.5, -22.5), so the default puts both beside TalkBack at the same level.  It is
measured through the app's own path (test_android_native.c --level: ssa_engine, the fresh unit, the gain).

The Aicom Accent-mini (built in, 0.7.5) takes the Accent SA's level (SSA_ACCENT_LEVEL), measured as it.  The GW Micro
Speak-Out (imported, 0.7.5; measured when firmware/gw-micro-speakout/SPEAKOUT.HEX is there) shares it too:
at the driver's full volume (so_voice's gain 1) it peaks at -5.6 dBFS on these lines with its speech RMS near
-21 dBFS, the others' level; so_voice takes the app's 0-200 as as_voice does, and the default keeps its headroom.
The Mockingboard (imported, 0.8; measured when firmware/sweet-micro-mockingboard/mockingboard-tts-1.1.bin is there)
takes it as its gain too (mb_voice.h): at the default 150 it peaks at -3.1 dBFS on these lines, a little under the
others, unclipped.

    python test_volume_headroom.py        SSI263_VOLUME_TEST_BREAK=1: volume 250 for every voice -- must fail (all clip)
                                          SSI263_VOLUME_TEST_BREAK=accent: 250 for the Accent SA alone -- must fail
"""
import math
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_android_native as T  # noqa: E402

HEADROOM_DBFS = -0.5
SETTINGS = os.path.join(T.REPO, "src", "platforms", "android", "app", "src", "main", "kotlin", "com", "ssi263speech",
                        "tts", "SsiSettings.kt")
EN = ["Hello there. This is the Braille Lite, speaking on a phone.",
      "One, two, three, four. Custom number processing, check box, checked.",
      "Settings dialog, press tab for more options.",
      "Wow! That was loud.", "WOW! LOUD CAPITALS!", "Yes? No! 1,234,567.", "Ahhhh, ooooh, eeeee.",
      "The quick brown fox jumps over the lazy dog, again and again."]
ES = ["La lluvia en Sevilla es una maravilla.", "Hola, ¿qué tal? Él está aquí, mañana a las tres y media.",
      "¡Atención! El número es 1.234.567.", "Buenos días, señoras y señores."]
# the Accent SA: the English lines, and what TalkBack says most
ACCENT = EN + ["Battery 85 percent. 3 notifications.", "Double-tap to activate. Button.", "Capital B.",
               "Settings. Network and internet. Wi-Fi, connected to Home."]


def default_volume():
    m = re.search(r"const val DEFAULT_VOLUME = (\d+)", open(SETTINGS, encoding="utf-8").read())
    if not m:
        sys.exit("DEFAULT_VOLUME not found in %s" % SETTINGS)
    return int(m.group(1))


def verdict(label, volume, lines, db, worst, clipped):
    ok = clipped == 0 and db <= HEADROOM_DBFS
    print("%s volume %d: %d lines, loudest peak %.2f dBFS (%r), %d clipped samples -- %s" % (
        label, volume, lines, db, worst[:40], clipped, "ok" if ok else "FAILED (limit %.1f dBFS, no clipping)"
        % HEADROOM_DBFS))
    return ok


def blazie(volume, firmware):
    arch = "x64" if sys.maxsize > 2 ** 32 else "x86"
    lib_path = os.path.join(T.REPO, "nvda", "dist", "blazie-lib", arch, "bl.dll")
    chip = os.path.join(T.REPO, "src", "ssi263", "_bin", arch, "ssi263.dll")
    lib = T.load_reference(lib_path, chip)
    worst, clipped, lines = (0.0, ""), 0, 0
    with tempfile.TemporaryDirectory() as tmp:
        data = T.data_folder(firmware, tmp)
        for spanish, texts in ((False, EN), (True, ES)):
            ref = T.Ref(lib, data, spanish)
            for text in texts:
                lib.blv_set(ref.v, 50, 50, 7, volume, 1)
                lib.blv_speak(ref.v, text.encode("utf-8"))
                pcm, done, peak = T.ctypes.POINTER(T.ctypes.c_short)(), T.ctypes.c_int(0), 0
                while not done.value:
                    n = lib.blv_render(ref.v, T.ctypes.byref(pcm), T.ctypes.byref(done))
                    for i in range(n):
                        v = abs(pcm[i])
                        peak = max(peak, v)
                        clipped += v >= 32767
                lines += 1
                if peak / 32768.0 > worst[0]:
                    worst = (peak / 32768.0, text)
            lib.blv_destroy(ref.v)
    db = 20 * math.log10(worst[0]) if worst[0] else -120.0
    return verdict("braille lite", volume, lines, db, worst[1], clipped)


_exe = []


def program():
    """test_android_native, the one build every run shares (test_android_native.py's build_desktop)."""
    if not _exe:
        _exe.append(T.build_desktop())
    return _exe[0]


def app_level(label, voice, data, texts, volume, extra=()):
    """Through the app's own path: test_android_native --level, the voice at the app's defaults and this volume."""
    r = subprocess.run([program(), "--level", data, T.AICOM, str(voice), str(volume)] + list(extra),
                       input="".join(t.encode("utf-8").hex() + "\n" for t in texts), capture_output=True, text=True)
    if r.returncode:
        sys.exit("test_android_native --level failed: %s" % r.stderr.strip())
    peak, clipped, worst = 0, 0, ""
    for k, p, c in re.findall(r"^level (\d+) (\d+) (\d+) ", r.stdout, re.M):
        clipped += int(c)
        if int(p) > peak:
            peak, worst = int(p), texts[int(k)]
    db = 20 * math.log10(peak / 32768.0) if peak else -120.0
    return verdict(label, volume, len(texts), db, worst, clipped)


def accent(volume):
    return app_level("accent sa", 2, ".", ACCENT, volume)


def mini(volume):
    if not os.path.isfile(T.MINI_DVC):
        print("accent-mini: skipped (no firmware/aicom-accent-mini/SPKEMS.DVC)")
        return True
    return app_level("accent-mini", 4, ".", ACCENT, volume, [T.MINI_DVC])


def speakout(volume):
    if not os.path.isfile(T.SPEAKOUT_HEX):
        print("speak-out: skipped (no firmware/gw-micro-speakout/SPEAKOUT.HEX)")
        return True
    with tempfile.TemporaryDirectory() as tmp:
        T.shutil.copy2(T.SPEAKOUT_HEX, tmp)
        return app_level("speak-out", 3, tmp, ACCENT, volume)


def mockingboard(volume):
    if not os.path.isfile(T.MB_BIN):
        print("mockingboard: skipped (no firmware/sweet-micro-mockingboard/mockingboard-tts-1.1.bin)")
        return True
    with tempfile.TemporaryDirectory() as tmp:
        T.shutil.copy2(T.MB_BIN, tmp)
        return app_level("mockingboard", 5, tmp, ACCENT, volume)


def main():
    brk = os.environ.get("SSI263_VOLUME_TEST_BREAK", "")
    firmware = os.environ.get("SSI263_FIRMWARE") or os.path.join(T.REPO, "firmware", "blazie")
    default = default_volume()
    ok = blazie(250 if brk == "1" else default, firmware)
    ok = accent(250 if brk in ("1", "accent") else default) and ok
    ok = speakout(250 if brk == "1" else default) and ok
    ok = mini(250 if brk == "1" else default) and ok
    ok = mockingboard(250 if brk == "1" else default) and ok
    print("volume headroom: %s" % ("PASS" if ok else "FAILED"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
