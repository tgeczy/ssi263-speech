/* numwords_es.c -- see numwords_es.h.  The three regexes of normalise(lang="es", decimal_comma=True) -- _ES_THOUSANDS,
 * _ES_DECIMAL, then _NUMBER -- each tried as Python's re tries it (at each position, the alternatives and their
 * backtracking choices in the engine's order), each sub() over the whole text before the next. */
#include <stdlib.h>
#include <string.h>

#include "numwords.h"
#include "numwords_es.h"

static int isword(unsigned c) { return c == '_' || nw_isalnum(c); }       /* \w (str.isalnum, approximated) */
static int isdig(unsigned c) { return c >= '0' && c <= '9'; }

/* ---- a growing UTF-8 string ------------------------------------------------------------------------------------- */
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

static void sb_putcp(sbuf *b, unsigned c)
{
    char u[4];
    if (c < 0x80) { u[0] = (char)c; sb_putn(b, u, 1); }
    else if (c < 0x800) { u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 0x3F)); sb_putn(b, u, 2); }
    else if (c < 0x10000) {
        u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 0x3F)); u[2] = (char)(0x80 | (c & 0x3F));
        sb_putn(b, u, 3);
    } else {
        u[0] = (char)(0xF0 | (c >> 18)); u[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((c >> 6) & 0x3F)); u[3] = (char)(0x80 | (c & 0x3F));
        sb_putn(b, u, 4);
    }
}

/* ---- the words (UTF-8) ------------------------------------------------------------------------------------------- */
static const char *ONES_ES[30] = {
    "cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "once", "doce", "trece",
    "catorce", "quince", "diecis\xc3\xa9is", "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno",
    "veintid\xc3\xb3s", "veintitr\xc3\xa9s", "veinticuatro", "veinticinco", "veintis\xc3\xa9is", "veintisiete",
    "veintiocho", "veintinueve"};
static const char *TENS_ES[10] = {"_", "_", "_", "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta",
                                  "noventa"};
static const char *HUNDREDS_ES[10] = {"_", "ciento", "doscientos", "trescientos", "cuatrocientos", "quinientos",
                                      "seiscientos", "setecientos", "ochocientos", "novecientos"};
static const struct { unsigned long long value; const char *one, *many; } SCALES_ES[2] = {
    {1000000000000ULL, "bill\xc3\xb3n", "billones"}, {1000000ULL, "mill\xc3\xb3n", "millones"}};
#define LARGE_ES "n\xc3\xbamero grande"
#define MINUS_ES "menos"
#define COMMA_ES "coma"

static void cardinal_es(sbuf *b, unsigned long long n);

/* _apocope(cardinal_es(q)): 'veintiuno' -> 'veintiún', '... uno' -> '... un' (only ever before a scale word) */
static void apocope_of(sbuf *b, unsigned long long q)
{
    sbuf w = {0};
    size_t k;
    cardinal_es(&w, q);
    if (w.oom || !w.p) { b->oom = 1; free(w.p); return; }
    k = w.n;
    if (k >= 9 && !memcmp(w.p + k - 9, "veintiuno", 9)) {
        sb_putn(b, w.p, k - 9);
        sb_puts(b, "veinti\xc3\xban");
    } else if (k >= 3 && !memcmp(w.p + k - 3, "uno", 3)) {
        sb_putn(b, w.p, k - 3);
        sb_puts(b, "un");
    } else {
        sb_putn(b, w.p, k);
    }
    free(w.p);
}

/* _cardinal_es(n), n < 10^18 (the caller says LARGE_ES from 19 significant digits up) */
static void cardinal_es(sbuf *b, unsigned long long n)
{
    int i;
    if (n < 30) { sb_puts(b, ONES_ES[n]); return; }
    if (n < 100) {
        sb_puts(b, TENS_ES[n / 10]);
        if (n % 10) { sb_puts(b, " y "); sb_puts(b, ONES_ES[n % 10]); }
        return;
    }
    if (n == 100) { sb_puts(b, "cien"); return; }
    if (n < 1000) {
        sb_puts(b, HUNDREDS_ES[n / 100]);
        if (n % 100) { sb_puts(b, " "); cardinal_es(b, n % 100); }
        return;
    }
    for (i = 0; i < 2; i++)
        if (n >= SCALES_ES[i].value) {
            unsigned long long q = n / SCALES_ES[i].value, r = n % SCALES_ES[i].value;
            if (q == 1) {
                sb_puts(b, "un ");
                sb_puts(b, SCALES_ES[i].one);
            } else {
                apocope_of(b, q);
                sb_puts(b, " ");
                sb_puts(b, SCALES_ES[i].many);
            }
            if (r) { sb_puts(b, " "); cardinal_es(b, r); }
            return;
        }
    /* thousands: bare "mil" for exactly one of them, never "uno mil" */
    if (n / 1000 == 1) sb_puts(b, "mil");
    else { apocope_of(b, n / 1000); sb_puts(b, " mil"); }
    if (n % 1000) { sb_puts(b, " "); cardinal_es(b, n % 1000); }
}

/* the digits t[a..e) (commas skipped) as words, LARGE_ES when more than 18 are left after the leading zeros (big());
   ord: the bare cardinal of an ordinal (never digit by digit) */
static void whole_words(sbuf *b, const unsigned *t, int a, int e, int ord)
{
    unsigned long long v = 0;
    int k, sig = 0, n = 0, first = -1;
    for (k = a; k < e; k++)
        if (isdig(t[k]) && (sig || t[k] != '0'))
            sig++;
    if (sig > 18) { sb_puts(b, LARGE_ES); return; }
    for (k = a; k < e; k++)
        if (isdig(t[k])) {
            if (first < 0) first = (int)t[k];
            n++;
        }
    if (!ord && n > 1 && first == '0' && !getenv("NUMWORDS_ZERO_BREAK")) {
        /* a number written with leading zeros: digit by digit, the zeros said (#65; numwords.c the same) */
        int lead = 1;
        for (k = a; k < e; k++)
            if (isdig(t[k])) {
                if (!lead) sb_puts(b, " ");
                sb_puts(b, ONES_ES[t[k] - '0']);
                lead = 0;
            }
        return;
    }
    for (k = a; k < e; k++)
        if (isdig(t[k]))
            v = v * 10 + (unsigned long long)(t[k] - '0');
    cardinal_es(b, v);
}

static int behind_ok(const unsigned *t, int i, int comma)   /* (?<![\w.]) or, with comma, (?<![\w.,]) */
{
    return i == 0 || !(isword(t[i - 1]) || t[i - 1] == '.' || (comma && t[i - 1] == ','));
}

/* ---- _ES_THOUSANDS.sub: (?<![\w.,])(-?\d{1,3}(?:\.\d{3})+)(?![\w.]|,\d) -> the group without its dots ------------- */
static int es_thousands(const unsigned *t, int n, unsigned *out)
{
    int i = 0, m = 0;
    while (i < n) {
        int end = 0, wm, j, d, g, G, e, ends[64];
        if (behind_ok(t, i, 1))
            for (wm = (t[i] == '-') ? 1 : 0; wm >= 0 && !end; wm--) {
                int p = i + wm;
                for (j = 3; j >= 1 && !end; j--) {
                    for (d = 0; d < j; d++) if (p + d >= n || !isdig(t[p + d])) break;
                    if (d < j) continue;
                    G = 0;
                    e = p + j;
                    while (G < 64 && e + 3 < n && t[e] == '.' && isdig(t[e + 1]) && isdig(t[e + 2]) && isdig(t[e + 3])) {
                        e += 4;
                        ends[G++] = e;
                    }
                    for (g = G - 1; g >= 0; g--) {
                        int x = ends[g];
                        if (x < n && (isword(t[x]) || t[x] == '.')) continue;
                        if (x + 1 < n && t[x] == ',' && isdig(t[x + 1])) continue;
                        end = x;
                        break;
                    }
                }
            }
        if (!end) { out[m++] = t[i++]; continue; }
        for (; i < end; i++)
            if (t[i] != '.') out[m++] = t[i];
    }
    return m;
}

/* ---- _ES_DECIMAL.sub: (?<![\w.,])(-?\d+),(\d+)(?![\w,]) -> group 1 "." group 2 --------------------------------------- */
static int es_decimal(const unsigned *t, int n, unsigned *out)
{
    int i = 0, m = 0;
    while (i < n) {
        int p = i + (t[i] == '-'), d = p, e;
        if (behind_ok(t, i, 1)) {
            while (d < n && isdig(t[d])) d++;
            if (d > p && d + 1 < n && t[d] == ',' && isdig(t[d + 1])) {
                for (e = d + 1; e < n && isdig(t[e]); e++) ;
                if (!(e < n && (isword(t[e]) || t[e] == ','))) {
                    for (; i < d; i++) out[m++] = t[i];
                    out[m++] = '.';
                    for (i = d + 1; i < e; i++) out[m++] = t[i];
                    continue;
                }
            }
        }
        out[m++] = t[i++];
    }
    return m;
}

/* ---- _NUMBER (numwords.c's match_number on code points): the match at i -- its end (0: none), *ord for an ordinal -- */
static int no_word_after(const unsigned *t, int n, int x)  /* (?!\w)(?!\.\w) */
{
    return !(x < n && isword(t[x])) && !(x + 1 < n && t[x] == '.' && isword(t[x + 1]));
}

static int match_number(const unsigned *t, int n, int i, int *ord)
{
    int p, j, g, G, ends[64], e, f, fend, d;
    if (!behind_ok(t, i, 0))
        return 0;
    /* ord: \d+ (?:st|nd|rd|th) \b, case-insensitive */
    *ord = 1;
    for (d = i; d < n && isdig(t[d]); d++) ;
    if (d > i && d + 2 <= n && t[d] < 128 && t[d + 1] < 128) {
        unsigned x = t[d] | 32, y = t[d + 1] | 32;
        if (((x == 's' && y == 't') || (x == 'n' && y == 'd') || (x == 'r' && y == 'd') || (x == 't' && y == 'h'))
                && (d + 2 == n || !isword(t[d + 2])))
            return d + 2;
    }
    *ord = 0;
    p = i + (t[i] == '-');
    if (p >= n || !isdig(t[p]))
        return 0;
    /* num 1: \d{1,3}(?:,\d{3})+ (?:\.\d+)? (?!\w) (?!\.\w) */
    for (j = 3; j >= 1; j--) {
        for (d = 0; d < j; d++) if (p + d >= n || !isdig(t[p + d])) break;
        if (d < j) continue;
        G = 0;
        e = p + j;
        while (G < 64 && e + 3 < n && t[e] == ',' && isdig(t[e + 1]) && isdig(t[e + 2]) && isdig(t[e + 3])) {
            e += 4;
            ends[G++] = e;
        }
        for (g = G - 1; g >= 0; g--) {
            e = ends[g];
            fend = -1;
            if (e + 1 < n && t[e] == '.' && isdig(t[e + 1]))
                for (fend = e + 1; fend < n && isdig(t[fend]); fend++) ;
            for (f = fend; f >= e + 2; f--)                 /* the fraction, longest first */
                if (no_word_after(t, n, f))
                    return f;
            if (no_word_after(t, n, e))                      /* no fraction */
                return e;
        }
    }
    for (d = p; d < n && isdig(t[d]); d++) ;
    /* num 2: \d+ \.\d+ (?!\w) (?!\.\w) */
    if (d + 1 < n && t[d] == '.' && isdig(t[d + 1])) {
        for (f = d + 1; f < n && isdig(t[f]); f++) ;
        if (no_word_after(t, n, f))
            return f;
    }
    /* num 3: \d+ (?!\w) (?!\.\d) */
    if (!(d < n && isword(t[d])) && !(d + 1 < n && t[d] == '.' && isdig(t[d + 1])))
        return d;
    return 0;
}

char *nw_normalise_es(const unsigned *t0, int n0)
{
    sbuf b = {0};
    unsigned *t1 = (unsigned *)malloc(sizeof(unsigned) * (size_t)(n0 + 1));
    unsigned *t = (unsigned *)malloc(sizeof(unsigned) * (size_t)(n0 + 1));
    int n, i = 0;
    if (!t1 || !t) { free(t1); free(t); return NULL; }
    n = es_decimal(t1, es_thousands(t0, n0, t1), t);
    free(t1);
    sb_puts(&b, "");
    while (i < n && !b.oom) {
        int ord = 0, end = match_number(t, n, i, &ord);
        if (!end) { sb_putcp(&b, t[i]); i++; continue; }
        if (ord) {
            whole_words(&b, t, i, end - 2, 1);               /* Spanish: the bare cardinal */
        } else {
            int a = i + (t[i] == '-'), dot = a, k;
            if (t[i] == '-') sb_puts(&b, MINUS_ES " ");
            while (dot < end && t[dot] != '.') dot++;
            whole_words(&b, t, a, dot, 0);
            if (dot < end) {                                 /* the fraction, digit by digit */
                sb_puts(&b, " " COMMA_ES);
                for (k = dot + 1; k < end; k++) { sb_puts(&b, " "); sb_puts(&b, ONES_ES[t[k] - '0']); }
            }
        }
        i = end;
    }
    free(t);
    if (b.oom) { free(b.p); return NULL; }
    return b.p;
}
