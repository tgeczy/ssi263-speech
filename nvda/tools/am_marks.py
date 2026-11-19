"""Index marks in the Accent-mini voice (src/csrc/marks.h, accentmini/am_voice.h amv_speak_marked): the driver's own
Ctrl-F directly after the last spoken word before each marked word, and each mark reported where the driver's own
count of marks passed rises (am_host.h amh_replies).

  none     no marks: the line for the driver is amv_say_bytes's, byte for byte
  bytes    with marks: the line less its Ctrl-Fs is the unmarked line (a mark never changes the words), every Ctrl-F
           stands directly after a word and before the space, and the word after it is the one the case names -- a
           mark inside a word, or inside a number that is rewritten, at the start of it
  same     three marks on one word are ONE Ctrl-F
  dash     a mark on a dash that stands alone and one on the word after it share ONE Ctrl-F, after the word before
           the dash (two with nothing spoken between them cost the driver 62 ms: measured)
  place    six words with a full stop after each and a mark before each: each mark is reported in the pause before
           its word, as the AUDIO has it (the words found by level), the first at sample 0 of the first block
  clause   six words with nothing between them and a mark before each: each mark is reported with the pause frame
           the CHIP is given after the word before it -- the marks are as far apart as those frames, to PLACE_MS
  frames   the chip's writes, a mark before every word against none, at rates 0, 50 and 100 with the intonation
           full and flat.  A text of one clause: EVERY frame (register 0 with the R1-R4 in force, the pauses too) the
           unmarked text's, the times between them the same to STEP_MS (the audio is compared on two fresh units by
           tools/check_ssv_marks.py's "audio": one unit says the same text twice with other samples).  A text of three sentences: every phoneme the unmarked text's, in order; for each marked clause
           end ONE pause frame (C0h) put in front of the next clause, at most TWO more frames there with other
           duration, articulation or inflection bits (the same phoneme), and nothing more than SHIFT_MS later
  capital  a capital's pitch (ESC P printed before the marked text, and again after it): every frame as with the
           pitch and no marks, and the marks where they are without the pitch
  twins    two marks on one word are reported together
  end      a mark after the last word is reported with the utterance's last sample
  dense    a mark before every word of a long sentence (Orca's way): all reported once, in order, at as many places
  cancel   a cancel at four places in a marked utterance: nothing of it is reported afterwards, and the next
           utterance reports its own two marks, the second in the pause before its word

    python am_marks.py [ssi263speech library]

Controls (AM_MARKS_BREAK, am_voice.c amv_marks_break): 1 reports every mark with the first block (fails place, clause,
twins, end, dense, cancel); 2 gives the driver a Ctrl-F for every mark (fails same and dash); 3 leaves the marks
after a cancel (fails cancel); 4 puts the Ctrl-F inside the word it should follow (fails bytes, and frames: the
phonemes change).
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import voice_marks as vm  # noqa: E402  (the words in audio, the marks' structures)

ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
WINDOWS = sys.platform == "win32"
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
LIB = (_args[0] if _args else os.environ.get("SSI263_VOICES_LIB") or (
    os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll") if WINDOWS
    else os.path.join(REPO, "build", "linux", "libssi263_voices.so")))
DVC = os.environ.get("SSI263_ACCENT_DVC") or os.path.join(REPO, "firmware", "aicom-accent-mini", "SPKEMS.DVC")
BREAK = int(os.environ.get("AM_MARKS_BREAK", "0") or 0)
RATE = vm.RATE
MAX_BLOCKS = 3000
EDGE_MS = 45
STEP_MS = 0.5               # the time between two frames, marked against unmarked, in a text of one clause
PLACE_MS = 3                # a mark against the pause frame after the word before it (the host looks every 0.5 ms)
SHIFT_MS = 9.5              # what a marked clause end may delay: its one pause frame, 8.4 ms
PAUSE_IN = 0xC0             # ... which is this register 0 byte
Mark, Reached, Write = vm.Mark, vm.Reached, vm.Write
words_in, ms, marks_of = vm.words_in, vm.ms, vm.marks_of
PLACE = "one. nine. one. nine. one. nine."
CLAUSE = "one nine one nine one nine"


def load():
    lib = ctypes.CDLL(LIB)
    lib.amv_create_mem.restype = ctypes.c_void_p
    lib.amv_create_mem.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.amv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 6
    lib.amv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.amv_speak_marked.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int]
    lib.amv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.amv_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reached))]
    lib.amv_marks_exact.argtypes = [ctypes.c_void_p]
    lib.amv_cancel.argtypes = [ctypes.c_void_p]
    lib.amv_destroy.argtypes = [ctypes.c_void_p]
    lib.amv_host.restype = ctypes.c_void_p
    lib.amv_host.argtypes = [ctypes.c_void_p]
    lib.amh_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.amh_writes.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.amh_clear_writes.argtypes = [ctypes.c_void_p]
    lib.amv_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.amv_say_bytes_marked.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int,
                                         ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "amv_marks_break").value = BREAK
    return lib


def unit_bytes(lib, text, ats):
    buf = ctypes.create_string_buffer(8192)
    raw = text.encode("utf-8")
    if ats is None:
        n = lib.amv_say_bytes(raw, 1, buf, 8192)
    else:
        n = lib.amv_say_bytes_marked(raw, 1, marks_of(ats), len(ats), buf, 8192)
    return buf.raw[:max(n, 0)]


def split_marks(data):
    """(the line less its Ctrl-Fs, [(the word after each run of them, the run stands directly after a word and
    before a space, how many in the run, the word it follows)])"""
    plain, words, k = bytearray(), [], 0
    while k < len(data):
        if data[k] == 6:
            j = k
            while j < len(data) and data[j] == 6:
                j += 1
            e = j
            while e < len(data) and data[e] == 32:
                e += 1
            f = e
            while f < len(data) and data[f] not in (32, 6):
                f += 1
            b = k
            while b > 0 and data[b - 1] != 32:
                b -= 1
            words.append((data[e:f].decode("latin-1"), k > 0 and data[k - 1] != 32 and j < len(data) and data[j] == 32,
                          j - k, data[b:k].decode("latin-1")))
            k = j
            continue
        plain.append(data[k])
        k += 1
    return bytes(plain), words


def at(text, piece, inside=0):
    return len(text[:text.index(piece)].encode("utf-8")) + inside


def run(lib, v, text, ats, cancel_after=None, offset=0):
    """Speaks; (samples, [(id, absolute sample, exact, block index)], done, reports after a cancel)."""
    raw = text.encode("utf-8")
    if ats is not None:
        lib.amv_speak_marked(v, raw, offset, marks_of(ats), len(ats))
    else:
        lib.amv_speak(v, raw, offset)
    pcm, done, out, got, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], 0
    r = ctypes.POINTER(Reached)()
    after = 0
    while not done.value and blocks < MAX_BLOCKS:
        n = lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.amv_reached(v, ctypes.byref(r))):
            got.append((r[k].id, len(out) + r[k].sample if 0 <= r[k].sample <= n else -1, r[k].exact, blocks))
        out.extend(pcm[i] for i in range(n))
        blocks += 1
        if cancel_after is not None and blocks == cancel_after:
            lib.amv_cancel(v)
            after = lib.amv_reached(v, ctypes.byref(r))
            lib.amv_render(v, ctypes.byref(pcm), ctypes.byref(done))    # a caller's next poll: nothing more of it
            after += lib.amv_reached(v, ctypes.byref(r))
            break
    return out, got, bool(done.value), after


def every_word(text):
    ats, p = [], 0
    for w in text.split(" "):
        ats.append(p)
        p += len(w) + 1
    return ats


def against(a, b):
    """The marked text's frames b against the plain text's a, each (t, register 0's byte, (R1, R2, R3, R4)):
    (pause frames put in, [other frames that differ: (the phoneme is the same, text)], the largest delay in ms)"""
    i = j = ins = 0
    other, shift = [], 0.0
    while i < len(a) and j < len(b):
        if a[i][1:] == b[j][1:]:
            shift = max(shift, abs((b[j][0] - b[0][0]) - (a[i][0] - a[0][0])))
            i, j = i + 1, j + 1
        elif b[j][1] == PAUSE_IN and a[i][1] != PAUSE_IN:
            ins, j = ins + 1, j + 1
        else:
            other.append(((a[i][1] & 0x3F) == (b[j][1] & 0x3F), "frame %d: %02X %s, with marks %02X %s" % (
                i, a[i][1], "%02X %02X %02X %02X" % a[i][2], b[j][1], "%02X %02X %02X %02X" % b[j][2])))
            i, j = i + 1, j + 1
    if i < len(a) or j < len(b):
        other.append((False, "%d and %d frames left over" % (len(a) - i, len(b) - j)))
    return ins, other, 1000 * shift


def main():
    if not os.path.isfile(LIB):
        sys.exit("no library: %s" % LIB)
    lib = load()
    bad = [0]

    def check(name, ok, what):
        bad[0] += not ok
        print("%-4s %-7s %s" % ("ok" if ok else "FAIL", name, what))

    texts = ["Hello there. This is a test of the marks, and it goes on for a while, word after word after word.",
             "I paid $12.50 for 3 books.", "café naïve — done", "  leading and trailing  ",
             "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen"]
    check("none", all(unit_bytes(lib, t, []) == unit_bytes(lib, t, None) for t in texts),
          "no marks: the driver's line is amv_say_bytes's for %d texts" % len(texts))
    first = lambda t, piece: unit_bytes(lib, t[t.index(piece):], None).split(b" ")[0].decode("latin-1")   # noqa: E731
    cases = [
        ("one two three", [(at("one two three", "two"), "two")]),
        ("one two three", [(at("one two three", "two", -1), "two")]),
        ("one two three", [(at("one two three", "three", 2), "three")]),
        ("one two three", [(0, None), (len("one two three"), None)]),
        ("Hello there. This is it.", [(at("Hello there. This is it.", "This"), "This")]),
        ("we have 125 of them", [(at("we have 125 of them", "25"), first("we have 125 of them", "125"))]),
        ("we have 125 of them", [(at("we have 125 of them", "of"), "of")]),
    ]
    wrong = []
    for text, ms_ in cases:
        ats = [a for a, _ in ms_]
        plain, words = split_marks(unit_bytes(lib, text, ats))
        want = []
        for _, w in ms_:
            if w is not None and w not in want:
                want.append(w)
        if plain != unit_bytes(lib, text, None):
            wrong.append("%r: the words changed" % text)
        elif [w[0] for w in words] != want or not all(after and n == 1 for _, after, n, _ in words):
            wrong.append("%r %s: Ctrl-F before %s (directly after a word: %s), wanted %s" % (
                text, ats, [w[0] for w in words], [w[1] for w in words], want))
    check("bytes", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d cases: the words unchanged, each Ctrl-F directly after the word before its own" % len(cases))
    t = "one two three"
    plain, words = split_marks(unit_bytes(lib, t, [at(t, "two"), at(t, "two"), at(t, "two", 1)]))
    check("same", len(words) == 1 and words[0][2] == 1 and words[0][0] == "two",
          "three marks on one word: %s" % (["%d Ctrl-F before %r" % (w[2], w[0]) for w in words] or "none"))
    t = "it is. No - it is not."
    plain, words = split_marks(unit_bytes(lib, t, [at(t, "- "), at(t, "it is not")]))
    check("dash", len(words) == 1 and words[0][2] == 1 and words[0][3] == "No" and plain == unit_bytes(lib, t, None),
          "a mark on a dash alone and one on the word after it: %s" % (
              ["%d Ctrl-F after %r" % (w[2], w[3]) for w in words] or "none"))

    if not os.path.isfile(DVC):
        print("skip the unit's cases: the Accent-mini's driver is not in the firmware folder")
        print("am marks: %s" % ("PASS (bytes only)" if not bad[0] else "%d FAILED" % bad[0]))
        sys.exit(1 if bad[0] else 0)
    dvc = open(DVC, "rb").read()
    err = ctypes.create_string_buffer(256)
    v = lib.amv_create_mem(dvc, len(dvc), float(RATE), err, 256)
    if not v:
        sys.exit("boot failed: %s" % err.value)
    h = lib.amv_host(v)
    if not lib.amv_marks_exact(v):
        sys.exit("FAIL: the host does not know this driver's count of marks")

    def settings(rate=50, inflection=100):
        lib.amv_set(v, rate, 50, inflection, 100, 1, 5)

    settings()
    run(lib, v, "Warm up.", None)

    words, ats = PLACE.split(" "), every_word(PLACE)
    ref, _, _, _ = run(lib, v, PLACE, None)
    y, got, done, _ = run(lib, v, PLACE, ats)
    rw, w = words_in(ref), words_in(y)
    if len(rw) != len(words) or len(w) != len(words):
        ok, what = False, "words found: %d without marks, %d with (%d wanted)" % (len(rw), len(w), len(words))
    else:
        ok = done and [g[0] for g in got] == [100 + k for k in range(len(ats))] and all(g[2] == 1 for g in got)
        where = []
        for k, g in enumerate(got):
            if k == 0:
                ok = ok and g[1] == 0 and g[3] == 0
                where.append("word 1 at sample %d of block %d" % (g[1], g[3]))
            else:
                lo, hi = w[k - 1][1] - RATE * EDGE_MS // 1000, w[k][0] + RATE * EDGE_MS // 1000
                ok = ok and lo <= g[1] <= hi
                where.append("word %d at %d in %d-%d" % (k + 1, ms(g[1]), ms(w[k - 1][1]), ms(w[k][0])))
        what = "%d of %d reported, done %s; ms: %s" % (len(got), len(ats), done, ", ".join(where))
    check("place", ok, "a mark before every word, each in the pause before it: " + what)

    def frames(text, ats, offset=0):
        """speaks; ([(chip time, register 0's byte, (R1, R2, R3, R4) in force)] outside the chip's control mode,
        the marks, done, the samples)"""
        lib.amh_clear_writes(h)
        y, got, done, _ = run(lib, v, text, ats, offset=offset)
        p = ctypes.POINTER(Write)()
        regs, out = {1: 0, 2: 0, 3: 0, 4: 0}, []
        for k in range(lib.amh_writes(h, ctypes.byref(p))):
            if p[k].reg in regs:
                regs[p[k].reg] = p[k].val
            elif p[k].reg == 0 and not regs[3] & 0x80:
                out.append((p[k].t, p[k].val, (regs[1], regs[2], regs[3], regs[4])))
        return out, got, done, y

    lib.amh_set_int(h, b"log_writes", 1)
    # where the chip is given the pause frame after each word: the driver counts the mark that follows the word there
    b, got, done, y = frames(CLAUSE, every_word(CLAUSE))
    pauses, sounding = [], False
    for t, val, _ in b:
        if val & 0x3F:
            sounding = True
        elif sounding:
            pauses.append(t)
            sounding = False
    ok = (done and [g[0] for g in got] == [100 + k for k in range(6)] and all(g[2] == 1 for g in got)
          and got[0][1] == 0 and got[0][3] == 0 and len(pauses) >= 5)
    off = []
    if ok:
        for k in range(2, 6):
            off.append(ms(got[k][1] - got[1][1]) - 1000.0 * (pauses[k - 1] - pauses[0]))
        ok = all(abs(d) <= PLACE_MS for d in off) and got[1][1] > 0
    check("clause", ok, "a mark before every word of one clause, each with the pause frame after the word before it: %s" % (
        "words 3 to 6 off by %s ms (counted from word 2's)" % ", ".join("%+.1f" % d for d in off) if off else
        "%d pause frames after words (5 wanted), %d of 6 reported, done %s" % (len(pauses), len(got), done)))

    wrong, runs, worst, worst_plain, most, most_other = [], 0, 0.0, 0.0, 0.0, 0
    for inflection in (100, 0):
        for rate in (0, 50, 100):
            settings(rate, inflection)
            run(lib, v, "Warm up.", None)
            text = "one, nine, one (nine) one nine"
            a0, _, _, _ = frames(text, None)
            a, _, da, _ = frames(text, None)
            b, got, db, _ = frames(text, every_word(text))
            ins, other, shift = against(a, b)
            twice = against(a0, a)
            if not twice[0] and not twice[1]:
                worst_plain = max(worst_plain, twice[2])
            runs += 1
            worst = max(worst, shift)
            if ins or other or shift > STEP_MS or not (da and db) or len(got) != 6:
                wrong.append("intonation %d rate %d, one clause: %d pause frames put in, %d other frames differ%s, up to "
                             "%.1f ms apart, %d of 6 marks" % (
                                 inflection, rate, ins, len(other), " (%s)" % other[0][1] if other else "", shift,
                                 len(got)))
            text = "Hello there. This is the second sentence of it. And a third one, with a comma in it."
            ats = every_word(text)
            a, _, da, _ = frames(text, None)
            b, got, db, _ = frames(text, ats)
            ins, other, shift = against(a, b)
            runs += 1
            most, most_other = max(most, shift), max(most_other, len(other))
            phonemes = [f[1] & 0x3F for f in a if f[1] & 0x3F] == [f[1] & 0x3F for f in b if f[1] & 0x3F]
            if not (phonemes and ins == 2 and len(other) <= 4 and all(o[0] for o in other) and shift <= 2 * SHIFT_MS
                    and da and db and len(got) == len(ats)):
                wrong.append("intonation %d rate %d, three sentences: phonemes %s, %d pause frames put in (2 wanted), %d "
                             "other frames differ%s, up to %.1f ms later, %d of %d marks" % (
                                 inflection, rate, "the same" if phonemes else "DIFFER", ins, len(other),
                                 " (%s)" % other[0][1] if other else "", shift, len(got), len(ats)))
    settings()
    run(lib, v, "Warm up.", None)
    check("frames", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d marked utterances (rates 0, 50, 100; the intonation full and flat).  One clause: every frame and its "
          "R1-R4 as without marks, the times the same to %.1f ms (the same text twice without marks: %.1f ms).  "
          "Three sentences: every phoneme the same; one pause frame put in after each of the 2 marked "
          "clause ends, up to %d more frames there with other bits, nothing more than %.1f ms later" % (
              runs, worst, worst_plain, most_other, most))

    _, plain_got, _, _ = frames(CLAUSE, every_word(CLAUSE))
    frames(CLAUSE, None, offset=20)                         # (the first text after another pitch begins with the
    a, _, _, _ = frames(CLAUSE, None, offset=20)            # old one in its first frame, marks or none)
    b, got, done, _ = frames(CLAUSE, every_word(CLAUSE), offset=20)
    apart = max([abs(ms(g[1] - p[1])) for g, p in zip(got, plain_got)] or [0])
    check("capital", done and [g[0] for g in got] == [100 + k for k in range(6)] and all(g[2] == 1 for g in got)
          and len(plain_got) == 6 and apart <= 35 and [f[1:] for f in a] == [f[1:] for f in b],
          "a pitch command printed before the marked text: %d frames, %s; %d of 6 marks, up to %d ms from where they "
          "are without it" % (len(b), "each as with the pitch and no marks" if [f[1:] for f in a] == [f[1:] for f in b]
                               else "NOT as with the pitch and no marks", len(got), apart))
    lib.amh_set_int(h, b"log_writes", 0)
    lib.amh_clear_writes(h)

    text = "one. nine. one."
    y, got, done, _ = run(lib, v, text, [5, 5, 11])
    check("twins", done and [g[0] for g in got] == [100, 101, 102] and got[0][1] == got[1][1] < got[2][1] < len(y),
          "two marks on one word and one on the next: reported at %s ms of %d" % (
              ", ".join("%d" % ms(g[1]) for g in got), ms(len(y))))

    y, got, done, _ = run(lib, v, text, [len(text)])
    check("end", done and len(got) == 1 and got[0][0] == 100 and got[0][1] == len(y) and got[0][2] == 1,
          "a mark after the last word: reported at sample %s of %d" % (got[0][1] if got else "never", len(y)))

    text = "this is a long line of plain words that goes on and on, and then it goes on some more, until it ends"
    ats = every_word(text)
    y, got, done, _ = run(lib, v, text, ats)
    ids, times = [g[0] for g in got], [g[1] for g in got]
    spread = len(set(times))
    ok = (done and ids == [100 + k for k in range(len(ats))] and all(times[k] <= times[k + 1] for k in range(len(times) - 1))
          and min(times) >= 0 and spread >= len(ats) - 2 and times[-1] > 0.8 * len(y) * (len(ats) - 1) / len(ats))
    check("dense", ok, "%d marks, one a word: %d reported in order at %d places, the last at %d of %d ms" % (
        len(ats), len(got), spread, ms(times[-1]) if times else -1, ms(len(y))))

    ats = every_word(PLACE)
    wrong, said = [], []
    for stop in (4, 12, 30, 45):
        y, got, done, after = run(lib, v, PLACE, ats, cancel_after=stop)
        y2, got2, done2, _ = run(lib, v, "nine. one.", [0, 6])
        w2 = words_in(y2)
        ok = (after == 0 and len(got) < 6 and done2 and [g[0] for g in got2] == [100, 101] and len(w2) == 2
              and got2[0][1] == 0
              and w2[0][1] - RATE * EDGE_MS // 1000 <= got2[1][1] <= w2[1][0] + RATE * EDGE_MS // 1000)
        said.append("%d of 6" % len(got))
        if not ok:
            wrong.append("cancelled after %d blocks (%d of 6 marks): %d reported after it (0 wanted); the next "
                         "utterance's: %s, its words %s" % (stop, len(got), after, [(g[0], int(ms(g[1]))) for g in got2],
                                                            ["%d-%d" % (ms(a), ms(b)) for a, b in w2]))
    check("cancel", not wrong, "; ".join(wrong[:2]) if wrong else
          "cancelled at four places (%s marks reported before): none after, and the next utterance's two marks "
          "in place" % ", ".join(said))
    lib.amv_destroy(v)
    print("am marks: %s" % ("PASS" if not bad[0] else "%d FAILED" % bad[0]))
    sys.exit(1 if bad[0] else 0)


if __name__ == "__main__":
    main()
