"""Speech Systems' Super Voice as a voice (voices.c's supervoice:supertok; supervoice/sv_voice.h) through the voice
table and through the NVDA driver's binding (nvda/shared/ssi263speech.py SuperVoiceC), and its disk reader
(supervoice/sv_dsk.h): listed, available only with SUPERTOK.BIN, made, speaking; its numbers its own by default; rate
and pitch reaching the chip; a bank's unit made again on a boot change; a cancel, then a text said whole; and
SUPERTOK.BIN taken from a Color Computer disk image -- one laid out here around the program, its granules on both
sides of the directory's track and its last sector part-used -- and refused when changed or missing.

    python tools/check_sv_voice.py [library]     (default build/win/x64/ssi263speech.dll); skips without SUPERTOK.BIN

Controls (each must fail its own checks, and only them):
  SSV_BANK_BREAK=supervoice   a bank keeps the Super Voice's unit on a boot change: the bank check fails
  SV_DSK_BREAK=track17        granules from 34 on read from the directory's own track: the disk check fails
  SV_DSK_BREAK=last           the last sector taken whole: the disk check fails
"""
import ctypes
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware")
BIN = os.path.join(FW, "speech-systems-super-voice", "SUPERTOK.BIN")
sys.path.insert(0, os.path.join(ROOT, "nvda", "shared"))
import ssi263speech  # noqa: E402

TEXT = "This is a test sentence for the Super Voice."
SECTOR, PER_TRACK = 256, 18


def granule_offset(g):
    """the byte offset of granule g's first sector (Disk BASIC: track 17 skipped)"""
    track = g // 2 + (1 if g >= 34 else 0)
    first = (g & 1) * 9 + 1
    return (track * PER_TRACK + first - 1) * SECTOR


def make_disk(prog, granules, tracks=35, name=b"SUPERTOK", ext=b"BIN"):
    """a Disk BASIC image holding `prog` in `granules` (in that order), behind a deleted entry and another file"""
    img = bytearray(tracks * PER_TRACK * SECTOR)
    fat_at = (17 * PER_TRACK + 1) * SECTOR
    dir_at = (17 * PER_TRACK + 2) * SECTOR
    img[fat_at:fat_at + 68] = b"\xff" * 68
    img[dir_at:dir_at + 9 * SECTOR] = b"\xff" * (9 * SECTOR)
    sectors = [prog[k:k + SECTOR] for k in range(0, len(prog), SECTOR)]
    for n, g in enumerate(granules):
        chunk = sectors[9 * n:9 * n + 9]
        for j, s in enumerate(chunk):
            at = granule_offset(g) + j * SECTOR
            img[at:at + len(s)] = s
        last = n == len(granules) - 1
        img[fat_at + g] = (0xC0 + len(chunk)) if last else granules[n + 1]
    assert 9 * len(granules) >= len(sectors) > 9 * (len(granules) - 1)

    def entry(nm, ex, first, used):
        e = bytearray(32)
        e[0:8], e[8:11] = nm.ljust(8), ex.ljust(3)
        e[11], e[12], e[13] = 2, 0, first
        e[14:16] = used.to_bytes(2, "big")
        return e
    img[dir_at:dir_at + 32] = b"\x00" + bytes(entry(b"OLDFILE", b"BAS", 0, 1)[1:])       # deleted
    img[dir_at + 32:dir_at + 64] = entry(b"README", b"TXT", 0, 10)
    img[fat_at + 0] = 0xC1
    img[dir_at + 64:dir_at + 96] = entry(name, ext, granules[0], len(sectors[-1]))
    return bytes(img)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "win", "x64", "ssi263speech.dll")
    if not os.path.isfile(BIN):
        print("skip  the Super Voice: SUPERTOK.BIN is not in firmware/speech-systems-super-voice")
        return 0
    with open(BIN, "rb") as f:
        prog = f.read()
    lib = ssi263speech.load(path)
    lib.ssv_find.argtypes = [ctypes.c_char_p]
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.ssv_create.restype = ctypes.c_void_p
    lib.ssv_create.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_speak.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.ssv_destroy.argtypes = [ctypes.c_void_p]
    lib.ssv_defaults.argtypes = [ctypes.c_int, ctypes.c_void_p]
    failures = 0

    def ok(cond, name, detail=""):
        nonlocal failures
        print("%-5s %-46s %s" % ("ok" if cond else "FAIL", name, detail))
        failures += not cond

    def table_says(u, text):
        pcm, done, got = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), []
        lib.ssv_speak(u, None, text, 0)
        for _ in range(6000):
            n = lib.ssv_render(u, ctypes.byref(pcm), ctypes.byref(done))
            if n > 0:
                got.extend(pcm[:n])
            if done.value or n < 0:
                break
        return got

    # ---- the table -------------------------------------------------------------------------------------------------
    i = lib.ssv_find(b"supervoice:supertok")
    ok(i >= 0, "listed: supervoice:supertok", "index %d" % i)
    ok(i >= 0 and lib.ssv_available(i, FW.encode()), "available with SUPERTOK.BIN")
    empty = tempfile.mkdtemp(prefix="sv_voice_")
    try:
        ok(i >= 0 and not lib.ssv_available(i, empty.encode()), "not available without it")
    finally:
        shutil.rmtree(empty, ignore_errors=True)

    class Settings(ctypes.Structure):
        _fields_ = [(n, ctypes.c_int) for n in ("rate", "pitch", "volume", "tone", "pack", "join", "inflection",
                                                 "numbers", "run_ahead", "line_lift")]
    d, so = Settings(), Settings()
    lib.ssv_defaults(i, ctypes.byref(d))
    lib.ssv_defaults(lib.ssv_find(b"speakout:speakout"), ctypes.byref(so))
    ok(d.numbers == 0 and so.numbers == 1, "its numbers its own by default",
       "numbers %d (the Speak-Out's %d)" % (d.numbers, so.numbers))
    err = ctypes.create_string_buffer(300)
    t = lib.ssv_create(i, FW.encode(), None, err, 300) if i >= 0 else None
    ok(bool(t), "made through the table", err.value.decode() or "made")
    if t:
        out = table_says(t, TEXT.encode())
        r = (sum(x * x for x in out) / len(out)) ** 0.5 if out else 0
        ok(r > 300 and 0.8 < len(out) / 22050.0 < 8, "speaks through the table", "%.2f s at 22050 Hz, rms %.0f" % (
            len(out) / 22050.0, r))
        lib.ssv_destroy(t)
        # through a bank, as the SAPI engine keeps its units: a boot change (here the sample rate) lets the Super
        # Voice's unit go, the last slot (SSV_BANK_BREAK=supervoice keeps it).  Every text finds the program as loaded,
        # so a kept unit says the same thing -- at the rate it was made at, which is how it shows
        lib.ssv_bank_new.restype = ctypes.c_void_p
        lib.ssv_bank_new.argtypes = [ctypes.c_char_p]
        lib.ssv_bank_voice.restype = ctypes.c_void_p
        lib.ssv_bank_voice.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        lib.ssv_bank_boot.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        lib.ssv_bank_free.argtypes = [ctypes.c_void_p]

        class Boot(ctypes.Structure):
            _fields_ = [("sample_rate", ctypes.c_int), ("inflection", ctypes.c_int), ("whine", ctypes.c_int)]
        k = lib.ssv_bank_new(FW.encode())
        u = lib.ssv_bank_voice(k, i, err, 300)
        first = table_says(u, TEXT.encode()) if u else []
        lib.ssv_bank_boot(k, ctypes.byref(Boot(44100, 1, 0)))
        u = lib.ssv_bank_voice(k, i, err, 300)
        again = table_says(u, TEXT.encode()) if u else []
        lib.ssv_bank_free(k)
        ratio = len(again) / float(len(first)) if first else 0
        ok(bool(first) and 1.9 < ratio < 2.1, "a bank's unit made again on a boot change",
           "%d samples at 22050 Hz, then %d at 44100 (x%.2f)" % (len(first), len(again), ratio))

    # ---- the voice, through the NVDA binding -------------------------------------------------------------------------
    v = ssi263speech.SuperVoiceC(path, BIN, 22050)
    v.log_writes(True)

    def say(text, rate=50, pitch=50, offset=0, blocks=None):
        v.set(rate, pitch, 100, False)
        v.writes()
        pcm, n = [], 0
        if v.say(text, offset):
            while True:
                data, done = v.render()
                pcm.append(data)
                n += 1
                if done or (blocks and n >= blocks):
                    break
        return b"".join(pcm), v.writes()

    def phonemes(writes):
        return [(t, val) for t, reg, val in writes if reg == 0 and val & 0x3F]

    def span(writes):
        p = phonemes(writes)
        return p[-1][0] - p[0][0] if len(p) > 1 else 0.0

    def mean_reg(writes, reg):
        cur, vals = None, []
        for _t, r, val in writes:
            if r == reg:
                cur = val
            elif r == 0 and val & 0x3F and cur is not None:
                vals.append(cur)
        return sum(vals) / float(len(vals)) if vals else 0.0

    _p, slow = say(TEXT, rate=0)
    _p, fast = say(TEXT, rate=100)
    ok(span(slow) > 1.3 * span(fast) > 0, "rate: 0 slower than 100", "%.2f s, %.2f s" % (span(slow), span(fast)))
    _p, low = say(TEXT, pitch=0)
    _p, high = say(TEXT, pitch=100)
    ok(mean_reg(high, 1) > mean_reg(low, 1), "pitch: 100 higher than 0 at the chip",
       "R1 (inflection) mean %.1f / %.1f" % (mean_reg(low, 1), mean_reg(high, 1)))
    _p, whole = say(TEXT)
    say("A long sentence that is cut off part of the way through it, long before its end.", blocks=20)
    v.cancel()
    _p, after = say(TEXT)
    ok(len(phonemes(after)) == len(phonemes(whole)) > 0, "a cancel, then a text said whole",
       "%d phonemes, as on its own: %d" % (len(phonemes(after)), len(phonemes(whole))))
    v.close()

    # ---- the disk reader (sv_dsk.h) --------------------------------------------------------------------------------
    def extract(data):
        try:
            return ssi263speech.supervoice_firmware(path, data), ""
        except ssi263speech.NativeVoiceError as e:
            return None, str(e)
    got, why = extract(prog)
    ok(got == prog, "the program itself is taken", why or "%d bytes" % len(prog))
    granules = [33, 34, 5, 60, 41]          # across the directory's track, out of order, the last one part-used
    got, why = extract(make_disk(prog, granules))
    ok(got == prog, "a disk image: SUPERTOK.BIN taken", why or "granules %s, last sector %d bytes" % (
        granules, len(prog) % SECTOR))
    changed = bytearray(prog)
    changed[5000] ^= 0x40
    got, why = extract(make_disk(bytes(changed), granules))
    ok(got is None and "sha256" in why, "a disk with it changed: refused", why)
    got, why = extract(make_disk(prog, granules, name=b"SUPERTAK"))
    ok(got is None and "no SUPERTOK.BIN" in why, "a disk without it: refused", why)
    got, why = extract(b"\x00" * 1000)
    ok(got is None, "bytes that are neither: refused", why)

    print("sv voice: %s" % ("all passed" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
