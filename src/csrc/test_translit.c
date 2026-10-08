/* test_translit.c -- translit.h on its own: the table's coverage (U+00C0-U+017F and the extras), a lone letter's words
 * against a letter inside text, case, the voice's known characters (ASCII; ASCII and cp850 for the Spanish unit),
 * combining marks, and everything else left alone.  One line per check, then the total; exit 1 on any failure.
 *
 *     gcc -std=c99 -Wall -I src/csrc -o test_translit src/csrc/test_translit.c && ./test_translit
 *
 * nvda/tools/translit_test.py builds and runs it (Windows), tools/linux_ci_checks.sh too (Linux).  Its control:
 * TRANSLIT_BREAK=1 sets ssv_translit_break, the pass off, as every voice then speaks (the old path) -- this test must
 * FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "translit.h"
#include "blazie/bl_cp850.h"

static int failures, checks;

static int known_cp850(unsigned c)
{
    int i;
    if (c < 0x80) return 1;
    for (i = 0; i < 128; i++)
        if (bl_cp850_high[i] == c) return 1;
    return 0;
}

/* UTF-8 through tl_apply, back to UTF-8 (NUL-terminated) */
static void run(const char *in, tl_known_fn known, char *out)
{
    unsigned a[256], b[tl_room(256)];
    int n = tl_utf8_decode(in, -1, a), m = tl_apply(a, n, known, b);
    out[tl_utf8_encode(b, m, (unsigned char *)out)] = 0;
}

/* s as printable ASCII: other bytes as \xNN (the console and a screen reader get plain text) */
static const char *shown(const char *s, char *buf)
{
    char *p = buf;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 32 && c < 127 && c != '\\') *p++ = (char)c;
        else p += sprintf(p, "\\x%02x", c);
    }
    *p = 0;
    return buf;
}

static void expect(const char *what, const char *in, tl_known_fn known, const char *want)
{
    char got[2048], a[1024], b[1024], c[1024];
    run(in, known, got);
    checks++;
    if (strcmp(got, want)) {
        failures++;
        printf("FAIL %s: \"%s\" -> \"%s\", not \"%s\"\n", what, shown(in, a), shown(got, b), shown(want, c));
    } else {
        printf("ok   %s: \"%s\" -> \"%s\"\n", what, shown(in, a), shown(got, b));
    }
}

static int ascii_letters(const char *s, int apostrophe)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!(((*s | 32) >= 'a' && (*s | 32) <= 'z') || (apostrophe && *s == '\'')))
            return 0;
    return 1;
}

/* every code point of the block (and the extras): in text, base letters only; alone, ASCII words; the two symbols
   unchanged; and each letter's capital and small forms give the same lone words */
static void coverage(void)
{
    static const unsigned extra[] = {0x0218, 0x0219, 0x021A, 0x021B, 0x1E9E};
    unsigned cps[192 + 5], in[3], out[64];
    int i, n = 0, letters = 0, symbols = 0, bad = 0;
    for (i = 0xC0; i <= 0x17F; i++) cps[n++] = (unsigned)i;
    for (i = 0; i < 5; i++) cps[n++] = extra[i];
    for (i = 0; i < n; i++) {
        unsigned c = cps[i];
        char word[64], alone[64];
        int k, m;
        in[0] = 'x'; in[1] = c; in[2] = 'x';                        /* inside a word */
        m = tl_apply(in, 3, tl_known_ascii, out);
        for (k = 0; k < m - 2; k++) word[k] = (char)(out[k + 1] < 128 ? out[k + 1] : '?');
        word[m > 2 ? m - 2 : 0] = 0;
        m = tl_apply(&c, 1, tl_known_ascii, out);                    /* alone */
        for (k = 0; k < m && k < 63; k++) alone[k] = (char)(out[k] < 128 ? out[k] : '?');
        alone[k] = 0;
        if (c == 0xD7 || c == 0xF7) {
            symbols++;
            if (m != 1 || out[0] != c || strcmp(word, "?")) {
                bad++;
                printf("FAIL coverage: U+%04X is a symbol, left as it is; got \"%s\" / \"%s\"\n", c, word, alone);
            }
            continue;
        }
        letters++;
        if (!ascii_letters(word, c == 0x149)) {
            bad++;
            printf("FAIL coverage: U+%04X inside a word gives \"%s\"\n", c, word);
        }
        for (k = 0; alone[k]; k++)
            if (!((alone[k] >= 'a' && alone[k] <= 'z') || alone[k] == ' ')) break;
        if (alone[k] || strlen(alone) < 3) {                       /* words: small letters and spaces */
            bad++;
            printf("FAIL coverage: U+%04X alone gives \"%s\"\n", c, alone);
        }
    }
    checks++;
    if (bad) failures++;
    printf("%s coverage: %d letters (U+00C0-U+017F and %d more) give ASCII letters in a word and words alone, "
           "%d symbols left as they are\n", bad ? "FAIL" : "ok  ", letters, 5, symbols);
}

static void same_words_both_cases(void)
{
    static const unsigned pairs[][2] = {{0xC1, 0xE1}, {0xC6, 0xE6}, {0xD0, 0xF0}, {0xDE, 0xFE}, {0x150, 0x151},
                                        {0x152, 0x153}, {0x141, 0x142}, {0x17D, 0x17E}, {0x1E9E, 0xDF}};
    int i, bad = 0;
    for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
        unsigned a[64], b[64];
        int na = tl_apply(&pairs[i][0], 1, tl_known_ascii, a), nb = tl_apply(&pairs[i][1], 1, tl_known_ascii, b);
        if (na != nb || memcmp(a, b, sizeof(unsigned) * (size_t)na)) {
            bad++;
            printf("FAIL case: U+%04X and U+%04X alone give different words\n", pairs[i][0], pairs[i][1]);
        }
    }
    checks++;
    if (bad) failures++;
    printf("%s case: a capital alone gives its small letter's words (%d pairs)\n", bad ? "FAIL" : "ok  ",
           (int)(sizeof pairs / sizeof pairs[0]));
}

static void ascii_unchanged(void)
{
    unsigned in[128], out[tl_room(128)];
    int i, m, bad = 0;
    for (i = 0; i < 128; i++) in[i] = (unsigned)i;
    m = tl_apply(in, 128, tl_known_ascii, out);
    if (m != 128 || memcmp(in, out, sizeof in)) bad = 1;
    for (i = 0; i < 128 && !bad; i++) {                              /* each one alone too */
        m = tl_apply(&in[i], 1, tl_known_ascii, out);
        if (m != 1 || out[0] != in[i]) bad = 1;
    }
    checks++;
    if (bad) failures++;
    printf("%s ascii: all 128 characters pass unchanged, together and alone\n", bad ? "FAIL" : "ok  ");
}

int main(void)
{
    /* the control: TRANSLIT_BREAK=1 in this test's environment sets the library's flag (the library reads none) */
    const char *brk = getenv("TRANSLIT_BREAK");
    if (brk && !strcmp(brk, "1"))
        ssv_translit_break = 1;
    /* the lone letters' words (the whole text one character, whitespace trimmed) */
    expect("lone", "\xc3\xa1", tl_known_ascii, "a acute");                          /* á */
    expect("lone", "\xc3\xa0", tl_known_ascii, "a grave");                          /* à */
    expect("lone", "\xc3\xa2", tl_known_ascii, "a circumflex");                     /* â */
    expect("lone", "\xc3\xa3", tl_known_ascii, "a tilde");                          /* ã */
    expect("lone", "\xc3\xa4", tl_known_ascii, "a umlaut");                         /* ä */
    expect("lone", "\xc3\xbc", tl_known_ascii, "u umlaut");                         /* ü */
    expect("lone", "\xc3\xb6", tl_known_ascii, "o umlaut");                         /* ö */
    expect("lone", "\xc3\xa5", tl_known_ascii, "a ring");                           /* å */
    expect("lone", "\xc3\xa7", tl_known_ascii, "c cedilla");                        /* ç */
    expect("lone", "\xc5\x91", tl_known_ascii, "o double acute");                   /* ő */
    expect("lone", "\xc5\xb1", tl_known_ascii, "u double acute");                   /* ű */
    expect("lone", "\xc4\x8d", tl_known_ascii, "c caron");                          /* č */
    expect("lone", "\xc5\xa1", tl_known_ascii, "s caron");                          /* š */
    expect("lone", "\xc5\xbe", tl_known_ascii, "z caron");                          /* ž */
    expect("lone", "\xc4\x83", tl_known_ascii, "a breve");                          /* ă */
    expect("lone", "\xc4\x81", tl_known_ascii, "a macron");                         /* ā */
    expect("lone", "\xc4\x85", tl_known_ascii, "a ogonek");                         /* ą */
    expect("lone", "\xc5\xbc", tl_known_ascii, "z dot");                            /* ż */
    expect("lone", "\xc5\x82", tl_known_ascii, "l stroke");                         /* ł */
    expect("lone", "\xc3\xb8", tl_known_ascii, "o slash");                          /* ø */
    expect("lone", "\xc3\x9f", tl_known_ascii, "sharp s");                          /* ß */
    expect("lone", "\xc3\xa6", tl_known_ascii, "a e");                              /* æ */
    expect("lone", "\xc5\x93", tl_known_ascii, "o e");                              /* œ */
    expect("lone", "\xc3\xb0", tl_known_ascii, "eth");                              /* ð */
    expect("lone", "\xc3\xbe", tl_known_ascii, "thorn");                            /* þ */
    expect("lone", "\xc8\x99", tl_known_ascii, "s comma");                          /* ș */
    expect("lone, a capital", "\xc3\x81", tl_known_ascii, "a acute");               /* Á */
    expect("lone, a capital", "\xc5\x90", tl_known_ascii, "o double acute");        /* Ő */
    expect("lone, a capital", "\xc3\x86", tl_known_ascii, "a e");                   /* Æ */
    expect("lone, whitespace kept", "  \xc3\xa1\r\n", tl_known_ascii, "  a acute\r\n");
    /* inside text: the base letters, capitals keeping case */
    expect("in a word", "t\xc3\xbck\xc3\xb6r", tl_known_ascii, "tukor");            /* tükör */
    expect("in a word", "Geczy Tam\xc3\xa1s", tl_known_ascii, "Geczy Tamas");
    expect("in a word", "Tam\xc3\xa1s", tl_known_ascii, "Tamas");
    expect("in a word", "\xc3\x81RV\xc3\x8dZT\xc5\xb0R\xc5\x90 t\xc3\xbck\xc3\xb6rf\xc3\xbar\xc3\xb3g\xc3\xa9p",
           tl_known_ascii, "ARVIZTURO tukorfurogep");                              /* árvíztűrő tükörfúrógép */
    expect("in a word", "\xc5\x91\xc5\xb1\xc3\xbc\xc3\xa7\xc3\xb1\xc5\x82\xc3\xb8\xc4\x91\xc3\xb0", tl_known_ascii,
           "ouucnlodd");                                                           /* ő ű ü ç ñ ł ø đ ð */
    expect("in a word", "\xc3\xbe\xc3\x9f\xc3\xa6\xc5\x93", tl_known_ascii, "thssaeoe");     /* þ ß æ œ */
    expect("in a word, capitals", "\xc3\x9e\xc3\xb3r \xc3\x86sir \xc5\x92uvre STRA\xe1\xba\x9e" "E", tl_known_ascii,
           "Thor AEsir OEuvre STRASSE");                                           /* Þór Æsir Œuvre STRAẞE */
    expect("not lone: a word of one letter in text", "a \xc5\x91 b", tl_known_ascii, "a o b");
    expect("not lone: with punctuation", "\xc5\x91.", tl_known_ascii, "o.");
    expect("not lone: two letters", "\xc3\xa1\xc3\xa1", tl_known_ascii, "aa");
    /* combining marks after a letter go; the base letter stays */
    expect("combining", "cafe\xcc\x81 na\xc3\xafve", tl_known_ascii, "cafe naive");
    expect("combining, not after a letter", " \xcc\x81", tl_known_ascii, " \xcc\x81");
    /* everything else is left to the voice's own rules */
    expect("symbols", "\xc3\x97 \xc3\xb7 \xe2\x82\xac \xc2\xbd \xc2\xa9 \xe2\x80\x9cq\xe2\x80\x9d \xf0\x9f\x8e\x89",
           tl_known_ascii, "\xc3\x97 \xc3\xb7 \xe2\x82\xac \xc2\xbd \xc2\xa9 \xe2\x80\x9cq\xe2\x80\x9d \xf0\x9f\x8e\x89");
    expect("symbols alone", "\xc2\xbd", tl_known_ascii, "\xc2\xbd");
    expect("other scripts", "\xce\xb1\xd0\xb1", tl_known_ascii, "\xce\xb1\xd0\xb1");
    /* the Spanish unit: cp850's letters are its own, alone or not; only the rest falls back */
    expect("cp850 known", "\xc3\xa1", known_cp850, "\xc3\xa1");                     /* á */
    expect("cp850 known", "\xc3\xb1", known_cp850, "\xc3\xb1");                     /* ñ */
    expect("cp850 known", "t\xc3\xbck\xc3\xb6r", known_cp850, "t\xc3\xbck\xc3\xb6r");
    expect("cp850 known", "Ma\xc3\xb1" "ana, \xc2\xbfqu\xc3\xa9 tal?", known_cp850, "Ma\xc3\xb1" "ana, \xc2\xbfqu\xc3\xa9 tal?");
    expect("cp850 unknown, lone", "\xc5\x91", known_cp850, "o double acute");
    expect("cp850 unknown, in a word", "Erd\xc5\x91s", known_cp850, "Erdos");
    coverage();
    same_words_both_cases();
    ascii_unchanged();
    if (failures)
        printf("translit: %d of %d FAILED%s\n", failures, checks, tl_broken() ? " (TRANSLIT_BREAK=1: the pass is off)" : "");
    else
        printf("translit: all %d ok\n", checks);
    return failures ? 1 : 0;
}
