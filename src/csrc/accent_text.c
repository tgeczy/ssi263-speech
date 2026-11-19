/* accent_text.c -- see accent_text.h.  Moved here from accentsa/as_voice.c unchanged (and the _ESC rule from
 * accentsa/as_host.c's ash_is_speech), so the Accent SA and the Accent-mini voices read one copy.  MIT. */
#include <stdlib.h>
#include <string.h>

#include "accent_text.h"
#include "numwords.h"
#include "translit.h"

/* ---- _accent_pitch, _accent_settings (int() of a positive float = floor) --------------------------------------- */
static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

int at_pitch_step(int p)
{
    p = clamp100(p);
    return p <= 50 ? (int)(p * 5 / 50.0 + 0.5) : 5 + (int)((p - 50) * 4 / 50.0 + 0.5);
}

char at_rate_char(int r)
{
    static const char RATES[] = "0123456789ABCDEFGH";
    r = clamp100(r);
    return RATES[r <= 50 ? (int)(r * 5 / 50.0 + 0.5) : 5 + (int)((r - 50) * 12 / 50.0 + 0.5)];
}

/* INFLECTION = ((0, 1), (25, 2), (50, 3), (75, 4), (100, 0)): min() by distance, the first of a tie */
int at_inflection(int x)
{
    static const int at[5] = {0, 25, 50, 75, 100}, cmd[5] = {1, 2, 3, 4, 0};
    int i, best = 0;
    for (i = 1; i < 5; i++)
        if (abs(at[i] - x) < abs(at[best] - x))
            best = i;
    return cmd[best];
}

/* ---- the text: _translit (translit.h), currencies, _clean, strip, _numbers ------------------------------------- */

/* _clean: 7-bit, no control characters (ESC and Ctrl-X are the Accent's commands), no tilde ("~/" opens its phoneme
   input and swallows everything to the next "~").  Its few letters (e-acute ... c-cedilla) are 0.7's; translit.h
   now reaches every letter first, and they stay so that ssv_translit_break is exactly the old path. */
static int clean(const unsigned *in, int n, char *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        const char *r = NULL;
        if (c < 32 || c == 127 || c == '~') out[m++] = ' ';
        else if (c < 128) out[m++] = (char)c;
        else {
            switch (c) {
            case 0x2018: case 0x2019: r = "'"; break;
            case 0x201C: case 0x201D: r = "\""; break;
            case 0x2013: case 0x2014: r = "-"; break;
            case 0x2026: r = "..."; break;
            case 0xE9: case 0xE8: r = "e"; break;
            case 0xE1: case 0xE0: r = "a"; break;
            case 0xF6: r = "o"; break;
            case 0xFC: r = "u"; break;
            case 0xF1: r = "n"; break;
            case 0xE7: r = "c"; break;
            default: r = " "; break;
            }
            while (*r) out[m++] = *r++;
        }
    }
    out[m] = 0;
    return m;
}

/* _grouped: a dollar amount with its whole part re-grouped by commas (format(int, ",")) when it has 1-15 digits;
   appended to out, which has room for 2 * len + 8 */
static int grouped(const char *money, int len, char *out)
{
    char digits[16];
    int dot = 1, k, nd = 0, m = 0, i, lead = 0;
    while (dot < len && money[dot] != '.') dot++;
    for (k = 1; k < dot; k++)
        if (money[k] != ',') {
            if (nd < 16) digits[nd] = money[k];
            nd++;
        }
    if (!nd || nd > 15) {                                     /* as it came */
        memcpy(out, money, (size_t)len);
        return len;
    }
    out[m++] = '$';
    while (lead < nd - 1 && digits[lead] == '0') lead++;      /* int() drops leading zeros */
    for (i = lead; i < nd; i++) {
        out[m++] = digits[i];
        if ((nd - 1 - i) % 3 == 0 && i != nd - 1) out[m++] = ',';
    }
    memcpy(out + m, money + dot, (size_t)(len - dot));
    return m + len - dot;
}

/* MONEY = (\$\d[\d,]*(?:\.\d+)?|\$\.\d+): the end of the amount at i, or 0 */
static int money_at(const char *s, int n, int i)
{
    int j;
    if (s[i] != '$' || i + 1 >= n) return 0;
    if (s[i + 1] >= '0' && s[i + 1] <= '9') {
        for (j = i + 2; j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ','); j++) ;
        if (j + 1 < n && s[j] == '.' && s[j + 1] >= '0' && s[j + 1] <= '9')
            for (j += 1; j < n && s[j] >= '0' && s[j] <= '9'; j++) ;
        return j;
    }
    if (s[i + 1] == '.' && i + 2 < n && s[i + 2] >= '0' && s[i + 2] <= '9') {
        for (j = i + 2; j < n && s[j] >= '0' && s[j] <= '9'; j++) ;
        return j;
    }
    return 0;
}

/* _numbers: MONEY.split, the amounts _grouped and the text between them normalise()d, each part on its own */
static char *numbers(const char *s)
{
    int n = (int)strlen(s), i = 0, start = 0, cap = 256, len = 0;
    char *out = (char *)malloc((size_t)cap);
    if (!out) return NULL;
    out[0] = 0;
    for (;;) {
        int end = i < n ? money_at(s, n, i) : 0, need, w;
        char *part, *words;
        if (i < n && !end) { i++; continue; }
        /* the text before the amount (start..i), normalised on its own; then the amount, grouped */
        part = (char *)malloc((size_t)(i - start) + 1);
        if (!part) { free(out); return NULL; }
        memcpy(part, s + start, (size_t)(i - start));
        part[i - start] = 0;
        words = nw_normalise(part);
        free(part);
        if (!words) { free(out); return NULL; }
        w = (int)strlen(words);
        need = len + w + (end ? 2 * (end - i) : 0) + 9;
        if (need > cap) {
            char *q;
            while (cap < need) cap *= 2;
            q = (char *)realloc(out, (size_t)cap);
            if (!q) { free(words); free(out); return NULL; }
            out = q;
        }
        memcpy(out + len, words, (size_t)w);
        len += w;
        free(words);
        if (end)
            len += grouped(s + i, end - i, out + len);
        out[len] = 0;
        if (!end) break;
        i = start = end;
    }
    return out;
}

char *at_say_text(const char *utf8, int with_numbers)
{
    size_t n = strlen(utf8);
    unsigned *t = (unsigned *)malloc((n + 1) * sizeof(unsigned)), *u;
    char *c, *p, *q;
    int k, m;
    if (!t) return NULL;
    k = nw_utf8(utf8, t);
    /* translit.h first: the letters the Accents' 7-bit alphabet lacks, as base letters (or a lone one's words) */
    u = (unsigned *)malloc((size_t)tl_room(k) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    k = tl_apply(t, k, tl_known_ascii, u);
    free(t);
    t = u;
    u = (unsigned *)malloc(((size_t)k * 16 + 64) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    m = nw_currencies(t, k, u);
    free(t);
    c = (char *)malloc((size_t)m * 3 + 1);                    /* "…" -> "..." */
    if (!c) { free(u); return NULL; }
    clean(u, m, c);
    free(u);
    for (p = c; *p == ' '; p++) ;                             /* strip(): only spaces are left to strip */
    for (q = p + strlen(p); q > p && q[-1] == ' '; q--) ;
    *q = 0;
    memmove(c, p, strlen(p) + 1);
    if (!with_numbers) return c;
    p = numbers(c);
    free(c);
    return p;
}

/* ---- _ESC and say(speech=None) ----------------------------------------------------------------------------------- */

/* Python's str.isalnum() on chr(0..255) */
/* ---- index marks: the text with Ctrl-F at the front of the marked words (accent_text.h) ---------------------------- */
static int words_of(const char *t, int n, int *ws, int *we)
{
    int nw = 0, i = 0;
    while (i < n) {
        if (t[i] == ' ') { i++; continue; }
        ws[nw] = i;
        while (i < n && t[i] != ' ') i++;
        we[nw++] = i;
    }
    return nw;
}

/* the word of the whole prepared text a mark at byte `at` of the source stands before (0 to nw): the source up to the
   mark -- from the start of the run of text it stands in -- prepared by itself, its words held against the whole's */
static int mark_word(const char *utf8, int at, int with_numbers, const char *t, const int *ws, const int *we, int nw)
{
    int len = (int)strlen(utf8), pn, pnw, j = 0, *pws, *pwe;
    char *prefix, *pt;
    if (at <= 0) return 0;
    if (at > len) at = len;
    if (at < len)
        while (at > 0 && (unsigned char)utf8[at] > ' ' && (unsigned char)utf8[at - 1] > ' ') at--;
    while (at > 0 && at < len && ((unsigned char)utf8[at] & 0xC0) == 0x80) at--;
    prefix = (char *)malloc((size_t)at + 1);
    if (!prefix) return 0;
    memcpy(prefix, utf8, (size_t)at);
    prefix[at] = 0;
    pt = at_say_text(prefix, with_numbers);
    free(prefix);
    if (!pt) return 0;
    pn = (int)strlen(pt);
    pws = (int *)malloc(sizeof(int) * (size_t)(pn + 1));
    pwe = (int *)malloc(sizeof(int) * (size_t)(pn + 1));
    if (pws && pwe) {
        pnw = words_of(pt, pn, pws, pwe);
        for (j = 0; j < pnw && j < nw; j++) {
            int a = pwe[j] - pws[j];
            if (a != we[j] - ws[j] || memcmp(pt + pws[j], t + ws[j], (size_t)a)) break;
        }
    }
    free(pt); free(pws); free(pwe);
    return j;
}

/* a word the unit speaks: one with a letter or a digit (a dash or a bracket alone is none) */
static int spoken(const char *t, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if ((t[i] >= '0' && t[i] <= '9') || (t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z'))
            return 1;
    return 0;
}

char *at_say_marked(const char *utf8, int with_numbers, const ssv_mark *marks, int n_marks, int *seqs, int *n06,
                    int where, int brk)
{
    char *t = at_say_text(utf8, with_numbers), *out = NULL;
    int n, nw, i, w, s, m = 0, last = 0, count = 0, *ws = NULL, *we = NULL, *mk = NULL;
    if (n06) *n06 = 0;
    for (i = 0; i < n_marks; i++) seqs[i] = 0;
    if (!t || !*t) return t;
    n = (int)strlen(t);
    ws = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    we = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    mk = (int *)calloc((size_t)(n + 2), sizeof(int));          /* per word: the Ctrl-Fs it gets, then their number */
    out = (char *)malloc((size_t)n + (size_t)n_marks + 1);
    if (!ws || !we || !mk || !out) { free(ws); free(we); free(mk); free(out); return t; }
    nw = words_of(t, n, ws, we);
    for (i = 0; i < n_marks; i++) {
        w = mark_word(utf8, marks[i].at, with_numbers, t, ws, we, nw);
        if (w < last) w = last;                                /* in the caller's order, never back */
        last = w;
        /* the word that gets the Ctrl-F: the marked one (in front of it), or the last spoken one before it (after
           it); 0: no word before it, reached at once */
        if (where == AT_MARK_AFTER && w > 0 && w < nw) {
            while (w > 0 && !spoken(t + ws[w - 1], we[w - 1] - ws[w - 1])) w--;
        }
        seqs[i] = w;                                           /* (until the text is laid out) */
        if (w > 0 && w < nw && (!mk[w] || brk == 2)) mk[w]++;
    }
    for (s = 0, i = 0; i <= n; i++) {
        for (; s < nw; s++) {                                  /* the Ctrl-Fs that go in before the character at i */
            int at = where == AT_MARK_AFTER ? (s ? (brk == 4 ? ws[s - 1] + 1 : we[s - 1]) : 0)
                                            : ws[s] + (brk == 4);
            if (at > i) break;
            for (w = mk[s]; w > 0; w--) out[m++] = 0x06;
            count += mk[s];
            if (mk[s]) mk[s] = count;                          /* now: the number of its (last) Ctrl-F */
        }
        if (i < n) out[m++] = t[i];
    }
    out[m] = 0;
    for (i = 0; i < n_marks; i++)
        seqs[i] = seqs[i] <= 0 ? 0 : seqs[i] >= nw ? -1 : mk[seqs[i]];
    if (n06) *n06 = count;
    free(t); free(ws); free(we); free(mk);
    return out;
}

static const unsigned char ALNUM[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0xFE, 0xFF, 0xFF, 0x07, 0xFE, 0xFF, 0xFF, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x2C, 0x76, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF};

static int upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
static int letter(unsigned char c) { return upper(c) || (c >= 'a' && c <= 'z'); }
static int digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* _ESC, "\x1b(?:[=+\-O][A-Za-z]|[A-Z][0-9A-Z]|\|~[^~]*~)": the length of the command at s, or 0 */
static int esc_len(const unsigned char *s, int n)
{
    int k;
    if (n < 3 || s[0] != 0x1B)
        return 0;
    if ((s[1] == '=' || s[1] == '+' || s[1] == '-' || s[1] == 'O') && letter(s[2]))
        return 3;
    if (upper(s[1]) && (digit(s[2]) || upper(s[2])))
        return 3;
    if (s[1] == '|' && s[2] == '~')
        for (k = 3; k < n; k++)
            if (s[k] == '~')
                return k + 1;
    return 0;
}

int at_is_speech(const unsigned char *bytes, int n)
{
    int i = 0;
    while (i < n) {
        int k = esc_len(bytes + i, n - i);
        if (k) {
            i += k;
            continue;
        }
        if (ALNUM[bytes[i] >> 3] >> (bytes[i] & 7) & 1)
            return 1;
        i++;
    }
    return 0;
}
