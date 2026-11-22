"""The SynPhonix 215's research host (src/hosts/synphonix.py) on Artic's own VEST.COM: it loads and finds its card,
speaks its own announcement and a sentence with the phonemes it always has, finds the card of another copy of VEST
(another serial), and cuts speech when asked.

    python tools/check_synphonix_host.py [VEST.COM]      exit 1 on any failure; skips (exit 0, said so) without it
    python tools/check_synphonix_host.py --print [VEST.COM]   the phonemes, to read goldens from

The goldens are VEST's phonemes (R0's low six bits, PA left out) in the chip's names, read from the host's first runs
and checked by ear against the text (2026-11-22): every phoneme comes from VEST's rules, none from the host.

Must-fail controls (SYNPHONIX_HOST_BREAK, the host's own switch; nvda/tools/run_tests.py and tools/linux_tests.sh):
  key       the key made with one serial digit changed: VEST finds no card of its own and every case fails
  rotate    the PC's bytes written to the chip unrotated: the start-up, the announcement and the sentence differ
  eoi       VEST's specific EOI ignored: its IRQ 7 is served once, so the announcement and the sentence stop
  cancel    a cut by AL=0 (VEST's chip reset) instead of AL=1: the queue goes on speaking
  keycache  the key made once per process: the second copy's card is the first copy's
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "src"))
from hosts import synphonix                      # noqa: E402
from hosts.synphonix import SynPhonix            # noqa: E402

DEFAULT = os.path.join(ROOT, "firmware", "artic-synphonix", "VEST.COM")
START = [(2, 0xF8), (4, 0xE9), (3, 0x82), (0, 0xEB), (3, 0x52)]     # VEST's own start-up (0919)
ANNOUNCEMENT = ("V I I J UH3 N Z EH1 N HF AE AE N T S T S P E E E T SCH T ER AE AE N Z LF A AY Y T ER ER "
                "W W UH1 UH3 N P O UH3 I Y N T S I I K S Z AY I ER R O U")
SENTENCE = "The time is 12:45, and I owe you $3.50."
SENTENCE_PH = ("THV UH3 UH3 UH3 T AH1 AE1 I Y M I I Z W W UH1 UH3 N T HF IU U U U F O O UH3 ER F AH1 AE1 I Y V "
               "AE AE1 N D AH1 AE1 EH1 I Y OO O U Y IU U U U D AH1 AW LF ER Z TH HF R E E Y P O UH3 Y YI N T "
               "F AH1 AE1 I Y V Z AY I ER R O U")
HELLO = "Hello."
HELLO_PH = "HF EH1 LF O U"
LONG = ("This is a long sentence that goes on and on, so that there is still a great deal of it left to say "
        "when the cut comes, a quarter of the way through.")


def phonemes(host, writes):
    names = host.chip.rom.names
    return " ".join(names.get(v & 0x3F, "?") for t, r, v in writes if r == 0 and v & 0x3F)


def say_all(host, text, limit=20.0):
    n0 = len(host.writes)
    host.say(text)
    t = host.skip(0.05)
    while host.busy() and t < limit:
        t += host.skip(0.05)
    return host.writes[n0:], t


def other_copy(path):
    """VEST with another serial: its encoded serial's last digit changed (the one the IRQ handler's check does not
    read), the rest as it is."""
    image = bytearray(open(path, "rb").read())
    at = synphonix._find_serial(image)
    d = (image[at] - 0xA7 - 0x30 + 1) % 10        # the serial's last digit before the pairs are reversed (9662)
    image[at] = (d + 0x30 + 0xA7) & 0xFF
    fd, name = tempfile.mkstemp(suffix=".COM")
    with os.fdopen(fd, "wb") as f:
        f.write(image)
    return name


def main(argv):
    show = "--print" in argv
    args = [a for a in argv if a != "--print"]
    com = args[0] if args else DEFAULT
    if not os.path.isfile(com):
        print("skip  synphonix host: no VEST.COM (firmware/artic-synphonix/README.txt)")
        return 0
    results = []

    def case(name, ok, detail):
        results.append(ok)
        print("%s %s: %s" % ("ok   " if ok else "FAIL ", name, detail[:160]))

    try:
        host = SynPhonix(com)
    except Exception as e:                       # a refused card: every case fails, each said so
        for name in ("load", "announcement", "sentence", "another copy", "cancel"):
            case(name, False, "VEST did not load: %s" % os.path.basename(str(e).splitlines()[0])[:120])
        print("synphonix host: %d FAILED" % len(results))
        return 1
    screen = " ".join("".join(host.screen).split())
    regs = [(r, v) for t, r, v in host.writes[:5]]
    case("load", "VEST Ver 1.60 ENGLISH" in screen and regs == START and host.ports.get(("in", 0x2E0)),
         "card key read at 2E0h, start-up writes %s" % " ".join("R%d=%02X" % rv for rv in regs))
    host.boot()
    got = phonemes(host, host.writes[len(START):])      # after the start-up, whose R0 sets the mode
    if show:
        print("announcement:", got)
    case("announcement", got == ANNOUNCEMENT, "%d phonemes%s" % (len(got.split()), "" if got == ANNOUNCEMENT else
                                                                  ": " + got[:80]))
    writes, t = say_all(host, SENTENCE)
    got = phonemes(host, writes)
    if show:
        print("sentence:", got)
    case("sentence", got == SENTENCE_PH and 2.0 < t < 8.0,
         "%d phonemes in %.2f s%s" % (len(got.split()), t, "" if got == SENTENCE_PH else ": " + got[:80]))
    copy = other_copy(com)
    try:
        other = SynPhonix(copy)
        other.boot()
        got = phonemes(other, say_all(other, HELLO)[0])
        case("another copy", got == HELLO_PH and other.key != host.key,
             "its own key %s, %r" % ("made" if other.key != host.key else "NOT made", got[:60]))
    except Exception as e:
        case("another copy", False, "did not load: %s" % str(e).splitlines()[0][:120])
    finally:
        os.remove(copy)
    n0 = len(host.writes)
    host.say(LONG)
    host.skip(0.8)
    cut = host.chip.time
    host.cancel()
    t = host.skip(0.05)
    while host.busy() and t < 2.0:
        t += host.skip(0.05)
    late = [w for w in host.writes[n0:] if w[0] > cut + 0.15 and w[1] == 0 and w[2] & 0x3F]
    case("cancel", not late and t < 1.0, "%d phonemes more than 150 ms after the cut, quiet after %.2f s"
         % (len(late), t))
    bad = results.count(False)
    print("synphonix host: %s" % ("%d FAILED" % bad if bad else "all %d passed" % len(results)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
