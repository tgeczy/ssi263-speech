"""The Accent-PC host in C (src/csrc/accentsa, MAME's 8085: hosts/accent_sa_c.py) against the Python host
(hosts/accent_sa.py, the reference), on a scripted session, the same chip model (SSI263C) on both sides.

    python src/csrc/accentsa/compare_accent_sa.py [--quick] [--only greeting|commands|driver]

The session, in three parts (three units, each from power-on):
  greeting  the "Accent ready." greeting heard out, flushed, then texts with punctuation, numbers and money in 50 ms
            blocks.  It holds one interrupt accepted at the end of the Python host's slice (below).
  commands  boot(), then twice a command (ESC R H, ESC P 3), 50 ms, and a short text: short, and it holds one too.
  driver    the add-on's way: boot() (the greeting flushed), the settings commands _speakJob sends (ESC R, P, V, M),
            texts in 30 ms blocks, a capital's pitch with snap_pitch, a cancel (Ctrl-X) in the middle of an
            utterance, then another text.
--quick: the greeting heard out and flushed without the texts after it, the commands part, and the driver part
without its middle texts.  --only: one part.

Predicates, for the C host as it stands (the Python host's counting, python_slices: the migration candidate): every
SSI-263 write identical in order and VALUE; every write's chip TIME identical, exactly; the audio identical block for
block; and the two counting differences between the hosts (as_board.h) seen as often on each side -- an acceptance that
ended the Python host's slice (its vector's instruction ran in the next slice) against the board's carried count, a
TRAP raised in EI's shadow (held one instruction by the Python core) against the board's held count.  The Python side
is counted by instrumenting its I8085.run; the C side by the board.

Reported and classified, not a predicate: the C host with the core's own counting (SSI263_ACCENT_SA_SLICES=chip,
experimental): which writes move, by how much, and after which counting event.

Must-fail controls: ACCENTSA_COMPARE_FLIP=1 flips one C write's value before comparing; ACCENTSA_COMPARE_SLICES=chip
compares the C host on the core's own counting, which the counting predicate must catch (its writes stay identical).

Where the carried acceptances fall depends on the chip's timing (boot() ends when A/R lets it).  If a retune of the chip
(params.py) moves them out of the commands part, the counting check fails for that reason, not the C host's:
    python src/csrc/accentsa/compare_accent_sa.py --find-split 300
runs the C host (fast) on that many sessions of boot(), then twice a command, 50 ms and a short text, and prints the
shortest that hold one; put its command and texts in COMMANDS below.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "src"))
from hosts import i8085 as I  # noqa: E402
from hosts.accent_sa import AccentSA  # noqa: E402
from hosts.accent_sa_c import _ESC, AccentSAC, _load  # noqa: E402
from ssi263.native import SSI263C  # noqa: E402

ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa")
BLOCK_S = 0.03
FLUSH = "\x1b=F\x1b=M\x18"
TEXTS = ["Hello, this is the Accent S A.",
         "It costs $30.50, or 1,234 dollars; call 555-1212!",
         "Is it 3:45 p.m.? Yes -- 100%.",
         "Numbers: 7, 42, 1999; the 3rd of May, (maybe) \"quoted\".",
         "A long sentence that the listener interrupts before it is over, because another one is waiting."]
PARTS = ("greeting", "commands", "driver")
COMMANDS = (("\x1bRH\x1bP3", "Yes."), ("\x1bRH\x1bP3", "Hello there."))   # rate H, pitch 3 (--find-split)
SPLIT = "acceptance ended the slice"
HELD = "TRAP in EI's shadow"


class SpyChip(SSI263C):
    """The Python host's chip, logging every write at its exact chip time."""

    def __init__(self, *a, **k):
        SSI263C.__init__(self, *a, **k)
        self.log = []

    def write(self, addr, value):
        SSI263C.write(self, addr, value)
        self.log.append((self.time, addr, value))


# ---- the Python host's counting events (i8085.py instrumented) ----------------------------------------------------
EVENTS = []                 # (part, chip time, kind)
_HOST = [None]
_run, _take = I.I8085.run, I.I8085._take


def _run_spy(self, cycles):
    h = _HOST[0]
    if self.ei_pending and self.trap_edge and h is not None:
        EVENTS.append((h.part, h.chip.time, HELD))
    self._end = self.cycles + cycles
    return _run(self, cycles)


def _take_spy(self, vector):
    _take(self, vector)
    h = _HOST[0]
    if self.cycles >= getattr(self, "_end", 1 << 62) and h is not None:
        EVENTS.append((h.part, h.chip.time, "%s (vector %04X)" % (SPLIT, vector)))


def session(host, quick):
    """One part of the session (host.part); returns its audio blocks (bytes)."""
    blocks = []

    def run(block):
        blocks.append(host.run(block).tobytes())

    def speak(text, block=BLOCK_S, max_blocks=2000, stop_after=None):
        host.say(text + "\r", background=True)
        n = 0
        while True:
            run(block)
            n += 1
            if stop_after is not None and n >= stop_after:
                return
            if not host.busy() or n >= max_blocks:
                return

    if host.part == "greeting":
        run(3.0)
        host.say(FLUSH, speech=False)
        run(0.05)
        if quick:
            return blocks
        speak(TEXTS[0], block=0.05)
        speak(TEXTS[1], block=0.05)
        host.say("\x1bR9\x1bP3", speech=False)
        run(0.05)
        while host.busy():
            run(0.05)
        return blocks
    if host.part == "commands":
        host.boot()
        for cmd, text in host.commands:
            host.say(cmd, speech=False)
            run(0.05)
            speak(text)
        return blocks
    host.boot()
    host.say("\x1bRA\x1bP7\x1bV3\x1bM2", speech=False)
    speak(TEXTS[2])
    if not quick:
        host.chip.snap_pitch = True                   # a capital: the pitch raised, then restored
        host.say("\x1bP9", speech=False)
        speak("A")
        host.say("\x1bP7", speech=False)
        speak(TEXTS[0])
        host.say("\x1bR5\x1bP5\x1bV5\x1bM0", speech=False)
        speak(TEXTS[3])
    speak(TEXTS[4], stop_after=20)                    # cancelled 0.6 s in
    host.cancel()
    speak("After the cancel.")
    return blocks


def run_python(quick, parts):
    I.I8085.run, I.I8085._take = _run_spy, _take_spy
    log, blocks, t0 = [], [], time.perf_counter()
    try:
        for part in parts:
            chip = SpyChip()
            host = AccentSA(ROMS, chip=chip, core="python")
            host.keep_writes = False
            host.part, host.commands = part, COMMANDS
            _HOST[0] = host
            blocks += session(host, quick)
            log += [(part,) + w for w in chip.log]
        return log, blocks, time.perf_counter() - t0
    finally:
        I.I8085.run, I.I8085._take = _run, _take
        _HOST[0] = None


def run_c(quick, slices, parts):
    log, blocks, t0, splits, held = [], [], time.perf_counter(), 0, 0
    for part in parts:
        host = AccentSA(ROMS, chip=SSI263C(), core="c", slices=slices)
        assert isinstance(host, AccentSAC)
        host.keep_writes = False
        host.part, host.commands = part, COMMANDS
        host.on_write = lambda t, reg, val, part=part: log.append((part, t, reg, val))
        blocks += session(host, quick)
        splits += host.get("splits")
        held += host.get("ei_traps")
    return log, blocks, time.perf_counter() - t0, splits, held


def speech_rule():
    """accent_sa.py's say(speech=None) against as_host.c's on Latin-1 text: every byte, and ESC commands."""
    import random
    lib = _load(SSI263C())
    rng = random.Random(1)
    pool = ["\x1b", "=", "+", "-", "O", "|", "~", "A", "Z", "a", "5", " ", ".", "\xe9", "\xb2", "R", "P", "x"]
    cases = [chr(b) for b in range(256)] + ["".join(rng.choice(pool) for _ in range(rng.randint(1, 9)))
                                            for _ in range(3000)]
    bad = [s for s in cases if bool(lib.ash_is_speech(s.encode("latin-1"), len(s)))
           != any(ch.isalnum() for ch in _ESC.sub("", s))]
    return len(cases), bad


def find_split(n):
    """The commands part's shape on the C host over n random sessions: the shortest that hold a carried acceptance."""
    import random
    texts = ["Yes.", "No, 42.", "It costs $3.50.", "Hello there.", "OK?", "Call 555-1212!", "Is it 3:45?"]
    cmds = ["\x1bR%s\x1bP%d" % (r, p) for r in "0379AH" for p in (0, 3, 5, 9)] + ["\x1bV3", "\x1bM1", "\x1bM0\x1bV5"]
    found = []
    for seed in range(n):
        rng = random.Random(seed)
        host = AccentSA(ROMS, chip=SSI263C(), core="c")
        host.keep_writes = False
        host.part = "commands"
        host.commands = tuple((rng.choice(cmds), rng.choice(texts)) for _ in range(2))
        session(host, True)
        if host.get("splits"):
            found.append((host.chip.time, host.get("splits"), host.commands))
    for t, k, c in sorted(found)[:5]:
        print("%.2f s, %d carried: COMMANDS = %r" % (t, k, c))
    print("%d of %d sessions hold one" % (len(found), n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--only", choices=PARTS)
    ap.add_argument("--find-split", type=int, metavar="N")
    a = ap.parse_args()
    if a.find_split:
        find_split(a.find_split)
        return
    parts = (a.only,) if a.only else PARTS
    failed = 0

    n, bad = speech_rule()
    print("%-4s speech rule   %d texts: as_host.c's = accent_sa.py's%s" % ("ok" if not bad else "FAIL", n,
                                                                          "" if not bad else ": %r" % bad[:5]))
    failed += bool(bad)

    del EVENTS[:]
    pw, pb, pt = run_python(a.quick, parts)
    cw, cb, ct, splits, held = run_c(a.quick, os.environ.get("ACCENTSA_COMPARE_SLICES", "python"), parts)
    if os.environ.get("ACCENTSA_COMPARE_FLIP"):
        k = min(100, len(cw) - 1)
        part, t, reg, val = cw[k]
        cw[k] = (part, t, reg, val ^ 0x01)
    print("     session      %s%s; Python host %.1f s, C host %.2f s (wall); %d and %d writes"
          % (" + ".join(parts), " (quick)" if a.quick else "", pt, ct, len(pw), len(cw)))
    n_split = sum(1 for e in EVENTS if e[2].startswith(SPLIT))
    n_held = sum(1 for e in EVENTS if e[2] == HELD)
    ok = n_split == splits and n_held == held and n_split > 0
    print("%-4s counting     the Python host: %d acceptance(s) ended a slice, %d TRAP(s) in EI's shadow; the C board: "
          "%d carried, %d held%s" % ("ok" if ok else "FAIL", n_split, n_held, splits, held,
                                     "" if n_split else " (the session must hold an acceptance at a slice's end)"))
    failed += not ok
    vals_p, vals_c = [w[::2] + w[3:] for w in pw], [w[::2] + w[3:] for w in cw]   # (part, reg, val)
    if vals_p == vals_c:
        print("ok   values       %d writes, values identical" % len(pw))
    else:
        k = next((i for i, (x, y) in enumerate(zip(vals_p, vals_c)) if x != y), min(len(pw), len(cw)))
        print("FAIL values       write values DIFFER at write %d of %d/%d: Python %s, C %s"
              % (k, len(pw), len(cw), vals_p[k] if k < len(pw) else None, vals_c[k] if k < len(cw) else None))
        failed += 1
    diffs = [i for i, (x, y) in enumerate(zip(pw, cw)) if x[1] != y[1]]
    if not diffs and len(pw) == len(cw):
        print("ok   times        every write's chip time identical")
    else:
        big = max((abs(pw[i][1] - cw[i][1]) for i in diffs), default=0.0)
        print("FAIL times        write times DIFFER: %d of %d, the largest %.6f s%s" % (
            len(diffs), len(pw), big, ", first at write %d" % diffs[0] if diffs else ""))
        failed += 1
    if pb == cb:
        print("ok   audio        %d blocks, %d samples, identical" % (len(pb), sum(len(b) for b in pb) // 8))
    else:
        k = next((i for i, (x, y) in enumerate(zip(pb, cb)) if x != y), min(len(pb), len(cb)))
        print("FAIL audio        DIFFERS at block %d of %d/%d" % (k, len(pb), len(cb)))
        failed += 1
    for e in EVENTS:
        print("     event        %s part, %.6f s: %s" % e)

    # the core's own counting (experimental): reported and classified, not a predicate
    xw, _xb, _xt, xs, xh = run_c(a.quick, "chip", parts)
    vals_x = [w[::2] + w[3:] for w in xw]
    line = "info chip slices  the core's own counting (experimental): %d writes, values %s" % (
        len(xw), "identical" if vals_x == vals_p else "DIFFER")
    if vals_x == vals_p:
        moved = [i for i, (x, y) in enumerate(zip(pw, xw)) if x[1] != y[1]]
        print(line + "; %d time(s) differ%s" % (len(moved), ", the largest %.6f s" % max(
            abs(pw[i][1] - xw[i][1]) for i in moved) if moved else ""))
        for i in moved[:8]:
            before = [e for e in EVENTS if e[0] == pw[i][0] and e[1] <= pw[i][1]]
            print("       %s write %d (R%d=%02X) at %.6f s: %+.6f s; after %s" % (
                pw[i][0], i, pw[i][2], pw[i][3], pw[i][1], xw[i][1] - pw[i][1],
                "the Python host's %s at %.6f s" % (before[-1][2], before[-1][1]) if before else "NO counting event"))
    else:
        k = next((i for i, (x, y) in enumerate(zip(vals_p, vals_x)) if x != y), min(len(pw), len(xw)))
        print(line + " from write %d" % k)
    print("accent_sa cores: %s" % ("FAILED" if failed else "all identical"))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
