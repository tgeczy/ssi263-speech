"""Currency amounts of thousands of digits are said, not dropped with an error (Panthera's audit, P28's variant).

numwords.currencies (the NVDA drivers' "£2.63" -> "2 pounds 63 pence", the English unit) decided whether the pounds
part was zero with int(): Python refuses that conversion past 4300 digits (ValueError, Python 3.11 and later -- the
NVDA versions this add-on runs on), so "£" and a long run of digits, or such a run before "€", raised; the driver
caught it, dropped the utterance and restarted the unit.  The number path already decides on the string (big()); the
currency words now do too.

    python currency_long.py          exit 1 on any failure
    NUMWORDS_CURRENCY_BREAK=1        int() again, as it was -- must fail the long cases
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "shared"))
import ssi263_numwords as numwords  # noqa: E402

LONG = "1" * 5000
ZEROS = "0" * 5000
CASES = [("£" + LONG, LONG + " pounds"),
         ("£" + ZEROS, ZEROS + " pounds"),
         ("£" + ZEROS + ".50", "50 pence"),
         ("4" * 4400 + " €", "4" * 4400 + " euros"),
         ("£2.63", "2 pounds 63 pence"), ("£1", "1 pound"), ("5 €", "5 euros")]   # (the short ones, as they were)


def main():
    bad = 0
    for text, want in CASES:
        try:
            got = numwords.currencies(text)
            ok = got == want
            what = "%d characters" % len(got) if len(got) > 60 else ascii(got)
        except Exception as e:
            ok, what = False, "%s: %s" % (type(e).__name__, str(e)[:70])
        bad += not ok
        print("%-4s %-34s -> %s" % ("ok" if ok else "FAIL", ascii((text[:24] + "...") if len(text) > 27 else text),
                                    what))                  # (ascii: NVDA-safe, and the console's code page)
    print("currency long: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
