"""The Speak-Out's front end (the Speak-Out and the Sounding Board) at rate 0, letters typed one after another as NVDA
says them (a listener: "set the sounding board to rate 0 then start typing letters. You'll hear part of the previous
one prepended to the next one").  A text was over once no phoneme had been loaded for busy()'s `quiet` (50-60 ms); at
rate 0 one phoneme lasts longer, so the voice stopped with the chip still sounding a letter's last phoneme (A's AY,
F's F, X's S), and the rest of it played at the head of the next letter, before any write of its own.  Each letter
here, said to its end and cut after 120 ms: the chip must hold PA (or be powered down) when the next one begins, and
no sound may come before that one's first chip write.

    python tools/check_so_tail.py [library [firmware folder]]
                                    (default build/win/x64/ssi263speech.dll and firmware/: the folder holds
                                     gw-micro-speakout/SPEAKOUT.HEX and gw-micro-sounding-board/SBLOAD.COM, SB.COM)

Control (SSI263_SO_TAIL_BREAK=1): busy() leaves out the phoneme the chip still holds, as it was -- the letters after
A, F and X then start with it, and every case must fail.
"""
import array
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "firmware")
sys.path.insert(0, os.path.join(ROOT, "nvda", "shared"))
import ssi263speech  # noqa: E402

SR = 22050
LETTERS = "xabcdefg"             # after x (S), a (AY) and f (F): the phonemes a letter at rate 0 ends on
CUT_BLOCKS = 4                   # 120 ms: the next key comes while the letter is still being said
LOUD = 300                       # a sample louder than this is sound
NAMES = ("PA E E1 Y Y1 AY IE I A AI EH EH1 AE AE1 AH AH1 AW O OU OO IU IU1 U U1 UH UH1 UH2 UH3 ER R R1 R2 L L1 LF W B "
         "D KV P T K HV HVC HF HFC HN Z S J SCH V F THV TH M N NG :A :OH :U :UH E2 LB").split()


def units(lib):
    sb = os.path.join(FW, "gw-micro-sounding-board")
    so = os.path.join(FW, "gw-micro-speakout", "SPEAKOUT.HEX")
    if all(os.path.isfile(os.path.join(sb, f)) for f in ("SBLOAD.COM", "SB.COM")):
        yield "soundingboard", lambda: ssi263speech.SoundingBoardC(
            lib, os.path.join(sb, "SBLOAD.COM"), os.path.join(sb, "SB.COM"), SR)
    else:
        print("skip  the Sounding Board: its drivers are not in firmware/gw-micro-sounding-board")
    if os.path.isfile(so):
        yield "speakout", lambda: ssi263speech.SpeakOutC(lib, so, SR)
    else:
        print("skip  the Speak-Out: SPEAKOUT.HEX is not in firmware/gw-micro-speakout")


def letters(make, cut):
    """each letter after the first: (letter, the phoneme the chip held at its start or None if silent, the sound in
    ms before its first write, its length in s)"""
    u = make()
    u.set(0, 50, 8, 100, True, True)
    log = []
    u.on_write = lambda t, r, v: log.append((t, r, v))
    out = []
    for i, letter in enumerate(LETTERS):
        r0, r3 = u.chip.regs[0], u.chip.regs[3]
        held = None if (r0 & 0x3F) == 0 or (r3 & 0x80) else r0 & 0x3F
        del log[:]
        t_begin = u.chip.time
        u.begin()
        u.say(u.prepare(letter) + "\r")
        pcm, n, done = [], 0, False
        while not done and n < 400:
            data, done = u.render()
            pcm.append(data)
            n += 1
            if cut and n >= CUT_BLOCKS and i + 1 < len(LETTERS):
                break
        u.end()
        if not done:
            u.cancel()
        a = array.array("h", b"".join(pcm))
        first = [t for t, r, v in log if r == 0]
        head = int(round(((first[0] - t_begin) if first else 0) * SR))
        early = sum(1 for x in a[:max(0, head)] if abs(x) > LOUD) * 1000.0 / SR
        if i:
            out.append((letter, held, early, len(a) / float(SR)))
    u.close()
    return out


def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "win", "x64", "ssi263speech.dll")
    failures = 0
    for name, make in units(lib):
        for cut in (False, True):
            rows = letters(make, cut)
            bad = [(L, h, e) for L, h, e, _ in rows if h is not None or e > 0]
            longest = max(s for _, _, _, s in rows)
            ok = not bad and longest < 1.5
            failures += not ok
            what = "%s rate 0 %s" % (name, "cut at 0.12 s" if cut else "to its end")
            detail = ", ".join("%s: held %s, %.0f ms of sound before its first write" % (L, NAMES[h] if h else "PA", e)
                               for L, h, e in bad) or "every letter starts from PA, no sound before its own writes"
            print("%-5s %-40s %s (longest letter %.2f s)" % ("ok" if ok else "FAIL", what, detail, longest))
    print("so tail: %s" % ("all passed" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
