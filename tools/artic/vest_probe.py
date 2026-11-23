"""Exploring Artic's VEST.COM on the SynPhonix research host (src/hosts/synphonix.py): say texts with a chosen option
byte and offsets, try raw frames, and load a small exception dictionary.  Research only: no test rests on it.

    python tools/artic/vest_probe.py say "Hello." "\\x05N1 12 dollars."      phonemes of each text
    python tools/artic/vest_probe.py flags "Is it 12:45?" 00 40 80          the same text with AH = each byte
    python tools/artic/vest_probe.py lexicon                                 "artic" as "ark tick" (AH bit 0)
    python tools/artic/vest_probe.py frame                                   AL=4 and AL=5 raw frames

VEST.COM from firmware/artic-synphonix/ (or SYNPHONIX_VEST); libpc86 from SSI263_PC86_DLL, else build/linux/.
What each part of VEST's interface does is in docs/front-ends.md, "Artic SynPhonix 215".
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "src"))
os.environ.setdefault("SSI263_PC86_DLL", os.path.join(ROOT, "build", "linux", "libpc86.so"))
from hosts.synphonix import SynPhonix, TEXT_SEG, SAY_CX, SAY_DX   # noqa: E402

COM = os.environ.get("SYNPHONIX_VEST", os.path.join(ROOT, "firmware", "artic-synphonix", "VEST.COM"))
LEX_SEG = 0x6000           # where the probe keeps its dictionary (free memory on the host)


def phonemes(h, writes):
    return " ".join(h.chip.rom.names.get(v & 0x3F, "?") for t, r, v in writes if r == 0 and v & 0x3F)


def say(h, text, ah=0, cx=SAY_CX, dx=SAY_DX, limit=20.0):
    """AL=7 with option byte ah; run until VEST is idle.  Returns (the writes, seconds)."""
    n0 = len(h.writes)
    data = text.encode("latin-1")
    h.uc.mem_write(TEXT_SEG * 16, data)
    h.call((ah << 8) | 7, es=TEXT_SEG, si=0, di=len(data), cx=cx, dx=dx)
    t = h.skip(0.05)
    while h.busy() and t < limit:
        t += h.skip(0.05)
    return h.writes[n0:], t


def lexicon(h, pairs, slot=0):
    """A dictionary in VEST's format (text module 10A7): count, first search step, then from index 1 four bytes an
    entry (offset, word length, replacement length), the words in capitals, sorted; its segment into the text
    module at 110h + 2 slot."""
    pairs = sorted((w.upper().encode(), r.encode()) for w, r in pairs)
    n, step = len(pairs), 1
    while step * 2 <= n:
        step *= 2
    table, text, ents = 4 * (n + 1), bytearray(), []
    for w, r in pairs:
        ents.append((table + len(text), len(w), len(r)))
        text += w + r
    seg = bytearray(struct.pack("<HH", n, step))
    for off, lw, lr in ents:
        seg += struct.pack("<HBB", off, lw, lr)
    h.uc.mem_write(LEX_SEG * 16, bytes(seg + text))
    h.uc.mem_write(h.word(0x116) * 16 + 0x110 + 2 * slot, struct.pack("<H", LEX_SEG))


def main(argv):
    h = SynPhonix(COM)
    h.boot()
    what = argv[0] if argv else "say"
    if what == "say":
        for text in argv[1:] or ["Hello."]:
            text = text.encode().decode("unicode_escape")
            ws, t = say(h, text)
            print("%r %.2f s | %s" % (text, t, phonemes(h, ws)))
    elif what == "flags":
        text = argv[1].encode().decode("unicode_escape")
        base = None
        for ah in [int(a, 16) for a in argv[2:]]:
            ws, t = say(h, text, ah)
            regs = [(r, v) for _, r, v in ws]
            base = base or regs
            print("AH=%02X %.2f s, %d writes differ | %s"
                  % (ah, t, sum(a != b for a, b in zip(base, regs)) + abs(len(base) - len(regs)), phonemes(h, ws)))
    elif what == "lexicon":
        lexicon(h, [("artic", "ark tick")])
        for ah in (0, 1):
            print("AH=%02X | %s" % (ah, phonemes(h, say(h, "Artic cat.", ah)[0])))
    elif what == "frame":
        for al in (4, 5):        # AH = R0, BL = R1, BH = R2, CL = R3, CH = R4
            n0 = len(h.writes)
            h.call((0xEB << 8) | al, bx=0xA850, cx=0xE85C)
            h.skip(0.1)
            print("AL=%d:" % al, " ".join("R%d=%02X" % (r, v) for _, r, v in h.writes[n0:n0 + 10]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
