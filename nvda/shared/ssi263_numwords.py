# Copied from outspoken-nvda, addon/synthDrivers/_outspoken/numwords.py (commit 0fbb903, MIT,
# Tamas Geczy), renamed: the outSPOKEN add-on puts its own numwords on NVDA's sys.path.
# -*- coding: utf-8 -*-
"""Say numbers as words, because none of these engines can.

Named `numwords` and not `numbers`: `tools/` and the add-on's engine folder
both go on `sys.path`, so a module called `numbers` shadows the standard
library's for the whole process. It does not fail where you can see it -- it
failed here as sixty unrelated test collections at once.

MacinTalk's own rules spell digits out one at a time and stop there.  `RULZ`
bucket 26 is the digits, and it holds nothing but the ten names, so `30` comes
out as "three zero" and `25` as "two five".  The Amiga narrator behaves the
same way; where the Amiga *add-on* does better it is because its author put a
modern dictionary in front of the engine, not because the 1985 engine knew how
to count.

So this is an addition, not a restoration, and it belongs in front of the
letter-to-sound rules rather than inside them:

    text -> normalise() -> NRL rules -> phonemes -> engine

It is deliberately free of engine data.  Nothing here depends on MacinTalk,
which is why MacinTalk 2 and Pro get it for the same price.

**Predictable beats clever.**  A screen reader user hears these thousands of
times a day and needs to know what a given string will sound like, so there is
no year heuristic and no attempt to guess that `1984` is a date rather than a
count.  `read_year` exists for a caller that knows better.

The scale names end at quadrillion.  From 10^18 up the whole part is said as
"large number": nobody can verify "three quintillion" by ear, and admitting
the size says more than a stack of scale words does.

    py -3 addon/synthDrivers/_outspoken/numwords.py "you owe 1,234.50"
"""
import re
import sys

ONES = ("zero one two three four five six seven eight nine ten eleven twelve "
        "thirteen fourteen fifteen sixteen seventeen eighteen nineteen").split()
TENS = ("_ _ twenty thirty forty fifty sixty seventy eighty ninety").split()

#: Short scale, which is what English screen readers expect.
SCALES = [(10 ** 15, "quadrillion"), (10 ** 12, "trillion"),
          (10 ** 9, "billion"), (10 ** 6, "million"), (1000, "thousand")]

#: Where the scale names stop.  From here up the whole part is said as
#: `LARGE`: a listener cannot check a stack of scale words by ear, and
#: Python itself refuses `int()` on digit runs past 4300 characters, so the
#: decision has to be taken from the string before `int` ever runs.
LIMIT = 10 ** 18
LARGE = "large number"

#: Irregular ordinals; the rest are formed by suffixing the cardinal.
ORDINALS = {"one": "first", "two": "second", "three": "third",
            "five": "fifth", "eight": "eighth", "nine": "ninth",
            "twelve": "twelfth"}

MINUS = "minus"
POINT = "point"

# ---------------------------------------------------------------------------
# Spanish, for the cami voices.  Carlos read "25" as "twenty five" in 1.2.0
# because this module only knew English and the driver fed its output to a
# Spanish front end all the same -- English number names pushed through
# Spanish letter-to-sound rules.  Tomi heard it within the hour.
#
# Mexican Spanish throughout, because that is what the cami engine is: the
# decimal mark is read "punto" (es-MX, like the US, points its decimals), and
# the scale is the long one, where mil millones is 10^9 and billón is 10^12
# -- a Spanish speaker hearing "billón" for 10^9 is hearing a number a
# thousand times too small.
#
# The composition rules that differ from English, each carried below:
# dieciséis through veintinueve are single fused words; tens join units with
# "y" (treinta y dos); 100 exactly is "cien" and 101-199 "ciento";
# five/seven/nine hundreds are irregular (quinientos, setecientos,
# novecientos); 1000 is bare "mil", never "uno mil"; and a multiplier ending
# in "uno" apocopates before a scale word -- veintiún mil, treinta y un
# millones.  The accented vowels are ordinary MacRoman, which is the engine's
# native encoding.
# ---------------------------------------------------------------------------

ONES_ES = ("cero uno dos tres cuatro cinco seis siete ocho nueve diez once "
           "doce trece catorce quince dieciséis diecisiete dieciocho "
           "diecinueve veinte veintiuno veintidós veintitrés veinticuatro "
           "veinticinco veintiséis veintisiete veintiocho "
           "veintinueve").split()
TENS_ES = ("_ _ _ treinta cuarenta cincuenta sesenta setenta ochenta "
           "noventa").split()
HUNDREDS_ES = ("_ ciento doscientos trescientos cuatrocientos quinientos "
               "seiscientos setecientos ochocientos novecientos").split()

#: Long scale.  10^9 and 10^15 need no names of their own: the quotient a
#: scale takes runs to 999999, so "dos mil quinientos millones" falls out of
#: the same composition that makes "dos mil quinientos".
SCALES_ES = [(10 ** 12, "billón", "billones"),
             (10 ** 6, "millón", "millones")]

MINUS_ES = "menos"
POINT_ES = "punto"
LARGE_ES = "número grande"


def _apocope(words):
    """'veintiuno' -> 'veintiún', 'treinta y uno' -> 'treinta y un'.

    Only ever before a scale word, which is the only place this is called.
    """
    if words.endswith("veintiuno"):
        return words[:-len("veintiuno")] + "veintiún"
    if words.endswith("uno"):
        return words[:-len("uno")] + "un"
    return words


def _cardinal_es(n):
    if n < 0:
        return MINUS_ES + " " + _cardinal_es(-n)
    if n >= LIMIT:
        return LARGE_ES
    if n < 30:
        return ONES_ES[n]
    if n < 100:
        t, r = divmod(n, 10)
        return TENS_ES[t] + (" y " + ONES_ES[r] if r else "")
    if n == 100:
        return "cien"
    if n < 1000:
        h, r = divmod(n, 100)
        return HUNDREDS_ES[h] + (" " + _cardinal_es(r) if r else "")
    for value, one, many in SCALES_ES:
        if n >= value:
            q, r = divmod(n, value)
            if q == 1:
                head = "un " + one
            else:
                head = _apocope(_cardinal_es(q)) + " " + many
            return head + (" " + _cardinal_es(r) if r else "")
    # Thousands: bare "mil" for exactly one of them, never "uno mil".
    q, r = divmod(n, 1000)
    head = "mil" if q == 1 else _apocope(_cardinal_es(q)) + " mil"
    return head + (" " + _cardinal_es(r) if r else "")


def cardinal(n, lang="en"):
    """0 -> 'zero', 1234 -> 'one thousand two hundred thirty four'.

    No "and" before the tens: American convention, which is what the phoneme
    rules and the voices were built around.  No hyphens either -- the rules
    treat a hyphen as punctuation and it would become a pause.

    `lang="es"` answers in Mexican Spanish for the cami voices; everything
    the docstring above says about hyphens holds there too.
    """
    if lang == "es":
        return _cardinal_es(n)
    if n < 0:
        return MINUS + " " + cardinal(-n)
    if n >= LIMIT:
        return LARGE
    if n < 20:
        return ONES[n]
    if n < 100:
        t, r = divmod(n, 10)
        return TENS[t] + (" " + ONES[r] if r else "")
    if n < 1000:
        h, r = divmod(n, 100)
        return ONES[h] + " hundred" + (" " + cardinal(r) if r else "")
    for value, name in SCALES:
        if n >= value:
            q, r = divmod(n, value)
            return cardinal(q) + " " + name + (" " + cardinal(r) if r else "")
    # Unreachable: negatives and LIMIT return above, and anything at or
    # above 1000 matches the thousand scale.
    raise AssertionError("cardinal fell through for %d" % n)


def ordinal(n):
    """1 -> 'first', 22 -> 'twenty second'."""
    if n >= LIMIT:
        return LARGE               # never "large numberth"
    words = cardinal(n).split()
    last = words[-1]
    if last in ORDINALS:
        words[-1] = ORDINALS[last]
    elif last.endswith("y"):
        words[-1] = last[:-1] + "ieth"
    else:
        words[-1] = last + "th"
    return " ".join(words)


def read_year(n):
    """1984 -> 'nineteen eighty four'.

    Never applied automatically -- only a caller that knows the number is a
    year should ask, because `1984 items` is not `nineteen eighty four items`.
    """
    if not (1100 <= n <= 9999) or n % 1000 == 0:
        return cardinal(n)
    hi, lo = divmod(n, 100)
    if lo == 0:
        return cardinal(hi) + " hundred"
    if lo < 10:
        return cardinal(hi) + " oh " + ONES[lo]
    return cardinal(hi) + " " + cardinal(lo)


def digits(s, lang="en"):
    """'2024' -> 'two zero two four', for when spelling out is wanted."""
    names = ONES_ES if lang == "es" else ONES
    return " ".join(names[int(c)] if c.isdigit() else c for c in s)


#: A signed number with optional thousands separators and an optional
#: fractional part, or an ordinal like `3rd`.  Ordered so the ordinal
#: alternative is tried first -- otherwise `3rd` matches as `3` and leaves a
#: stray `rd` behind.
#: The trailing `(?!\.\d)` is what keeps `192.168.0.1` whole.  Without it the
#: leading `192.168` matches as a decimal and the address is read as "one
#: hundred ninety two point one six eight.0.1" -- half converted, which is
#: worse than either extreme.  A bare `.5` is deliberately not matched either,
#: because it cannot be told apart from the end of a sentence.
_NUMBER = re.compile(r"""
    (?P<ord>  (?<![\w.])  \d+ (?:st|nd|rd|th)  \b )
  | (?P<num>  (?<![\w.])  -? \d{1,3}(?:,\d{3})+ (?:\.\d+)? (?![\w.])
            | (?<![\w.])  -? \d+  \.\d+                    (?![\w.])
            | (?<![\w.])  -? \d+                           (?![\w])  (?!\.\d) )
""", re.VERBOSE | re.IGNORECASE)


def normalise(text, spell_out=False, lang="en"):
    """Replace numbers in `text` with words.

    `spell_out` gives the engine's own behaviour, digit by digit, for a user
    who prefers it -- that is the setting worth exposing in the driver, since
    long identifiers and phone numbers really are easier to follow that way.

    `lang="es"` speaks the numbers in Mexican Spanish, for the cami voices.
    An English ordinal suffix (`3rd`) in Spanish text becomes the bare
    cardinal: Spanish ordinals are their own morphology, an English suffix in
    Spanish prose is already foreign, and "tres" is predictable where
    "tercero" would be a guess about gender and position.
    """
    minus = MINUS_ES if lang == "es" else MINUS
    point = POINT_ES if lang == "es" else POINT
    large = LARGE_ES if lang == "es" else LARGE

    def big(s):
        # 19 significant digits is LIMIT exactly.  Decided on the string,
        # never after `int()`: Python refuses the conversion itself past
        # 4300 digits, and a screen reader crashing on a long digit run is
        # a far worse answer than "large number".
        return len(s.lstrip("0")) > 18

    def sub(mo):
        # An ordinal always becomes a word.  Spelling `3rd` out character by
        # character says "three r d", which helps nobody, so `spell_out` is
        # about long numbers and does not reach here.
        if mo.group("ord"):
            head = mo.group("ord")[:-2]
            if big(head):
                return large
            if lang == "es":
                return cardinal(int(head), lang)
            return ordinal(int(head))

        raw = mo.group("num")
        neg = raw.startswith("-")
        raw = raw.lstrip("-").replace(",", "")
        whole, _, frac = raw.partition(".")
        if spell_out:
            out = digits(whole or "0", lang)
        elif big(whole):
            out = large
        else:
            out = cardinal(int(whole or "0"), lang)
        if frac:
            # The fractional part is always digit by digit either way: nobody
            # reads .50 as "fifty".
            out += " " + point + " " + digits(frac, lang)
        return (minus + " " + out) if neg else out

    return _NUMBER.sub(sub, text)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    for t in args:
        print("%r\n  -> %s" % (t, normalise(t)))
        print("  -> %s   (spelled out)" % normalise(t, spell_out=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
