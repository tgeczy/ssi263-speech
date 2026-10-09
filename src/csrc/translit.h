/* translit.h -- accented letters for firmwares that do not know them, shared by every voice's text path
 * (blazie/bl_voice.c, accent_text.c for both Accents, speakout/so_voice.c; voices.c exports it as ssv_translit for the
 * Braille Lite's NVDA driver and the tests).  Header-only: each file that includes it gets its own static copy, so no
 * build script names a new source file.  MIT.
 *
 * The firmwares know only their own alphabet: 7-bit ASCII (the English Braille Lite, the Speak-Out, the Accents) or a
 * DOS code page (the Spanish Braille Lite: cp850).  Before this pass, a letter outside it became a space or silence:
 * "á" alone said nothing, "tükör" became "t k r".  tl_apply runs first on a text item's code points, before every
 * other rule (currencies, each voice's _clean), and changes only letters the voice does not know.  It reads the text
 * as graphemes: a base character and the run of combining marks (U+0300-U+036F) attached to it.
 *
 *   - Composition first, before known() is asked: a letter (ASCII or the table's) with an attached run is one
 *     grapheme.  When the run is one mark and the two make a letter of the table ("a" U+0301 -> á, "n" U+0303 -> ñ),
 *     the grapheme IS that letter, exactly as if it had come precomposed.  Otherwise (no such letter: "q" U+0301, or a
 *     run of several marks, "u" U+0308 U+0301 -- the table has no letter with two marks) the grapheme is its base
 *     character, as the rules below take it in a word, and the whole run is dropped.  Only the table's letters
 *     compose (each with the mark it is named for: acute U+0301 ... ogonek U+0328, comma U+0326), in C, so it is the
 *     same on every front end -- never the OS's normalisation;
 *   - a known character passes unchanged (the voice's known() says which: tl_known_ascii, or the code page's) -- the
 *     Spanish unit is sent cp850's ñ for "n" U+0303 as for a precomposed ñ;
 *   - an unknown letter inside text becomes its base letters, capitals keeping case: "tükör" -> "tukor",
 *     "Tamás" -> "Tamas", ß -> "ss", Æ -> "AE", Þ -> "Th";
 *   - a LONE unknown letter -- the whole text item is that one grapheme once whitespace is trimmed (NVDA's typed
 *     character or character navigation) -- becomes its letter and its mark in English words: "á" (or "a" U+0301)
 *     -> "a acute", "ő" -> "o double acute", "ß" -> "sharp s".  Capitals give the same words (NVDA marks a capital
 *     itself).  A letter with punctuation ("ő.") is not lone, nor "cap á" (NVDA's "say cap"); a grapheme that is not
 *     a table letter ("u" U+0308 U+0301) is not named, only its base;
 *   - an UNATTACHED mark -- at the start, or after a character that is not a letter (a space, a digit, a symbol) --
 *     passes unchanged, with any marks after it, to the voice's own rules as before (they make it a space);
 *   - anything else (symbols, other scripts) passes unchanged, to the voice's own rules as before.
 *
 * The table covers Latin-1 Supplement and Latin Extended-A (U+00C0-U+017F, all but the two symbols x and ÷), the
 * Romanian comma-below letters (U+0218-U+021B) and the capital sharp s (U+1E9E).  It is compiled in -- never the OS's
 * Unicode functions -- so every platform says the same.  The lone words are English on every voice, the Spanish
 * unit's included (only the letters it does not know reach them); words in the unit's own language are a possible
 * refinement.
 *
 * ssv_translit_break (exported, set only by tests: through ctypes, or in test_translit.c; the test scripts set it when
 * their own environment has TRANSLIT_BREAK=1): nonzero turns the pass off and every voice speaks as before it -- the
 * tests' must-fail control.  The library never reads the environment.
 */
#ifndef SSI263_TRANSLIT_H
#define SSI263_TRANSLIT_H

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

/* A test's control (as bl_voice.h's blv_break_fault; nothing else sets it): nonzero turns the pass off, so every voice
   of the library speaks as before it.  One flag per library: each file that includes this header defines it, as a
   COMDAT (Windows) or weak (ELF) symbol the linker keeps once, exported.  Read at every call. */
#if defined(_WIN32)
#define TL_FLAG __declspec(dllexport) __attribute__((selectany))
#else
#define TL_FLAG __attribute__((weak, visibility("default")))
#endif
TL_FLAG int ssv_translit_break = 0;

TL_FN int tl_broken(void) { return ssv_translit_break != 0; }

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

/* a letter a mark can attach to: ASCII's or the table's */
TL_FN int tl_is_letter(unsigned c)
{
    return (c < 128 && (c | 32) >= 'a' && (c | 32) <= 'z') || tl_lookup(c) != NULL;
}

/* the combining mark each of the table's mark words is (its canonical decomposition's) */
static const struct {
    unsigned cp;
    const char *mark;
} tl_marks[] = {
    {0x0300, "grave"}, {0x0301, "acute"}, {0x0302, "circumflex"}, {0x0303, "tilde"}, {0x0304, "macron"},
    {0x0306, "breve"}, {0x0307, "dot"}, {0x0308, "umlaut"}, {0x030A, "ring"}, {0x030B, "double acute"},
    {0x030C, "caron"}, {0x0326, "comma"}, {0x0327, "cedilla"}, {0x0328, "ogonek"},
};

/* base + mark as one letter of the table (its code point), or 0: base one ASCII letter, mark named for it */
TL_FN unsigned tl_compose(unsigned base, unsigned mark)
{
    const char *word = NULL;
    size_t i;
    if (base >= 128 || !tl_is_letter(base))
        return 0;
    for (i = 0; i < sizeof tl_marks / sizeof tl_marks[0]; i++)
        if (tl_marks[i].cp == mark)
            word = tl_marks[i].mark;
    if (!word)
        return 0;
    for (i = 0; i < 192; i++)
        if (tl_table[i].base && tl_table[i].base[0] == (char)base && !tl_table[i].base[1]
                && !strcmp(tl_table[i].mark, word))
            return 0xC0 + (unsigned)i;
    for (i = 0; i < sizeof tl_extra / sizeof tl_extra[0]; i++)
        if (tl_extra[i].l.base[0] == (char)base && !tl_extra[i].l.base[1] && !strcmp(tl_extra[i].l.mark, word))
            return tl_extra[i].cp;
    return 0;
}

/* one character as the firmware is given it: known, its base letters, or (lone) its words; else unchanged */
TL_FN int tl_put(unsigned c, int lone, tl_known_fn known, unsigned *out)
{
    const tl_letter *l;
    const char *s;
    int m = 0;
    if (known(c) || (l = tl_lookup(c)) == NULL) {
        out[m++] = c;
    } else if (lone) {
        m = tl_put_words(l, out);
    } else {
        for (s = l->base; *s; s++) out[m++] = (unsigned char)*s;
    }
    return m;
}

/* The pass: in[0..n) -> out (room for tl_room(n)), known() saying what the firmware reads; returns the count. */
TL_FN int tl_apply(const unsigned *in, int n, tl_known_fn known, unsigned *out)
{
    int i = 0, j, m = 0, a = 0, b = n;
    if (tl_broken()) {
        memcpy(out, in, sizeof(unsigned) * (size_t)(n > 0 ? n : 0));
        return n;
    }
    while (a < b && tl_space(in[a])) a++;                   /* the item without its whitespace: lone if one grapheme */
    while (b > a && tl_space(in[b - 1])) b--;
    while (i < n) {
        unsigned c = in[i], p;
        for (j = i + 1; j < n && tl_combining(in[j]); j++) ;  /* the run of marks attached to in[i] */
        if (tl_combining(c) || (j > i + 1 && !tl_is_letter(c))) {
            for (; i < j; i++) out[m++] = in[i];             /* unattached: as it came */
            continue;
        }
        if (j == i + 2 && (p = tl_compose(c, in[i + 1])) != 0)
            c = p;                                            /* base + its one mark: the letter */
        m += tl_put(c, i == a && j == b && (j == i + 1 || c != in[i]), known, out + m);
        i = j;                                                /* a run that made no letter: dropped */
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
