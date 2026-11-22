"""Index marks in the Accent-PC voice (src/csrc/marks.h, accentpc/as_voice.h asv_speak_marked): the unit's own
Ctrl-F at the front of the word a mark stands before, and each mark reported where the unit's prompt for it comes on
its serial line -- in the audio the voice gives out, which holds some blocks back before it does.

  none     no marks: the line for the unit is asv_say_bytes's, byte for byte
  bytes    with marks: the line less its Ctrl-Fs is the unmarked line (a mark never changes the words), every Ctrl-F
           stands at the front of a word, after the space before it, and it is the word the case names -- a mark
           inside a word, or inside a number that is rewritten, at the start of it
  same     three marks on one word are ONE Ctrl-F (N with no word between them speak as N-1 carriage returns)
  place    six words with a full stop after each and a mark before each: each mark is reported in the pause before
           its word, as the AUDIO has it (the words found by level), the first at sample 0 of the first block
  frames   the chip's writes, a mark before every word against none, at rates 0, 50 and 100 with the intonation
           full and flat, a one-sentence and a three-sentence text: every phoneme load with its R1 to R4 the unmarked
           text's, in order; at rates 0 and 50 the times between phoneme loads the same to STEP_MS.  At rate 100
           the unit's pauses between sentences move with marks (it is busy there: up to 27 ms in this case, 28 to
           66 ms in a 300-word text), which is printed, not judged
  twins    two marks on one word are reported together
  end      a mark after the last word is reported with the utterance's last sample
  dense    a mark before every word of a long sentence (Orca's way): all reported once, in order, at as many places
  cancel   a cancel at four places in a marked utterance: nothing of it is reported afterwards, and the next
           utterance reports its own two marks, the second in the pause before its word

    python as_marks.py [ssi263speech library]

Controls (AS_MARKS_BREAK, as_voice.c asv_marks_break): 1 reports every mark with the first block (fails place, twins,
end, dense, cancel); 2 gives the unit a Ctrl-F for every mark (fails bytes and same); 3 leaves the marks after a
cancel (fails cancel); 4 puts the Ctrl-F inside its word (fails bytes, and frames: the phonemes change).
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
ROMS = os.environ.get("SSI263_ACCENT_PC_DIR") or os.path.join(REPO, "firmware", "aicom-accent-sa")
BREAK = int(os.environ.get("AS_MARKS_BREAK", "0") or 0)
RATE = vm.RATE
MAX_BLOCKS = 3000
EDGE_MS = 45
STEP_MS = 2.5               # the time between two phoneme loads, marked against unmarked (the case prints what the
                            # same text twice without marks differs by)
Mark, Reached, Write = vm.Mark, vm.Reached, vm.Write
words_in, ms, marks_of = vm.words_in, vm.ms, vm.marks_of
PLACE = "one. nine. one. nine. one. nine."


def load():
    lib = ctypes.CDLL(LIB)
    lib.asv_create.restype = ctypes.c_void_p
    lib.asv_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t] * 3 + [ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.asv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 5
    lib.asv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.asv_speak_marked.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int]
    lib.asv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.asv_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reached))]
    lib.asv_cancel.argtypes = [ctypes.c_void_p]
    lib.asv_destroy.argtypes = [ctypes.c_void_p]
    lib.asv_host.restype = ctypes.c_void_p
    lib.asv_host.argtypes = [ctypes.c_void_p]
    lib.ash_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ash_writes.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.ash_clear_writes.argtypes = [ctypes.c_void_p]
    lib.asv_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.asv_say_bytes_marked.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int,
                                         ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "asv_marks_break").value = BREAK
    return lib


def unit_bytes(lib, text, ats):
    buf = ctypes.create_string_buffer(8192)
    raw = text.encode("utf-8")
    if ats is None:
        n = lib.asv_say_bytes(raw, 1, buf, 8192)
    else:
        n = lib.asv_say_bytes_marked(raw, 1, marks_of(ats), len(ats), buf, 8192)
    return buf.raw[:max(n, 0)]


def split_marks(data):
    """(the line less its Ctrl-Fs, [(the word after each run of them, it stands after a space, how many in the run)])"""
    plain, words, k = bytearray(), [], 0
    while k < len(data):
        if data[k] == 6:
            j = k
            while j < len(data) and data[j] == 6:
                j += 1
            e = j
            while e < len(data) and data[e] != 32:
                e += 1
            words.append((data[j:e].decode("latin-1"), k > 0 and data[k - 1] == 32, j - k))
            k = j
            continue
        plain.append(data[k])
        k += 1
    return bytes(plain), words


def at(text, piece, inside=0):
    return len(text[:text.index(piece)].encode("utf-8")) + inside


def run(lib, v, text, ats, cancel_after=None):
    """Speaks; (samples, [(id, absolute sample, exact, block index)], done, reports after a cancel)."""
    raw = text.encode("utf-8")
    if ats is not None:
        lib.asv_speak_marked(v, raw, 0, marks_of(ats), len(ats))
    else:
        lib.asv_speak(v, raw, 0)
    pcm, done, out, got, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], 0
    r = ctypes.POINTER(Reached)()
    after = 0
    while not done.value and blocks < MAX_BLOCKS:
        n = lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.asv_reached(v, ctypes.byref(r))):
            got.append((r[k].id, len(out) + r[k].sample if 0 <= r[k].sample <= n else -1, r[k].exact, blocks))
        out.extend(pcm[i] for i in range(n))
        blocks += 1
        if cancel_after is not None and blocks == cancel_after:
            lib.asv_cancel(v)
            after = lib.asv_reached(v, ctypes.byref(r))
            lib.asv_render(v, ctypes.byref(pcm), ctypes.byref(done))    # a caller's next poll: nothing more of it
            after += lib.asv_reached(v, ctypes.byref(r))
            break
    return out, got, bool(done.value), after


def every_word(text):
    ats, p = [], 0
    for w in text.split(" "):
        ats.append(p)
        p += len(w) + 1
    return ats


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
          "no marks: the unit's line is asv_say_bytes's for %d texts" % len(texts))
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
        elif [w for w, _, _ in words] != want or not all(front and n == 1 for _, front, n in words):
            wrong.append("%r %s: Ctrl-F before %s (after a space: %s), wanted %s" % (
                text, ats, [w for w, _, _ in words], [f for _, f, _ in words], want))
    check("bytes", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d cases: the words unchanged, each Ctrl-F at the front of its word, after the space" % len(cases))
    t = "one two three"
    plain, words = split_marks(unit_bytes(lib, t, [at(t, "two"), at(t, "two"), at(t, "two", 1)]))
    check("same", len(words) == 1 and words[0][2] == 1 and words[0][0] == "two",
          "three marks on one word: %s" % (["%d Ctrl-F before %r" % (n, w) for w, _, n in words] or "none"))

    files = [os.path.join(ROMS, n) for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
    if not all(os.path.isfile(f) for f in files):
        print("skip the unit's cases: the Accent-PC's ROMs are not in the firmware folder")
        print("as marks: %s" % ("PASS (bytes only)" if not bad[0] else "%d FAILED" % bad[0]))
        sys.exit(1 if bad[0] else 0)
    roms = [open(f, "rb").read() for f in files]
    err = ctypes.create_string_buffer(256)
    v = lib.asv_create(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]), float(RATE), err, 256)
    if not v:
        sys.exit("boot failed: %s" % err.value)
    h = lib.asv_host(v)

    def settings(rate=50, inflection=100):
        lib.asv_set(v, rate, 50, inflection, 100, 1)

    settings()
    run(lib, v, "Warm up.", None)

    def placed(text, which):
        words, ats, p = text.split(" "), [], 0
        for k, w in enumerate(words):
            if k in which:
                ats.append(p)
            p += len(w) + 1
        ref, _, _, _ = run(lib, v, text, None)
        y, got, done, _ = run(lib, v, text, ats)
        rw, w = words_in(ref), words_in(y)
        if len(rw) != len(words) or len(w) != len(words):
            return False, "words found: %d without marks, %d with (%d wanted)" % (len(rw), len(w), len(words))
        ok = done and [g[0] for g in got] == [100 + k for k in range(len(ats))] and all(g[2] == 1 for g in got)
        where = []
        for g, k in zip(got, which):
            if k == 0:
                ok = ok and g[1] == 0 and g[3] == 0
                where.append("word 1 at sample %d of block %d" % (g[1], g[3]))
            else:
                lo, hi = w[k - 1][1] - RATE * EDGE_MS // 1000, w[k][0] + RATE * EDGE_MS // 1000
                ok = ok and lo <= g[1] <= hi
                where.append("word %d at %d in %d-%d" % (k + 1, ms(g[1]), ms(w[k - 1][1]), ms(w[k][0])))
        return ok, "%d of %d reported, done %s; ms: %s" % (len(got), len(ats), done, ", ".join(where))

    ok, what = placed(PLACE, range(6))
    check("place", ok, "a mark before every word, each in the pause before it: " + what)

    def frames(text, ats):
        lib.ash_clear_writes(h)
        _, got, done, _ = run(lib, v, text, ats)
        p = ctypes.POINTER(Write)()
        regs, out = {}, []
        for k in range(lib.ash_writes(h, ctypes.byref(p))):
            regs[p[k].reg] = p[k].val
            if p[k].reg == 0 and (p[k].val & 0x3F):                # a phoneme load that is not a pause
                out.append((p[k].t, p[k].val, regs.get(1), regs.get(2), regs.get(3), regs.get(4)))
        return out, got, done

    lib.ash_set_int(h, b"log_writes", 1)
    wrong, worst_all, worst_plain, worst_fast, runs = [], 0.0, 0.0, 0.0, 0
    for inflection in (100, 0):
        for rate in (0, 50, 100):
            settings(rate, inflection)
            run(lib, v, "Warm up.", None)
            for text in ("one, nine, one, nine, one, nine",
                         "Hello there. This is the second sentence of it. And a third one, with a comma in it."):
                ats = every_word(text)
                a0, _, _ = frames(text, None)
                a, _, da = frames(text, None)
                b, got, db = frames(text, ats)
                same = [f[1:] for f in a] == [f[1:] for f in b]
                if [f[1:] for f in a0] == [f[1:] for f in a]:
                    worst_plain = max([worst_plain] + [abs((a[k][0] - a[k - 1][0]) - (a0[k][0] - a0[k - 1][0])) * 1000
                                                       for k in range(1, len(a))])
                worst = max([abs((a[k][0] - a[k - 1][0]) - (b[k][0] - b[k - 1][0]))
                             for k in range(1, len(a))] or [0.0]) * 1000 if same else 0.0
                runs += 1
                if rate == 100:
                    worst_fast = max(worst_fast, worst)
                else:
                    worst_all = max(worst_all, worst)
                if not (same and da and db and len(got) == len(ats) and (rate == 100 or worst <= STEP_MS)):
                    wrong.append("intonation %d rate %d %r: phonemes %s (%d/%d), worst interval %.1f ms, %d of %d marks" % (
                        inflection, rate, text[:12], "the same" if same else "DIFFER", len(a), len(b), worst,
                        len(got), len(ats)))
    lib.ash_set_int(h, b"log_writes", 0)
    lib.ash_clear_writes(h)
    settings()
    run(lib, v, "Warm up.", None)
    check("frames", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d marked utterances (rates 0, 50, 100; the intonation full and flat): every phoneme load and its R1-R4 as "
          "without marks; at rates 0 and 50 the times between them the same to %.1f ms (the same text twice without "
          "marks: %.1f ms); at rate 100 a pause between sentences up to %.1f ms longer or shorter" % (
              runs, worst_all, worst_plain, worst_fast))

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
    lib.asv_destroy(v)
    print("as marks: %s" % ("PASS" if not bad[0] else "%d FAILED" % bad[0]))
    sys.exit(1 if bad[0] else 0)


if __name__ == "__main__":
    main()
