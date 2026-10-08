/* translit.h -- accented letters for firmwares that do not know them, shared by every voice's text path
 * (blazie/bl_voice.c, accent_text.c for both Accents, speakout/so_voice.c; voices.c exports it as ssv_translit for the
 * Braille Lite's NVDA driver and the tests).  Header-only: each file that includes it gets its own static copy, so no
 * build script names a new source file.  MIT.
 *
 * The firmwares know only their own alphabet: 7-bit ASCII (the English Braille Lite, the Speak-Out, the Accents) or a
 * DOS code page (the Spanish Braille Lite: cp850).  Before this pass, a letter outside it became a space or silence:
 * "á" alone said nothing, "tükör" became "t k r".  tl_apply runs first on a text item's code points, before every
 * other rule (currencies, each voice's _clean), and changes only letters the voice does not know:
 *
 *   - a known character passes unchanged (the voice's known() says which: tl_known_ascii, or the code page's);
 *   - an unknown letter inside text becomes its base letters, capitals keeping case: "tükör" -> "tukor",
 *     "Tamás" -> "Tamas", ß -> "ss", Æ -> "AE", Þ -> "Th";
 *   - a LONE unknown letter -- the whole text item is that one character once whitespace is trimmed (NVDA's typed
 *     character or character navigation) -- becomes its letter and its mark in English words: "á" -> "a acute",
 *     "ő" -> "o double acute", "ß" -> "sharp s".  Capitals give the same words (NVDA marks a capital itself).  A
 *     letter with punctuation ("ő.") is not lone;
 *   - a combining mark (U+0300-U+036F) right after a letter is dropped ("a" U+0301 -> "a"), the base letter stays;
 *   - anything else (symbols, other scripts) passes unchanged, to the voice's own rules as before.
 *
 * The table covers Latin-1 Supplement and Latin Extended-A (U+00C0-U+017F, all but the two symbols x and ÷), the
 * Romanian comma-below letters (U+0218-U+021B) and the capital sharp s (U+1E9E).  It is compiled in -- never the OS's
 * Unicode functions -- so every platform says the same.  The lone words are English on every voice, the Spanish
 * unit's included (only the letters it does not know reach them); words in the unit's own language are a possible
 * refinement.
 *
 * TRANSLIT_BREAK=1 in the environment (read once): the pass is off and every voice speaks as before it -- the tests'
 * must-fail control.
 */
#ifndef SSI263_TRANSLIT_H
#define SSI263_TRANSLIT_H

#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define TL_FN static __inline__ __attribute__((unused))
#else
#define TL_FN static
#endif

/* the characters a firmware knows natively: nonzero passes c unchanged */
typedef int (*tl_known_fn)(unsigned c);

TL_FN int tl_known_ascii(unsigned c) { return c < 128; }

/* One letter: its base letters in text (capitals keep case) and its mark, the lone words being the base letter in
   lower case, a space, the mark -- or, for a mark starting with '=', the rest of it alone ("=sharp s"). */
typedef struct {
    const char *base, *mark;
} tl_letter;

/* U+00C0-U+017F by cp - 0xC0; {NULL, NULL}: not a letter (x, ÷) */
static const tl_letter tl_table[192] = {
    /* C0 */ {"A", "grave"}, {"A", "acute"}, {"A", "circumflex"}, {"A", "tilde"},
    /* C4 */ {"A", "umlaut"}, {"A", "ring"}, {"AE", "=a e"}, {"C", "cedilla"},
    /* C8 */ {"E", "grave"}, {"E", "acute"}, {"E", "circumflex"}, {"E", "umlaut"},
    /* CC */ {"I", "grave"}, {"I", "acute"}, {"I", "circumflex"}, {"I", "umlaut"},
    /* D0 */ {"D", "=eth"}, {"N", "tilde"}, {"O", "grave"}, {"O", "acute"},
    /* D4 */ {"O", "circumflex"}, {"O", "tilde"}, {"O", "umlaut"}, {NULL, NULL},
    /* D8 */ {"O", "slash"}, {"U", "grave"}, {"U", "acute"}, {"U", "circumflex"},
    /* DC */ {"U", "umlaut"}, {"Y", "acute"}, {"Th", "=thorn"}, {"ss", "=sharp s"},
    /* E0 */ {"a", "grave"}, {"a", "acute"}, {"a", "circumflex"}, {"a", "tilde"},
    /* E4 */ {"a", "umlaut"}, {"a", "ring"}, {"ae", "=a e"}, {"c", "cedilla"},
    /* E8 */ {"e", "grave"}, {"e", "acute"}, {"e", "circumflex"}, {"e", "umlaut"},
    /* EC */ {"i", "grave"}, {"i", "acute"}, {"i", "circumflex"}, {"i", "umlaut"},
    /* F0 */ {"d", "=eth"}, {"n", "tilde"}, {"o", "grave"}, {"o", "acute"},
    /* F4 */ {"o", "circumflex"}, {"o", "tilde"}, {"o", "umlaut"}, {NULL, NULL},
    /* F8 */ {"o", "slash"}, {"u", "grave"}, {"u", "acute"}, {"u", "circumflex"},
    /* FC */ {"u", "umlaut"}, {"y", "acute"}, {"th", "=thorn"}, {"y", "umlaut"},
    /* 100 */ {"A", "macron"}, {"a", "macron"}, {"A", "breve"}, {"a", "breve"},
    /* 104 */ {"A", "ogonek"}, {"a", "ogonek"}, {"C", "acute"}, {"c", "acute"},
    /* 108 */ {"C", "circumflex"}, {"c", "circumflex"}, {"C", "dot"}, {"c", "dot"},
    /* 10C */ {"C", "caron"}, {"c", "caron"}, {"D", "caron"}, {"d", "caron"},
    /* 110 */ {"D", "stroke"}, {"d", "stroke"}, {"E", "macron"}, {"e", "macron"},
    /* 114 */ {"E", "breve"}, {"e", "breve"}, {"E", "dot"}, {"e", "dot"},
    /* 118 */ {"E", "ogonek"}, {"e", "ogonek"}, {"E", "caron"}, {"e", "caron"},
    /* 11C */ {"G", "circumflex"}, {"g", "circumflex"}, {"G", "breve"}, {"g", "breve"},
    /* 120 */ {"G", "dot"}, {"g", "dot"}, {"G", "cedilla"}, {"g", "cedilla"},
    /* 124 */ {"H", "circumflex"}, {"h", "circumflex"}, {"H", "stroke"}, {"h", "stroke"},
    /* 128 */ {"I", "tilde"}, {"i", "tilde"}, {"I", "macron"}, {"i", "macron"},
    /* 12C */ {"I", "breve"}, {"i", "breve"}, {"I", "ogonek"}, {"i", "ogonek"},
    /* 130 */ {"I", "dot"}, {"i", "=dotless i"}, {"IJ", "=i j"}, {"ij", "=i j"},
    /* 134 */ {"J", "circumflex"}, {"j", "circumflex"}, {"K", "cedilla"}, {"k", "cedilla"},
    /* 138 */ {"k", "=kra"}, {"L", "acute"}, {"l", "acute"}, {"L", "cedilla"},
    /* 13C */ {"l", "cedilla"}, {"L", "caron"}, {"l", "caron"}, {"L", "middle dot"},
    /* 140 */ {"l", "middle dot"}, {"L", "stroke"}, {"l", "stroke"}, {"N", "acute"},
    /* 144 */ {"n", "acute"}, {"N", "cedilla"}, {"n", "cedilla"}, {"N", "caron"},
    /* 148 */ {"n", "caron"}, {"'n", "=apostrophe n"}, {"Ng", "=eng"}, {"ng", "=eng"},
    /* 14C */ {"O", "macron"}, {"o", "macron"}, {"O", "breve"}, {"o", "breve"},
    /* 150 */ {"O", "double acute"}, {"o", "double acute"}, {"OE", "=o e"}, {"oe", "=o e"},
    /* 154 */ {"R", "acute"}, {"r", "acute"}, {"R", "cedilla"}, {"r", "cedilla"},
    /* 158 */ {"R", "caron"}, {"r", "caron"}, {"S", "acute"}, {"s", "acute"},
    /* 15C */ {"S", "circumflex"}, {"s", "circumflex"}, {"S", "cedilla"}, {"s", "cedilla"},
    /* 160 */ {"S", "caron"}, {"s", "caron"}, {"T", "cedilla"}, {"t", "cedilla"},
    /* 164 */ {"T", "caron"}, {"t", "caron"}, {"T", "stroke"}, {"t", "stroke"},
    /* 168 */ {"U", "tilde"}, {"u", "tilde"}, {"U", "macron"}, {"u", "macron"},
    /* 16C */ {"U", "breve"}, {"u", "breve"}, {"U", "ring"}, {"u", "ring"},
    /* 170 */ {"U", "double acute"}, {"u", "double acute"}, {"U", "ogonek"}, {"u", "ogonek"},
    /* 174 */ {"W", "circumflex"}, {"w", "circumflex"}, {"Y", "circumflex"}, {"y", "circumflex"},
    /* 178 */ {"Y", "umlaut"}, {"Z", "acute"}, {"z", "acute"}, {"Z", "dot"},
    /* 17C */ {"z", "dot"}, {"Z", "caron"}, {"z", "caron"}, {"s", "=long s"},
};

/* beyond the block: Romanian's comma below, the capital sharp s */
static const struct {
    unsigned cp;
    tl_letter l;
} tl_extra[] = {
    {0x0218, {"S", "comma"}}, {0x0219, {"s", "comma"}}, {0x021A, {"T", "comma"}}, {0x021B, {"t", "comma"}},
    {0x1E9E, {"SS", "=sharp s"}},
};

/* the letter c is, or NULL when the table has none */
TL_FN const tl_letter *tl_lookup(unsigned c)
{
    size_t i;
    if (c >= 0xC0 && c <= 0x17F)
        return tl_table[c - 0xC0].base ? &tl_table[c - 0xC0] : NULL;
    for (i = 0; i < sizeof tl_extra / sizeof tl_extra[0]; i++)
        if (tl_extra[i].cp == c)
            return &tl_extra[i].l;
    return NULL;
}

/* Python's str.isspace(), as the drivers' strip() trims */
TL_FN int tl_space(unsigned c)
{
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

TL_FN int tl_combining(unsigned c) { return c >= 0x300 && c <= 0x36F; }

/* TRANSLIT_BREAK=1: the pass off (the tests' control), read once */
TL_FN int tl_broken(void)
{
    static int broken = -1;
    if (broken < 0) {
        const char *e = getenv("TRANSLIT_BREAK");
        broken = e && e[0] == '1' && !e[1];
    }
    return broken;
}

/* The room tl_apply needs for n code points: two per letter in text ("ss"), or a lone letter's words. */
TL_FN int tl_room(int n) { return 2 * n + 32; }

/* The lone words of l into out; returns the count. */
TL_FN int tl_put_words(const tl_letter *l, unsigned *out)
{
    const char *s = l->mark;
    int m = 0;
    if (*s == '=') {
        s++;
    } else {
        unsigned b = (unsigned char)l->base[0];
        out[m++] = b >= 'A' && b <= 'Z' ? b + 32 : b;
        out[m++] = ' ';
    }
    while (*s) out[m++] = (unsigned char)*s++;
    return m;
}

/* The pass: in[0..n) -> out (room for tl_room(n)), known() saying what the firmware reads; returns the count. */
TL_FN int tl_apply(const unsigned *in, int n, tl_known_fn known, unsigned *out)
{
    int i, m = 0, a = 0, b = n;
    const tl_letter *l;
    if (tl_broken()) {
        memcpy(out, in, sizeof(unsigned) * (size_t)(n > 0 ? n : 0));
        return n;
    }
    while (a < b && tl_space(in[a])) a++;
    while (b > a && tl_space(in[b - 1])) b--;
    if (b - a == 1 && !known(in[a]) && (l = tl_lookup(in[a])) != NULL) {     /* a lone letter: its words */
        for (i = 0; i < a; i++) out[m++] = in[i];
        m += tl_put_words(l, out + m);
        for (i = b; i < n; i++) out[m++] = in[i];
        return m;
    }
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        const char *s;
        if (known(c)) {
            out[m++] = c;
        } else if ((l = tl_lookup(c)) != NULL) {
            for (s = l->base; *s; s++) out[m++] = (unsigned char)*s;
        } else if (tl_combining(c) && i > 0 && (tl_lookup(in[i - 1]) || (in[i - 1] < 128 && (
                       ((in[i - 1] | 32) >= 'a' && (in[i - 1] | 32) <= 'z'))))) {
            ;                                                                   /* the mark of the letter before */
        } else {
            out[m++] = c;
        }
    }
    return m;
}

/* ---- UTF-8, for the callers that hold bytes (voices.c's ssv_translit) --------------------------------------------- */

/* n bytes (n < 0: up to the NUL) -> code points; an invalid byte is U+FFFD.  out has room for n. */
TL_FN int tl_utf8_decode(const char *s, int n, unsigned *out)
{
    const unsigned char *p = (const unsigned char *)s, *e;
    int k = 0;
    if (n < 0) n = (int)strlen(s);
    e = p + n;
    while (p < e) {
        unsigned c = *p, cp;
        int j, len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || p + len > e) { out[k++] = 0xFFFD; p++; continue; }
        cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (j = 1; j < len; j++) {
            if ((p[j] & 0xC0) != 0x80) break;
            cp = (cp << 6) | (p[j] & 0x3F);
        }
        if (j < len) { out[k++] = 0xFFFD; p++; continue; }
        out[k++] = cp;
        p += len;
    }
    return k;
}

/* code points -> UTF-8 (room for 4 * n); returns the bytes written (no NUL) */
TL_FN int tl_utf8_encode(const unsigned *t, int n, unsigned char *u)
{
    int i, k = 0;
    for (i = 0; i < n; i++) {
        unsigned c = t[i];
        if (c < 0x80) u[k++] = (unsigned char)c;
        else if (c < 0x800) { u[k++] = (unsigned char)(0xC0 | (c >> 6)); u[k++] = (unsigned char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            u[k++] = (unsigned char)(0xE0 | (c >> 12)); u[k++] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
            u[k++] = (unsigned char)(0x80 | (c & 0x3F));
        } else {
            u[k++] = (unsigned char)(0xF0 | (c >> 18)); u[k++] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
            u[k++] = (unsigned char)(0x80 | ((c >> 6) & 0x3F)); u[k++] = (unsigned char)(0x80 | (c & 0x3F));
        }
    }
    return k;
}

#endif
