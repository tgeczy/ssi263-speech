"""The firmware import's native part, on the desktop: what the Android app does with a file a user brings
(src/csrc/blazie/bl_firmware.c: only the releases on its list) and the state it makes from the firmware (bl_state.c),
through the desktop library (bl.dll + ssi263.dll on Windows, libssi263speech.so on Linux), loaded with ctypes.

    python test_import_native.py            every case; the generated states must be the listed ones, byte for byte
                                            (MAME-made, 0.7; the desktop's shipped states, made on z180emu, are
                                            refused), and speak within a few samples of the shipped ones
                                            (test_android_native.py's cases, through bl_voice); the progress
                                            blv_make_state reports must come steadily through each recipe
    SSI263_IMPORT_TEST_BREAK=1              control: the English warm reset holds the wrong chord (bl_state.h), so
                                            the English state and its speech must differ -- this run must FAIL
                                            (English only: Spanish takes 1150 million instructions)
    SSI263_IMPORT_TEST_BREAK=hash           control: the list dropped (bl_firmware.h's blv_firmware_break), so an
                                            image not on it is taken -- the unknown-release case must FAIL
    SSI263_IMPORT_TEST_BREAK=lead           control: the speech comparison's leading bound dropped (same_speech), so
                                            a delay past it is taken for the same speech -- its control must FAIL

Every fixture is made here, at test time, from the unit's files in the firmware folder (--firmware, default
$SSI263_FIRMWARE, else firmware/blazie): BL2ENG.BNS + bl2_2003_warm.state, spanish/BL2SPA.BNS + bl2spa_fresh.state,
tns/TNSENG.TNS (a Type 'n Speak image: Blazie's notice, but not a Braille Lite the voice can run),
once2000/BL2ENG.BNS (ONCE's September 2000 English; its cases are skipped, and said so, when it is not there), and
0.8's units -- german/Lite2000.BNS, italian/Lite2000.BNS, bns2000/BS2SLL.BNS, each with its state -- each imported
as its language, its state made with that language's recipe (blv_firmware_recipe: the Slovak unit's is the
English one) into the app's flat names and checked against the list and the firmware folder's, then made to speak
(skipped, and said so, when not there).  Nothing is kept.
"""
import argparse
import ctypes
import hashlib
import os
import random
import shutil
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, HERE)
import test_android_native as TAN  # noqa: E402

WINDOWS = sys.platform == "win32"
BREAK = os.environ.get("SSI263_IMPORT_TEST_BREAK", "")
BREAK_STATE = bool(BREAK) and BREAK not in ("hash", "lead")
BREAK_HASH = BREAK == "hash"
ENGLISH, SPANISH, NONE, REFUSED, WRITE, UNKNOWN = 0, 1, -1, -2, -3, -4
GERMAN, ITALIAN, SLOVAK = 2, 3, 4            # bl_firmware.h's BLV_FW_* (languages, not the app's voices: 7, 8, 9)
NAMES = {ENGLISH: "English", SPANISH: "Spanish", GERMAN: "German", ITALIAN: "Italian", SLOVAK: "Slovak",
         NONE: "none", REFUSED: "refused", WRITE: "write", UNKNOWN: "unknown"}
LABELS = ["Braille Lite English: the June 5, 2003 revision",
          "Braille Lite English: ONCE's September 20, 2000 revision",
          "Braille Lite Spanish: ONCE's September 20, 2000 revision",
          "Braille Lite German: the September 20, 2000 revision",
          "Braille Lite Italian: the September 20, 2000 revision",
          "Braille 'n Speak 2000 Slovak"]
LANGUAGES = (ENGLISH, ENGLISH, SPANISH, GERMAN, ITALIAN, SLOVAK)
# blv_firmware_recipe: each language's state recipe (bl_state.h's BLV_STATE_*): the Slovak unit's is the English one
RECIPES = {ENGLISH: 0, SPANISH: 1, GERMAN: 2, ITALIAN: 3, SLOVAK: 0}
# 0.8's units: (language, the app's voice key in test_android_native's FILES, the label)
NEW = [(GERMAN, "de", LABELS[3]), (ITALIAN, "it", LABELS[4]), (SLOVAK, "sk", LABELS[5])]
CHUNK = 10 * 1000 * 1000                     # bl_state.c's: instructions between progress reports
# by recipe: German and Italian take the English runs (their yes in their own language), the Slovak unit the English
RECIPE = {0: 150 * 1000 * 1000, 1: 1150 * 1000 * 1000, 2: 150 * 1000 * 1000, 3: 150 * 1000 * 1000}
PROGRESS = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_double)

bad = 0


def result(ok, what, detail=""):
    global bad
    print("%-4s %s%s" % ("ok" if ok else "FAIL", what, (": " + detail) if detail else ""))
    bad += not ok


# The speech from a generated state against the shipped state's (made on z180emu: other bytes, so a few samples
# apart).  Equal lengths within SAME_SAMPLES pass as before; otherwise the speech must be the same behind ONE bounded
# leading shift, its end and everything inside it still compared (Astra, Reply 269: no trailing trim, no stretching,
# no per-word realignment; a relative-peak span alone could hide a quiet first or last phoneme).  The bound is the
# voice's own: bl_voice.c keeps LEAD_PREROLL samples before the first sound but never reaches back before the first
# sounding block, so two renders can open with up to that many samples' different silence.  Seen first in 0.9.6: with
# the chip's zero-volume prime silent, slow-low's two states open 182 samples apart (the sound span within 25).
SAME_SAMPLES = 64
LEAD_MAX = 220                               # bl_voice.c LEAD_PREROLL, at the reference's 22050 Hz
ENV = 220                                    # 10-ms envelope windows
FLOOR_DB = -50.0                             # below this (re the shipped peak) in both: silence, not compared
DIFF_DB = 10.0                               # a window differs when its level is this far off ...
RUN_MAX = 2                                  # ... and more than two in a row (30 ms) are a changed sound
CUT = 882                                    # the controls' edits: 40 ms, shorter than any phoneme
BREAK_LEAD = BREAK == "lead"                 # control: the leading bound dropped, so an excessive delay passes


def same_speech(a_bytes, b_bytes):
    """(verdict, detail): b is a's speech behind at most one leading shift of LEAD_MAX samples"""
    import numpy as np
    a = np.frombuffer(a_bytes, dtype="<i2").astype(float)
    b = np.frombuffer(b_bytes, dtype="<i2").astype(float)
    if not len(a) or not len(b):
        return False, "no speech"
    peak = np.max(np.abs(a)) or 1.0
    lead_max = 2000 if BREAK_LEAD else LEAD_MAX
    # the one shift: b opening s samples later than a (s < 0: earlier), from 5-ms |x| envelopes of the first 0.3 s
    k = np.ones(110) / 110.0
    ea, eb = np.convolve(np.abs(a), k, "same"), np.convolve(np.abs(b), k, "same")
    span = min(6615, len(a) - lead_max, len(b) - lead_max)
    if span < 1000:
        return False, "too short to align"
    best = min(range(-lead_max, lead_max + 1), key=lambda s: np.sum(np.abs(
        ea[max(0, -s):max(0, -s) + span] - eb[max(0, s):max(0, s) + span])))
    a2, b2 = a[max(0, -best):], b[max(0, best):]
    ends = len(b2) - len(a2)
    detail = "leading shift %+d samples (bound %d), aligned ends %+d" % (best, lead_max, ends)
    if abs(ends) > SAME_SAMPLES:
        return False, detail + " -- the ends differ"
    n = min(len(a2), len(b2)) // ENV

    def env_db(x):
        return 20 * np.log10(np.maximum(np.sqrt(np.mean(x[:n * ENV].reshape(n, ENV) ** 2, axis=1)), 1e-9) / peak)
    da, db = env_db(a2), env_db(b2)
    off = (np.maximum(da, db) > FLOOR_DB) & (np.abs(da - db) > DIFF_DB)
    run = longest = 0
    for v in off:
        run = run + 1 if v else 0
        longest = max(longest, run)
    detail += ", %d of %d 10-ms windows over %.0f dB apart, longest run %d" % (
        int(off.sum()), n, DIFF_DB, longest)
    return longest <= RUN_MAX, detail


def comparison_controls(a_bytes):
    """same_speech's decisions on the shipped audio itself, before it judges a real pair: unchanged audio and added
    leading silence within the bound pass; a removed or silenced head or tail, an internal deletion (also with its
    length made up at the end) and a leading delay past the bound fail"""
    import numpy as np
    a = np.frombuffer(a_bytes, dtype="<i2")
    loud = np.nonzero(np.abs(a.astype(float)) > 0.01 * np.max(np.abs(a.astype(float))))[0]
    first, last, mid = int(loud[0]), int(loud[-1]), int((loud[0] + loud[-1]) // 2)
    z = np.zeros(CUT, dtype="<i2")

    def silenced(x, at):
        y = x.copy()
        y[at:at + CUT] = 0
        return y
    cases = [("unchanged", a, True),
             ("leading silence added (150 samples)", np.concatenate([np.zeros(150, "<i2"), a]), True),
             ("leading silence added (the bound, %d)" % LEAD_MAX, np.concatenate([np.zeros(LEAD_MAX, "<i2"), a]), True),
             ("leading delay past the bound (661)", np.concatenate([np.zeros(661, "<i2"), a]), False),
             ("audible head removed", np.concatenate([a[:first], a[first + CUT:]]), False),
             ("audible head silenced", silenced(a, first), False),
             ("audible tail removed", np.concatenate([a[:last - CUT], a[last:]]), False),
             ("audible tail silenced", silenced(a, last - CUT), False),
             ("internal deletion", np.concatenate([a[:mid], a[mid + CUT:]]), False),
             ("internal deletion, its length made up at the end", np.concatenate([a[:mid], a[mid + CUT:], z]), False)]
    ok_all = True
    for name, b, want in cases:
        got, detail = same_speech(a_bytes, b.astype("<i2").tobytes())
        result(got == want, "comparison control: %s" % name, "%s (%s)" % ("same" if got else "differs", detail))
        ok_all = ok_all and got == want
    return ok_all


def load(lib_path, chip_path):
    lib = TAN.load_reference(lib_path, chip_path)
    lib.blv_find_image.restype = ctypes.c_long
    lib.blv_find_image.argtypes = [ctypes.c_char_p, ctypes.c_long]
    lib.blv_import_firmware.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.blv_firmware_label.restype = ctypes.c_char_p
    lib.blv_firmware_label.argtypes = [ctypes.c_int]
    lib.blv_firmware_language.argtypes = [ctypes.c_int]
    if not hasattr(lib, "blv_firmware_recipe"):
        sys.exit("%s is older than bl_firmware.h (no blv_firmware_recipe): rebuild it" % lib_path)
    lib.blv_firmware_recipe.argtypes = [ctypes.c_int]
    lib.blv_state_check.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.blv_sha256.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p]
    lib.blv_make_state.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, PROGRESS, ctypes.c_void_p,
                                   ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "blv_state_break").value = 1 if BREAK_STATE else 0
    ctypes.c_int.in_dll(lib, "blv_firmware_break").value = 1 if BREAK_HASH else 0
    return lib


def import_bytes(lib, data, out):
    msg = ctypes.create_string_buffer(256)
    return lib.blv_import_firmware(data, len(data), out.encode(), msg, 256), msg.value.decode()


def check_sha256(lib):
    rng = random.Random(263)
    good = True
    for n in (0, 1, 55, 56, 63, 64, 65, 119, 120, 1000, 262074):
        data = bytes(rng.getrandbits(8) for _ in range(n))
        out = ctypes.create_string_buffer(32)
        lib.blv_sha256(data, n, out)
        good = good and out.raw == hashlib.sha256(data).digest()
    result(good, "sha256 = hashlib's, at every padding boundary")


def check_list(lib):
    # the voices' releases, first and in this order; then the emulator's only (bl_languages.txt, no voice: -1)
    got = [(lib.blv_firmware_label(k).decode(), lib.blv_firmware_language(k)) for k in range(lib.blv_firmware_count())]
    want = list(zip(LABELS, LANGUAGES)) + [(label, NONE) for label, _ in got[len(LABELS):]]
    result(got == want, "the list: %d releases, labelled" % len(want), "" if got == want else "got %s" % got)
    result(lib.blv_firmware_label(len(want)) is None and lib.blv_firmware_language(-1) == -1, "past the list: none")
    got = {lang: lib.blv_firmware_recipe(lang) for lang in RECIPES}
    result(got == RECIPES and lib.blv_firmware_recipe(-1) == -1 and lib.blv_firmware_recipe(5) == -1,
           "each language's state recipe (the Slovak unit's the English one)", "" if got == RECIPES else "got %s" % got)


def new_unit(fw, key):
    """0.8's unit in the firmware folder (test_android_native's LANG_SOURCES): (its .BNS, its state), or None."""
    sub, bns, state = TAN.LANG_SOURCES[key]
    paths = (os.path.join(fw, sub, bns), os.path.join(fw, sub, state))
    return paths if all(os.path.isfile(p) for p in paths) else None


def check_new_units(lib, fw, made, tmp):
    """0.8's units, each when its files are here: its .BNS imported as its language, written as it came; its state
    made with its language's recipe (blv_firmware_recipe) into the app's flat names, the list's byte for byte -- and
    the one in the firmware folder (MAME-made too) -- with its progress steady; one byte changed, refused."""
    for lang, key, label in NEW:
        unit = new_unit(fw, key)
        if not unit:
            print("skip %s: no %s/%s + %s in the firmware folder" % ((NAMES[lang],) + TAN.LANG_SOURCES[key]))
            continue
        data = open(unit[0], "rb").read()
        out = os.path.join(tmp, "out.BNS")
        if os.path.exists(out):
            os.remove(out)
        got, msg = import_bytes(lib, data, out)
        result(got == lang and msg == label and open(out, "rb").read() == data,
               "%s: %s, written as it came" % (os.path.basename(unit[0]), NAMES[lang]), "%s (%s)" % (got, msg))
        bns, state = (os.path.join(made, f) for f in TAN.FILES[key])
        shutil.copy2(unit[0], bns)
        recipe = lib.blv_firmware_recipe(lang)
        ok, err, secs, reports = make_state(lib, bns, recipe, state)
        listed = ok and lib.blv_state_check(bns.encode(), state.encode()) == 1
        result(listed and open(state, "rb").read() == open(unit[1], "rb").read(),
               "generated %s state = the list's and the firmware folder's, byte for byte" % NAMES[lang],
               "%.1f s" % secs if listed else (err or "a different state"))
        check_progress(NAMES[lang], recipe, secs, reports)
        changed = bytearray(open(state, "rb").read())
        changed[-1] ^= 1
        path = os.path.join(tmp, "changed.state")
        open(path, "wb").write(bytes(changed))
        result(lib.blv_state_check(bns.encode(), path.encode()) == 0,
               "the generated %s state with one byte changed is not the list's" % NAMES[lang])


def spanish_dir(fw):
    return fw if os.path.isfile(os.path.join(fw, "BL2SPA.BNS")) else os.path.join(fw, "spanish")


def once_english(fw):
    path = os.path.join(fw, "once2000", "BL2ENG.BNS")
    return path if os.path.isfile(path) else None


def check_import(lib, fw, tmp):
    eng = open(os.path.join(fw, "BL2ENG.BNS"), "rb").read()
    spa = open(os.path.join(spanish_dir(fw), "BL2SPA.BNS"), "rb").read()
    tns = open(os.path.join(fw, "tns", "TNSENG.TNS"), "rb").read()
    out = os.path.join(tmp, "out.BNS")
    rng = random.Random(1)
    junk = lambda n: b"MZ" + bytes(rng.getrandbits(8) for _ in range(n - 2))  # noqa: E731

    def case(name, data, want, want_bytes=None, want_msg=None):
        if os.path.exists(out):
            os.remove(out)
        got, msg = import_bytes(lib, data, out)
        ok = got == want
        detail = "%s (%s)" % (NAMES.get(got, got), msg) if not ok else NAMES[got]
        if ok and want_bytes is not None:
            ok = os.path.isfile(out) and open(out, "rb").read() == want_bytes
            detail += "" if ok else ", but the file written is not the one expected"
        if ok and want < 0:
            ok = not os.path.exists(out)
            detail += "" if ok else ", but a file was left"
        if ok and want_msg is not None:
            ok = msg == want_msg
            detail += "" if ok else ", but said %r" % msg
        result(ok, name, detail)

    if not BREAK_HASH:
        case("BL2ENG.BNS: English, June 2003, written as it came", eng, ENGLISH, eng, LABELS[0])
        case("BL2SPA.BNS: Spanish, September 2000, written as it came", spa, SPANISH, spa, LABELS[2])
        once = once_english(fw)
        if once:
            data = open(once, "rb").read()
            case("once2000/BL2ENG.BNS: English, September 2000, written as it came", data, ENGLISH, data, LABELS[1])
        else:
            print("skip once2000/BL2ENG.BNS (ONCE's September 2000 English) is not in the firmware folder")
        image = eng[0x3000:]
        case("an update program with the .BNS inside it: English, only the image written, at 3000h",
             junk(0x1234) + eng + junk(5000), ENGLISH, bytes(0x3000) + image)
        case("the 1998 layout (image at 5000h): English", junk(0x5000) + image, ENGLISH, bytes(0x3000) + image)
        case("TNSENG.TNS (Type 'n Speak): refused", tns, REFUSED,
             want_msg="Braille Lite firmware, but not a release this voice can run")
        case("the English state (no image in it): none", open(os.path.join(fw, "bl2_2003_warm.state"), "rb").read(),
             NONE, want_msg="no Braille Lite firmware in it")
        case("random bytes: none", junk(300000), NONE, want_msg="no Braille Lite firmware in it")
        case("an empty file: none", b"", NONE)
        at = lib.blv_find_image(image[:9] + b"x" + image[10:], len(image))
        result(at == -1, "the notice must follow F3 C3 xx xx FF", "found at %d" % at if at != -1 else "")
    # The unknown releases: images the voice can start (bl_create's sites are there) that are not on the list, as
    # another country's release would be.  The hash control takes them, so these must fail under it.
    changed = bytearray(eng)
    changed[-1] ^= 0xFF
    unknown = "Braille Lite 2000 firmware, but not a release this app knows"
    case("a release not on the list (June 2003, its last byte changed): refused as unknown", bytes(changed), UNKNOWN,
         want_msg=unknown)
    case("June 2003 one byte short: refused as unknown", eng[:-1], UNKNOWN, want_msg=unknown)


def check_states(lib, fw, made, tmp):
    """blv_state_check on the states made here (the list's hashes are MAME-made, 0.7): one byte changed, the other
    language's, and the desktop's shipped states -- made on z180emu, other bytes -- are each refused."""
    spa = spanish_dir(fw)
    pairs = {"English": (os.path.join(made, "BL2ENG.BNS"), os.path.join(made, "bl2_2003_warm.state"),
                         os.path.join(fw, "bl2_2003_warm.state")),
             "Spanish": (os.path.join(made, "BL2SPA.BNS"), os.path.join(made, "bl2spa_fresh.state"),
                         os.path.join(spa, "bl2spa_fresh.state"))}
    for name, (bns, state, shipped) in pairs.items():
        data = bytearray(open(state, "rb").read())
        data[-1] ^= 1
        changed = os.path.join(tmp, "changed.state")
        open(changed, "wb").write(bytes(data))
        result(lib.blv_state_check(bns.encode(), changed.encode()) == 0,
               "the generated %s state with one byte changed is not the list's" % name)
        result(lib.blv_state_check(bns.encode(), shipped.encode()) == 0,
               "the shipped %s state (made on z180emu) is not the list's" % name)
    crossed = lib.blv_state_check(pairs["English"][0].encode(), pairs["Spanish"][1].encode())
    result(crossed == 0, "the Spanish state is not the English release's")


def make_state(lib, firmware, language, out):
    """blv_make_state with a progress callback: (ok, message, seconds, [(seconds, done), ...])."""
    err = ctypes.create_string_buffer(256)
    reports = []
    t = time.time()

    def progress(ctx, done):
        reports.append((time.time() - t, done))
        return 0
    ok = lib.blv_make_state(firmware.encode(), language, out.encode(), PROGRESS(progress), None, err, 256)
    return ok, err.value.decode(), time.time() - t, reports


def check_progress(label, language, secs, reports):
    """Every 10 million instructions, strictly rising to 1, and steady in time: the bar moves all through."""
    want = -(-RECIPE[language] // CHUNK)
    dones = [d for _, d in reports]
    rising = all(b > a for a, b in zip(dones, dones[1:])) and dones and abs(dones[-1] - 1.0) < 1e-9
    times = [0.0] + [s for s, _ in reports]
    gaps = [b - a for a, b in zip(times, times[1:])]
    # the count and order are exact above; time only has to catch a bar that jumps at the end (one gap ~ all of
    # secs).  8x the mean, not 3x: under a full run_tests load a 1.4 s scheduler pause beat 3x (0.99 s) twice.
    steady = gaps and max(gaps) <= max(8 * secs / want, 0.5)
    ok = len(reports) == want and rising and steady
    result(ok, "%s: progress reported %d times, rising to 1, steadily" % (label, want),
           "longest gap %.2f s of %.1f s" % (max(gaps), secs) if ok else
           "%d reports, rising %s, longest gap %.2f s of %.1f s" % (len(reports), rising, max(gaps or [0]), secs))


def main():
    arch = "x64" if struct.calcsize("P") == 8 else "x86"
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie"))
    ap.add_argument("--lib", default=os.path.join(REPO, "nvda", "dist", "blazie-lib", arch, "bl.dll") if WINDOWS
                    else os.path.join(REPO, "build", "linux", "libssi263speech.so"))
    ap.add_argument("--chip", default=os.path.join(REPO, "src", "ssi263", "_bin", arch, "ssi263.dll") if WINDOWS
                    else None)
    a = ap.parse_args()
    fw = a.firmware
    spa_dir = spanish_dir(fw)
    lib = load(a.lib, a.chip)
    tmp = tempfile.mkdtemp(prefix="ssi263-import-test-")
    try:
        if BREAK_HASH:
            check_import(lib, fw, tmp)
            return finish()
        if not BREAK_STATE:
            check_sha256(lib)
            check_list(lib)
            check_import(lib, fw, tmp)
        # the states, made from the firmware alone, into a folder laid out as the app's
        made = os.path.join(tmp, "made")
        os.makedirs(made)
        shutil.copy2(os.path.join(fw, "BL2ENG.BNS"), made)
        langs = [("English", ENGLISH, "BL2ENG.BNS", "bl2_2003_warm.state", fw)]
        if not BREAK_STATE:
            shutil.copy2(os.path.join(spa_dir, "BL2SPA.BNS"), made)
            langs.append(("Spanish", SPANISH, "BL2SPA.BNS", "bl2spa_fresh.state", spa_dir))
        for label, lang, bns, state, where in langs:
            ok, err, secs, reports = make_state(lib, os.path.join(made, bns), lang, os.path.join(made, state))
            # the list's hash: the state the phone, the desktop and Linux all make (MAME's Z180), byte for byte
            listed = ok and lib.blv_state_check(os.path.join(made, bns).encode(),
                                                os.path.join(made, state).encode()) == 1
            result(listed, "generated %s state = the list's, byte for byte" % label,
                   "%.1f s" % secs if listed else (err or "a different state"))
            if not BREAK_STATE:
                check_progress(label, lang, secs, reports)
        if not BREAK_STATE:
            check_states(lib, fw, made, tmp)
            check_new_units(lib, fw, made, tmp)
        # ... and it speaks every case of the Android test as the desktop's shipped state does (made on z180emu, so a
        # REFERENCE: within a few samples, the length of each case; the state's own bytes are checked above)
        shipped = os.path.join(tmp, "shipped")
        os.makedirs(shipped)
        # (the lockstep's cases: run ahead's are its own test's, test_android_native.py)
        want_pcm, got_pcm = {}, {}
        want = TAN.reference(lib, TAN.data_folder(fw, shipped), run_ahead=False, pcms=want_pcm)
        if BREAK_STATE:                      # the control makes the English state alone
            for name in ["spanish"] + [n for n in want if n.split("-")[0] in TAN.LANG_SOURCES]:
                want.pop(name, None)
        got = TAN.reference(lib, made, run_ahead=False, pcms=got_pcm)
        # the comparison proves its decisions on the shipped audio first (same_speech, comparison_controls); if it
        # cannot, only equal lengths count, as before
        probe = "slow-low" if "slow-low" in want_pcm else next(iter(want_pcm), None)
        trusted = probe is not None and comparison_controls(want_pcm[probe])
        for name in want:
            n, want_n = got.get(name, (0, ""))[0], want[name][0]
            raw = "%d samples (the shipped state's: %d)" % (n, want_n)
            if n > 0 and abs(n - want_n) <= SAME_SAMPLES:
                result(True, "speech from the generated states: %s" % name, raw)
                continue
            same, how = (same_speech(want_pcm[name], got_pcm[name]) if trusted and n > 0 and name in got_pcm
                         else (False, "lengths apart and the comparison not trusted"))
            result(same, "speech from the generated states: %s" % name, "%s; %s" % (raw, how))
        if not BREAK_STATE:
            check_once_english(lib, fw, tmp, want)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return finish()


def check_once_english(lib, fw, tmp, june):
    """ONCE's September 2000 English: no state ships for it, so the list's hash is the reference -- made here with
    the English recipe, checked against it, then made to speak every English case, near June 2003's lengths."""
    once = once_english(fw)
    if not once:
        print("skip ONCE's September 2000 English state: once2000/BL2ENG.BNS is not in the firmware folder")
        return
    d = os.path.join(tmp, "once")
    os.makedirs(d)
    shutil.copy2(once, os.path.join(d, "BL2ENG.BNS"))
    state = os.path.join(d, "bl2_2003_warm.state")
    ok, err, secs, reports = make_state(lib, os.path.join(d, "BL2ENG.BNS"), ENGLISH, state)
    listed = ok and lib.blv_state_check(os.path.join(d, "BL2ENG.BNS").encode(), state.encode()) == 1
    result(listed, "generated September 2000 English state = the list's", "%.1f s" % secs if listed else
           (err or "a different state"))
    check_progress("September 2000 English", ENGLISH, secs, reports)
    got = TAN.reference(lib, d, run_ahead=False)
    for name in (n for n in june if n != "spanish" and n.split("-")[0] not in TAN.LANG_SOURCES):
        n, want_n = got.get(name, (0, ""))[0], june[name][0]
        close = n > 0 and abs(n - want_n) <= max(0.02 * want_n, 2000)
        result(close, "September 2000 English speaks: %s" % name, "%d samples (June 2003: %d)" % (n, want_n))


def finish():
    print("import: %d FAILED" % bad if bad else "import: every case as expected")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
