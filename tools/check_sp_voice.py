"""The Artic SynPhonix as a voice (voices.c's synphonix:vest; synphonix/sp_voice.h) through the voice table and through
the NVDA driver's binding (nvda/shared/ssi263speech.py SynPhonixC): listed, available only with VEST.COM, made,
speaking with the silence before its first phoneme left out, as loud as the Speak-Out; each setting reaching the chip
through VEST's own offsets (rate, pitch both ways, inflection; volume the PCM gain only, the chip's writes the same);
a capital's pitch for its text only; a long text said in parts with no gap between them; a cancel that stops at once,
the next text starting as after a natural end (none of the cut text's tail at its head); VEST's own contractions
kept, its two misreadings changed ("Artic's", a quote mark), its pitch steps never coming from the text; the number
words, dollars and percent.

    python tools/check_sp_voice.py [library]     (default build/win/x64/ssi263speech.dll); skips without VEST.COM

Controls (each must fail its own checks, and only them):
  SPV_PITCH_BREAK=1    pitch below 50 as 20h + the step (the offset's description; measured, it goes up): "pitch: 0
                       lower" fails
  SPV_QUEUE_BREAK=1    every part at once: past VEST's queue its call waits, and "a long text: every block short"
                       fails (the first block holds what the waiting call made)
  SPV_TEXT_BREAK=1     the apostrophes as they are: the possessive and quote-mark checks fail
  SPV_CUT_BREAK=1      the cut left as it was, not played out (sp_voice.c play_out): "a cancel: the next text starts
                       as after an end" fails
  SPV_SPEAK_FAULT_BREAK=1    spv_speak's fault not looked at again after its own cancel: "a cancel that faults" fails
  SPV_STALL_BREAK=1          a stall judged by silence, as it was: "sentence marks alone" fails
  SSV_BANK_BREAK=synphonix   a bank keeps the SynPhonix's unit on a boot change: the bank check fails
"""
import ctypes
import math
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware")
COM = os.path.join(FW, "artic-synphonix", "VEST.COM")
sys.path.insert(0, os.path.join(ROOT, "nvda", "shared"))
import ssi263speech  # noqa: E402

TEXT = "This is a test sentence for the SynPhonix."
RATE = 44100


class Voice:
    """SynPhonixC, with what the tests look at: each utterance's samples, its chip writes and the block count."""

    def __init__(self, lib_path):
        self.v = ssi263speech.SynPhonixC(lib_path, COM, RATE)
        self.v.log_writes(True)
        self.v.writes()
        self.set()

    def set(self, rate=50, pitch=50, volume=100, inflection=True, numbers=True):
        self.v.set(rate, pitch, volume, inflection, numbers)

    def say(self, text, offset=0, blocks=None):
        """(samples as a bytes string, the chip writes, the block count); blocks: stop after that many"""
        pcm, n = [], 0
        if self.v.say(text, offset):
            while True:
                data, done = self.v.render()
                pcm.append(data)
                n += 1
                if done or (blocks and n >= blocks):
                    break
        return b"".join(pcm), self.v.writes(), n


def samples(pcm):
    return [int.from_bytes(pcm[i:i + 2], "little", signed=True) for i in range(0, len(pcm) - 1, 2)]


def rms(pcm):
    s = samples(pcm)
    return (sum(x * x for x in s) / len(s)) ** 0.5 if s else 0.0


def phonemes(writes):
    return [(t, v) for t, r, v in writes if r == 0 and v & 0x3F]


def regs_at_phonemes(writes, reg):
    """the value register reg held at each phoneme"""
    cur, out = None, []
    for t, r, v in writes:
        if r == reg:
            cur = v
        elif r == 0 and v & 0x3F:
            out.append(cur)
    return out


def length(writes):
    p = phonemes(writes)
    return p[-1][0] - p[0][0] if len(p) > 1 else 0.0


def longest_gap(writes):
    p = [t for t, _ in phonemes(writes)]
    return max((b - a for a, b in zip(p, p[1:])), default=0.0)


def said(writes):
    return [x & 0x3F for _t, x in phonemes(writes)]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "win", "x64", "ssi263speech.dll")
    if not os.path.isfile(COM):
        print("skip  the SynPhonix voice: VEST.COM is not in firmware/artic-synphonix")
        return 0
    lib = ssi263speech.load(path)
    lib.ssv_find.argtypes = [ctypes.c_char_p]
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.ssv_create.restype = ctypes.c_void_p
    lib.ssv_create.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_speak.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.ssv_destroy.argtypes = [ctypes.c_void_p]
    failures = 0

    def ok(cond, name, detail):
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
    i = lib.ssv_find(b"synphonix:vest")
    ok(i >= 0, "listed: synphonix:vest", "index %d" % i)
    ok(i >= 0 and lib.ssv_available(i, FW.encode()), "available with VEST.COM", "")
    empty = tempfile.mkdtemp(prefix="sp_voice_")
    try:
        ok(i >= 0 and not lib.ssv_available(i, empty.encode()), "not available without it", "")
    finally:
        shutil.rmtree(empty, ignore_errors=True)
    err = ctypes.create_string_buffer(300)
    t = lib.ssv_create(i, FW.encode(), None, err, 300) if i >= 0 else None
    ok(bool(t), "made through the table", err.value.decode() or "made")
    if t:
        out = table_says(t, TEXT.encode())
        r = (sum(x * x for x in out) / len(out)) ** 0.5 if out else 0
        ok(r > 300 and 0.8 < len(out) / 22050.0 < 8, "speaks through the table", "%.2f s at 22050 Hz, rms %.0f" % (
            len(out) / 22050.0, r))
        lib.ssv_destroy(t)
        # through a bank, as the SAPI engine keeps its units: a boot change lets the SynPhonix's unit go, the last slot
        # (SSV_BANK_BREAK=synphonix keeps it, as the SmarTalk's was kept when its slot sat past the bank's end)
        lib.ssv_bank_new.restype = ctypes.c_void_p
        lib.ssv_bank_new.argtypes = [ctypes.c_char_p]
        lib.ssv_bank_voice.restype = ctypes.c_void_p
        lib.ssv_bank_voice.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        lib.ssv_bank_boot.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        lib.ssv_bank_free.argtypes = [ctypes.c_void_p]

        class Boot(ctypes.Structure):
            _fields_ = [("sample_rate", ctypes.c_int), ("inflection", ctypes.c_int), ("whine", ctypes.c_int)]

        def bank_says(k):
            u = lib.ssv_bank_voice(k, i, err, 300)
            return table_says(u, TEXT.encode()) if u else []
        k = lib.ssv_bank_new(FW.encode())
        first = bank_says(k)
        lib.ssv_bank_boot(k, ctypes.byref(Boot(22050, 0, 0)))
        again = bank_says(k)
        lib.ssv_bank_free(k)
        ok(bool(first) and again == first, "a bank's unit made again on a boot change",
           "%d samples, then %d%s" % (len(first), len(again), "" if again == first else ": NOT a fresh unit's"))
        # as loud as the Speak-Out (VEST's amplitude offset near its top, sp_voice.h): the speech's level, within 1 dB
        so = lib.ssv_find(b"speakout:speakout")
        if so >= 0 and lib.ssv_available(so, FW.encode()):
            def level(index, text):
                u = lib.ssv_create(index, FW.encode(), None, err, 300)
                got = table_says(u, text)
                lib.ssv_destroy(u)
                loud = [x for x in got if abs(x) > 300]
                return (sum(x * x for x in loud) / len(loud)) ** 0.5 if loud else 1.0
            text = b"This is a test sentence, said by each unit at its default settings."
            db = 20 * math.log10(level(i, text) / level(so, text))
            ok(abs(db) < 1.0, "as loud as the Speak-Out", "%+.1f dB" % db)

    # ---- the voice, through the NVDA binding -------------------------------------------------------------------------
    v = Voice(path)
    pcm, w, _n = v.say(TEXT)
    s = samples(pcm)
    first = next((k for k, x in enumerate(s) if abs(x) > 98), len(s))       # 0.003 of full scale
    ok(len(phonemes(w)) > 20 and rms(pcm) > 300, "speaks", "%d phonemes, %.2f s, rms %.0f" % (
        len(phonemes(w)), len(s) / float(RATE), rms(pcm)))
    ok(first <= 400, "the silence before speech left out", "first sound at sample %d" % first)
    base_len, base_pitch = length(w), regs_at_phonemes(w, 1)

    v.set(rate=0)
    _p, w0, _n = v.say(TEXT)
    v.set(rate=100)
    _p, w9, _n = v.say(TEXT)
    ok(length(w9) < base_len < length(w0), "rate: 0 slower, 100 faster", "%.2f s / %.2f s / %.2f s" % (
        length(w0), base_len, length(w9)))
    v.set(rate=50)
    _p, w5, _n = v.say(TEXT)
    ok(abs(length(w5) - base_len) <= 0.03 * base_len, "rate back at 50: the first speed",
       "%.2f s against %.2f s" % (length(w5), base_len))
    v.set(pitch=0)
    _p, wp0, _n = v.say(TEXT)
    v.set(pitch=100)
    _p, wp9, _n = v.say(TEXT)
    mean = lambda w: sum(x for x in regs_at_phonemes(w, 1) if x is not None) / max(1, len(phonemes(w)))  # noqa: E731
    ok(mean(wp0) < mean(w) < mean(wp9), "pitch: 0 lower, 100 higher", "mean R1 %.0f / %.0f / %.0f" % (
        mean(wp0), mean(w), mean(wp9)))
    v.set(inflection=False)
    _p, wm, _n = v.say(TEXT)
    flat, own = set(regs_at_phonemes(wm, 1)) - {None}, set(base_pitch) - {None}
    ok(len(flat) < len(own), "inflection off: flatter", "%d pitch values against %d" % (len(flat), len(own)))
    v.set()
    loud, wl, _n = v.say(TEXT)
    v.set(volume=50)
    quiet, wq, _n = v.say(TEXT)
    same = [(r, x) for _t, r, x in wl] == [(r, x) for _t, r, x in wq]
    ok(same and 0.45 < rms(quiet) / rms(loud) < 0.55, "volume: the gain only", "rms ratio %.3f, writes %s" % (
        rms(quiet) / rms(loud), "the same" if same else "DIFFERENT"))
    v.set()

    # a capital: its text a pitch higher, the next text back at the user's pitch
    _p, wc, _n = v.say("A", 30)
    _p, wn, _n = v.say("A")
    ok(mean(wc) > mean(wn), "a capital's pitch, for its text only", "mean R1 %.0f, then %.0f" % (mean(wc), mean(wn)))

    # a long text: parts, the next sent while VEST still has frames queued -- no gap longer than VEST's own pause between
    # two sentences in one part (0.41 s, measured: every sentence end in the long text has it, and nothing more)
    sentence = "The quick brown fox jumps over the lazy dog while the band plays on. "
    _p, w1, _n = v.say(sentence * 2)
    _p, w2, _n = v.say(sentence * 14)
    parts = lib.spv_get_int(v.v._v, b"parts")
    gap1, gap2 = longest_gap(w1), longest_gap(w2)
    ok(parts > 3 and len(phonemes(w2)) >= 6 * len(phonemes(w1)) and gap2 < gap1 + 0.15,
       "a long text in parts, no gap between them", "%d parts, %d phonemes (two sentences %d), longest gap %.2f s "
       "(two sentences in one part %.2f s)" % (parts, len(phonemes(w2)), len(phonemes(w1)), gap2, gap1))
    # ... and no call waits on VEST's queue (1,000 frames, about 20 s): a text of over a minute comes in blocks of
    # 30 ms, the first one too (SPV_QUEUE_BREAK=1 sends every part at once: the first block then holds what the
    # waiting call made)
    blocks = []
    if v.v.say(sentence * 20):
        while True:
            data, done = v.v.render()
            blocks.append(len(data) // 2)
            if done:
                break
    v.v.writes()
    longest = max(blocks) / float(RATE) if blocks else 0.0
    total = sum(blocks) / float(RATE)
    ok(total > 60 and longest < 0.1, "a long text: every block short", "%.1f s of speech in %d blocks, the longest "
       "%.2f s" % (total, len(blocks), longest))

    # a cancel: nothing more of the text after it, and the next text said whole, as a fresh one
    _p, wh, _n = v.say("Said after the cancel, whole.")
    v.say(sentence * 4, blocks=20)
    v.v.cancel()
    v.v.writes()
    _p, wa, _n = v.say("Said after the cancel, whole.")
    ok(said(wa) == said(wh), "a cancel: the next text said whole", "%d phonemes against %d" % (
        len(said(wa)), len(said(wh))))
    # ... and when the cancel spv_speak makes itself faults (sp_host's test hook "fail_cancel"), the text it was given
    # is still said, on a fresh unit: the fault was looked at before that cancel only, and say_next ran on a dead host
    # (the 0.9.5 audit; SPV_SPEAK_FAULT_BREAK=1, the control, fails this alone)
    lib = v.v._lib                                   # (spv_host, sph_set_int: declared by the binding)
    v.say(sentence * 4, blocks=20)                   # still talking: spv_speak cancels it itself
    lib.sph_set_int(lib.spv_host(v.v._v), b"fail_cancel", 1)
    v.v.writes()
    pf, _w, _n = v.say("Said after the cancel, whole.")
    lib.sph_set_int(lib.spv_host(v.v._v), b"log_writes", 1)   # (a fresh unit logs nothing until asked: by audio here)
    v.v.writes()
    ph, _w, _n = v.say("Said after the cancel, whole.")
    ok(len(ph) > 0 and abs(len(pf) - len(ph)) <= 0.2 * len(ph), "a cancel that faults: the next text said whole",
       "%.2f s against %.2f s" % (len(pf) / 2.0 / RATE, len(ph) / 2.0 / RATE))

    # sentence marks alone keep VEST busy writing pauses and saying nothing for longer than the stall limit (4 s): that
    # is the card at work, not a stall.  It was cut and the card booted again: a row of dots faulted at every rate, and
    # "Hi. . . . there." lost "there" at rate 0 (the 0.9.5 audit's question, measured; SPV_STALL_BREAK=1, the control,
    # fails this alone)
    v.set(rate=0)
    _p, w_dots, _n = v.say(". " * 60)
    dots_fault = v.v.fault
    _p, w_hi, _n = v.say("Hi. . . . . . . . . . . . . . . . . . . . . . there.")
    hi_fault = v.v.fault
    _p, w_both, _n = v.say("Hi there.")
    v.set()
    ok(not dots_fault and not hi_fault and said(w_hi) == said(w_both),
       "sentence marks alone: no stall, the text said whole",
       "a row of dots: %s; 'Hi. . . there.': %s, %d phonemes against 'Hi there.' %d" % (
           "a fault" if dots_fault else "said", "a fault" if hi_fault else "said", len(said(w_hi)), len(said(w_both))))
    # ... and started as after a natural end: its first phoneme at once, VEST's writes the same, none of the cut text's
    # sound at its head.  VEST's cut leaves the chip on a pause with the cut phoneme fading into it and its own closing
    # frames to come, and the card's time stands still between texts, so those came at the next text's head: it started
    # 33-99 ms late with the fade heard first (a listener, typing and arrowing: "the last tail of the previous utterance
    # is still heard").  Cut at four points; against the same text said again after its end (SPV_CUT_BREAK=1, the cut
    # left as it was, fails this check alone)
    word = "Down arrow."
    v.say(word)
    _p, wd, _n = v.say(word)

    def start(writes):
        """(ms from the text's first write to its first phoneme, its (register, value) writes)"""
        p = phonemes(writes)
        return (1000.0 * (p[0][0] - writes[0][0]) if p else -1.0), [(r, x) for _t, r, x in writes]
    d_end, regs_end = start(wd)
    rows, good = [], True
    for cut in (2, 5, 12, 20):
        v.say(sentence * 2, blocks=cut)
        v.v.cancel()
        v.v.writes()
        pc, wc, _n = v.say(word)
        d, regs = start(wc)
        head = max((abs(x) for x in samples(pc)[:int(0.002 * RATE)]), default=0)
        good = good and 0.0 <= d <= d_end + 1.0 and regs == regs_end and head < 300
        rows.append("%d: %.1f ms%s, head %d" % (cut, d, "" if regs == regs_end else " WRITES DIFFER", head))
    ok(good, "a cancel: the next text starts as after an end", "cut at blocks %s (after its end: %.1f ms)" % (
        "; ".join(rows), d_end))

    # the text: VEST's own contractions kept; its two misreadings changed; its pitch steps never from the text
    out = ctypes.create_string_buffer(600)
    for text, want, name in (
            ("don't, I'm, we'll, can't, o'clock", "don't, I'm, we'll, can't, o'clock", "contractions kept"),
            ("Artic's, ERIC'S, music's", "Artics, ERICS, musics", "a possessive after a C: no apostrophe"),
            ("rock 'n' roll, the dogs' bowls", "rock n roll, the dogs bowls", "a quote mark goes"),
            ("John's cat's", "John's cat's", "other possessives kept"),
            ("up .+ down .- done", "up  + down  - done", "no pitch step from the text"),
            ("left\x07right", "left right", "a control character a space")):
        lib.spv_parts(text.encode("utf-8"), 0, out, 600)
        ok(out.value.decode() == want, "text: " + name, repr(out.value.decode()))
    _p, wq1, _n = v.say("Artic's")
    _p, wq2, _n = v.say("Artics")
    ok(said(wq1) == said(wq2), "Artic's said as Artics (no extra syllable)", "%d phonemes against %d" % (
        len(said(wq1)), len(said(wq2))))
    lib.spv_parts(b"In 1995 it cost $395.00, 10% less.", 1, out, 600)
    words = out.value.decode()
    lib.spv_parts(b"In 1995 it cost $395.00, 10% less.", 0, out, 600)
    raw = out.value.decode()
    ok("thousand" in words and "dollars" in words and "percent" in words and "$" not in words and "1995" in raw
       and "395 dollars" in raw and "10 percent" in raw, "number words on, digits off; dollars, percent",
       "%r / %r" % (words, raw))
    v.v.close()
    print("sp voice: %s" % ("all passed" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
