"""Index marks in the SmarTalk voice (src/csrc/marks.h, smartalk/st_voice.h stv_speak_marked): the TSR's own Ctrl-F
directly after the word before each marked word, and each mark reported where the TSR's own count of marks passed
rises (st_host.h sth_replies).

  none     no marks: the parts for the TSR are stv_parts's, byte for byte
  bytes    with marks: the parts less their Ctrl-Fs are the unmarked parts (a mark never changes the words, nor where
           the text is cut), every Ctrl-F stands directly after a word, and the word after it is the one the case
           names -- a mark inside a word, or inside a number that is rewritten, at the start of it
  same     three marks on one word are ONE Ctrl-F
  cuts     a text of several parts with a mark before every word: the parts are cut where the unmarked text's are
  place    six words with a mark before each: each mark is reported where the CHIP begins its word.  The TSR runs
           its words together (no silence between them, and from "nine" into "one" not even a dip), so the audio
           cannot show a word's start: each word said alone gives its number of phoneme loads, and a mark must be
           as far from the second word's mark as its word's first load is from the second word's (PLACE_MS); the
           first at sample 0 of the first block
  frames   the chip's writes, a mark before every word against none, at rates 0, 50 and 100 with the inflection on
           and off, a one-sentence and a three-sentence text: every phoneme load with its R1 to R4 the unmarked
           text's, in order; at rates 0 and 50 the times between phoneme loads the same to STEP_MS.  At rate 100
           (the TSR's digit 9) a mark can move a 4.3 ms pause, which is printed, not judged; and there the TSR gives
           the phonemes one of two duration settings in turn, starting with one or the other by what was said
           before, so the two duration bits of R0 are compared at rates 0 and 50 only
  twins    two marks on one word are reported together
  end      a mark after the last word is reported with the utterance's last sample
  dense    a mark before every word of a long sentence (Orca's way): all reported once, in order, at as many places
  cancel   a cancel at four places in a marked utterance: nothing of it is reported afterwards, and the next
           utterance reports its own two marks, the second where it is without a cancel before it (to a block)

    python st_marks.py [ssi263speech library]

Controls (ST_MARKS_BREAK, st_voice.c stv_marks_break): 1 reports every mark with the first block (fails place, twins,
end, dense, cancel); 2 gives the TSR a Ctrl-F for every mark (fails same); 3 leaves the marks after a cancel (fails
cancel); 4 puts the Ctrl-F inside the marked word (fails bytes, and frames: the phonemes change).
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
EXE = os.environ.get("SSI263_SMARTALK_EXE") or os.path.join(REPO, "firmware", "automated-functions-smartalk", "S_TALK.EXE")
BREAK = int(os.environ.get("ST_MARKS_BREAK", "0") or 0)
RATE = vm.RATE
MAX_BLOCKS = 3000
EDGE_MS = 45
PLACE_MS = 6                # a mark against its word's first phoneme load, both counted from the second word's
STEP_MS = 2.5               # the time between two phoneme loads, marked against unmarked (the case prints what the
                            # same text twice without marks differs by)
Mark, Reached, Write = vm.Mark, vm.Reached, vm.Write
words_in, ms, marks_of = vm.words_in, vm.ms, vm.marks_of
PLACE = "one. nine. one. nine. one. nine."


def load():
    lib = ctypes.CDLL(LIB)
    lib.stv_create.restype = ctypes.c_void_p
    lib.stv_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.stv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 6
    lib.stv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.stv_speak_marked.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int]
    lib.stv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.stv_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reached))]
    lib.stv_cancel.argtypes = [ctypes.c_void_p]
    lib.stv_destroy.argtypes = [ctypes.c_void_p]
    lib.stv_host.restype = ctypes.c_void_p
    lib.stv_host.argtypes = [ctypes.c_void_p]
    lib.sth_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.sth_writes.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.sth_clear_writes.argtypes = [ctypes.c_void_p]
    lib.stv_parts.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.stv_parts_marked.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int,
                                     ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "stv_marks_break").value = BREAK
    return lib


def unit_bytes(lib, text, ats):
    buf = ctypes.create_string_buffer(8192)
    raw = text.encode("utf-8")
    if ats is None:
        n = lib.stv_parts(raw, 1, buf, 8192)
    else:
        n = lib.stv_parts_marked(raw, 1, marks_of(ats), len(ats), buf, 8192)
    return buf.raw[:max(n, 0)]


def split_marks(data):
    """(the parts less their Ctrl-Fs, [(the word after each run of them, the run stands directly after a word and
    before a space or a part's end, how many in the run)]); the parts are joined by "|" """
    plain, words, k = bytearray(), [], 0
    while k < len(data):
        if data[k] == 6:
            j = k
            while j < len(data) and data[j] == 6:
                j += 1
            e = j
            while e < len(data) and data[e] in (32, 124):        # the space (or the part's end) after the mark
                e += 1
            f = e
            while f < len(data) and data[f] not in (32, 124, 6):
                f += 1
            words.append((data[e:f].decode("latin-1"),
                          k > 0 and data[k - 1] not in (32, 124) and (j == len(data) or data[j] in (32, 124)), j - k))
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
        lib.stv_speak_marked(v, raw, 0, marks_of(ats), len(ats))
    else:
        lib.stv_speak(v, raw, 0)
    pcm, done, out, got, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], 0
    r = ctypes.POINTER(Reached)()
    after = 0
    while not done.value and blocks < MAX_BLOCKS:
        n = lib.stv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.stv_reached(v, ctypes.byref(r))):
            got.append((r[k].id, len(out) + r[k].sample if 0 <= r[k].sample <= n else -1, r[k].exact, blocks))
        out.extend(pcm[i] for i in range(n))
        blocks += 1
        if cancel_after is not None and blocks == cancel_after:
            lib.stv_cancel(v)
            after = lib.stv_reached(v, ctypes.byref(r))
            lib.stv_render(v, ctypes.byref(pcm), ctypes.byref(done))    # a caller's next poll: nothing more of it
            after += lib.stv_reached(v, ctypes.byref(r))
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
          "no marks: the TSR's parts are stv_parts's for %d texts" % len(texts))
    first = lambda t, piece: unit_bytes(lib, t[t.index(piece):], None).split(b" ")[0].split(b"|")[0].decode("latin-1")   # noqa: E731
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
            wrong.append("%r %s: Ctrl-F before %s (directly after a word: %s), wanted %s" % (
                text, ats, [w for w, _, _ in words], [f for _, f, _ in words], want))
    check("bytes", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d cases: the words unchanged, each Ctrl-F directly after the word before its own" % len(cases))
    t = "one two three"
    plain, words = split_marks(unit_bytes(lib, t, [at(t, "two"), at(t, "two"), at(t, "two", 1)]))
    check("same", len(words) == 1 and words[0][2] == 1 and words[0][0] == "two",
          "three marks on one word: %s" % (["%d Ctrl-F before %r" % (n, w) for w, _, n in words] or "none"))

    long_text = " ".join("word number %d of a long text, which goes on." % k for k in range(1, 31))
    cut = lambda b: [len(p) for p in b.split(b"|")]             # noqa: E731
    marked = unit_bytes(lib, long_text, every_word(long_text))
    plain_parts, _ = split_marks(marked)
    check("cuts", plain_parts == unit_bytes(lib, long_text, None) and len(cut(plain_parts)) > 3
          and marked.count(b"\x06") == len(long_text.split(" ")) - 1,
          "%d parts, cut where the unmarked text's are, %d Ctrl-Fs for %d words" % (
              len(cut(plain_parts)), marked.count(b"\x06"), len(long_text.split(" "))))

    if not os.path.isfile(EXE):
        print("skip the unit's cases: S_TALK.EXE is not in the firmware folder")
        print("st marks: %s" % ("PASS (bytes only)" if not bad[0] else "%d FAILED" % bad[0]))
        sys.exit(1 if bad[0] else 0)
    exe = open(EXE, "rb").read()
    err = ctypes.create_string_buffer(256)
    v = lib.stv_create(exe, len(exe), float(RATE), err, 256)
    if not v:
        sys.exit("boot failed: %s" % err.value)
    h = lib.stv_host(v)

    def settings(rate=50, inflection=100):
        lib.stv_set(v, rate, 50, 8, 100, inflection, 1)

    settings()
    run(lib, v, "Warm up.", None)

    def loads(text, ats):
        """speaks; (the chip times of the phoneme loads that are not pauses, the marks reported, the samples, done)"""
        lib.sth_set_int(h, b"log_writes", 1)
        lib.sth_clear_writes(h)
        y, got, done, _ = run(lib, v, text, ats)
        p = ctypes.POINTER(Write)()
        out = [p[k].t for k in range(lib.sth_writes(h, ctypes.byref(p))) if p[k].reg == 0 and (p[k].val & 0x3F)]
        lib.sth_set_int(h, b"log_writes", 0)
        lib.sth_clear_writes(h)
        return out, got, y, done

    # where each word begins, by the chip: the TSR runs its words together (no silence between them whatever stands
    # there, and from "nine" into "one" not even a dip in the level), so the audio cannot show it.  Each word said
    # alone gives its number of phoneme loads; in the sentence the k-th word then begins at a known load, and a mark
    # must be as far from the second word's mark as its word's first load is from the second word's
    n_one, n_nine = len(loads("one.", None)[0]), len(loads("nine.", None)[0])
    times, got, y, done = loads(PLACE, every_word(PLACE))
    starts = [0, n_one, n_one + n_nine, 2 * n_one + n_nine, 2 * n_one + 2 * n_nine, 3 * n_one + 2 * n_nine]
    ok = (done and len(times) == 3 * n_one + 3 * n_nine and [g[0] for g in got] == [100 + k for k in range(6)]
          and all(g[2] == 1 for g in got) and got[0][1] == 0 and got[0][3] == 0 and 0 < got[1][1])
    off = []
    if ok:
        for k in range(2, 6):
            d = ms(got[k][1] - got[1][1]) - 1000.0 * (times[starts[k]] - times[starts[1]])
            off.append(d)
            ok = ok and abs(d) <= PLACE_MS
    check("place", ok, "a mark before every word, each where the chip begins its word: %s" % (
        "words 3 to 6 off by %s ms (counted from word 2's)" % ", ".join("%+.1f" % d for d in off) if off else
        "%d loads (%d wanted), %d of 6 reported, done %s" % (len(times), 3 * n_one + 3 * n_nine, len(got), done)))
    _, ref2, _, _ = loads("nine. one.", [0, 6])

    def frames(text, ats):
        lib.sth_clear_writes(h)
        _, got, done, _ = run(lib, v, text, ats)
        p = ctypes.POINTER(Write)()
        regs, out = {}, []
        for k in range(lib.sth_writes(h, ctypes.byref(p))):
            regs[p[k].reg] = p[k].val
            if p[k].reg == 0 and (p[k].val & 0x3F):                # a phoneme load that is not a pause
                out.append((p[k].t, p[k].val, regs.get(1), regs.get(2), regs.get(3), regs.get(4)))
        return out, got, done

    lib.sth_set_int(h, b"log_writes", 1)
    wrong, worst_all, worst_plain, worst_fast, runs, turns = [], 0.0, 0.0, 0.0, 0, 0
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
                # the phoneme and its registers; R0's two duration bits apart: at rate 100 the TSR gives the phonemes
                # one of two durations in turn, and which comes first depends on what was said before
                code = lambda fs: [(f[1] & 0x3F,) + f[2:] for f in fs]          # noqa: E731
                same = code(a) == code(b) and (rate == 100 or [f[1] for f in a] == [f[1] for f in b])
                if rate == 100 and [f[1] for f in a0] != [f[1] for f in a]:
                    turns += 1
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
                    wrong.append("inflection %d rate %d %r: phonemes %s (%d/%d), worst interval %.1f ms, %d of %d marks" % (
                        inflection, rate, text[:12], "the same" if same else "DIFFER", len(a), len(b), worst,
                        len(got), len(ats)))
    lib.sth_set_int(h, b"log_writes", 0)
    lib.sth_clear_writes(h)
    settings()
    run(lib, v, "Warm up.", None)
    check("frames", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d marked utterances (rates 0, 50, 100; the inflection on and off): every phoneme load and its R1-R4 as "
          "without marks; at rates 0 and 50 the times between them the same to %.1f ms (the same text twice without "
          "marks: %.1f ms); at rate 100 up to %.1f ms apart, the phonemes' duration bits taking turns (so they did "
          "in %d of 4 texts said twice without marks)" % (runs, worst_all, worst_plain, worst_fast, turns))

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
        ok = (after == 0 and len(got) < 6 and done2 and [g[0] for g in got2] == [100, 101] and len(ref2) == 2
              and got2[0][1] == 0 and abs(ms(got2[1][1] - ref2[1][1])) <= 35)      # (a block: the cut's leftovers)
        said.append("%d of 6" % len(got))
        if not ok:
            wrong.append("cancelled after %d blocks (%d of 6 marks): %d reported after it (0 wanted); the next "
                         "utterance's: %s" % (stop, len(got), after, [(g[0], int(ms(g[1]))) for g in got2]))
    check("cancel", not wrong, "; ".join(wrong[:2]) if wrong else
          "cancelled at four places (%s marks reported before): none after, and the next utterance's two marks "
          "in place" % ", ".join(said))
    lib.stv_destroy(v)
    print("st marks: %s" % ("PASS" if not bad[0] else "%d FAILED" % bad[0]))
    sys.exit(1 if bad[0] else 0)


if __name__ == "__main__":
    main()
