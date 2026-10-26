"""The SmarTalk as a voice (voices.c's smartalk:smartalk; smartalk/st_voice.h) through the voice table and through the
NVDA driver's binding (nvda/shared/ssi263speech.py SmarTalkC): listed, available only with S_TALK.EXE, made, speaking
with the silence before its first phoneme left out; each setting reaching the chip as the TSR's own command (rate,
pitch, tone, inflection; volume the PCM gain only, the chip's writes the same); the settings kept by the TSR across a
cancel (none sent again, the speech as fast); a capital's pitch for its text only; a long text said in parts with no
gap between them; ESC and Ctrl-X in the text never reaching the TSR as commands; the number words.

    python tools/check_st_voice.py [library]     (default build/win/x64/ssi263speech.dll)

And the pitch snap (st_voice.c, a host feature as the Speak-Out's and the Accents'): an utterance after one that ended
low starts on its own pitch target, read in the chip's state, and a capital's raised pitch lands on its letter; the
glides inside the utterance stay (the TSR's own: the slowest glide field).

Controls: ST_VOICE_BREAK=1 (or tone) -- the voice is told the tone is A for every text whatever it is asked (stv_set's
tone pinned): the tone check must then fail, and only it.  ST_VOICE_BREAK=snap -- the snap disarmed after each
speak: the phrase-start and capital checks must fail, and only they.
ST_RATE_BREAK=1 -- the rate's old mapping (50 = the TSR's ESC >5, not its power-up speed): the rate-back-at-50
check must fail, and only it.
ST_TEXT_BREAK=1 -- the contractions left as they are (st_voice.c contractions()): the contraction checks must fail.
SSV_BANK_BREAK=1 -- a bank keeps its last slot on a boot change (the SmarTalk's, as when its slot was past the
bank's end): the bank check must fail, and only it.
"""
import ctypes
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "firmware")
EXE = os.path.join(FW, "automated-functions-smartalk", "S_TALK.EXE")
sys.path.insert(0, os.path.join(ROOT, "nvda", "shared"))
import ssi263speech  # noqa: E402

BREAK = os.environ.get("ST_VOICE_BREAK") in ("1", "tone")
BREAK_SNAP = os.environ.get("ST_VOICE_BREAK") == "snap"
TEXT = "This is a test sentence for the SmarTalk."
RATE = 44100


def table(lib):
    lib.ssv_find.argtypes = [ctypes.c_char_p]
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.ssv_create.restype = ctypes.c_void_p
    lib.ssv_create.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_speak.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.ssv_destroy.argtypes = [ctypes.c_void_p]
    lib.stv_parts.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.stv_get_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    return lib


class Voice:
    """SmarTalkC, with what the tests look at: each utterance's samples, its chip writes and the blocks' timing."""

    def __init__(self, lib_path):
        self.v = ssi263speech.SmarTalkC(lib_path, EXE, RATE)
        self.v.log_writes(True)
        self.v.writes()
        self.set()

    def set(self, rate=50, pitch=50, tone=8, volume=100, inflection=True, numbers=True):
        self.v.set(rate, pitch, 0 if BREAK else tone, volume, inflection, numbers)

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

    def commands(self):
        lib = self.v._lib
        return lib.stv_get_int(self.v._v, b"commands")

    def onset(self, text, offset=0):
        """(how far the chip's pitch is from its target, in I counts, at the block where the utterance's first
        phoneme was loaded; the largest distance after it): the pitch counter and its target, ssi263_state's 2 and 3"""
        lib = self.v._lib
        lib.ssi263_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
        lib.ssi263_set_snap_pitch.argtypes = [ctypes.c_void_p, ctypes.c_int]
        host = lib.stv_host(self.v._v)
        chip = lib.sth_chip(host)
        state = (ctypes.c_double * 40)()
        t0 = lib.sth_get_double(host, b"time")
        first, later = None, 0.0
        if self.v.say(text, offset):
            if BREAK_SNAP:
                lib.ssi263_set_snap_pitch(chip, 0)
            while True:
                _pcm, done = self.v.render()
                lib.ssi263_state(chip, state, 40)
                gap = abs(state[2] - state[3])
                if first is None:
                    if lib.sth_get_double(host, b"last_speech") >= t0:
                        first = gap
                else:
                    later = max(later, gap)
                if done:
                    break
        self.v.writes()
        return first, later


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


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "win", "x64", "ssi263speech.dll")
    if not os.path.isfile(EXE):
        print("skip  the SmarTalk voice: S_TALK.EXE is not in firmware/automated-functions-smartalk")
        return 0
    lib = table(ssi263speech.load(path))
    failures = 0

    def ok(cond, name, detail):
        nonlocal failures
        print("%-5s %-46s %s" % ("ok" if cond else "FAIL", name, detail))
        failures += not cond

    # ---- the table -------------------------------------------------------------------------------------------------
    i = lib.ssv_find(b"smartalk:smartalk")
    ok(i >= 0, "listed: smartalk:smartalk", "index %d" % i)
    ok(i >= 0 and lib.ssv_available(i, FW.encode()), "available with S_TALK.EXE", "")
    empty = tempfile.mkdtemp(prefix="st_voice_")
    try:
        ok(i >= 0 and not lib.ssv_available(i, empty.encode()), "not available without it", "")
    finally:
        shutil.rmtree(empty, ignore_errors=True)
    err = ctypes.create_string_buffer(300)
    t = lib.ssv_create(i, FW.encode(), None, err, 300) if i >= 0 else None
    ok(bool(t), "made through the table", err.value.decode() or "made")
    if t:
        pcm, done, out = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), []
        lib.ssv_speak(t, None, TEXT.encode(), 0)
        for _ in range(4000):
            n = lib.ssv_render(t, ctypes.byref(pcm), ctypes.byref(done))
            if n > 0:
                out.extend(pcm[:n])
            if done.value or n < 0:
                break
        r = (sum(x * x for x in out) / len(out)) ** 0.5 if out else 0
        ok(r > 300 and 0.8 < len(out) / 22050.0 < 6, "speaks through the table", "%.2f s at 22050 Hz, rms %.0f" % (
            len(out) / 22050.0, r))
        lib.ssv_destroy(t)
        # through a bank, as the SAPI engine keeps its units: a boot change lets the SmarTalk's unit go, and the next
        # one speaks as a fresh unit does (its slot was once past the bank's end, kept for ever: SSV_BANK_BREAK=1 puts
        # that back)
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
            got = []
            if u:
                lib.ssv_speak(u, None, TEXT.encode(), 0)
                for _ in range(4000):
                    n = lib.ssv_render(u, ctypes.byref(pcm), ctypes.byref(done))
                    if n > 0:
                        got.extend(pcm[:n])
                    if done.value or n < 0:
                        break
            return got
        k = lib.ssv_bank_new(FW.encode())
        first = bank_says(k)
        lib.ssv_bank_boot(k, ctypes.byref(Boot(22050, 0, 0)))
        again = bank_says(k)
        lib.ssv_bank_free(k)
        ok(bool(first) and again == first, "a bank's unit made again on a boot change",
           "%d samples, then %d%s" % (len(first), len(again), "" if again == first else ": NOT a fresh unit's"))
        # as loud as the Speak-Out (the box's volume 9, st_voice.h): the speech's level (samples above 300), within 1 dB
        so = lib.ssv_find(b"speakout:speakout")
        if so >= 0 and lib.ssv_available(so, FW.encode()):
            def level(index, text):
                u = lib.ssv_create(index, FW.encode(), None, err, 300)
                pcm, done, got = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), []
                lib.ssv_speak(u, None, text, 0)
                for _ in range(4000):
                    n = lib.ssv_render(u, ctypes.byref(pcm), ctypes.byref(done))
                    if n > 0:
                        got.extend(pcm[:n])
                    if done.value or n < 0:
                        break
                lib.ssv_destroy(u)
                loud = [x for x in got if abs(x) > 300]
                return (sum(x * x for x in loud) / len(loud)) ** 0.5 if loud else 1.0
            import math
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
    base_len, base_pitch, base_r4 = length(w), regs_at_phonemes(w, 1), v.v.regs[4]     # R4 is written on a change only

    v.set(rate=0)
    _p, w0, _n = v.say(TEXT)
    v.set(rate=100)
    _p, w9, _n = v.say(TEXT)
    ok(length(w9) < base_len < length(w0), "rate: 0 slower, 100 faster", "%.2f s / %.2f s / %.2f s" % (
        length(w0), base_len, length(w9)))
    # back at 50, the speed a fresh unit speaks at (its startup rate, ESC >3's: st_voice.c DEF_RATE): it was 27 %
    # shorter, the TSR's ESC >5 (ST_RATE_BREAK=1 puts that back)
    v.set(rate=50)
    _p, w5, _n = v.say(TEXT)
    ok(abs(length(w5) - base_len) <= 0.03 * base_len, "rate back at 50: a fresh unit's speed",
       "%.2f s against %.2f s" % (length(w5), base_len))
    v.set(pitch=0)
    _p, wp0, _n = v.say(TEXT)
    v.set(pitch=100)
    _p, wp9, _n = v.say(TEXT)
    hi = lambda w: max(x for x in regs_at_phonemes(w, 1) if x is not None)        # noqa: E731
    ok(hi(wp0) < hi(w) < hi(wp9), "pitch: 0 lower, 100 higher", "R1 up to %d / %d / %d" % (hi(wp0), hi(w), hi(wp9)))
    v.set(tone=0)
    v.say(TEXT)
    t0 = v.v.regs[4]
    v.set(tone=25)
    v.say(TEXT)
    t25 = v.v.regs[4]
    ok(t0 < base_r4 < t25, "tone: A lower, Z higher than I", "R4 %02X / %02X / %02X" % (t0, base_r4, t25))
    v.set(inflection=False)
    _p, wm, _n = v.say(TEXT)
    flat = set(regs_at_phonemes(wm, 1)) - {None}
    ok(len(flat) == 1 and len(set(base_pitch) - {None}) > 1, "inflection off: one pitch", "R1 %s" % sorted(flat))
    v.set()
    v.say(TEXT)                                  # the inflection back on first: both texts below then send nothing
    loud, wl, _n = v.say(TEXT)
    v.set(volume=50)
    quiet, wq, _n = v.say(TEXT)
    same = [(r, x) for _t, r, x in wl] == [(r, x) for _t, r, x in wq]
    ok(same and 0.45 < rms(quiet) / rms(loud) < 0.55, "volume: the gain only", "rms ratio %.3f, writes %s" % (
        rms(quiet) / rms(loud), "the same" if same else "DIFFERENT"))

    # the settings stay with the TSR across a cancel: none sent again, and as fast as before
    v.set(rate=100)
    _p, wf, _n = v.say(TEXT)
    v.say(TEXT, blocks=4)
    v.v.cancel()
    v.writes = None
    _p, wa, _n = v.say(TEXT)
    ok(v.commands() == 0 and abs(length(wa) - length(wf)) < 0.05, "settings kept across a cancel",
       "%d command bytes, %.2f s against %.2f s" % (v.commands(), length(wa), length(wf)))
    v.set()
    v.say("Reset.")

    # a capital: its text a pitch higher, the next text back at the user's pitch
    _p, wc, _n = v.say("A", 30)
    _p, wn, _n = v.say("A")
    ok(hi(wc) > hi(wn), "a capital's pitch, for its text only", "R1 up to %d, then %d" % (hi(wc), hi(wn)))

    # the pitch snap: a phrase after one that ended low starts on its own target; a capital lands on its letter; the
    # glides inside the phrase stay (one I step is 64 counts)
    v.say("This is where the sentence ends.")
    first, later = v.onset("Many melodies, more and more.")
    ok(first is not None and first < 16, "a phrase starts on its own pitch", "%.0f counts from its target" % (
        first if first is not None else -1))
    ok(later > 64, "the glides inside the phrase stay", "up to %.0f counts behind its targets" % later)
    v.say("This is where the sentence ends.")
    first, _later = v.onset("M", 30)
    ok(first is not None and first < 16, "a capital's pitch lands on its letter", "%.0f counts from its target" % (
        first if first is not None else -1))
    v.say("This is where the sentence ends.")
    v.set(pitch=90)
    first, _later = v.onset("Many melodies, more and more.")
    ok(first is not None and first < 16, "a new pitch setting lands at once", "%.0f counts from its target" % (
        first if first is not None else -1))
    v.set()
    v.say("Reset.")

    # a long text: parts, the next sent while the box still talks -- no gap longer than a sentence's own pause
    sentence = "The quick brown fox jumps over the lazy dog while the band plays on. "
    pcm1, w1, _n = v.say(sentence)
    long_text = sentence * 14
    pcm2, w2, _n = v.say(long_text)
    parts = v.v._lib.stv_get_int(v.v._v, b"parts")
    gap1, gap2 = longest_gap(w1), longest_gap(w2)
    ok(parts > 3 and len(phonemes(w2)) >= 13 * len(phonemes(w1)) and gap2 < gap1 + 0.15,
       "a long text in parts, no gap between them", "%d parts, %d phonemes (one sentence %d), longest gap %.2f s "
       "(one sentence %.2f s)" % (parts, len(phonemes(w2)), len(phonemes(w1)), gap2, gap1))

    # ESC and Ctrl-X in the text are spaces: no command reaches the TSR, both words are said
    _p, wx, _n = v.say("left\x1b>9 right\x18 end")
    rates = set(x >> 4 for x in regs_at_phonemes(wx, 2) if x is not None)
    _p, wy, _n = v.say("left 9 right end")
    ok(len(phonemes(wx)) >= len(phonemes(wy)) - 2 and abs(length(wx) - length(wy)) < 0.15,
       "ESC and Ctrl-X in the text are no commands", "%d phonemes in %.2f s against %d in %.2f s, rate codes %s" % (
           len(phonemes(wx)), length(wx), len(phonemes(wy)), length(wy), sorted(rates)))

    # the number words
    out = ctypes.create_string_buffer(400)
    lib.stv_parts(b"In 1995 it cost $395.00, 10% less.", 1, out, 400)
    words = out.value.decode()
    lib.stv_parts(b"In 1995 it cost $395.00, 10% less.", 0, out, 400)
    raw = out.value.decode()
    ok("thousand" in words and "dollars" in words and "percent" in words and "$" not in words and "1995" in raw
       and "395 dollars" in raw and "10 percent" in raw, "number words on, digits off; dollars, percent",
       "%r / %r" % (words, raw))
    for text, want in (("$2.63", "2 dollars 63 cents"), ("$1", "1 dollar"), ("$.5", "50 cents"),
                       ("$0.01", "1 cent"), ("$2.635", "2.635 dollars"), ("a$3", "a 3 dollars"), ("$", "$"),
                       ("50 %", "50 percent"), ("5%.", "5 percent.")):
        lib.stv_parts(text.encode(), 0, out, 400)
        ok(out.value.decode() == want, "said as: %s" % text, repr(out.value.decode()))

    # the contractions (a listener: "Don't is dontee"): the TSR ends a word at an apostrophe and spells a rest with no
    # vowel, so the voice joins the word, and spells the pronouns ending in a vowel as the TSR says them right
    for text, want in (("don't", "dont"), ("I'm", "Ime"), ("WE'LL", "WEEL"), ("They're here", "There here"),
                       (u"it\u2019s", "its"), ("rock 'n roll", "rock 'n roll"), ("the dogs' bowls", "the dogs' bowls")):
        lib.stv_parts(text.encode("utf-8"), 0, out, 400)
        name = text if text.isascii() else "it's, curly"
        ok(out.value.decode() == want, "said as: %s" % name, repr(out.value.decode()))
    _p, wc, _n = v.say("don't")
    _p, wj, _n = v.say("dont")
    said = lambda w: [x & 0x3F for _t, x in phonemes(w)]                          # noqa: E731
    ok(said(wc) == said(wj), "don't: said as one word, no letter T",
       "%d phonemes against %d" % (len(said(wc)), len(said(wj))))
    v.v.close()
    print("st voice: %s" % ("all passed" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
