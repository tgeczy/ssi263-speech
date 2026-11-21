"""The Braille Lite driver's unit for a language NVDA marks (issue #69).

NVDA's automatic language switching (on by default) puts a LangChangeCommand for the voice's own language in front
of every utterance, and the driver sends what follows to the unit for that language (_voice_for).  It took the first
voice whose base language matched: the Portuguese voice was spoken by the Brazilian unit, the Arabic (UAE) one by the
plain Arabic unit, and any second voice of a language by the first.  Now the voice tagged exactly so first, then the
language's own with no region, then another region of it, and the chosen voice kept when it answers as well.

  chosen    the chosen voice, its own language marked (as NVDA marks every utterance): that voice
  switch    another language marked: the voice tagged exactly so, else the language's own

    python voice_for_lang.py                                             exit 1 on any failure
    VOICE_FOR_LANG_BREAK=1   the driver's LANG_EXACT off: the first voice of the base language, as it was
"""
import os
import sys
import types

sys.argv = sys.argv[:1] + ["blazie"]
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("d = drv_mod.SynthDriver()")[0])          # the stand-in NVDA and the driver's module, no unit booted

if not hasattr(drv_mod, "LANG_EXACT"):
    print("FAIL the driver has no LANG_EXACT (built before the fix?)")
if os.environ.get("VOICE_FOR_LANG_BREAK"):
    drv_mod.LANG_EXACT = False
voices = drv_mod._voices()
present = drv_mod.SynthDriver._present()                  # the voices whose image and state are there
by_lang = {}
for v in present:
    by_lang.setdefault(voices[v][1], []).append(v)
bad = 0


def unit_for(chosen, lang):
    fake = types.SimpleNamespace(_voice=chosen, _present=lambda: list(present))
    return drv_mod.SynthDriver._voice_for(fake, lang)


def check(name, chosen, lang, want):
    global bad
    got = unit_for(chosen, lang)
    ok = got == want
    bad += not ok
    print("%-4s %-7s %-14s chosen, NVDA marks %-6s -> %s%s" % ("ok" if ok else "FAIL", name, chosen, lang, got,
                                                              "" if ok else " (want %s)" % want))


cases = 0
for tag in ("pt", "ar-AE"):
    if tag in by_lang:
        v = by_lang[tag][0]
        check("chosen", v, tag.replace("-", "_"), v)                     # NVDA's own mark: pt, ar_AE
        cases += 1
english = by_lang.get("en", [])
if len(english) > 1:
    check("chosen", english[-1], "en", english[-1])                    # a second English release kept
    check("chosen", english[-1], "en_US", english[-1])
    cases += 2
start = english[0] if english else present[0]
for lang, tag in (("pt_PT", "pt"), ("pt", "pt"), ("pt_BR", "pt-BR"), ("ar_AE", "ar-AE"), ("ar", "ar")):
    if tag in by_lang and lang.split("_")[0] in [t.split("-")[0] for t in by_lang]:
        check("switch", start, lang, by_lang[tag][0])
        cases += 1
if cases < 6:
    print("FAIL only %d cases: the Portuguese, Arabic and English voices are not all here" % cases)
    bad += 1
print("voice for language: %s" % ("%d FAILED" % bad if bad else "all %d ok" % cases))
sys.exit(1 if bad else 0)
