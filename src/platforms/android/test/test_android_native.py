"""The Android app's native part, proven without a phone: test_android_native.c (the app's front end, ssa_engine.c,
ssa_map.c and ssa_import.c, on the same chip, boards, hosts and voices), every case's PCM compared by hash.

- The Braille Lite: against bl_voice driven directly the way the speech-dispatcher module drives it
  (src/platforms/speechd/sd_ssi263.c: SSIP -100..100 through its to100), from the desktop library the NVDA add-on
  and Linux ship (bl.dll + ssi263.dll on Windows, libssi263speech.so on Linux), loaded with ctypes.  Lockstep, and
  with the EXPERIMENTAL run ahead on (blv_set_run_ahead, the app's setting), whose audio must also differ from the
  lockstep's (the run-ahead path taken).  And its number words ("Read numbers as words", the NVDA driver's "Custom
  number processing", on by default): "1,234,567", "3.5" and "$12.50" on the English unit and Spain's "1.234.567" and
  "3,5" on the Spanish one, on and off, each on a fresh unit, against bl_voice with blv_set_numbers(bl_numbers) and
  without -- from a library that has the number words (bl.dll has none): ssi263speech.dll on Windows
  (src/csrc/build_ssi263speech.py), libssi263speech.so on Linux.  On and off must also sound different (fresh units:
  by the setting alone).
- The GW Micro Speak-Out (imported; its cases run when firmware/gw-micro-speakout/SPEAKOUT.HEX is there): against
  so_voice driven directly (test_android_native --so-direct: sov_set, sov_speak, sov_render, sov_cancel on one kept
  unit, the settings computed here: the request's rate on the slider, the slider's pitch as the setting and the
  request's as a capital's PitchCommand offset, moved at least one of the box's ten steps).  so_voice itself is the
  NVDA driver byte for byte (nvda/tools/so_voice_equiv.py).  A capital's 150/120/75 % differ from 100 % and 100 %
  again is byte-identical.
- The Aicom Accent-mini (built in, Aicom's SPKEMS.DVC handed over in memory as the app does; its cases run when
  firmware/aicom-accent-mini/SPKEMS.DVC is there): against am_voice driven directly (test_android_native --am-direct:
  amv_set, amv_speak with the capital's offset, amv_render, amv_cancel on one kept unit), the settings computed here
  as the Accent SA's are.  am_voice itself is the NVDA Accent driver's "mini" voice (nvda/tools/am_voice_equiv.py).
- The two Mockingboards (Sweet Micro Systems' firmware: mockingboard-tts-1.1.bin and mockingboard-tts-early.bin,
  built into the GitHub APK and importable; each one's cases run when its file is in
  firmware/sweet-micro-mockingboard): against mb_voice driven directly on the same file (test_android_native
  --mb-direct: mbv_set, mbv_speak with the capital's offset, mbv_render, mbv_cancel on one kept unit), the settings
  computed here as the Speak-Out's are, its inflection's steps (mbv_pitch_step) for the capital's move, and the
  number words from the settings: on and off must sound different.  Their built-in copies, handed over in memory: each
  speaks from its own with no import, an imported copy wins, neither takes the other's file.  Their import: each file
  taken by its sha256, as it is; with one byte changed, or cut short, refused.  And the disk images, when
  MOCKINGBOARD_DISKS (paths.local) has them (never in the repository; skipped, and said so, without them): the
  toolkit's gives the 1.1 file, Mockingboard disk 1's the early one, in DOS and ProDOS order and out of a zip; disk 2
  and a blank 140 KB disk refused with the disk reader's reason.
- The import: GW Micro's SPEAKOUT.HEX recognised by content (Intel HEX, then the known sha256) as it is, with LF
  line endings, with a DOS end-of-file mark, and out of a speakout.zip like GW Micro's; a HEX with one digit
  changed, one whose checksums were fixed after the change (another HEX), one cut short, and other files refused;
  a Braille Lite .BNS still the Braille Lite's.
- The Aicom Accent SA (the built-in voice): against the NVDA Accent add-on's own driver, its "sa" voice on the
  desktop's C host (accent_reference.py; the built nvda/dist/accent-build and accentsa-lib), a fresh unit per case as
  the app has; the request's rate and pitch put on the driver's scales as ssa_map.c does, the pitch as a capital's
  PitchCommand.  Its front end's text (currencies, _clean, number words) is compared on TEXTS as well.  And a capital:
  a request's raised pitch (150 %, and 120 %, which ssa_accent_pitch moves to the next of the Accent's ten steps) and
  lowered one (75 %) must sound different from 100 %, and the next request at 100 % must be byte-identical to the
  first.

    python test_android_native.py                 build the desktop program, run it, compare
    python test_android_native.py --adb           also run build/android/<abi>/test_android_native on the attached
                                                  device (sh build_android.sh --test <abi> first; ANDROID_SERIAL picks
                                                  the device), pushed to /data/local/tmp and removed afterwards
    SSI263_ANDROID_TEST_BREAK=1                   the control: the program drops the request's rate (ssa_map.h), so
                                                  the "fast" cases must differ -- this run must FAIL
    SSI263_ANDROID_TEST_BREAK=accent-pitch        the Accent SA's controls (ssa_engine.h's ssa_accent_break), each
                             accent-glide         must FAIL: the request's pitch dropped; the pitch sent as a setting,
                             accent-reuse         glided to (no snap_pitch); one unit kept across utterances; the
                             accent-step          plain pitch mapping (a 120 % request on the 100 % step; the
                                                  Speak-Out's too)
                             speakout-pitch       ssa_voice_break 1: the Speak-Out's request pitch dropped
                             speakout-settings    ssa_voice_break 2: its tone, join and short pauses dropped
                             run-ahead            ssa_voice_break 3: the Braille Lite's run ahead dropped
                             numbers              ssa_voice_break 4: its number words dropped (always off)
                             mockingboard-pitch   ssa_voice_break 5: the Mockingboard's request pitch dropped
                             mockingboard-numbers ssa_voice_break 6: its number words dropped (always off)
                             mockingboard-import-ignored   7: the imported copy ignored (the built-in one used)
                             mockingboard-builtin-ignored  8: the built-in copies ignored (import only)
                             mockingboard-variant          9: either file taken for either voice
                             import-hash          ssa_import_break: any well-formed Intel HEX taken as the Speak-Out,
                                                  any file of a Mockingboard file's size as that one
                             import-dsk           ssa_import_break 2: a disk image never tried (the .dsk cases fail)

SSI263_ANDROID_TEST_ONLY=<blocks>, a comma list of bl (the Braille Lite in lockstep, its Spanish unit and the probe),
ra (run ahead), num (the number words), accent (the Accent SA and its text), so (the Speak-Out), mini (the
Accent-mini), mb (the Mockingboard), import (the Speak-Out's and the Mockingboard's import): only those blocks, on
both sides -- for the controls, so each runs what
its bug touches (the 3-minute gate).

Options: --firmware <folder> (default $SSI263_FIRMWARE, else firmware/blazie; the Spanish unit there or in its
spanish/ folder), --lib <reference library>, --chip <ssi263.dll bl.dll needs>, --numbers-lib <the number words'
reference library>, --abi <abi> (default arm64-v8a), --aicom <folder> (default firmware/aicom-accent-sa).
"""
import argparse
import ctypes
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

SRC = os.path.join(REPO, "src", "csrc")
CPP = os.path.join(REPO, "src", "platforms", "android", "app", "src", "main", "cpp")
# One program for every run: the controls choose their bug at run time (SSI263_ANDROID_TEST_BREAK), so run_tests's
# dozen runs share one build -- the first to need it builds it under a lock, the others wait and reuse it (its key:
# every source it is built from, and this file's flags).
OUT = os.path.join(REPO, "build", "android-host")
WINDOWS = sys.platform == "win32"

HELLO = "Hello there. This is the Braille Lite, speaking on a phone."
LONG = ("This is a long message for the stop test, with a comma or two, that keeps going well past the moment the "
        "harness says stop. It has a second sentence as well.")
SPANISH = "Mañana, ¿qué tal? Él está aquí."
# (name, text, SSIP rate, SSIP pitch, blocks before a cancel or None) -- the same order as the C program, one unit
# for the English cases: the unit's state carries from one utterance to the next on both sides
CASES = [
    ("default", HELLO, 0, 0, None),
    ("default-97", HELLO, 0, 0, None),
    ("fast", HELLO, 50, 0, None),          # Android 200% rate = SSIP 50
    ("slow-low", HELLO, -40, -50, None),   # the app's rate slider at 30 = SSIP -40; Android 50% pitch = SSIP -50
    ("stopped", LONG, 0, 0, 5),
    ("after-stop", "Next message.", 0, 0, None),
]
# the Braille Lite with run ahead on, on a unit of its own (the C program's ra- cases, in the same order)
RA_CASES = [
    ("ra-default", HELLO, 0, 0, None),
    ("ra-fast", HELLO, 50, 0, None),
    ("ra-stopped", LONG, 0, 0, 5),
    ("ra-after-stop", "Next message.", 0, 0, None),
]
# the number words (the C program's num- cases): each language's text on and off, each on a fresh unit
NUM_TEXTS = {"en": "It is 1,234,567 steps, 3.5 miles and $12.50.", "es": "Son 1.234.567 pasos y 3,5 kilos."}
FILES = {"en": ("BL2ENG.BNS", "bl2_2003_warm.state"), "es": ("BL2SPA.BNS", "bl2spa_fresh.state")}
AICOM = os.path.join(REPO, "firmware", "aicom-accent-sa")
SPEAKOUT_HEX = os.path.join(REPO, "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
MINI_DVC = os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
# the Mockingboards' files: local copies (never committed), imported here as the app's data folder has them, and the
# same folder handed over as the built-in copies (the APK's assets/sweet-micro)
MB_BUILTIN = os.path.join(REPO, "firmware", "sweet-micro-mockingboard")
MB_BIN = os.path.join(MB_BUILTIN, "mockingboard-tts-1.1.bin")
MB_BIN_EARLY = os.path.join(MB_BUILTIN, "mockingboard-tts-early.bin")
MB_SHA256 = "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae"    # mb_host.h's MB_SHA256
MB_SHA256S = {5: MB_SHA256, 6: "c7c049b1b61792719e21e461a2a8c25fc32c12882c81305814a3dc67af6e5835"}  # ... _EARLY
# the disk images in MOCKINGBOARD_DISKS (run_tests.py's names): the toolkit (1.1), Mockingboard disks 1 (the early
# text-to-speech) and 2 (the same program, other rules: refused)
MB_DISK_NAMES = ("Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk", "mockingboard1.dsk",
                 "mockingboard2.dsk")
SPEAKOUT_SHA256 = "1c6930c8c6aed0550bc267c14032f9195b450ed95de606f2fa9727e2b7eb1eb1"   # its README's

# The Accent-mini's cases: test_android_native.c's mini_cases, in the same order -- (name, text, the app's rate and
# pitch sliders, the request's rate and pitch percentages, volume, inflection, blocks before a stop).  One unit kept.
HELLO_M = "Hello there. This is the Accent-mini, speaking on a phone."
MINI_CASES = [
    ("m-default", HELLO_M, 50, 50, 100, 100, 100, 1, 0),
    ("m-fast", HELLO_M, 50, 50, 200, 100, 100, 1, 0),
    ("m-sliders", HELLO_M, 70, 80, 100, 100, 150, 1, 0),
    ("m-pitch-100", "B", 50, 50, 100, 100, 100, 1, 0),
    ("m-pitch-150", "B", 50, 50, 100, 150, 100, 1, 0),
    ("m-pitch-75", "B", 50, 50, 100, 75, 100, 1, 0),
    ("m-stopped", LONG, 50, 50, 100, 100, 100, 1, 8),
    ("m-after-stop", "Next message, $3.50.", 50, 50, 100, 100, 100, 1, 0),
    ("m-monotone", HELLO_M, 50, 50, 100, 100, 100, 0, 0),
]

# The Speak-Out's cases: test_android_native.c's speakout_cases, in the same order -- (name, text, the app's rate and
# pitch sliders, the request's rate and pitch percentages, volume, tone, join, short pauses, sample rate, pull size,
# blocks before a stop).  One unit across them, as the app keeps one.
HELLO_S = "Hello there. This is the Speak-Out, speaking on a phone."
SETTINGS_S = "One. Two, three! Four? Five, $3.50."
SPEAKOUT_CASES = [
    ("s-default", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-default-97", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 97, 0),
    ("s-fast", HELLO_S, 50, 50, 200, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-sliders", HELLO_S, 70, 80, 100, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-settings", SETTINGS_S, 50, 50, 100, 100, 100, 0, 0, 0, 22050, 4096, 0),
    ("s-pitch-100", "B", 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-pitch-150", "B", 50, 50, 100, 150, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-pitch-75", "B", 50, 50, 100, 75, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-pitch-120", "B", 50, 50, 100, 120, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-pitch-100-again", "B", 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-stopped", LONG, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 8),
    ("s-after-stop", "Next message.", 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0),
    ("s-volume-150", HELLO_S, 50, 50, 100, 100, 150, 8, 1, 1, 22050, 4096, 0),
    ("s-11k", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 11025, 4096, 0),
]
ROMS = ("u2.BIN", "u3.BIN", "u4.BIN")

# The Mockingboard's cases: test_android_native.c's mb_cases, in the same order -- (name, text, the app's rate and
# pitch sliders, the request's rate and pitch percentages, volume, numbers, sample rate, pull size, blocks before a
# stop).  One unit across them, as the app keeps one.
HELLO_MB = "Hello there. This is the Mockingboard, speaking on a phone."
MB_CASES = [
    ("mb-default", HELLO_MB, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-default-97", HELLO_MB, 50, 50, 100, 100, 100, 1, 22050, 97, 0),
    ("mb-fast", HELLO_MB, 50, 50, 200, 100, 100, 1, 22050, 4096, 0),
    ("mb-sliders", HELLO_MB, 70, 80, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-pitch-100", "B", 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-pitch-150", "B", 50, 50, 100, 150, 100, 1, 22050, 4096, 0),
    ("mb-pitch-75", "B", 50, 50, 100, 75, 100, 1, 22050, 4096, 0),
    ("mb-pitch-120", "B", 50, 50, 100, 120, 100, 1, 22050, 4096, 0),
    ("mb-pitch-100-again", "B", 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-num", NUM_TEXTS["en"], 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-num-off", NUM_TEXTS["en"], 50, 50, 100, 100, 100, 0, 22050, 4096, 0),
    ("mb-stopped", LONG, 50, 50, 100, 100, 100, 1, 22050, 4096, 8),
    ("mb-after-stop", "Next message.", 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("mb-volume-150", HELLO_MB, 50, 50, 100, 100, 150, 1, 22050, 4096, 0),
    ("mb-11k", HELLO_MB, 50, 50, 100, 100, 100, 1, 11025, 4096, 0),
]

# The Accent SA's cases: test_android_native.c's accent_cases, in the same order -- (name, text, the app's rate and
# pitch sliders, the request's rate and pitch percentages, the engine volume, inflection, sample rate, pull size,
# blocks before a stop).  Each is a unit of its own on both sides.
HELLO_A = "Hello there. This is the Accent SA, speaking on a phone."
LONG_A = ("This is a long message for the stop test, with a comma or two, that keeps going well past the moment the "
          "harness says stop. It has a second sentence as well.")
NUMBERS_A = "You owe $1234.50 for 3 items: 100 percent, the 21st of 1,000,000 and -2.5 degrees."
TEXT_A = "It\u2019s \u201cquoted\u201d \u2013 see ~/code\u2026 \u00a32.63, 5 \u20ac and caf\u00e9\tend \U0001F389"
CAP_A = "B"
ACCENT_CASES = [
    ("a-default", HELLO_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-default-97", HELLO_A, 50, 50, 100, 100, 100, 1, 22050, 97, 0),
    ("a-fast", HELLO_A, 50, 50, 200, 100, 100, 1, 22050, 4096, 0),
    ("a-slow-low", HELLO_A, 30, 50, 100, 50, 100, 1, 22050, 4096, 0),
    ("a-sliders", HELLO_A, 70, 80, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-numbers", NUMBERS_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-text", TEXT_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-pitch-100", CAP_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-pitch-150", CAP_A, 50, 50, 100, 150, 100, 1, 22050, 4096, 0),
    ("a-pitch-75", CAP_A, 50, 50, 100, 75, 100, 1, 22050, 4096, 0),
    ("a-pitch-120", CAP_A, 50, 50, 100, 120, 100, 1, 22050, 4096, 0),     # a raise that would land on the same step
    ("a-pitch-100-again", CAP_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-stopped", LONG_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 5),
    ("a-after-stop", "Next message.", 50, 50, 100, 100, 100, 1, 22050, 4096, 0),
    ("a-volume-150", HELLO_A, 50, 50, 100, 100, 150, 1, 22050, 4096, 0),
    ("a-monotone", HELLO_A, 50, 50, 100, 100, 100, 0, 22050, 4096, 0),
    ("a-11k", HELLO_A, 50, 50, 100, 100, 100, 1, 11025, 4096, 0),
]
# The front end's text, against the driver's (currencies, _clean, strip, _numbers): numbers and money in every shape
# the regexes treat differently, and the characters _clean maps
TEXTS = [
    "100", "1,234,567.", "3.5.", "192.168.0.1", "1st 2nd 3rd 4th 11th 12th 21st 22nd 101st 1000000th 3RD", "-5 and 5-3",
    "1,2345 and 1234,567", "1,234,567.x", "1,234.56a", "0.5 .5 00012 0", "12345678901234567890 and 1,000,000,000,000",
    "$5 $1234.50 $1,234,567.89 $.50 $5, $0012 $1234567890123456 $12,34", "room 12a b12 12_3 x.5 5.x",
    "\u00a32.63 \u20ac5 5 \u20ac 50\u00a2 \u00a51000 a\u00a33 \u00a3 alone", "caf\u00e9 \u00f1 \u00e7a \u00fc\u00f6 \u00e0\u00e8\u00e1",
    "tabs\tand\nnewlines\x07 and ~tilde~ \x7f", "  leading and trailing  ", "", "...", "\u2014 \u2013 \u2018q\u2019 \u201cw\u201d \u2026 \u00bf\u00a1",
    "The 3rd of May, 2026 at 10:30, 99.9% of 7,000 people.", "Version 2.0.1, 3.14159 and 1,000.5",
]


def fnv(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def to100(ssip):                                    # sd_ssi263.c's to100
    return max(0, min(100, (ssip + 100) // 2))


def ssip_from_percent(p):                           # ssa_map.c's
    return 0 if p <= 0 or p == 100 else max(-100, min(100, int(math.floor(50.0 * math.log2(p / 100.0) + 0.5))))


def on_top(slider, percent):                        # ssa_map.c's ssa_rate / ssa_pitch: the request on the slider
    return max(0, min(100, max(0, min(100, slider)) + to100(ssip_from_percent(percent)) - 50))


def accent_step(p):                                 # as_voice.c's accent_pitch: the driver's _accent_pitch
    p = max(0, min(100, p))
    return int(p * 5 / 50 + 0.5) if p <= 50 else 5 + int((p - 50) * 4 / 50 + 0.5)


def accent_pitch(slider, percent):                  # ssa_engine.c's ssa_accent_pitch: at least one step moved
    p, base = on_top(slider, percent), accent_step(slider)
    if percent > 100:
        while p < 100 and accent_step(p) == base:
            p += 1
    elif 0 < percent < 100:
        while p > 0 and accent_step(p) == base:
            p -= 1
    return p


def speakout_step(p):                               # so_voice.c's box_pitch: the driver's _box_pitch
    p = max(0, min(100, p))
    return int(p * 3 / 50.0 + 0.5) if p <= 50 else 3 + int((p - 50) * 6 / 50.0 + 0.5)


def step_pitch(slider, percent, step):              # ssa_engine.c's ssa_step_pitch: at least one step moved
    p, base = on_top(slider, percent), step(max(0, min(100, slider)))
    if percent > 100:
        while p < 100 and step(p) == base:
            p += 1
    elif 0 < percent < 100:
        while p > 0 and step(p) == base:
            p -= 1
    return p


def accent_level():
    """SSA_ACCENT_LEVEL, from ssa_engine.h (one place)"""
    m = re.search(r"#define SSA_ACCENT_LEVEL (\d+)", open(os.path.join(CPP, "ssa_engine.h"), encoding="utf-8").read())
    return int(m.group(1))


# ---- the reference: bl_voice, as sd_ssi263 drives it --------------------------------------------------------------
class Ref:
    def __init__(self, lib, data, spanish, run_ahead=0, numbers_fn=None):
        fw, st = FILES["es" if spanish else "en"]
        err = ctypes.create_string_buffer(256)
        self.lib = lib
        self.run_ahead = run_ahead
        self.numbers_fn = numbers_fn            # bl_numbers, from a library that has it: say(numbers=...) sets it
        self.v = lib.blv_create(os.path.join(data, fw).encode(), os.path.join(data, st).encode(), int(spanish),
                                22050.0, 1, 0, err, 256)
        if not self.v:
            sys.exit("reference boot failed: %s" % err.value)

    def say(self, text, rate, pitch, blocks, volume=100, numbers=None):
        lib = self.lib
        lib.blv_set(self.v, to100(rate), to100(pitch), 7, volume, 1)
        lib.blv_set_run_ahead(self.v, self.run_ahead)
        if numbers is not None:
            lib.blv_set_numbers(self.v, self.numbers_fn if numbers else None)
        lib.blv_speak(self.v, text.encode("utf-8"))
        pcm, done, out, n_blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], 0
        while not done.value:
            n = lib.blv_render(self.v, ctypes.byref(pcm), ctypes.byref(done))
            if n:
                out.append(ctypes.string_at(pcm, 2 * n))
                n_blocks += 1
                if blocks is not None and n_blocks == blocks and not done.value:
                    lib.blv_cancel(self.v)
                    break
        return b"".join(out)


def load_reference(lib_path, chip_path):
    """The desktop reference library.  It must be the shipping kind -- the Braille Lite on MAME's Z180 -- never a
    z180emu build (a legacy bl.dll left in nvda/dist would compare the app's MAME engine with the old core)."""
    from tools import check_no_gpl
    hits = check_no_gpl.check(lib_path)[0]
    if hits:
        sys.exit("the reference library is not the shipping MAME build: %s" % hits[0])
    if chip_path:
        ctypes.CDLL(chip_path)                      # bl.dll imports ssi263.dll: load that copy first
    lib = ctypes.CDLL(lib_path)
    lib.blv_create.restype = ctypes.c_void_p
    lib.blv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_double, ctypes.c_int,
                               ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.blv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 5
    lib.blv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.blv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.blv_cancel.argtypes = [ctypes.c_void_p]
    lib.blv_destroy.argtypes = [ctypes.c_void_p]
    lib.blv_set_run_ahead.argtypes = [ctypes.c_void_p, ctypes.c_int]
    return lib


ONLY = [b for b in os.environ.get("SSI263_ANDROID_TEST_ONLY", "").split(",") if b]


def block(name):
    """The block runs: every block, unless SSI263_ANDROID_TEST_ONLY names some."""
    return not ONLY or name in ONLY


def reference(lib, data, run_ahead=True):
    ref = Ref(lib, data, False)
    got = {}
    for name, text, rate, pitch, blocks in CASES if block("bl") else ():
        pcm = ref.say(text, rate, pitch, blocks)
        got[name] = (len(pcm) // 2, fnv(pcm))
    lib.blv_destroy(ref.v)
    ra = Ref(lib, data, False, run_ahead=1) if run_ahead and block("ra") else None
    for name, text, rate, pitch, blocks in RA_CASES if ra else ():
        pcm = ra.say(text, rate, pitch, blocks)
        got[name] = (len(pcm) // 2, fnv(pcm))
    if ra:
        lib.blv_destroy(ra.v)
    if block("bl") and all(os.path.isfile(os.path.join(data, f)) for f in FILES["es"]):
        es = Ref(lib, data, True)
        pcm = es.say(SPANISH, 0, 0, None)
        got["spanish"] = (len(pcm) // 2, fnv(pcm))
        lib.blv_destroy(es.v)
    return got


def numbers_reference(lib_path, data):
    """The num- cases: bl_voice with the number words on and off, each on a fresh unit -- from lib_path, which
    must carry bl_numbers beside bl_voice (the whole reference from that one library: bl_voice frees what bl_numbers
    returns).  None when it is not there."""
    if not os.path.isfile(lib_path):
        return None
    lib = load_reference(lib_path, None)
    if not hasattr(lib, "bl_numbers"):
        return None
    lib.blv_set_numbers.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    fn = ctypes.cast(lib.bl_numbers, ctypes.c_void_p)
    got = {}
    for lang in ("en", "es"):
        if not all(os.path.isfile(os.path.join(data, f)) for f in FILES[lang]):
            continue
        for name, on in (("num-" + lang, 1), ("num-%s-off" % lang, 0)):
            ref = Ref(lib, data, lang == "es", numbers_fn=fn)
            pcm = ref.say(NUM_TEXTS[lang], 0, 0, None, numbers=on)
            got[name] = (len(pcm) // 2, fnv(pcm))
            lib.blv_destroy(ref.v)
    return got


def numbers_heard(label, got):
    """The number words heard: on and off (each on a fresh unit) sound different, in each language spoken."""
    bad = 0
    for lang in ("en", "es"):
        on, off = got.get("num-" + lang), got.get("num-%s-off" % lang)
        if lang == "es" and on is None and off is None:
            continue
        ok = on is not None and off is not None and on[1] != off[1]
        print("%-5s %-8s num-%s differs from num-%s-off (the number words heard)" % ("ok" if ok else "FAIL", label,
                                                                                     lang, lang))
        bad += not ok
    return bad


# ---- the Accent SA's reference: the NVDA driver (accent_reference.py) -----------------------------------------------
def accent_reference():
    level = accent_level()
    speech = []
    for name, text, rate, pitch, req_rate, req_pitch, volume, inflection, sample_rate, chunk, stop in ACCENT_CASES:
        speech.append(dict(name=name, text=text, rate=on_top(rate, req_rate), offset=accent_pitch(pitch, req_pitch) - 50,
                           inflection=100 if inflection else 0, volume=volume * level // 100,
                           sample_rate=sample_rate, blocks=stop))
    tmp = tempfile.mkdtemp(prefix="ssi263-accent-ref-")
    try:
        path = os.path.join(tmp, "cases.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"speech": speech, "texts": TEXTS}, f)
        env = dict(os.environ, SSI263_ACCENT_SA_CORE="c", PYTHONIOENCODING="utf-8")
        r = subprocess.run([sys.executable, os.path.join(HERE, "accent_reference.py"), path], capture_output=True,
                           text=True, env=env)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if r.returncode:
        sys.exit("accent_reference.py failed (%d): %s" % (r.returncode, (r.stdout + r.stderr).strip()[-2000:]))
    got = parse(r.stdout)
    texts = {int(k): v for k, v in re.findall(r"^text (\d+) ([0-9a-f]*)$", r.stdout, re.M)}
    return got, texts


# ---- the Speak-Out's reference: so_voice driven directly ---------------------------------------------------------
def speakout_settings(rate, pitch, req_rate, req_pitch):
    """The app's mapping, computed here: (so_voice's rate, its pitch setting, the capital's offset)."""
    return on_top(rate, req_rate), pitch, step_pitch(pitch, req_pitch, speakout_step) - max(0, min(100, pitch))


def speakout_reference(exe, cases=None):
    lines = []
    for name, text, rate, pitch, rr, rp, volume, tone, join, short, sr, chunk, stop in cases or SPEAKOUT_CASES:
        r, p, off = speakout_settings(rate, pitch, rr, rp)
        lines.append("%s %d %d %d %d %d %d %d %d %d %s\n" % (name, sr, r, p, tone, volume, join, short, off, stop,
                                                           text.encode("utf-8").hex()))
    out = subprocess.run([exe, "--so-direct", SPEAKOUT_HEX], input="".join(lines), capture_output=True, text=True)
    if out.returncode:
        sys.exit("test_android_native --so-direct failed (%d): %s" % (out.returncode, out.stderr.strip()))
    return parse(out.stdout)


# ---- the Accent-mini's reference: am_voice driven directly ---------------------------------------------------------
def mini_reference(exe):
    level, lines = accent_level(), []
    for name, text, rate, pitch, rr, rp, volume, infl, stop in MINI_CASES:
        slider = max(0, min(100, pitch))
        lines.append("%s %d 22050 %d %d %d %d 1 5 %d %d %s\n" % (
            name, infl, on_top(rate, rr), slider, 100 if infl else 0, volume * level // 100,
            accent_pitch(pitch, rp) - slider, stop, text.encode("utf-8").hex()))
    out = subprocess.run([exe, "--am-direct", MINI_DVC], input="".join(lines), capture_output=True, text=True)
    if out.returncode:
        sys.exit("test_android_native --am-direct failed (%d): %s" % (out.returncode, out.stderr.strip()))
    return parse(out.stdout)


def mini_capitals(label, got):
    """The Accent-mini's capital (one kept unit, as the Speak-Out's: its return is the reference's, compared above)."""
    bad = 0
    for name in ("m-pitch-150", "m-pitch-75"):
        ok = got.get(name) is not None and got.get(name)[1] != (got.get("m-pitch-100") or (0, None))[1]
        print("%-5s %-8s %s sounds different from m-pitch-100" % ("ok" if ok else "FAIL", label, name))
        bad += not ok
    return bad


# ---- the Mockingboard's reference: mb_voice driven directly ------------------------------------------------------
def mb_step(p):                                     # mb_voice.c's mbv_pitch_step: its inflection 0-26 (50 = 8)
    p = max(0, min(100, p))
    return int(p * 8 / 50.0 + 0.5) if p <= 50 else 8 + int((p - 50) * 18 / 50.0 + 0.5)


def mb_reference(exe, data, cases=None, prefix="mb"):
    """The app's mapping, computed here as the Speak-Out's: the request's rate on the slider, the slider's pitch as the
    setting and the request's as a capital's offset, moved at least one of the inflection's steps.  data: the voice's
    firmware file; prefix "mbe" names the early voice's cases."""
    lines = []
    for name, text, rate, pitch, rr, rp, volume, numbers, sr, chunk, stop in cases or MB_CASES:
        slider = max(0, min(100, pitch))
        lines.append("%s %d %d %d %d %d %d %d %s\n" % (prefix + name[2:], sr, on_top(rate, rr), slider, volume,
                                                     numbers, step_pitch(pitch, rp, mb_step) - slider, stop,
                                                     text.encode("utf-8").hex()))
    out = subprocess.run([exe, "--mb-direct", data], input="".join(lines), capture_output=True, text=True)
    if out.returncode:
        sys.exit("test_android_native --mb-direct failed (%d): %s" % (out.returncode, out.stderr.strip()))
    return parse(out.stdout)


def mb_voices():
    """The Mockingboard voices whose file is here: [(the file, the cases' prefix, ssa_engine.h's index)]."""
    return [(f, p, v) for f, p, v in ((MB_BIN, "mb", 5), (MB_BIN_EARLY, "mbe", 6)) if os.path.isfile(f)]


def mb_want(exe):
    """Every Mockingboard voice's cases from mb_voice itself, each from a folder holding its file alone; its probe (a
    fresh unit's "Hello."), which its built-in copy must also say."""
    want = {}
    for f, p, v in mb_voices():
        want.update(mb_reference(exe, f, prefix=p))
        probe = mb_reference(exe, f, [("mb-probe", "Hello.", 50, 50, 100, 100, 100, 1, 22050, 4096, 0)], p)
        want[p + "-probe"] = want[p + "-builtin"] = probe[p + "-probe"]
    return want


def mb_heard(label, got, out=""):
    """Each Mockingboard's capitals (one kept unit, as the Speak-Out's: the same text twice in a row differs by the
    unit's own timing, so the pitch coming back is the reference's, compared above) and its number words: on and off
    sound different.  And the built-in copies (out: the program's output): each voice speaks from its own with no
    imported copy, an imported copy wins over it, and neither voice takes the other's file."""
    bad = 0
    for f, p, v in mb_voices():
        bad += capitals(label, got, p)
        on, off = got.get(p + "-num"), got.get(p + "-num-off")
        ok = on is not None and off is not None and on[1] != off[1]
        print("%-5s %-8s %s-num differs from %s-num-off (the number words heard)" % ("ok" if ok else "FAIL", label,
                                                                                    p, p))
        bad += not ok
        src = re.search(r"^mb-source %d (\d) (\d)$" % v, out, re.M)
        ok = bool(src) and src.groups() == ("2", "1")
        print("%-5s %-8s %s: its built-in copy used with no import, an imported one over it (source %s)" % (
            "ok" if ok else "FAIL", label, p, " then ".join(src.groups()) if src else "not reported"))
        bad += not ok
        ref = re.search(r"^mb-refused %d (\d)$" % v, out, re.M)
        ok = bool(ref) and ref.group(1) == "1"
        print("%-5s %-8s %s: the other Mockingboard's file refused as its built-in copy" % ("ok" if ok else "FAIL",
                                                                                           label, p))
        bad += not ok
    return bad


def mb_disks():
    """The Mockingboard disk images in MOCKINGBOARD_DISKS (paths.local; never in the repository): (the toolkit, disk 1,
    disk 2), each None when it is not there."""
    folder = repo_paths.lookup("MOCKINGBOARD_DISKS") or ""
    found = [os.path.join(folder, n) for n in MB_DISK_NAMES]
    return tuple(p if folder and os.path.isfile(p) else None for p in found)


def to_prodos(dos):
    """A DOS-order disk image in ProDOS order (mb_dsk.c's PRODOS_HALVES, test_mb_dsk.c's to_prodos)."""
    halves = ((0, 14), (13, 12), (11, 10), (9, 8), (7, 6), (5, 4), (3, 2), (1, 15))
    po = bytearray(len(dos))
    for t in range(35):
        for b in range(8):
            for h in range(2):
                at, frm = (t * 8 + b) * 512 + h * 256, (t * 16 + halves[b][h]) * 256
                po[at:at + 256] = dos[frm:frm + 256]
    return bytes(po)


def mb_import_cases(exe, tmp):
    """Each Mockingboard file as a user may bring it: taken by its sha256 (5: the 1.1 file, 6: the early one), written
    as it is; changed or cut, refused.  And the disk images, when MOCKINGBOARD_DISKS has them: the toolkit's gives the
    1.1 file, Mockingboard disk 1's the early one (DOS or ProDOS order, alone or in a zip, at its root or in a folder),
    written as a .bin import writes it; disk 2 and a blank disk refused with the disk reader's reason."""
    import hashlib
    import io
    import zipfile
    bad = 0

    def zipped(name, blob):
        zbuf = io.BytesIO()
        with zipfile.ZipFile(zbuf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(name, blob)
        with zipfile.ZipFile(io.BytesIO(zbuf.getvalue())) as z:
            return z.read(name)
    cases = []
    for path, code, label in ((MB_BIN, 5, "mockingboard-tts-1.1.bin"), (MB_BIN_EARLY, 6, "mockingboard-tts-early.bin")):
        if not os.path.isfile(path):
            continue
        data = open(path, "rb").read()
        flipped = bytearray(data)
        flipped[0x800] ^= 0x01
        cases += [(label, data, code, True),
                  ("a renamed copy of %s (speech.dat)" % label, data, code, True),
                  ("a zip's %s" % label, zipped("mb/" + label, data), code, True),
                  ("%s with one byte changed" % label, bytes(flipped), -1, None),
                  ("%s cut short" % label, data[:-1], -1, None)]
    toolkit, disk1, disk2 = mb_disks()
    if toolkit:
        dsk = open(toolkit, "rb").read()
        cases += [("the toolkit's .dsk", dsk, 5, True),
                  ("the toolkit's .dsk in ProDOS order (.po)", to_prodos(dsk), 5, True),
                  ("a zip's toolkit .dsk", zipped("toolkit.dsk", dsk), 5, True),
                  ("a blank 140 KB disk", bytes(len(dsk)), -7, None)]
    else:
        print("skip  the Mockingboard's disk import: no toolkit .dsk in MOCKINGBOARD_DISKS (paths.local)")
    if disk1:
        dsk = open(disk1, "rb").read()
        cases += [("Mockingboard disk 1's .dsk", dsk, 6, True),
                  ("Mockingboard disk 1's .dsk in ProDOS order (.po)", to_prodos(dsk), 6, True),
                  ("a zip's Mockingboard disk 1 .dsk, in a folder", zipped("apple/mockingboard1.dsk", dsk), 6, True)]
    if disk2:
        cases.append(("Mockingboard disk 2 (%s)" % os.path.basename(disk2), open(disk2, "rb").read(), -7, None))
    for name, blob, code, want in cases:
        src, out = os.path.join(tmp, "import.in"), os.path.join(tmp, "import.out")
        with open(src, "wb") as f:
            f.write(blob)
        if os.path.exists(out):
            os.remove(out)
        r = subprocess.run([exe, "--import", src, out], capture_output=True, text=True)
        m = re.match(r"^import (-?\d+) (.*)$", r.stdout.strip())
        got = int(m.group(1)) if m else None
        written = open(out, "rb").read() if os.path.exists(out) else None
        ok = got == code
        if ok and want:                      # the known file of that voice, whatever its source
            ok = written is not None and hashlib.sha256(written).hexdigest() == MB_SHA256S[code]
        if ok and code < 0:
            ok = written is None
        print("%-5s import   %s: %s" % ("ok" if ok else "FAIL", name, m.group(2) if m else r.stdout + r.stderr))
        bad += not ok
    return bad


# ---- the import: GW Micro's SPEAKOUT.HEX by content -----------------------------------------------------------------
def import_cases(exe, tmp):
    """(name, bytes, the code wanted, the bytes wanted in out or None): what a user may bring."""
    import hashlib
    import io
    import zipfile
    bad = 0
    hexdata = open(SPEAKOUT_HEX, "rb").read()
    lines = hexdata.split(b"\r\n")
    # one data digit changed (its checksum now wrong), and the same with the checksum put right: another HEX
    k = next(i for i, ln in enumerate(lines) if ln.startswith(b":20") and len(ln) > 20)
    rec = bytearray(lines[k])
    rec[12] = ord("0") if rec[12] != ord("0") else ord("1")
    flipped = b"\r\n".join(lines[:k] + [bytes(rec)] + lines[k + 1:])
    body = bytes.fromhex(rec[1:-2].decode())
    fixed = bytes(rec[:-2]) + b"%02X" % ((-sum(body)) & 0xFF)
    other = b"\r\n".join(lines[:k] + [fixed] + lines[k + 1:])
    cut = b"\r\n".join(lines[:len(lines) // 2]) + b"\r\n"
    # GW Micro's speakout.zip: the HEX among the package's other files; each member judged as the app judges it
    zbuf = io.BytesIO()
    with zipfile.ZipFile(zbuf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("README.TXT", b"Speak-Out firmware update.\r\n")
        z.writestr("SPEAKOUT.HEX", hexdata)
        z.writestr("UPDATE.EXE", b"MZ" + bytes(5000))
    with zipfile.ZipFile(io.BytesIO(zbuf.getvalue())) as z:
        members = {n: z.read(n) for n in z.namelist()}
    bns = open(os.path.join(tmp, "BL2ENG.BNS"), "rb").read()
    cases = [("SPEAKOUT.HEX", hexdata, 3, hexdata),
             ("the HEX with LF line endings", hexdata.replace(b"\r\n", b"\n"), 3, hexdata),
             ("the HEX with a DOS end-of-file mark", hexdata + b"\x1a", 3, hexdata),
             ("a renamed copy (firmware.txt)", hexdata, 3, hexdata),
             ("speakout.zip's SPEAKOUT.HEX", members["SPEAKOUT.HEX"], 3, hexdata),
             ("speakout.zip's README.TXT", members["README.TXT"], -1, None),
             ("speakout.zip's UPDATE.EXE", members["UPDATE.EXE"], -1, None),
             ("a HEX with one digit changed (its checksum wrong)", flipped, -5, None),
             ("another HEX (the change with its checksum put right)", other, -5, None),
             ("a HEX cut short", cut, -5, None),
             ("a text file", b"Hello there.\r\nNot firmware.\r\n", -1, None),
             ("BL2ENG.BNS (the Braille Lite's)", bns, 0, None)]
    for name, data, code, want in cases:
        src, out = os.path.join(tmp, "import.in"), os.path.join(tmp, "import.out")
        with open(src, "wb") as f:
            f.write(data)
        if os.path.exists(out):
            os.remove(out)
        r = subprocess.run([exe, "--import", src, out], capture_output=True, text=True)
        m = re.match(r"^import (-?\d+) (.*)$", r.stdout.strip())
        got = int(m.group(1)) if m else None
        ok = got == code
        written = open(out, "rb").read() if os.path.exists(out) else None
        if ok and want is not None:
            ok = written == want and hashlib.sha256(written).hexdigest() == SPEAKOUT_SHA256
        if ok and code < 0:
            ok = written is None
        print("%-5s import   %s: %s" % ("ok" if ok else "FAIL", name, m.group(2) if m else r.stdout + r.stderr))
        bad += not ok
    return bad


# ---- the program ------------------------------------------------------------------------------------------------
def source_key():
    """What the program is built from: every C/C++ file under src/csrc and the app's cpp folder, the test program and
    this file (its flags)."""
    import hashlib
    h = hashlib.sha256()
    files = [os.path.abspath(__file__), os.path.join(HERE, "test_android_native.c")]
    for top in (SRC, CPP):
        for d, dirs, names in os.walk(top):
            dirs.sort()
            files += [os.path.join(d, n) for n in sorted(names)
                      if os.path.splitext(n)[1].lower() in (".c", ".h", ".cpp", ".hpp", ".ipp", ".hxx", ".inc")]
    for f in files:
        h.update(f.encode("utf-8", "replace"))
        with open(f, "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def build_desktop():
    """test_android_native, built once for every run (see OUT): reused when its key matches, else built under a lock."""
    import time
    exe = os.path.join(OUT, "test_android_native" + (".exe" if WINDOWS else ""))
    stamp, lock = os.path.join(OUT, "key.txt"), OUT + ".lock"
    key = source_key()

    def built():
        try:
            return os.path.isfile(exe) and open(stamp).read() == key
        except OSError:
            return False
    if built():
        return exe
    os.makedirs(os.path.dirname(lock), exist_ok=True)
    deadline = time.time() + 600
    while True:
        try:
            os.mkdir(lock)                         # atomic: one builder at a time
            break
        except FileExistsError:
            try:
                if time.time() - os.path.getmtime(lock) > 600:
                    os.rmdir(lock)                 # a builder that died
                    continue
            except OSError:
                pass
            if time.time() > deadline:
                sys.exit("waited 10 minutes for %s" % lock)
            time.sleep(0.5)
    try:
        if not built():                            # another run may have built it meanwhile
            if os.path.exists(stamp):
                os.remove(stamp)
            build_into(exe)
            with open(stamp, "w") as f:
                f.write(key)
    finally:
        os.rmdir(lock)
    return exe


def build_into(exe):
    """test_android_native with the desktop compiler, the flags of build_linux.sh / build_board.py."""
    if WINDOWS:
        bindir = repo_paths.bin_dir("W64DEVKIT")
        cc, cxx = os.path.join(bindir, "gcc.exe"), os.path.join(bindir, "g++.exe")
        env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    else:
        cc, cxx, env = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++"), None
    os.makedirs(OUT, exist_ok=True)
    chip = ["-O2", "-std=c99", "-ffp-contract=off"]
    # the Braille Lite's board on MAME's Z180, as the shipping libraries (bl.dll, libssi263speech.so, the app's)
    board = ["-O3", "-std=gnu89", "-ffp-contract=off", "-w", "-DBL_Z180_MAME", "-I" + os.path.join(SRC, "blazie"),
             "-I" + os.path.join(SRC, "cpu"), "-I" + SRC]
    z180 = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-I" + os.path.join(SRC, "cpu"),
            "-I" + SRC]
    front = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-I" + SRC, "-I" + CPP]
    mame = ["-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-Wall", "-Wno-sign-compare",
            "-I" + os.path.join(SRC, "cpu"), "-I" + SRC]
    accent = ["-O2", "-std=gnu89", "-ffp-contract=off", "-Wall", "-I" + os.path.join(SRC, "cpu"),
              "-I" + os.path.join(SRC, "accentsa"), "-I" + SRC]
    speakout = ["-O2", "-std=gnu89", "-ffp-contract=off", "-Wall", "-I" + os.path.join(SRC, "cpu"),
                "-I" + os.path.join(SRC, "speakout"), "-I" + SRC]
    asa, spk = os.path.join(SRC, "accentsa"), os.path.join(SRC, "speakout")
    units = [(chip, os.path.join(SRC, "ssi263.c")), (chip, os.path.join(SRC, "ssi263dsp.c")),
             (z180, os.path.join(SRC, "cpu", "z180_mame.cpp")), (z180, os.path.join(SRC, "cpu", "z180_asci.cpp"))] + \
            [(board, os.path.join(SRC, "blazie", n + ".c")) for n in ("bl_board", "flash29", "bl_serial", "bl_idle", "bl_clock",
                                                                     "bl_host", "bl_voice", "bl_firmware")] + [
             (mame, os.path.join(SRC, "cpu", "i8085_mame.cpp")), (accent, os.path.join(asa, "as_board.c")),
             (accent, os.path.join(asa, "as_usart.c")), (accent, os.path.join(asa, "as_host.c")),
             (front, os.path.join(asa, "as_voice.c")), (front, os.path.join(SRC, "numwords.c")),
             (front, os.path.join(SRC, "accent_text.c")),
             (mame, os.path.join(SRC, "cpu", "v40_mame.cpp"))] + \
            [(speakout, os.path.join(spk, n + ".c")) for n in ("so_board", "so_icu", "so_scu", "so_hex", "so_host")] + [
             (front, os.path.join(spk, "so_voice.c")),
             (front + ["-I" + os.path.join(SRC, "blazie")], os.path.join(SRC, "blazie", "bl_numbers.c")),
             (front, os.path.join(SRC, "numwords_es.c")),
             (front, os.path.join(CPP, "ssa_map.c")), (front, os.path.join(HERE, "test_android_native.c"))]
    # the voices' table (src/csrc/voices.c) with the engines in the tree, as build_android.sh builds it (and the app's
    # engine and import with them: its Mockingboard pitch steps, its disk reader)
    have = ["-DSSV_HAVE_SPEAKOUT"]
    if os.path.isfile(os.path.join(SRC, "accentmini", "am_voice.c")):
        have.append("-DSSV_HAVE_ACCENTMINI")
        am = accent + ["-I" + os.path.join(SRC, "pc86")]
        units += [(mame, os.path.join(SRC, "cpu", "i86_mame.cpp")), (am, os.path.join(SRC, "pc86", "pc86.c")),
                  (am, os.path.join(SRC, "accentmini", "am_host.c")), (am, os.path.join(SRC, "accentmini", "am_voice.c"))]
    if os.path.isfile(os.path.join(SRC, "mockingboard", "mb_voice.c")):
        have.append("-DSSV_HAVE_MOCKINGBOARD")
        mb = ["-O2", "-std=gnu99", "-ffp-contract=off", "-w", "-I" + os.path.join(SRC, "cpu"),
              "-I" + os.path.join(SRC, "mockingboard"), "-I" + SRC]
        units += [(mb, os.path.join(SRC, "cpu", "m6502.c"))] + \
                 [(mb, os.path.join(SRC, "mockingboard", n + ".c")) for n in ("mb_board", "mb_host", "mb_voice",
                                                                                         "mb_dsk")]
    units += [(front + have, os.path.join(SRC, "voices.c")), (front + have, os.path.join(CPP, "ssa_engine.c")),
              (front + have, os.path.join(CPP, "ssa_import.c"))]

    def compile_one(unit):
        flags, src = unit
        obj = os.path.join(OUT, os.path.splitext(os.path.basename(src))[0] + ".o")
        r = subprocess.run([cxx if src.endswith(".cpp") else cc] + flags + ["-c", "-o", obj, src], env=env,
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("compiling %s failed:\n%s" % (os.path.basename(src), (r.stdout + r.stderr)[-3000:]))
        return obj
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 2)) as pool:   # every unit at once: the gate's minutes
        objs = list(pool.map(compile_one, units))
    link = ["-static", "-static-libstdc++", "-static-libgcc"] if WINDOWS else ["-lm"]
    subprocess.run([cxx, "-o", exe] + objs + link, check=True, env=env)
    return exe


def parse(text):
    got = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] != "text":
            got[parts[0]] = (int(parts[1]), parts[2])
    return got


def run_desktop(exe, data, aicom):
    # the data folder holds the Mockingboards' files as imported ones, and is also where the built-in copies are read
    r = subprocess.run([exe, data, aicom, MINI_DVC if os.path.isfile(MINI_DVC) else "-"] +
                       ([data] if mb_voices() else []), capture_output=True, text=True)
    if r.returncode:
        sys.exit("test_android_native failed (%d): %s" % (r.returncode, r.stderr.strip()))
    got = parse(r.stdout)
    got["_out"] = r.stdout
    return got


def run_texts(exe):
    r = subprocess.run([exe, "--texts"], input="".join(t.encode("utf-8").hex() + "\n" for t in TEXTS),
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("test_android_native --texts failed (%d): %s" % (r.returncode, r.stderr.strip()))
    return {int(k): v for k, v in re.findall(r"^text (\d+) ([0-9a-f]*)$", r.stdout, re.M)}


def run_adb(abi, data, aicom):
    adb = shutil.which("adb") or os.path.join(os.environ.get("ANDROID_HOME", ""), "platform-tools", "adb")
    exe = os.path.join(REPO, "build", "android", abi, "test_android_native")
    if not os.path.isfile(exe):
        sys.exit("no %s: sh build_android.sh --test %s" % (exe, abi))
    where = "/data/local/tmp/ssi263-android-test"
    subprocess.run([adb, "shell", "rm -rf %s && mkdir -p %s" % (where, where)], check=True)
    try:
        for f in [exe] + [os.path.join(data, n) for n in os.listdir(data)]:
            subprocess.run([adb, "push", f, where + "/"], check=True, capture_output=True)
        subprocess.run([adb, "shell", "mkdir -p %s/aicom" % where], check=True)
        for n in ROMS:
            subprocess.run([adb, "push", os.path.join(aicom, n), where + "/aicom/"], check=True, capture_output=True)
        mini = " -"
        if os.path.isfile(MINI_DVC):
            subprocess.run([adb, "push", MINI_DVC, where + "/aicom/"], check=True, capture_output=True)
            mini = " aicom/SPKEMS.DVC"
        brk = os.environ.get("SSI263_ANDROID_TEST_BREAK", "")
        r = subprocess.run([adb, "shell", "cd %s && chmod 755 test_android_native && SSI263_ANDROID_TEST_BREAK=%s "
                            "./test_android_native . aicom%s%s" % (where, brk, mini, " ." if mb_voices() else "")],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("on the device, test_android_native failed (%d): %s" % (r.returncode, r.stderr.strip()))
        got = parse(r.stdout)
        got["_out"] = r.stdout
        return got
    finally:
        subprocess.run([adb, "shell", "rm -rf %s" % where], capture_output=True)


def data_folder(firmware, tmp):
    """The units' files in one folder, as the app has them (English, Spanish when both of its files exist, and the
    Speak-Out's HEX and the Mockingboard's file when they are in the repository's firmware folder)."""
    for f in FILES["en"]:
        shutil.copy2(os.path.join(firmware, f), tmp)
    for f in (SPEAKOUT_HEX, MB_BIN, MB_BIN_EARLY):
        if os.path.isfile(f):
            shutil.copy2(f, tmp)
    for d in (firmware, os.path.join(firmware, "spanish")):
        if all(os.path.isfile(os.path.join(d, f)) for f in FILES["es"]):
            for f in FILES["es"]:
                shutil.copy2(os.path.join(d, f), tmp)
            break
    return tmp


def compare(label, got, want):
    bad = 0
    for name in want:
        g = got.get(name)
        ok = g == want[name]
        print("%-5s %-8s %-17s %s" % ("ok" if ok else "FAIL", label, name,
                                     "%d samples %s" % want[name] if ok else "got %s, reference %s" % (g, want[name])))
        bad += not ok
    return bad


def capitals(label, got, p="a"):
    """A capital's pitch (TalkBack raises the request's pitch for its own utterance): heard, and gone after it.  The
    Speak-Out ("s") keeps one unit, as the NVDA driver keeps one box: the same text twice in a row already differs a
    little (the box's own timing carries over), so its pitch coming back is the reference's -- so_voice itself, given
    no offset -- matched byte for byte above, not a repeat of the first.  The Mockingboard ("mb") keeps one unit too,
    and is checked the same way, as is the early one ("mbe")."""
    bad = 0
    base, again = got.get(p + "-pitch-100"), got.get(p + "-pitch-100-again")
    for name, other in (("-pitch-150", "-pitch-100"), ("-pitch-75", "-pitch-100"), ("-pitch-150", "-pitch-75"),
                        ("-pitch-120", "-pitch-100")):
        name, other = p + name, p + other
        ok = got.get(name) is not None and got.get(name)[1] != (got.get(other) or (0, None))[1]
        print("%-5s %-8s %s sounds different from %s" % ("ok" if ok else "FAIL", label, name, other))
        bad += not ok
    if p in ("s", "mb", "mbe"):
        return bad
    ok = base is not None and base == again
    print("%-5s %-8s %s-pitch-100-again = %s-pitch-100, byte for byte (the pitch came back)" % (
        "ok" if ok else "FAIL", label, p, p))
    return bad + (not ok)


def run_ahead_taken(label, got):
    """The run-ahead cases took the run-ahead path: their audio is not the lockstep's."""
    ok = got.get("ra-default") is not None and got.get("ra-default") != got.get("default")
    print("%-5s %-8s ra-default differs from default (run ahead taken, not the lockstep)" % ("ok" if ok else "FAIL",
                                                                                              label))
    return not ok


def compare_texts(got, want):
    bad = 0
    for i, t in enumerate(TEXTS):
        ok = got.get(i) == want.get(i)
        if not ok:
            print("FAIL text     %r: C %r, driver %r" % (t, bytes.fromhex(got.get(i, "")).decode("latin-1"),
                                                       bytes.fromhex(want.get(i, "")).decode("latin-1")))
        bad += not ok
    print("%-5s text     %d of %d texts as the driver sends them" % ("ok" if not bad else "FAIL", len(TEXTS) - bad,
                                                                    len(TEXTS)))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie"))
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    ap.add_argument("--lib", default=os.path.join(REPO, "nvda", "dist", "blazie-lib", arch, "bl.dll") if WINDOWS
                    else os.path.join(REPO, "build", "linux", "libssi263speech.so"))
    ap.add_argument("--chip", default=os.path.join(REPO, "src", "ssi263", "_bin", arch, "ssi263.dll") if WINDOWS
                    else None)
    ap.add_argument("--numbers-lib", default=os.path.join(REPO, "build", "win", arch, "ssi263speech.dll") if WINDOWS
                    else os.path.join(REPO, "build", "linux", "libssi263speech.so"))
    ap.add_argument("--adb", action="store_true")
    ap.add_argument("--abi", default="arm64-v8a")
    ap.add_argument("--aicom", default=AICOM)
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ssi263-android-test-")
    try:
        data = data_folder(a.firmware, tmp)
        want = reference(load_reference(a.lib, a.chip), data) if block("bl") or block("ra") else {}
        if block("bl"):
            want["probe"] = want["default"]        # ssa_probe: a fresh unit speaks the first case as it did
        numbers = block("num") and numbers_reference(a.numbers_lib, data)
        if numbers:
            want.update(numbers)
        elif block("num"):
            print("skip  the number words: no bl_numbers in %s (python src/csrc/build_ssi263speech.py)" % a.numbers_lib)
        accent_want, text_want = accent_reference() if block("accent") else ({}, {})
        want.update(accent_want)
        exe = build_desktop()
        speakout = os.path.isfile(SPEAKOUT_HEX)
        if speakout and block("so"):
            want.update(speakout_reference(exe))
            # ssa_probe on the Speak-Out: a fresh unit says the import's "Hello." at the defaults
            want["s-probe"] = speakout_reference(exe, [("s-probe", "Hello.", 50, 50, 100, 100, 100, 8, 1, 1, 22050,
                                                        4096, 0)])["s-probe"]
        elif not speakout:
            print("skip  the Speak-Out: no firmware/gw-micro-speakout/SPEAKOUT.HEX")
        mini = os.path.isfile(MINI_DVC)
        if mini and block("mini"):
            want.update(mini_reference(exe))
        elif not mini:
            print("skip  the Accent-mini: no firmware/aicom-accent-mini/SPKEMS.DVC")
        mb = bool(mb_voices())
        if mb and block("mb"):
            want.update(mb_want(exe))
        for f, what in ((MB_BIN, "Mockingboard"), (MB_BIN_EARLY, "early Mockingboard")):
            if not os.path.isfile(f):
                print("skip  the %s: no firmware/sweet-micro-mockingboard/%s" % (what, os.path.basename(f)))

        def checks(label, got):
            bad = compare(label, got, want)
            if block("accent"):
                bad += capitals(label, got)
            if block("bl") and block("ra"):
                bad += run_ahead_taken(label, got)
            if numbers:
                bad += numbers_heard(label, got)
            if speakout and block("so"):
                bad += capitals(label, got, "s")
            if mini and block("mini"):
                bad += mini_capitals(label, got)
            if mb and block("mb"):
                bad += mb_heard(label, got, got.get("_out", ""))
            return bad
        bad = 0
        if want:
            bad += checks("desktop", run_desktop(exe, data, a.aicom))
        if block("accent"):
            bad += compare_texts(run_texts(exe), text_want)
        if speakout and block("import"):
            bad += import_cases(exe, data)
        if (mb or any(mb_disks())) and block("import"):
            bad += mb_import_cases(exe, data)
        if a.adb:
            bad += checks("device", run_adb(a.abi, data, a.aicom))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAILED: %d case(s) differ" % bad if bad else "the app's native part speaks as the reference, byte for byte")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
