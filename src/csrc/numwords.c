/* numwords.c -- see numwords.h.  The text functions are bl_voice.c's (the Braille Lite voice's), copied as they are
 * there; normalise() is new here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "numwords.h"

/* ---- text as Python sees it: code points, str.isspace, str.isalnum ------------------------------------------------ */
int nw_utf8(const char *s, unsigned *out)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p) {
        unsigned c = *p, cp;
        int k, len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len) { out[n++] = 0xFFFD; p++; continue; }
        cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (k = 1; k < len; k++) {
            if ((p[k] & 0xC0) != 0x80) break;
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (k < len) { out[n++] = 0xFFFD; p++; continue; }
        out[n++] = cp;
        p += len;
    }
    return n;
}

int nw_isspace(unsigned c)
{
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

int nw_isalnum(unsigned c)
{
    if (c < 128) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    if (c < 256) return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA
                     || (c >= 0xBC && c <= 0xBE) || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xE000 && c <= 0xF8FF)
            || (c >= 0xFE00 && c <= 0xFE0F) || c >= 0x1F000)
        return 0;
    return !nw_isspace(c);
}

static int py_isword(unsigned c) { return c == '_' || nw_isalnum(c); }
static int isdig(unsigned c) { return c >= '0' && c <= '9'; }

/* ---- currencies (English only) ------------------------------------------------------------------------------------
   The regex, tried as Python's re does: at each position the three alternatives in order, each amount in the
   engine's preference order (longest first, then backtracking), the first that passes the lookahead wins. */

/* the amount at i: (whole)? (.frac)? -- candidates in the regex's order; returns how many were written to cand
   (each: whole start/end, frac start/end, -1 = absent, and the end) */
typedef struct { int w0, w1, f0, f1, end; } amount;

static int amounts(const unsigned *t, int n, int i, amount *cand, int cap)
{
    int k = 0, wend[64], nw = 0, j, g;
    /* whole: \d{1,3}(?:,\d{3})+ -- greedy digits 3..1, then groups most..1 -- then \d+ longest..1, then absent */
    for (j = 3; j >= 1; j--) {
        int d;
        for (d = 0; d < j; d++) if (i + d >= n || !isdig(t[i + d])) break;
        if (d < j) continue;
        {
            int ends[32], ne = 0, p = i + j;
            while (ne < 32 && p + 3 < n && t[p] == ',' && isdig(t[p + 1]) && isdig(t[p + 2]) && isdig(t[p + 3])) {
                p += 4;
                ends[ne++] = p;
            }
            for (g = ne - 1; g >= 0 && nw < 64; g--) wend[nw++] = ends[g];
        }
    }
    for (j = i; j < n && isdig(t[j]); j++) ;
    for (g = j; g > i && nw < 63; g--) wend[nw++] = g;
    wend[nw++] = -1;                                       /* whole absent */
    for (g = 0; g < nw && k < cap; g++) {
        int w1 = wend[g], p = w1 < 0 ? i : w1, f;
        if (p < n && t[p] == '.' && p + 1 < n && isdig(t[p + 1])) {
            for (f = p + 1; f < n && isdig(t[f]); f++) ;
            for (; f > p + 1 && k < cap; f--) {
                cand[k].w0 = w1 < 0 ? -1 : i; cand[k].w1 = w1; cand[k].f0 = p + 1; cand[k].f1 = f; cand[k].end = f;
                k++;
            }
        }
        if (k < cap) {
            cand[k].w0 = w1 < 0 ? -1 : i; cand[k].w1 = w1; cand[k].f0 = -1; cand[k].f1 = -1; cand[k].end = p;
            k++;
        }
    }
    return k;
}

static int put_ascii(unsigned *out, int m, const char *s)
{
    while (*s) out[m++] = (unsigned char)*s++;
    return m;
}

static int put_span(unsigned *out, int m, const unsigned *t, int a, int b)
{
    while (a < b) out[m++] = t[a++];
    return m;
}

/* _currency_words: sym 0 pound, 1 euro, 2 yen */
static int currency_words(const unsigned *t, const amount *a, int sym, unsigned *out, int m)
{
    static const char *units[3][4] = {{"pound", "pounds", "penny", "pence"}, {"euro", "euros", "cent", "cents"},
                                      {"yen", "yen", NULL, NULL}};
    int cents = 0, whole_nonzero = 0, first = 1, i;
    char buf[32];
    if (a->f0 >= 0 && (a->f1 - a->f0 > 2 || !units[sym][2])) {        /* "2.635 pounds" */
        if (a->w0 >= 0) m = put_span(out, m, t, a->w0, a->w1); else out[m++] = '0';
        out[m++] = '.';
        m = put_span(out, m, t, a->f0, a->f1);
        out[m++] = ' ';
        return put_ascii(out, m, units[sym][1]);
    }
    if (a->f0 >= 0)
        cents = (int)(t[a->f0] - '0') * 10 + (a->f1 - a->f0 > 1 ? (int)(t[a->f0 + 1] - '0') : 0);
    if (a->w0 >= 0)
        for (i = a->w0; i < a->w1; i++) if (isdig(t[i]) && t[i] != '0') whole_nonzero = 1;
    if (a->w0 >= 0 && (whole_nonzero || !cents)) {
        int one = a->w1 - a->w0 == 1 && t[a->w0] == '1';
        m = put_span(out, m, t, a->w0, a->w1);
        out[m++] = ' ';
        m = put_ascii(out, m, units[sym][one ? 0 : 1]);
        first = 0;
    }
    if (cents) {
        if (!first) out[m++] = ' ';
        snprintf(buf, sizeof buf, "%d ", cents);
        m = put_ascii(out, m, buf);
        m = put_ascii(out, m, units[sym][cents == 1 ? 2 : 3]);
    }
    return m;
}

static int lookahead_ok(const unsigned *t, int n, int p)   /* (?![\d.]\d) */
{
    return !(p + 1 < n && (isdig(t[p]) || t[p] == '.') && isdig(t[p + 1]));
}

int nw_currencies(const unsigned *t, int n, unsigned *out)
{
    int i = 0, m = 0;
    amount cand[256];
    while (i < n) {
        int nc, c, end = -1, sym = -1, done = 0, before = i > 0 && !py_isword(t[i - 1]) && t[i - 1] != '.'
                                                           && t[i - 1] != ',';
        amount a;
        memset(&a, 0, sizeof a);
        if (i == 0) before = 1;
        /* alternative 1: [£€¥] ?amount(?![\d.]\d) */
        if (t[i] == 0xA3 || t[i] == 0x20AC || t[i] == 0xA5) {
            int s = t[i] == 0xA3 ? 0 : t[i] == 0x20AC ? 1 : 2, sp;
            for (sp = (i + 1 < n && t[i + 1] == ' ') ? 1 : 0; sp >= 0 && !done; sp--) {
                nc = amounts(t, n, i + 1 + sp, cand, 256);
                for (c = 0; c < nc; c++)
                    if (lookahead_ok(t, n, cand[c].end)) { a = cand[c]; end = a.end; sym = s; done = 1; break; }
            }
        }
        /* alternative 2: (?<![\w.,])amount ?€ */
        if (!done && before) {
            nc = amounts(t, n, i, cand, 256);
            for (c = 0; c < nc && !done; c++) {
                int p = cand[c].end;
                if (p < n && t[p] == ' ' && p + 1 < n && t[p + 1] == 0x20AC) { a = cand[c]; end = p + 2; done = 2; }
                else if (p < n && t[p] == 0x20AC) { a = cand[c]; end = p + 1; done = 2; }
            }
        }
        /* alternative 3: (?<![\w.,])(\d+) ?¢ */
        if (!done && before && isdig(t[i])) {
            int p = i;
            while (p < n && isdig(t[p])) p++;
            if (p < n && t[p] == ' ' && p + 1 < n && t[p + 1] == 0xA2) { end = p + 2; done = 3; }
            else if (p < n && t[p] == 0xA2) { end = p + 1; done = 3; }
            a.w0 = i; a.w1 = p;
        }
        if (!done) { out[m++] = t[i++]; continue; }
        if ((done == 1 || done == 2) && a.w0 < 0 && a.f0 < 0) {  /* a bare symbol (or "€" alone): unchanged */
            m = put_span(out, m, t, i, end);
            i = end > i ? end : i + 1;
            continue;
        }
        if (i > 0 && nw_isalnum(t[i - 1])) out[m++] = ' ';
        if (done == 3) {
            m = put_span(out, m, t, a.w0, a.w1);
            m = put_ascii(out, m, (a.w1 - a.w0 == 1 && t[a.w0] == '1') ? " cent" : " cents");
        } else {
            m = currency_words(t, &a, done == 1 ? sym : 1, out, m);
        }
        i = end;
    }
    return m;
}

/* ---- normalise (English): the _NUMBER regex, tried as Python's re does, and its sub() --------------------------- */
typedef struct {
    char *p;
    size_t n, cap;
    int oom;
} sbuf;

static void sb_putn(sbuf *b, const char *s, size_t k)
{
    if (b->oom) return;
    if (b->n + k + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        char *q;
        while (cap < b->n + k + 1) cap *= 2;
        q = (char *)realloc(b->p, cap);
        if (!q) { b->oom = 1; return; }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, k);
    b->n += k;
    b->p[b->n] = 0;
}

static void sb_puts(sbuf *b, const char *s) { sb_putn(b, s, strlen(s)); }

static const char *ONES[20] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                               "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
                               "eighteen", "nineteen"};
static const char *TENS[10] = {"_", "_", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
static const struct { unsigned long long value; const char *name; } SCALES[5] = {
    {1000000000000000ULL, "quadrillion"}, {1000000000000ULL, "trillion"}, {1000000000ULL, "billion"},
    {1000000ULL, "million"}, {1000ULL, "thousand"}};

/* cardinal(n): n < 10^18 (LIMIT: the caller says "large number" above it) */
static void cardinal(sbuf *b, unsigned long long n)
{
    int i;
    if (n < 20) { sb_puts(b, ONES[n]); return; }
    if (n < 100) {
        sb_puts(b, TENS[n / 10]);
        if (n % 10) { sb_puts(b, " "); sb_puts(b, ONES[n % 10]); }
        return;
    }
    if (n < 1000) {
        sb_puts(b, ONES[n / 100]);
        sb_puts(b, " hundred");
        if (n % 100) { sb_puts(b, " "); cardinal(b, n % 100); }
        return;
    }
    for (i = 0; i < 5; i++)
        if (n >= SCALES[i].value) {
            cardinal(b, n / SCALES[i].value);
            sb_puts(b, " ");
            sb_puts(b, SCALES[i].name);
            if (n % SCALES[i].value) { sb_puts(b, " "); cardinal(b, n % SCALES[i].value); }
            return;
        }
}

/* ordinal(n): the cardinal's last word made ordinal */
static void ordinal(sbuf *b, unsigned long long n)
{
    static const char *irregular[7][2] = {{"one", "first"}, {"two", "second"}, {"three", "third"}, {"five", "fifth"},
                                          {"eight", "eighth"}, {"nine", "ninth"}, {"twelve", "twelfth"}};
    sbuf w = {0};
    const char *last;
    size_t head;
    int i;
    cardinal(&w, n);
    if (w.oom || !w.p) { b->oom = 1; free(w.p); return; }
    last = strrchr(w.p, ' ');
    last = last ? last + 1 : w.p;
    head = (size_t)(last - w.p);
    sb_putn(b, w.p, head);
    for (i = 0; i < 7; i++)
        if (!strcmp(last, irregular[i][0])) { sb_puts(b, irregular[i][1]); free(w.p); return; }
    if (last[strlen(last) - 1] == 'y') {
        sb_putn(b, last, strlen(last) - 1);
        sb_puts(b, "ieth");
    } else {
        sb_puts(b, last);
        sb_puts(b, "th");
    }
    free(w.p);
}

static int a_digit(char c) { return c >= '0' && c <= '9'; }
static int a_word(char c) { return a_digit(c) || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_'; }   /* \w, 7-bit */

/* two digits or more in s[a..e) (commas skipped), the first a 0: a number written with leading zeros (#65).
   NUMWORDS_ZERO_BREAK=1, the tests' control: never, as it was */
static int leading_zero(const char *s, int a, int e)
{
    int k, n = 0, first = -1;
    for (k = a; k < e; k++)
        if (a_digit(s[k])) {
            if (first < 0) first = s[k];
            n++;
        }
    return n > 1 && first == '0' && !getenv("NUMWORDS_ZERO_BREAK");
}

/* the digits s[a..b) (commas skipped); more than 18 left after the leading zeros (big()): digit by digit -- an
   identifier, not an amount (a tracking number: 20 to 22 digits; ssi263_numwords.py the same) -- an ordinal that long
   "large number".  NUMWORDS_LARGE_BREAK=1, the tests' control: "large number" for all of them, as it was */
static void whole_words(sbuf *b, const char *s, int a, int e, int ord)
{
    unsigned long long v = 0;
    int k, sig = 0;
    for (k = a; k < e; k++)
        if (a_digit(s[k]) && (sig || s[k] != '0'))
            sig++;
    if (sig > 18 && (ord || getenv("NUMWORDS_LARGE_BREAK"))) { sb_puts(b, "large number"); return; }
    if (sig > 18 || (!ord && leading_zero(s, a, e))) {
        /* a code, not an amount ("047291", "007", a ZIP code): digit by digit, the zeros said; as an integer they
           were dropped (#65, Tomi; ssi263_numwords.py the same) */
        int first = 1;
        for (k = a; k < e; k++)
            if (a_digit(s[k])) {
                if (!first) sb_puts(b, " ");
                sb_puts(b, ONES[s[k] - '0']);
                first = 0;
            }
        return;
    }
    for (k = a; k < e; k++)
        if (a_digit(s[k]))
            v = v * 10 + (unsigned long long)(s[k] - '0');
    if (ord) ordinal(b, v); else cardinal(b, v);
}

/* The match at i, as _NUMBER's alternatives are tried there: its end (0: none), *ord set for the ordinal. */
static int match_number(const char *s, int n, int i, int *ord)
{
    int p, j, g, G, ends[64], e, f, fend, d;
    if (i > 0 && (a_word(s[i - 1]) || s[i - 1] == '.'))          /* (?<![\w.]) */
        return 0;
    /* ord: \d+ (?:st|nd|rd|th) \b, case-insensitive */
    *ord = 1;
    for (d = i; d < n && a_digit(s[d]); d++) ;
    if (d > i && d + 2 <= n) {
        char x = (char)(s[d] | 32), y = (char)(s[d + 1] | 32);
        if (((x == 's' && y == 't') || (x == 'n' && y == 'd') || (x == 'r' && y == 'd') || (x == 't' && y == 'h'))
                && (d + 2 == n || !a_word(s[d + 2])))
            return d + 2;
    }
    *ord = 0;
    p = i + (s[i] == '-');
    if (p >= n || !a_digit(s[p]))
        return 0;
    /* num 1: \d{1,3}(?:,\d{3})+ (?:\.\d+)? (?!\w) (?!\.\w) -- every backtracking choice, in the engine's order */
    for (j = 3; j >= 1; j--) {
        for (d = 0; d < j; d++) if (p + d >= n || !a_digit(s[p + d])) break;
        if (d < j) continue;
        G = 0;
        e = p + j;
        while (G < 64 && e + 3 < n && s[e] == ',' && a_digit(s[e + 1]) && a_digit(s[e + 2]) && a_digit(s[e + 3])) {
            e += 4;
            ends[G++] = e;
        }
        for (g = G - 1; g >= 0; g--) {
            int cands[2], nc = 0, c;
            e = ends[g];
            fend = -1;
            if (e + 1 < n && s[e] == '.' && a_digit(s[e + 1]))
                for (fend = e + 1; fend < n && a_digit(s[fend]); fend++) ;
            for (f = fend; f >= e + 2; f--) {                /* the fraction, longest first */
                if ((f < n && a_word(s[f])) || (f + 1 < n && s[f] == '.' && a_word(s[f + 1])))
                    continue;
                return f;
            }
            cands[nc++] = e;                                 /* no fraction */
            for (c = 0; c < nc; c++)
                if (!(cands[c] < n && a_word(s[cands[c]])) && !(cands[c] + 1 < n && s[cands[c]] == '.'
                                                                  && a_word(s[cands[c] + 1])))
                    return cands[c];
        }
    }
    for (d = p; d < n && a_digit(s[d]); d++) ;
    /* num 2: \d+ \.\d+ (?!\w) (?!\.\w) */
    if (d + 1 < n && s[d] == '.' && a_digit(s[d + 1])) {
        for (f = d + 1; f < n && a_digit(s[f]); f++) ;
        if (!(f < n && a_word(s[f])) && !(f + 1 < n && s[f] == '.' && a_word(s[f + 1])))
            return f;
    }
    /* num 3: \d+ (?!\w) (?!\.\d) */
    if (!(d < n && a_word(s[d])) && !(d + 1 < n && s[d] == '.' && a_digit(s[d + 1])))
        return d;
    return 0;
}

char *nw_normalise(const char *s)
{
    sbuf b = {0};
    int n = (int)strlen(s), i = 0;
    sb_puts(&b, "");
    while (i < n && !b.oom) {
        int ord = 0, end = match_number(s, n, i, &ord);
        if (!end) { sb_putn(&b, s + i, 1); i++; continue; }
        if (ord) {
            whole_words(&b, s, i, end - 2, 1);
        } else {
            int a = i + (s[i] == '-'), dot = a, k;
            if (s[i] == '-') sb_puts(&b, "minus ");
            while (dot < end && s[dot] != '.') dot++;
            whole_words(&b, s, a, dot, 0);
            if (dot < end) {                                 /* the fraction, digit by digit */
                sb_puts(&b, " point");
                for (k = dot + 1; k < end; k++) { sb_puts(&b, " "); sb_puts(&b, ONES[s[k] - '0']); }
            }
        }
        i = end;
    }
    if (b.oom) { free(b.p); return NULL; }
    return b.p;
}
