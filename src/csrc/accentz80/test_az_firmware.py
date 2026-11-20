"""The Accent SA's own firmware on its board (az_board.c) and the Accent host: does the unit do what the real box
does, and does it take the commands the voices send it?  Through az_trace (the C API alone), on the two ROMs in
firmware/aicom-accent-sa-z80 (its README says which); without them every check is skipped and said so.

  greeting   left alone at power-up the unit says "Accent ready": the phonemes it writes, in order
  flush      booted as the voices boot it (ash_boot), the greeting is not said: a text's first phoneme is its own
  ports      the firmware reaches nothing the board does not have, but port 80h once each way at power-up (code it
             shares with the PC card; az_board.h); and it sends nothing on its serial line unasked
  blocks     the chip writes -- every value and every time -- do not depend on the size of the host's blocks
  rate       ESC R0 speaks a text slower than ESC R9 (the voices' rate)
  pitch      ESC P0 and ESC P9 differ in the pitch registers and in nothing else's count (the voices' pitch)
  voice      ESC V0 and ESC V9 differ in what is written (the voices' variant)
  monotone   ESC M1 holds the inflection register steadier than ESC M0 (the voices' inflection)
  marks      after ESC Y X and ESC = Y the unit sends one 06h for each Ctrl-F in the text, none without them
  versions   firmware 4.0 (ACU5_V40.BIN), when it is there too, writes what 4.1 writes: the same registers and
             values in the same order, each within a millisecond of 4.1's time (4.1's one patch is a few
             instructions at the head of the phoneme interrupt: measured, up to 0.3 ms)

    python src/csrc/accentz80/test_az_firmware.py [az_trace]        exit 1 on any failure
    AZ_FW_BREAK=flushed      the greeting check on a unit booted as the voices do   -- must fail: greeting
    AZ_FW_BREAK=flip         one value of one trace flipped before it is compared   -- must fail: blocks (versions)
    AZ_FW_BREAK=nocommands   the ESC commands left out of the texts                 -- must fail: rate, pitch, voice,
                                                                                       monotone, marks
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ROMS = os.path.join(REPO, "firmware", "aicom-accent-sa-z80")
U5, U4, U5_OLD = (os.path.join(ROMS, n) for n in ("ACU5_V41.BIN", "ACU4_V40.BIN", "ACU5_V40.BIN"))
BREAK = os.environ.get("AZ_FW_BREAK", "")
ESC = "\x1b"
# the SSI-263's phonemes by code (its data sheet), those the greeting needs
NAMES = {0x00: "PA", 0x05: "AY", 0x0B: "EH1", 0x0C: "AE", 0x1C: "ER", 0x1E: "R1", 0x25: "D", 0x28: "T", 0x29: "K",
         0x2C: "HF", 0x2D: "HFC", 0x30: "S", 0x38: "N"}
TEXT = "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs, 1996."
SHORT = "Testing one two three."
bad = 0


def exe():
    if len(sys.argv) > 1:
        return sys.argv[1]
    if os.name == "nt":
        return os.path.join(REPO, "nvda", "dist", "accentz80-lib", "az_trace.exe")
    return os.path.join(REPO, "build", "linux", "az_trace")


def trace(text, *options, u5=U5):
    """([(time, register, value)], {the END line's numbers})"""
    if BREAK == "nocommands":
        while text and ESC in text:
            i = text.index(ESC)
            text = text[:i] + text[i + 3:]
    out = subprocess.run([exe()] + list(options) + [u5, U4] + ([text] if text is not None else []),
                         stdout=subprocess.PIPE, check=True).stdout.decode("ascii")
    writes, end = [], {}
    for line in out.splitlines():
        f = line.split()
        if f[0] == "W":
            writes.append((float(f[1]), int(f[2][1:]), int(f[3], 16)))
        elif f[0] == "END":
            end = {"time": float(f[1]), "writes": int(f[3]), "tx": int(f[5]), "in": int(f[7]), "out": int(f[8]),
                   "nmi": int(f[10])}
    return writes, end


def phonemes(writes):
    """the phonemes written, a run of one code once, pauses and the aspirates left out"""
    seq = []
    for _t, reg, val in writes:
        name = NAMES.get(val & 0x3F, "%02X" % (val & 0x3F))
        if reg == 0 and name not in ("PA", "HF", "HFC") and (not seq or seq[-1] != name):
            seq.append(name)
    return seq


def report(name, ok, detail):
    global bad
    print("%-4s %-9s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def speech_end(writes):
    """the time of the last phoneme that is not a pause"""
    return max([t for t, reg, val in writes if reg == 0 and val & 0x3F] or [0.0])


def flipped(writes):
    w = list(writes)
    t, reg, val = w[len(w) // 2]
    w[len(w) // 2] = (t, reg, val ^ 0x01)
    return w


def main():
    if not (os.path.isfile(U5) and os.path.isfile(U4)):
        print("skip accent sa z80 firmware: ACU5_V41.BIN and ACU4_V40.BIN are not in firmware/aicom-accent-sa-z80")
        return 0
    if not os.path.isfile(exe()):
        print("skip accent sa z80 firmware: az_trace is not built (src/csrc/accentz80/build_board.py)")
        return 0

    want = ["AE", "K", "S", "EH1", "N", "T", "D", "ER", "R1", "EH1", "D", "AY"]
    w, end = trace(None, *([] if BREAK == "flushed" else ["--greeting"]))
    got = phonemes(w)
    report("greeting", got == want, "at power-up: %s" % (" ".join(got) or "nothing"))

    w, end = trace("Hello.")
    got = phonemes(w)
    report("flush", bool(got) and got[0] != "AE" and "K" not in got[:3] and len(got) < 12,
           "booted as the voices do, \"Hello.\" is %s" % " ".join(got))
    report("ports", end.get("in") == 1 and end.get("out") == 1 and end.get("tx") == 0,
           "undecoded ports: %s read, %s written; %s bytes sent unasked" % (end.get("in"), end.get("out"), end.get("tx")))

    ref, _ = trace(TEXT)
    same = []
    for block in ("0.011", "0.03", "0.2"):
        w, _ = trace(TEXT, "--block", block)
        if BREAK == "flip" and block == "0.03":
            w = flipped(w)
        same.append(w == ref)
    report("blocks", all(same) and len(ref) > 500,
           "%d writes at 50 ms blocks; 11, 30 and 200 ms blocks: %s" % (len(ref), ", ".join(
               "identical" if s else "DIFFERENT" for s in same)))

    slow, _ = trace(ESC + "R0" + SHORT)
    fast, _ = trace(ESC + "R9" + SHORT)
    a, b = speech_end(slow) - slow[0][0], speech_end(fast) - fast[0][0]
    report("rate", a > 1.3 * b > 0, "ESC R0 takes %.2f s, ESC R9 %.2f s" % (a, b))

    low, _ = trace(ESC + "P0" + SHORT)
    high, _ = trace(ESC + "P9" + SHORT)
    r1 = [sorted(set(v for _t, reg, v in w if reg == 1)) for w in (low, high)]
    report("pitch", r1[0] != r1[1] and phonemes(low) == phonemes(high),
           "the inflection register's values differ: %s; the phonemes %s" % (
               r1[0] != r1[1], "the same" if phonemes(low) == phonemes(high) else "DIFFER"))

    v0, _ = trace(ESC + "V0" + SHORT)
    v9, _ = trace(ESC + "V9" + SHORT)
    regs = sorted(set(reg for (_ta, reg, va), (_tb, _r, vb) in zip(v0, v9) if va != vb))
    report("voice", bool(regs) and phonemes(v0) == phonemes(v9),
           "ESC V0 and V9 differ in register(s) %s" % (", ".join("R%d" % r for r in regs) or "none"))

    lively, _ = trace(ESC + "M0" + TEXT)
    flat, _ = trace(ESC + "M1" + TEXT)
    spread = [len(set(v for _t, reg, v in w if reg == 1)) for w in (lively, flat)]
    report("monotone", spread[1] < spread[0], "inflection values used: %d with ESC M0, %d with ESC M1" % tuple(spread))

    marked = "\x06one \x06two \x06three"
    _, on = trace(ESC + "YX" + ESC + "=Y" + marked)
    _, off = trace(marked)
    report("marks", on.get("tx") == 3 and off.get("tx") == 0,
           "three Ctrl-Fs: %s prompts with the marks on, %s without" % (on.get("tx"), off.get("tx")))

    if os.path.isfile(U5_OLD):
        diff, late = [], 0.0
        for text in (TEXT, SHORT, "Rate: slider 55 alt+r", ESC + "R9" + ESC + "P2" + TEXT):
            new, _ = trace(text)
            old, _ = trace(text, u5=U5_OLD)
            if BREAK == "flip" and text == SHORT:
                old = flipped(old)
            diff.append([w[1:] for w in new] != [w[1:] for w in old])
            late = max([late] + [abs(a[0] - b[0]) for a, b in zip(new, old)])
        report("versions", not any(diff) and late < 0.001, "4.0 against 4.1 on %d texts: %s; times within %.2f ms" % (
            len(diff), "the same values in the same order" if not any(diff) else "%d DIFFER" % sum(diff),
            late * 1000))
    else:
        print("skip versions  ACU5_V40.BIN is not in firmware/aicom-accent-sa-z80")
    print("accent sa z80 firmware: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
