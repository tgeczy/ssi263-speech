"""Index marks in the Speak-Out voice (src/csrc/marks.h, speakout/so_voice.h sov_speak_marked): the box's own mark,
05h 5Bh xx, at the front of the word a mark stands before, and each mark reported where the box's reply comes on its
serial line.

  none     no marks: the bytes for the box are sov_say_bytes's, byte for byte
  bytes    with marks: the bytes less the marks' three bytes each are the unmarked bytes (a mark never changes the
           words), every mark stands at the front of a word, after the space before it, and it is the word the case
           names -- a mark inside a word, or inside an amount that is rewritten, at the start of it
  same     three marks on one word are ONE mark to the box
  place    "one, nine, one, nine, one, nine" with a mark before each word (the short pauses off, so that the box
           pauses audibly at the commas): each is reported in the pause before its word, as the AUDIO has it (the
           words found by level), the first at sample 0 of the first block
  lines    a mark on the first word of a second sentence and on the word after it: each in the pause before its word
  frames   the chip's writes, a mark before every word against none, at rates 0, 50 and 100, "Join phrases" and the
           short pauses on and off, a one-sentence and a three-sentence text: every phoneme load, with its R1 to R4,
           the unmarked text's, in order; the times between phoneme loads the same to STEP_MS
  twins    two marks on one word are reported together, where the box's one reply for them comes
  end      a mark after the last word is reported with the utterance's last sample
  dense    a mark before every word of a long sentence (Orca's way): all reported once, in order, at as many places
  cancel   a cancel at four places in a marked utterance: nothing of it is reported afterwards, and the next
           utterance reports its own two marks, the second in the pause before its word

    python so_marks.py [ssi263speech library] [--soundingboard]

--soundingboard: the same cases on GW Micro's Sounding Board, the Speak-Out's front end behind its DOS drivers
(SBLOAD.COM, SB.COM), whose marks the host reads from the driver's own count of marks crossed and the last one's
value (sb_host.h sbh_replies) -- what its status call, INT 17h AH=02h, reports.

Controls (SO_MARKS_BREAK, so_voice.c sov_marks_break): 1 reports every mark with the first block (fails place, lines,
end, dense, cancel); 2 gives the box a mark for every one of the caller's (fails bytes and same); 3 leaves the marks
after a cancel (fails cancel); 4 puts the box's mark inside its word (fails bytes, and frames: the phonemes change).
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
SB = "--soundingboard" in sys.argv[1:]
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
LIB = (_args[0] if _args else os.environ.get("SSI263_VOICES_LIB") or (
    os.path.join(REPO, "build", "win", ARCH, "ssi263speech.dll") if WINDOWS
    else os.path.join(REPO, "build", "linux", "libssi263_voices.so")))
HEX = os.environ.get("SSI263_SPEAKOUT_HEX") or os.path.join(REPO, "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
SB_DIR = os.environ.get("SSI263_SOUNDINGBOARD_DIR") or os.path.join(REPO, "firmware", "gw-micro-sounding-board")
HOST = "sbh" if SB else "soh"          # the host's calls: sb_host.h's or so_host.h's
BREAK = int(os.environ.get("SO_MARKS_BREAK", "0") or 0)
RATE = vm.RATE
MAX_BLOCKS = 2000
EDGE_MS = 45
STEP_MS = 2.5               # the time between two phoneme loads, marked against unmarked: the host's step is 0.5 ms,
                            # and the same text said twice WITHOUT marks differs by as much as the case prints
Mark, Reached, Write = vm.Mark, vm.Reached, vm.Write
words_in, ms, marks_of = vm.words_in, vm.ms, vm.marks_of


def load():
    lib = ctypes.CDLL(LIB)
    lib.sov_create.restype = ctypes.c_void_p
    lib.sov_create.argtypes = [ctypes.c_char_p, ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
    lib.sov_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 6
    lib.sov_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.sov_speak_marked.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Mark), ctypes.c_int]
    lib.sov_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.sov_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reached))]
    lib.sov_marks_exact.argtypes = [ctypes.c_void_p]
    lib.sov_cancel.argtypes = [ctypes.c_void_p]
    lib.sov_destroy.argtypes = [ctypes.c_void_p]
    lib.sov_host.restype = ctypes.c_void_p
    lib.sov_host.argtypes = [ctypes.c_void_p]
    if SB:
        lib.sov_create_sb.restype = ctypes.c_void_p
        lib.sov_create_sb.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
                                      ctypes.c_double, ctypes.c_char_p, ctypes.c_int]
        lib.sov_sb_host.restype = ctypes.c_void_p
        lib.sov_sb_host.argtypes = [ctypes.c_void_p]
    getattr(lib, HOST + "_set_int").argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    getattr(lib, HOST + "_writes").argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Write))]
    getattr(lib, HOST + "_clear_writes").argtypes = [ctypes.c_void_p]
    lib.sov_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.sov_say_bytes_marked.argtypes = [ctypes.c_char_p, ctypes.POINTER(Mark), ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "sov_marks_break").value = BREAK
    return lib


def box_bytes(lib, text, ats):
    buf = ctypes.create_string_buffer(8192)
    raw = text.encode("utf-8")
    if ats is None:
        n = lib.sov_say_bytes(raw, buf, 8192)
    else:
        n = lib.sov_say_bytes_marked(raw, marks_of(ats), len(ats), buf, 8192)
    return buf.raw[:n]


def split_marks(data):
    """(the bytes less the marks, [(the word after each mark, it stands after a space, the marks in a row there)])"""
    plain, words, k = bytearray(), [], 0
    while k < len(data):
        if data[k] == 5 and k + 2 < len(data) and data[k + 1] == 0x5B:
            j, n = k, 0
            while j + 2 < len(data) and data[j] == 5 and data[j + 1] == 0x5B:
                j += 3
                n += 1
            e = j
            while e < len(data) and data[e] not in (32, 13):
                e += 1
            words.append((data[j:e].decode("latin-1"), k > 0 and data[k - 1] == 32, n))
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
    said = lib.sov_speak_marked(v, raw, 0, marks_of(ats), len(ats)) if ats is not None else lib.sov_speak(v, raw, 0)
    pcm, done, out, got, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], 0
    r = ctypes.POINTER(Reached)()
    after = 0
    while not done.value and blocks < MAX_BLOCKS:
        n = lib.sov_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.sov_reached(v, ctypes.byref(r))):
            got.append((r[k].id, len(out) + r[k].sample if 0 <= r[k].sample <= n else -1, r[k].exact, blocks))
        out.extend(pcm[i] for i in range(n))
        blocks += 1
        if cancel_after is not None and blocks == cancel_after:
            lib.sov_cancel(v)
            after = lib.sov_reached(v, ctypes.byref(r))
            lib.sov_render(v, ctypes.byref(pcm), ctypes.byref(done))    # a caller's next poll: nothing more of it
            after += lib.sov_reached(v, ctypes.byref(r))
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
    check("none", all(box_bytes(lib, t, []) == box_bytes(lib, t, None) for t in texts),
          "no marks: the box's bytes are sov_say_bytes's for %d texts" % len(texts))
    cases = [
        ("one two three", [(at("one two three", "two"), "two")]),
        ("one two three", [(at("one two three", "two", -1), "two")]),
        ("one two three", [(at("one two three", "three", 2), "three")]),
        ("one two three", [(0, None), (len("one two three"), None)]),
        ("Hello there. This is it.", [(at("Hello there. This is it.", "This"), "This")]),
        ("pay $12.50 today", [(at("pay $12.50 today", "12"), "$12.50"), (at("pay $12.50 today", "50"), "$12.50")]),
        ("pay £5.50 today", [(at("pay £5.50 today", "50"), "5")]),
        ("pay £5.50 today", [(at("pay £5.50 today", "today"), "today")]),
        ("café naïve now", [(at("café naïve now", "ïve"), "naive")]),
    ]
    wrong = []
    for text, ms_ in cases:
        ats = [a for a, _ in ms_]
        plain, words = split_marks(box_bytes(lib, text, ats))
        want = []
        for _, w in ms_:
            if w is not None and w not in want:
                want.append(w)
        if plain != box_bytes(lib, text, None):
            wrong.append("%r: the words changed" % text)
        elif [w for w, _, _ in words] != want or not all(front and n == 1 for _, front, n in words):
            wrong.append("%r %s: marks before %s (after a space: %s), wanted %s" % (
                text, ats, [w for w, _, _ in words], [f for _, f, _ in words], want))
    check("bytes", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d cases: the words unchanged, each mark at the front of its word, after the space" % len(cases))
    t = "one two three"
    plain, words = split_marks(box_bytes(lib, t, [at(t, "two"), at(t, "two"), at(t, "two", 1)]))
    check("same", len(words) == 1 and words[0][2] == 1 and words[0][0] == "two",
          "three marks on one word: %s" % (["%d mark before %r" % (n, w) for w, _, n in words] or "none"))

    files = [os.path.join(SB_DIR, n) for n in ("SBLOAD.COM", "SB.COM")] if SB else [HEX]
    if not all(os.path.isfile(f) for f in files):
        print("skip the box's cases: %s not in the firmware folder" % ", ".join(os.path.basename(f) for f in files))
        print("so marks: %s" % ("PASS (bytes only)" if not bad[0] else "%d FAILED" % bad[0]))
        sys.exit(1 if bad[0] else 0)
    err = ctypes.create_string_buffer(256)
    if SB:
        a, b = (open(f, "rb").read() for f in files)
        v = lib.sov_create_sb(a, len(a), b, len(b), float(RATE), err, 256)
    else:
        v = lib.sov_create(os.fsencode(HEX), float(RATE), err, 256)
    if not v:
        sys.exit("boot failed: %s" % err.value)
    h = lib.sov_sb_host(v) if SB else lib.sov_host(v)
    set_int, writes, clear_writes = (getattr(lib, HOST + n) for n in ("_set_int", "_writes", "_clear_writes"))
    exact = lib.sov_marks_exact(v)

    def settings(rate=50, join=1, short=1):
        lib.sov_set(v, rate, 50, 8, 100, join, short)

    settings(short=0)                    # the box pauses audibly at a comma or a full stop only with its short
    run(lib, v, "Warm up.", None)        # pauses off: the cases that find the words in the audio need them

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

    ok, what = placed("one, nine, one, nine, one, nine", range(6))
    check("place", ok and exact == 1, "a mark before every word, each in the pause before it: " + what)
    ok, what = placed("one, nine. nine, one, nine", (2, 3))
    check("lines", ok, "the second sentence's first word and the next: " + what)

    def frames(text, ats):
        clear_writes(h)
        _, got, done, _ = run(lib, v, text, ats)
        p = ctypes.POINTER(Write)()
        regs, out = {}, []
        for k in range(writes(h, ctypes.byref(p))):
            regs[p[k].reg] = p[k].val
            if p[k].reg == 0 and (p[k].val & 0x3F):                # a phoneme load that is not a pause
                out.append((p[k].t, p[k].val, regs.get(1), regs.get(2), regs.get(3), regs.get(4)))
        return out, got, done

    set_int(h, b"log_writes", 1)
    wrong, worst_all, worst_plain, runs = [], 0.0, 0.0, 0
    for join, short in ((1, 1), (0, 1), (1, 0), (0, 0)):
        for rate in (0, 50, 100):
            settings(rate, join, short)
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
                worst_all = max(worst_all, worst)
                if not (same and da and db and len(got) == len(ats) and worst <= STEP_MS):
                    wrong.append("join %d short pauses %d rate %d %r: phonemes %s (%d/%d), worst interval %.1f ms, "
                                 "%d of %d marks" % (join, short, rate, text[:12], "the same" if same else "DIFFER",
                                                     len(a), len(b), worst, len(got), len(ats)))
    set_int(h, b"log_writes", 0)
    clear_writes(h)
    settings(short=0)                    # the box pauses audibly at a comma or a full stop only with its short
    run(lib, v, "Warm up.", None)        # pauses off: the cases that find the words in the audio need them
    check("frames", not wrong, "; ".join(wrong[:3]) if wrong else
          "%d marked utterances (rates 0, 50, 100; Join phrases and short pauses on and off): every phoneme load and its "
          "R1-R4 as without marks, the times between them the same to %.1f ms (the same text twice without marks: "
          "%.1f ms)" % (runs, worst_all, worst_plain))

    text = "one, nine, one"
    y, got, done, _ = run(lib, v, text, [5, 5, 11])
    check("twins", done and [g[0] for g in got] == [100, 101, 102] and got[0][1] == got[1][1] < got[2][1] < len(y),
          "two marks on one word and one on the next: reported at %s ms of %d" % (
              ", ".join("%d" % ms(g[1]) for g in got), ms(len(y))))

    text = "one, nine, one"
    y, got, done, _ = run(lib, v, text, [len(text)])
    check("end", done and len(got) == 1 and got[0][0] == 100 and got[0][1] == len(y) and got[0][2] == 1,
          "a mark after the last word: reported at sample %s of %d" % (got[0][1] if got else "never", len(y)))

    text = "this is a long line of plain words that goes on and on, and then it goes on some more, until it ends"
    ats = every_word(text)
    ref, _, _, _ = run(lib, v, text, None)
    y, got, done, _ = run(lib, v, text, ats)
    ids, times = [g[0] for g in got], [g[1] for g in got]
    spread = len(set(times))
    ok = (done and ids == [100 + k for k in range(len(ats))] and all(times[k] <= times[k + 1] for k in range(len(times) - 1))
          and min(times) >= 0 and spread >= len(ats) - 2 and times[-1] > 0.8 * len(y) * (len(ats) - 1) / len(ats)
          and abs(ms(len(y) - len(ref))) <= 45)
    check("dense", ok, "%d marks, one a word: %d reported in order at %d places, the last at %d of %d ms (%d ms without marks)" % (
        len(ats), len(got), spread, ms(times[-1]) if times else -1, ms(len(y)), ms(len(ref))))

    text = "one, nine, one, nine, one, nine"
    ats = [0, 5, 11, 16, 22, 27]
    wrong, said = [], []
    for stop in (4, 12, 30, 45):
        y, got, done, after = run(lib, v, text, ats, cancel_after=stop)
        y2, got2, done2, _ = run(lib, v, "nine, one", [0, 6])
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
    lib.sov_destroy(v)
    print("so marks: %s" % ("PASS" if not bad[0] else "%d FAILED" % bad[0]))
    sys.exit(1 if bad[0] else 0)


if __name__ == "__main__":
    main()
