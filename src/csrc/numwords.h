/* numwords.h -- nvda/shared/ssi263_numwords.py in C, the parts a front end without Python needs: text as Python sees
 * it (UTF-8 to code points), currencies() and English normalise() (numbers as words).  Each function names the
 * Python it ports; change them together.  The Accent-PC voice (accentpc/as_voice.c) uses them, and its Android test
 * holds them to the NVDA driver byte for byte (src/platforms/android/test/test_android_native.py); so does the
 * Speak-Out's (speakout/so_voice.c: currencies, nw_utf8, nw_isalnum; nvda/tools/so_voice_text_equiv.py).
 *
 * bl_voice.c (the Braille Lite's) carries its own copy of currencies() from before this file; it may move onto this
 * one.  MIT.
 */
#ifndef SSI263_NUMWORDS_H
#define SSI263_NUMWORDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 to code points, as Python decodes a str (invalid bytes -> U+FFFD).  out needs room for strlen(s) + 1. */
int nw_utf8(const char *s, unsigned *out);

/* Python's str.isalnum / str.isspace on a code point (Latin-1 exact; beyond it approximated: letters unless
   punctuation, symbols or emoji). */
int nw_isalnum(unsigned c);
int nw_isspace(unsigned c);

/* currencies(text, "en"): pound, euro and yen amounts (and cents) as words around their digits, "£2.63" -> "2 pounds
   63 pence".  out needs room for 16 * n + 64 code points.  Returns the count. */
int nw_currencies(const unsigned *t, int n, unsigned *out);

/* normalise(text) (English, digits as words, no spell_out, no decimal comma) on 7-bit text: a malloc'd string, NULL
   when out of memory. */
char *nw_normalise(const char *ascii);

#ifdef __cplusplus
}
#endif
#endif
