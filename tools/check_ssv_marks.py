"""Index marks through the voice table (src/csrc/voices.h ssv_speak_marked, ssv_reached; src/csrc/marks.h), on every
voice whose firmware is in the firmware folder.

  none     no marks: ssv_speak_marked is ssv_speak -- the same samples, byte for byte (two fresh units given the same
           utterances: a unit is not steady from one utterance to the next, a fresh one from the same start is),
           nothing reported
  once     a mark before every word: each reported exactly once, in the caller's order, its sample inside its block
  kind     exact on a Braille Lite whose release was measured for its own marks (blazie:blazie), estimated on every
           other voice -- and on that Braille Lite too while it is run ahead of the listener
  audio    estimated marks send the unit nothing, and the audio rendered ahead comes back whole and in order: the
           samples are the unmarked utterance's, byte for byte (fresh units, as "none")
  place    estimated: no mark before its share of the utterance (to a block), none later than the unit takes to
           finish ahead of the caller (an eighth of the utterance and two blocks); a mark at offset 0 at sample 0
           of the first block
  empty    a text with nothing to say: ssv_speak_marked returns 0, and the one render after it reports done and
           every mark
  cancel   a cancel in a marked utterance: the next render reports none of its marks; the next utterance reports
           its own, all of them

    python tools/check_ssv_marks.py [library] [--firmware DIR]

Controls: SSV_MARKS_BREAK=1 (voices.c) -- the estimate reports every mark at once: "place" must fail on every
estimated voice.  SSV_MARKS_BREAK=queue -- the estimate returns the start of its buffer for every block: "audio"
must fail on every estimated voice.  SSV_MARKS_OWN_BREAK=1 (bl_voice.c blv_marks_break 6) -- a Braille Lite whose release was not
measured for its own marks uses them all the same: "kind" must fail on the Spanish unit (whose marks then come early,
"place": what the release table's marks column keeps away).
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ARCH = "x64" if sys.maxsize > 2 ** 32 else "x86"
WINDOWS = sys.platform == "win32"
args = [a for a in sys.argv[1:]]
FW = os.path.join(ROOT, "firmware")
if "--firmware" in args:
    k = args.index("--firmware")
    FW = args[k + 1]
    del args[k:k + 2]
LIB = args[0] if args else os.environ.get("SSI263_LIB") or (
    os.path.join(ROOT, "build", "win", ARCH, "ssi263speech.dll") if WINDOWS
    else os.path.join(ROOT, "build", "linux", "libssi263speech.so"))
VOICES = ["blazie:blazie", "blazie:blazie_es", "blazie:blazie_de", "speakout:speakout", "accentmini:sa", "accentmini:mini",
          "mockingboard:mockingboard", "smartalk:smartalk"]
OWN = {"blazie:blazie"}             # the voices whose unit's own marks are used (bl_languages.txt's marks column)
TEXT = "this is a plain line of words, and then it goes on some more, until it ends"
MAX_BLOCKS = 1500
AHEAD = 8                           # voices.c AHEAD


class Mark(ctypes.Structure):
    _fields_ = [("at", ctypes.c_int), ("id", ctypes.c_int)]


class Reach(ctypes.Structure):
    _fields_ = [("id", ctypes.c_int), ("sample", ctypes.c_int), ("exact", ctypes.c_int)]


class Settings(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("rate", "pitch", "volume", "tone", "pack", "join", "inflection",
                                            "numbers", "run_ahead", "line_lift")]


def load():
    lib = ctypes.CDLL(LIB)
    lib.ssv_find.argtypes = [ctypes.c_char_p]
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.ssv_create.restype = ctypes.c_void_p
    lib.ssv_create.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.ssv_defaults.argtypes = [ctypes.c_int, ctypes.POINTER(Settings)]
    lib.ssv_sample_rate.argtypes = [ctypes.c_void_p]
    lib.ssv_speak.argtypes = [ctypes.c_void_p, ctypes.POINTER(Settings), ctypes.c_char_p, ctypes.c_int]
    lib.ssv_speak_marked.argtypes = [ctypes.c_void_p, ctypes.POINTER(Settings), ctypes.c_char_p, ctypes.c_int,
                                     ctypes.POINTER(Mark), ctypes.c_int]
    lib.ssv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                               ctypes.POINTER(ctypes.c_int)]
    lib.ssv_reached.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(Reach))]
    lib.ssv_cancel.argtypes = [ctypes.c_void_p]
    lib.ssv_destroy.argtypes = [ctypes.c_void_p]
    if os.environ.get("SSV_MARKS_OWN_BREAK"):        # bl_voice.c's control 6: every echoing release's marks its own
        ctypes.c_int.in_dll(lib, "blv_marks_break").value = 6
    return lib


def marks_of(ats):
    arr = (Mark * max(1, len(ats)))()
    for k, a in enumerate(ats):
        arr[k].at, arr[k].id = a, 100 + k
    return arr


def run(lib, v, s, text, ats, cancel_after=None):
    """(the speak's return, samples as bytes, [(id, absolute sample, exact, block, sample ok)], blocks' lengths, done,
    marks reported by the render after a cancel)"""
    raw = text.encode("utf-8")
    r = (lib.ssv_speak(v, s, raw, 0) if ats is None else
         lib.ssv_speak_marked(v, s, raw, 0, marks_of(ats), len(ats)))
    pcm, done, out, got, sizes = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], [], []
    p = ctypes.POINTER(Reach)()
    total, after = 0, 0
    while r >= 0 and not done.value and len(sizes) < MAX_BLOCKS:
        n = lib.ssv_render(v, ctypes.byref(pcm), ctypes.byref(done))
        for k in range(lib.ssv_reached(v, ctypes.byref(p))):
            got.append((p[k].id, total + p[k].sample, p[k].exact, len(sizes), 0 <= p[k].sample <= n))
        if n:
            out.append(ctypes.string_at(pcm, 2 * n))
        total += n
        sizes.append(n)
        if cancel_after is not None and len(sizes) == cancel_after:
            lib.ssv_cancel(v)
            lib.ssv_render(v, ctypes.byref(pcm), ctypes.byref(done))
            after = lib.ssv_reached(v, ctypes.byref(p))
            break
    return r, b"".join(out), got, sizes, bool(done.value), after


def fresh(lib, i, ats):
    """A new unit of voice i: "Warm up.", then TEXT with ssv_speak (ats None) or ssv_speak_marked; TEXT's samples."""
    err = ctypes.create_string_buffer(300)
    v = lib.ssv_create(i, os.fsencode(FW), None, err, 300)
    if not v:
        return None
    s = Settings()
    lib.ssv_defaults(i, ctypes.byref(s))
    run(lib, v, s, "Warm up.", None)
    _, y, _, _, done, _ = run(lib, v, s, TEXT, ats)
    lib.ssv_destroy(v)
    return y if done else None


def main():
    if not os.path.isfile(LIB):
        sys.exit("no library: %s" % LIB)
    lib = load()
    bad, ran = 0, 0
    words, ats, p = TEXT.split(" "), [], 0
    for w in words:
        ats.append(p)
        p += len(w) + 1
    for vid in VOICES:
        i = lib.ssv_find(vid.encode())
        if i < 0 or not lib.ssv_available(i, os.fsencode(FW)):
            print("skip %s: its firmware is not in the firmware folder" % vid)
            continue
        err = ctypes.create_string_buffer(300)
        v = lib.ssv_create(i, os.fsencode(FW), None, err, 300)
        if not v:
            print("FAIL %-26s create: %s" % (vid, err.value.decode(errors="replace")))
            bad += 1
            continue
        ran += 1
        s = Settings()
        lib.ssv_defaults(i, ctypes.byref(s))
        block = lib.ssv_sample_rate(v) * 30 // 1000
        own = vid in OWN

        def check(name, ok, what):
            nonlocal bad
            bad += not ok
            print("%-4s %-26s %-6s %s" % ("ok" if ok else "FAIL", vid, name, what))

        plain, again, zero = fresh(lib, i, None), fresh(lib, i, None), fresh(lib, i, [])
        if not plain or plain != again:
            check("none", False, "two fresh units did not say the same text the same: the comparison cannot run")
        else:
            check("none", zero == plain, "no marks: %s as ssv_speak's (%d samples)" % (
                "the same samples" if zero == plain else "NOT the same samples", len(plain) // 2))
        run(lib, v, s, "Warm up.", None)
        _, _, got0, _, _, _ = run(lib, v, s, TEXT, [])
        check("none", not got0, "no marks: %d reported" % len(got0))
        r, y, got, sizes, done, _ = run(lib, v, s, TEXT, ats)
        total = len(y) // 2
        ids = [g[0] for g in got]
        check("once", done and ids == [100 + k for k in range(len(ats))] and all(g[4] for g in got)
              and all(got[k][1] <= got[k + 1][1] for k in range(len(got) - 1)),
              "%d marks: %d reported, in order %s, inside their blocks %s" % (
                  len(ats), len(got), ids == sorted(ids) and len(set(ids)) == len(ids), all(g[4] for g in got)))
        exact = sorted(set(g[2] for g in got))
        check("kind", exact == [1 if own else 0], "%s (wanted %s)" % (
            "exact" if exact == [1] else "estimated" if exact == [0] else "mixed %s" % exact, "exact" if own else "estimated"))
        if not own:
            marked = fresh(lib, i, ats)
            check("audio", bool(plain) and marked == plain, "a mark before every word: %s as without (%d against %d)" % (
                "the same samples" if marked == plain else "NOT the same samples", len(marked or b"") // 2,
                len(plain or b"") // 2))
            early, late = [], []
            if len(got) == len(ats) and total:
                for k, g in enumerate(got):
                    want = float(ats[k]) / len(TEXT) * total
                    if g[1] < want - block:
                        early.append(k)
                    if g[1] > max(want, total / float(AHEAD)) + 2 * block:
                        late.append(k)
            first = bool(got) and got[0][1] == 0 and got[0][3] == 0
            check("place", len(got) == len(ats) and not early and not late and first,
                  "early %s, late %s, the first at sample %s of block %s; the last at %.2f of the utterance" % (
                      early or "none", late or "none", got[0][1] if got else "-", got[0][3] if got else "-",
                      got[-1][1] / float(total or 1) if got else 0))
        r, y, got, sizes, done, _ = run(lib, v, s, "   ", [0, 1, 3])
        check("empty", r == 0 and len(sizes) == 1 and done and [g[0] for g in got] == [100, 101, 102] and not y,
              "nothing to say: speak %d, %d render, done %s, %d of 3 marks reported" % (r, len(sizes), done, len(got)))
        r, y, got, sizes, done, after = run(lib, v, s, TEXT, ats, cancel_after=10)
        first = len(got)
        r2, y2, got2, _, done2, _ = run(lib, v, s, "it ends here", [0, 3, 8])
        check("cancel", after == 0 and first < len(ats) and done2 and [g[0] for g in got2] == [100, 101, 102],
              "cancelled after %d of %d marks, %d reported after it (0 wanted); the next utterance's: %s" % (
                  first, len(ats), after, [g[0] for g in got2]))
        if own:
            s.run_ahead, s.pack = 1, 1
            r, y, got, sizes, done, _ = run(lib, v, s, TEXT, ats)
            s.run_ahead = 0
            exact = sorted(set(g[2] for g in got))
            check("kind", done and exact == [0] and [g[0] for g in got] == [100 + k for k in range(len(ats))],
                  "run ahead: %s, %d of %d reported" % ("estimated" if exact == [0] else "exact %s" % exact, len(got), len(ats)))
        lib.ssv_destroy(v)
    if not ran:
        print("no voice's firmware is here: nothing was checked")
    print("ssv marks: %s" % ("PASS (%d voices)" % ran if not bad else "%d FAILED" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
