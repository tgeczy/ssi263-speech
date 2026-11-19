"""Index marks in the Braille Lite voice (src/csrc/marks.h, bl_voice.h blv_speak_marked): the unit's own Ctrl-F, glued
to the front of the word a mark stands before, and each mark reported where the unit echoes it.

  none     no marks: the bytes for the unit are blv_say_bytes's, byte for byte
  bytes    with marks: the bytes less the marks' Ctrl-Fs are the unmarked bytes (a mark never changes the words), every
           mark's Ctrl-F stands at the front of a word, and it is the word the case names -- a mark inside a word, or
           inside an amount of money that is rewritten, at the start of it
  same     three marks on one word are ONE Ctrl-F to the unit
  place    "one, nine, one, nine, one, nine" with a mark before each word: each is reported in the pause before its
           word, as the AUDIO has it (the words found by level), the first at sample 0 of the first block
  speech   that marked utterance's six words are each as long as without marks, to SAME_MS (the level's step)
  lines    a mark on the first word of a second sentence and on the word after it, short pauses off (the sentence
           a unit line of its own) and on: each in the pause before its word
  frames   the chip's writes, a mark before every word against none, at rates 0, 50 and 100, short pauses off and
           on, a one-sentence and a three-sentence text: every phoneme load and its R2, R3, R4 the same; R1 different
           on at most one phoneme a sentence (the unit starts a sentence's final fall a phoneme late when a mark
           stands where it begins: its own doing, bl_voice.h); at rates 0 and 50 the times between phoneme loads
           the same to STEP_MS and one pause frame added for each Ctrl-F
  end      a mark after the last word is reported once the last word has begun, and before done
  dense    a mark before every word of a long sentence (Orca's way): all reported once, in order, never earlier than
           the one before, at as many places
  cancel   a cancel at four places in a marked utterance: nothing of it is reported afterwards, and the next
           utterance reports its own two marks, the second in the pause before its word
  ahead    run ahead of the listener the unit's echoes come early: blv_marks_exact says 0, the text is sent without
           Ctrl-Fs of marks and none is reported (the voice table estimates them)

    python voice_marks.py

Controls (VOICE_MARKS_BREAK, bl_voice.c blv_marks_break): 1 reports every mark in the first block (fails place,
lines, end, dense, cancel); 2 gives the unit a Ctrl-F for every mark (fails bytes and same); 3 leaves the marks after
a cancel (fails cancel); 4 takes a mark inside a run of text where it stands (fails bytes: a mark in a rewritten
amount lands inside it); 5 puts the Ctrl-F inside its word (fails bytes, and frames: the phonemes change).
"""
import ctypes
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
WINDOWS = sys.platform == "win32"
DLL = os.environ.get("SSI263_LIB") or (os.path.join(REPO, "nvda", "dist", "blazie-lib", ARCH, "bl.dll") if WINDOWS
                                        else os.path.join(REPO, "build", "linux", "libssi263speech.so"))
CHIP = os.path.join(REPO, "src", "ssi263", "_bin", ARCH, "ssi263.dll")
FW = os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie")
BREAK = int(os.environ.get("VOICE_MARKS_BREAK", "0") or 0)
RATE = 22050
MAX_BLOCKS = 2000
LOUD = 0.02                 # 10 ms RMS (of full scale) that is a word; these words' vowels are 0.1 and more
GAP_MS = 60                 # a quieter stretch this long parts two words (the unit's comma pause is 250 ms and more)
EDGE_MS = 45                # a mark's report against the pause the audio shows: a block and a half
SAME_MS = 22                # a word's length, marked against unmarked: two steps of the 10 ms level; the same text
                            # twice without marks differs by as much (a word 230 then 220 ms, the last word's
                            # start 2,176 then 2,156 ms after the first's)
STEP_MS = 1.5               # the time between two phoneme loads, marked against unmarked (0.5 ms: the host's step)


class Mark(ctypes.Structure):
    _fields_ = [("at", ctypes.c_int), ("id", ctypes.c_int)]


class Reached(ctypes.Structure):
    _fields_ = [("id", ctypes.c_int), ("sample", ctypes.c_int), ("exact", ctypes.c_int)]


class Write(ctypes.Structure):
    _fields_ = [("t", ctypes.c_double), ("reg", ctypes.c_int), ("val", ctypes.c_int)]


def load():
    if WINDOWS:
        ctypes.CDLL(CHIP)
    lib = ctypes.CDLL(DLL)
    lib.blv_create.restype = ctypes.c_void_p
    lib.blv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_double, ctypes.c_int,
                               ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.blv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 5
    lib.blv_set_run_ahead.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.blv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.blv_speak_marked.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(Mark), ctypes.c_int]
    lib.blv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.blv_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reached))]
    lib.blv_marks_exact.argtypes = [ctypes.c_void_p]
    lib.blv_cancel.argtypes = [ctypes.c_void_p]
    lib.blv_destroy.argtypes = [ctypes.c_void_p]
    lib.blv_host.restype = ctypes.c_void_p
    lib.blv_host.argtypes = [ctypes.c_void_p]
    lib.bh_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.bh_writes.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Write))]
    lib.bh_clear_writes.argtypes = [ctypes.c_void_p]
    lib.blv_say_bytes.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.blv_say_bytes_marked.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(Mark),
                                         ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    ctypes.c_int.in_dll(lib, "blv_marks_break").value = BREAK
    return lib


def marks_of(ats):
    arr = (Mark * max(1, len(ats)))()
    for k, at in enumerate(ats):
        arr[k].at, arr[k].id = at, 100 + k
    return arr


def unit_bytes(lib, text, ats, pack):
    buf = ctypes.create_string_buffer(8192)
    raw = text.encode("utf-8")
    if ats is None:
        n = lib.blv_say_bytes(raw, 0, pack, buf, 8192)
    else:
        n = lib.blv_say_bytes_marked(raw, 0, pack, marks_of(ats), len(ats), buf, 8192)
    return buf.raw[:n]


def split_marks(data):
    """(the bytes less the marks' Ctrl-Fs, the word that follows each mark's Ctrl-F): a line's own Ctrl-F follows its
    carriage return, a mark's never does."""
    plain, words, k = bytearray(), [], 0
    while k < len(data):
        if data[k] == 6 and not (k and data[k - 1] == 13):
            before = data[k - 1] if k else 6
            j = k + 1
            while j < len(data) and data[j] == 6:
                j += 1
            e = j
            while e < len(data) and data[e] not in (32, 13):
                e += 1
            words.append((data[j:e].decode("latin-1"), before in (32, 6), j - k))
            k += 1
            continue
        plain.append(data[k])
        k += 1
    return bytes(plain), words


def at(text, piece, inside=0):
    """The byte offset of `piece` in the text (plus `inside`)."""
    return len(text[:text.index(piece)].encode("utf-8")) + inside


def run(lib, v, text, ats, cancel_after=None):
    """Speaks; (samples, [(id, absolute sample, exact, block index)], done, reports after a cancel)."""
    raw = text.encode("utf-8")
    lines = lib.blv_speak_marked(v, raw, marks_of(ats), len(ats)) if ats is not None else lib.blv_speak(v, raw)
    pcm, done, out, got, blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], 0
    r = ctypes.POINTER(Reached)()
    after = 0
    while lines >= 0 and not done.value and blocks < MAX_BLOCKS:
        n = lib.blv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.blv_reached(v, ctypes.byref(r))):
            if not 0 <= r[k].sample <= n:
                got.append((r[k].id, -1, r[k].exact, blocks))          # outside its block: never right
            else:
                got.append((r[k].id, len(out) + r[k].sample, r[k].exact, blocks))
        out.extend(pcm[i] for i in range(n))
        blocks += 1
        if cancel_after is not None and blocks == cancel_after:
            lib.blv_cancel(v)
            after = lib.blv_reached(v, ctypes.byref(r))
            lib.blv_render(v, ctypes.byref(pcm), ctypes.byref(done))    # a caller's next poll: nothing more of it
            after += lib.blv_reached(v, ctypes.byref(r))
            break
    return out, got, bool(done.value), after


def words_in(y):
    """[(first sample, last sample)] of the stretches whose 10 ms RMS is over LOUD, parted by GAP_MS or more."""
    w = RATE // 100
    loud = []
    for k in range(0, len(y) - w + 1, w):
        s = sum(float(a) * a for a in y[k:k + w]) / w
        loud.append((s ** 0.5) / 32768.0 > LOUD)
    out, start, quiet = [], None, 0
    for k, on in enumerate(loud):
        if on:
            if start is None:
                start = k
            last, quiet = k, 0
        elif start is not None:
            quiet += 1
            if quiet * 10 >= GAP_MS:
                out.append((start * w, (last + 1) * w))
                start = None
    if start is not None:
        out.append((start * w, (last + 1) * w))
    return out


def ms(n):
    return 1000.0 * n / RATE


def main():
    lib = load()
    bad = [0]

    def check(name, ok, what):
        bad[0] += not ok
        print("%-4s %-7s %s" % ("ok" if ok else "FAIL", name, what))

    # ---- the bytes: no unit needed ------------------------------------------------------------------------------
    texts = ["Hello there. This is a test of the marks, and it goes on for a while, word after word after word.",
             "I paid $12.50 for 3 books.", "café naïve — done", "  leading and trailing  ",
             "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen"]
    same = all(unit_bytes(lib, t, [], p) == unit_bytes(lib, t, None, p) for t in texts for p in (0, 1))
    check("none", same, "no marks: the unit's bytes are blv_say_bytes's for %d texts, short pauses off and on" % len(texts))

    cases = [  # (text, [(offset, the word its Ctrl-F must stand before, or None: before the first word or after the last)])
        ("one two three", [(at("one two three", "two"), "two")]),
        ("one two three", [(at("one two three", "two", -1), "two")]),          # the space before it
        ("one two three", [(at("one two three", "three", 2), "three")]),       # inside a word: its start
        ("one two three", [(0, None), (len("one two three"), None)]),
        ("Hello there. This is it.", [(at("Hello there. This is it.", "This"), "This")]),     # a line's start
        ("pay $12.50 today", [(at("pay $12.50 today", "12"), "$12.50"), (at("pay $12.50 today", "50"), "$12.50")]),
        ("pay £5.50 today", [(at("pay £5.50 today", "50"), "5")]),         # rewritten whole: its first word
        ("pay £5.50 today", [(at("pay £5.50 today", "today"), "today")]),
        ("café naïve now", [(at("café naïve now", "ïve"), "naive")]),
    ]
    wrong = []
    for text, ms_ in cases:
        ats = [a for a, _ in ms_]
        for p in (0, 1):
            data = unit_bytes(lib, text, ats, p)
            plain, words = split_marks(data)
            want = []
            for _, w in ms_:
                if w is not None and w not in want:
                    want.append(w)
            if plain != unit_bytes(lib, text, None, p):
                wrong.append("%r: the words changed" % text)
            elif [w for w, _, _ in words] != want or not all(front and n == 1 for _, front, n in words):
                wrong.append("%r %s: Ctrl-F before %s, wanted %s" % (text, ats, [w for w, _, _ in words], want))
    check("bytes", not wrong, "; ".join(wrong) if wrong else
          "%d cases: the words unchanged, each Ctrl-F at the front of its word" % len(cases))
    t = "one two three"
    plain, words = split_marks(unit_bytes(lib, t, [at(t, "two"), at(t, "two"), at(t, "two", 1)], 0))
    check("same", len(words) == 1 and words[0][2] == 1 and words[0][0] == "two",
          "three marks on one word: %s" % (["%d Ctrl-F before %r" % (n, w) for w, _, n in words] or "none"))

    # ---- the unit -----------------------------------------------------------------------------------------------
    missing = [n for n in ("BL2ENG.BNS", "bl2_2003_warm.state") if not os.path.isfile(os.path.join(FW, n))]
    if missing:
        print("skip the unit's cases: %s not in the firmware folder" % ", ".join(missing))
        print("voice marks: %s" % ("PASS (bytes only)" if not bad[0] else "%d FAILED" % bad[0]))
        sys.exit(1 if bad[0] else 0)
    with tempfile.TemporaryDirectory() as d:
        for n in ("BL2ENG.BNS", "bl2_2003_warm.state"):
            shutil.copy(os.path.join(FW, n), d)
        err = ctypes.create_string_buffer(256)
        v = lib.blv_create(os.path.join(d, "BL2ENG.BNS").encode(), os.path.join(d, "bl2_2003_warm.state").encode(),
                           0, float(RATE), 1, 0, err, 256)
        if not v:
            sys.exit("boot failed: %s" % err.value)
        lib.blv_set(v, 50, 50, 7, 100, 1)
        run(lib, v, "Warm up.", None)
        h = lib.blv_host(v)

        def placed(text, which, pack):
            """A mark before each word of `which` (indexes into the text's words): (ok, what) -- every mark reported
            in the pause before its word, as the audio of the same text without marks and with them has its words."""
            lib.blv_set(v, 50, 50, 7, 100, pack)
            words, ats, p = text.split(" "), [], 0
            for k, w in enumerate(words):
                if k in which:
                    ats.append(p)
                p += len(w) + 1
            ref, _, _, _ = run(lib, v, text, None)
            y, got, done, _ = run(lib, v, text, ats)
            rw, w = words_in(ref), words_in(y)
            if len(rw) != len(words) or len(w) != len(words):
                return False, "words found: %d without marks, %d with (%d wanted)" % (len(rw), len(w), len(words)), w, rw
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
            return ok, "%d of %d reported, done %s; ms: %s" % (len(got), len(ats), done, ", ".join(where)), w, rw

        text = "one, nine, one, nine, one, nine"
        ok, what, w, ref_w = placed(text, range(6), 1)
        check("place", ok, "a mark before every word, each in the pause before it: " + what)
        if len(w) == 6 and len(ref_w) == 6:
            diff = [abs(ms((w[k][1] - w[k][0]) - (ref_w[k][1] - ref_w[k][0]))) for k in range(6)]
            lag = ms((w[5][0] - w[0][0]) - (ref_w[5][0] - ref_w[0][0]))
            check("speech", max(diff) <= SAME_MS and abs(lag) <= SAME_MS,
                  "the six words as long as without marks to %.0f ms; the last starts %+.0f ms after the first" % (
                      max(diff), lag))
        else:
            check("speech", False, "the words were not found")
        # across a sentence's end (a unit line's, with short pauses off; the second sentence packed with short pauses
        # on): the first word of the second sentence and the word after it
        for pack in (0, 1):
            ok, what, _, _ = placed("one, nine. nine, one, nine", (2, 3), pack)
            check("lines", ok, "short pauses %s: the second sentence's first word and the next: %s" % (
                "on" if pack else "off", what))
        lib.blv_set(v, 50, 50, 7, 100, 1)

        # the chip's writes, marked against unmarked
        def frames(text, ats):
            lib.bh_clear_writes(h)
            _, got, done, _ = run(lib, v, text, ats)
            p = ctypes.POINTER(Write)()
            regs, out = {}, []
            for k in range(lib.bh_writes(h, ctypes.byref(p))):
                regs[p[k].reg] = p[k].val
                if p[k].reg == 0:
                    out.append((p[k].t, p[k].val, regs.get(1), regs.get(2), regs.get(3), regs.get(4)))
            return out, got, done

        lib.bh_set_int(h, b"log_writes", 1)
        wrong, r1_total, worst_all, runs = [], 0, 0.0, 0
        for pack in (0, 1):
            for rate in (0, 50, 100):
                lib.blv_set(v, rate, 50, 7, 100, pack)
                run(lib, v, "Warm up.", None)
                for text, sentences in (("one, nine, one, nine, one, nine", 1),
                                        ("Hello there. This is the second sentence of it. And a third one, with a comma in it.", 3)):
                    ats, p = [], 0
                    for wd in text.split(" "):
                        ats.append(p)
                        p += len(wd) + 1
                    a, _, da = frames(text, None)
                    b, got, db = frames(text, ats)
                    sa = [f for f in a if f[1] & 0x3F]             # the phoneme loads that are not a pause
                    sb = [f for f in b if f[1] & 0x3F]
                    same = [(f[1], f[3], f[4], f[5]) for f in sa] == [(f[1], f[3], f[4], f[5]) for f in sb]
                    r1 = sum(1 for x, y in zip(sa, sb) if x[2] != y[2]) if same else 0
                    worst = max([abs((sa[k][0] - sa[k - 1][0]) - (sb[k][0] - sb[k - 1][0]))
                                 for k in range(1, len(sa))] or [0.0]) * 1000 if same else 0.0
                    added = (len(b) - len(sb)) - (len(a) - len(sa))
                    runs += 1
                    r1_total += r1
                    if rate != 100:
                        worst_all = max(worst_all, worst)
                    if not (same and da and db and len(got) == len(ats) and r1 <= sentences
                            and (rate == 100 or (worst <= STEP_MS and added == len(ats) - 1))):
                        wrong.append("short pauses %d rate %d %r: phonemes %s (%d/%d), R1 differs on %d (at most %d), "
                                     "worst interval %.1f ms, %+d pause frames for %d Ctrl-Fs" % (
                                         pack, rate, text[:12], "the same" if same else "DIFFER", len(sa), len(sb), r1,
                                         sentences, worst, added, len(ats) - 1))
        lib.bh_set_int(h, b"log_writes", 0)
        lib.bh_clear_writes(h)
        lib.blv_set(v, 50, 50, 7, 100, 1)
        run(lib, v, "Warm up.", None)
        check("frames", not wrong, "; ".join(wrong[:3]) if wrong else
              "%d marked utterances (rates 0, 50, 100; short pauses off, on): every phoneme and its R2-R4 as without "
              "marks, R1 a phoneme late on %d, the times between phonemes the same to %.1f ms at rates 0 and 50, one "
              "pause frame a Ctrl-F" % (runs, r1_total, worst_all))

        text = "one, nine, one"
        y, got, done, _ = run(lib, v, text, [len(text)])
        w = words_in(y)
        ok = len(w) == 3 and done and len(got) == 1 and got[0][0] == 100 and got[0][1] >= w[2][0]
        check("end", ok, "a mark after the last word: reported at %s ms, the last word %s" % (
            "%d" % ms(got[0][1]) if got else "never", "%d-%d ms" % (ms(w[2][0]), ms(w[2][1])) if len(w) == 3 else "not found"))

        text = "this is a long line of plain words that goes on and on, and then it goes on some more, until it ends"
        ats, p = [], 0
        for wd in text.split(" "):
            ats.append(p)
            p += len(wd) + 1
        ref, _, _, _ = run(lib, v, text, None)
        y, got, done, _ = run(lib, v, text, ats)
        ids = [g[0] for g in got]
        times = [g[1] for g in got]
        spread = len(set(times))
        ok = (done and ids == [100 + k for k in range(len(ats))] and all(times[k] <= times[k + 1] for k in range(len(times) - 1))
              and min(times) >= 0 and spread >= len(ats) - 2 and times[-1] > 0.8 * len(y) * (len(ats) - 1) / len(ats)
              and abs(ms(len(y) - len(ref))) <= 2 * SAME_MS)
        check("dense", ok, "%d marks, one a word: %d reported in order at %d places, the last at %d of %d ms (%d ms without marks)" % (
            len(ats), len(got), spread, ms(times[-1]) if times else -1, ms(len(y)), ms(len(ref))))

        text = "one, nine, one, nine, one, nine"
        ats = [0, 5, 11, 16, 22, 27]
        wrong, said = [], []
        for stop in (4, 12, 30, 55):                 # in the first word, after it, in the middle, near the end
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

        lib.blv_set_run_ahead(v, 1)
        exact = lib.blv_marks_exact(v)
        y, got, done, _ = run(lib, v, "one, nine, one", [0, 5, 11])
        lib.blv_set_run_ahead(v, 0)
        back = lib.blv_marks_exact(v)
        check("ahead", exact == 0 and back == 1 and not got and done and len(words_in(y)) == 3,
              "run ahead: exact %d (0 wanted), %d marks reported (0 wanted), %d words heard; off again: exact %d" % (
                  exact, len(got), len(words_in(y)), back))
        lib.blv_destroy(v)
    print("voice marks: %s" % ("PASS" if not bad[0] else "%d FAILED" % bad[0]))
    sys.exit(1 if bad[0] else 0)


if __name__ == "__main__":
    main()
