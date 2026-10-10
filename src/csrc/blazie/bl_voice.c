/* bl_voice.c -- see bl_voice.h.  Each part names the driver function it ports (nvda/blazie/synthDrivers/blazie.py);
 * change them together.  voice_equiv.py compares the two byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI                   /* wingdi.h has a DEFAULT_PITCH of its own */
#include <windows.h>
#else
#include <time.h>
#endif
#include "bl_voice.h"
#include "bl_cp850.h"
#include "../translit.h"

/* the driver's constants */
#define BLOCK_S 0.03
#define UNIT_VOLUME 6
#define MAKEUP 2.0
#define BOARD_LOWPASS_HZ 5000.0
#define CLOSURE_NOISE_LEAD_MS 10.0
#define DEFAULT_RATE 11
#define DEFAULT_PITCH 16
#define DEFAULT_TONE 7
#define MAX_RATE 15
#define MAX_WORDS 14
#define LEAD_THRESHOLD 0.003
#define LEAD_PREROLL 220
#define LIFT_PAUSE_S 0.5         /* blazie.py LIFT_PAUSE_S: speech after this much silence starts fresh */
/* hosts/blazie.py boot_keys(menu=("punct_none", "numbers_toggle"), start=3000000, gap=1500000, status) */
#define KEY_START 3000000ULL
#define KEY_GAP 1500000ULL

struct bl_voice {
    ssi263 *chip;
    bl_host *host;
    int encoding;
    int rate, pitch, tone, volume, pack;          /* NVDA's scales */
    int run_ahead;                                /* the driver's runAhead (blv_set_run_ahead) */
    blv_numbers_fn numbers;                       /* the driver's numberWords (blv_set_numbers); NULL: off */
    int sent_rate, sent_pitch, sent_tone;         /* unit.sent_settings */
    double gain;
    int lead, active;
    int line_lift, lift_next;     /* blv_set_line_lift; the next utterance comes after a cancel (or is the first) */
    double quiet_since;           /* blv_clock() when the last utterance ended; < 0 while one is under way */
    int fault;                    /* the host refused or failed an utterance (bh_say/bh_busy < 0: run ahead's sticky
                                     faults, Astra's Reply 112); the next blv_speak recovers with bh_cancel first */
    short *pcm;
    int pcm_cap;
};

/* ---- _boot ---------------------------------------------------------------------------------------------------- */
BL_API bl_voice *blv_create(const char *firmware, const char *state, int encoding, double out_rate, int inflection,
                            int whine, char *err, int errlen)
{
    bl_voice *v = (bl_voice *)calloc(1, sizeof(bl_voice));
    ssi263_params p;
    unsigned char codes[16];
    unsigned long long key_at[16];
    int n = 0, i;
    const double *y;
    if (!v) { snprintf(err, errlen, "out of memory"); return NULL; }
    /* params = {"closure_noise_lead_ms": CLOSURE_NOISE_LEAD_MS}, and carrier_rel_db -300 under the whine model */
    ssi263_default_params(&p);
    p.closure_noise_lead_s = CLOSURE_NOISE_LEAD_MS / 1000.0;
    if (whine)
        p.carrier_rel_db = -300.0;
    v->chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
    if (!v->chip) { snprintf(err, errlen, "could not create the chip"); free(v); return NULL; }
    /* boot_keys: the status menu first when inflection goes off (34-chord, i, n, e-chord), then the speech menu
       (345-chord, z = punctuation none, n = full numbers, 123456-chord, L, e) */
    if (!inflection) {
        codes[n++] = 0x4C; codes[n++] = 0x0A; codes[n++] = 0x1D; codes[n++] = 0x51;
    }
    codes[n++] = 0x5C; codes[n++] = 0x35; codes[n++] = 0x1D; codes[n++] = 0x7F; codes[n++] = 0x07; codes[n++] = 0x51;
    for (i = 0; i < n; i++)
        key_at[i] = KEY_START + (unsigned long long)i * KEY_GAP;
    v->host = bh_create(firmware, state, v->chip, out_rate, BOARD_LOWPASS_HZ, key_at, codes, n,
                        KEY_START + (unsigned long long)n * KEY_GAP, 0, err, errlen);
    if (!v->host) { ssi263_free(v->chip); free(v); return NULL; }
    bh_send(v->host, (const unsigned char *)"\x18", 1);
    bh_send(v->host, (const unsigned char *)"\r\x06", 2);
    bh_run(v->host, 0.3, 0.0005, &y);
    {
        char vol[16];
        int k = snprintf(vol, sizeof vol, "\x05%dV", UNIT_VOLUME);
        bh_send(v->host, (const unsigned char *)vol, k);
    }
    bh_run(v->host, 0.05, 0.0005, &y);
    bh_set_whine(v->host, whine);
    v->encoding = encoding;
    v->sent_rate = DEFAULT_RATE; v->sent_pitch = DEFAULT_PITCH; v->sent_tone = DEFAULT_TONE;
    v->rate = 50; v->pitch = 50; v->tone = DEFAULT_TONE; v->volume = 100; v->pack = 1;
    v->lift_next = 1;
    v->quiet_since = -1.0;
    return v;
}

BL_API void blv_destroy(bl_voice *v)
{
    if (!v) return;
    if (v->host) bh_destroy(v->host);
    if (v->chip) ssi263_free(v->chip);
    free(v->pcm);
    free(v);
}

/* a monotonic clock in seconds (the lift's pause); the tests may set their own */
static double monotonic_s(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;                      /* every Windows (GetTickCount64 is Vista's, not in the
                                                x86 headers for Windows 7) */
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
#endif
}
BL_API double (*blv_clock)(void) = monotonic_s;

BL_API void blv_set_line_lift(bl_voice *v, int on) { v->line_lift = on != 0; }
BL_API int blv_get_line_lift(const bl_voice *v) { return v->line_lift; }
BL_API bl_host *blv_host(bl_voice *v) { return v->host; }
BL_API ssi263 *blv_chip(bl_voice *v) { return v->chip; }

BL_API void blv_set(bl_voice *v, int rate, int pitch, int tone, int volume, int pack)
{
    v->rate = rate; v->pitch = pitch; v->tone = tone; v->volume = volume; v->pack = pack != 0;
}

BL_API void blv_set_run_ahead(bl_voice *v, int on)
{
    v->run_ahead = on != 0;
}

BL_API void blv_set_numbers(bl_voice *v, blv_numbers_fn fn)
{
    v->numbers = fn;
}

/* ---- _unit_rate, _unit_pitch (int() of a positive float = floor) ---------------------------------------------- */
static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

static int unit_rate(int r)
{
    r = clamp100(r);
    return r <= 50 ? 1 + (int)(r * (DEFAULT_RATE - 1) / 50.0 + 0.5)
                   : DEFAULT_RATE + (int)((r - 50) * (MAX_RATE - DEFAULT_RATE) / 50.0 + 0.5);
}

static int unit_pitch(int p)
{
    p = clamp100(p);
    return p <= 50 ? 1 + (int)(p * (DEFAULT_PITCH - 1) / 50.0 + 0.5)
                   : DEFAULT_PITCH + (int)((p - 50) * (63 - DEFAULT_PITCH) / 50.0 + 0.5);
}

/* ---- text as Python sees it: code points, str.isspace ---------------------------------------------------------- */
static int utf8_decode(const char *s, unsigned *out)      /* returns the count; invalid bytes -> U+FFFD */
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

static int py_isspace(unsigned c)
{
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

static int cp850_byte(unsigned c)                           /* -1 if cp850 has no such character */
{
    int i;
    if (c < 0x80) return (int)c;
    for (i = 0; i < 128; i++)
        if (bl_cp850_high[i] == c) return 0x80 + i;
    return -1;
}

static int known_cp850(unsigned c) { return cp850_byte(c) >= 0; }    /* the Spanish unit's alphabet (translit.h) */

/* ---- ssi263_numwords.currencies (English only): "£2.63" -> "2 pounds 63 pence" -------------------------------------
   The regex, tried as Python's re does: at each position the three alternatives in order, each amount in the
   engine's preference order (longest first, then backtracking), the first that passes the lookahead wins.
   str.isalnum / \w beyond Latin-1 are approximated (letters unless punctuation, symbols or emoji). */
static int py_isalnum(unsigned c)
{
    if (c < 128) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    if (c < 256) return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA
                     || (c >= 0xBC && c <= 0xBE) || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xE000 && c <= 0xF8FF)
            || (c >= 0xFE00 && c <= 0xFE0F) || c >= 0x1F000)
        return 0;
    return !py_isspace(c);
}

static int py_isword(unsigned c) { return c == '_' || py_isalnum(c); }
static int isdig(unsigned c) { return c >= '0' && c <= '9'; }

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
    for (g = j; g > i && nw < 64; g--) wend[nw++] = g;
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

static int currencies(const unsigned *t, int n, unsigned *out)
{
    int i = 0, m = 0;
    amount cand[256];
    while (i < n) {
        int nc, c, end = -1, sym = -1, done = 0, before = i > 0 && !py_isword(t[i - 1]) && t[i - 1] != '.'
                                                           && t[i - 1] != ',';
        int m0 = m;
        amount a;
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
        if (i > 0 && py_isalnum(t[i - 1])) out[m++] = ' ';
        if (done == 3) {
            m = put_span(out, m, t, a.w0, a.w1);
            m = put_ascii(out, m, (a.w1 - a.w0 == 1 && t[a.w0] == '1') ? " cent" : " cents");
        } else {
            m = currency_words(t, &a, done == 1 ? sym : 1, out, m);
        }
        (void)m0;
        i = end;
    }
    return m;
}

/* ---- _clean ---------------------------------------------------------------------------------------------------- */
static int clean(const unsigned *in, int n, int encoding, unsigned *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        if (c < 32 || c == 127) out[m++] = ' ';
        else if (c < 128) out[m++] = c;
        else if (encoding == BLV_CP850 && cp850_byte(c) >= 0) out[m++] = c;
        else if (c == 0x2018 || c == 0x2019) out[m++] = '\'';
        else if (c == 0x201C || c == 0x201D) out[m++] = '"';
        else if (c == 0x2013 || c == 0x2014) out[m++] = '-';
        else if (c == 0x2026) { out[m++] = '.'; out[m++] = '.'; out[m++] = '.'; }
        else out[m++] = ' ';
    }
    return m;
}

/* ---- _lines: sentences split at whitespace after . ! ?, words by str.split(), long sentences cut at a comma (4+
   words) or at MAX_WORDS, then packed from the second line on.  Every line is a run of consecutive words. ---------- */
typedef struct { int w0, w1; } span;

static int lines(const unsigned *t, int n, int pack, int *ws, int *we, span *out)
{
    int nw = 0, i = 0, np = 0, no = 0, k;
    span *pieces = (span *)malloc(sizeof(span) * (size_t)(n + 1));
    int sent_w0 = 0;
    if (!pieces) return 0;
    while (i <= n) {
        /* one sentence: up to a whitespace run that follows . ! ? (or the end) */
        int end_of_sentence = 0;
        if (i < n && !py_isspace(t[i])) {
            ws[nw] = i;
            while (i < n && !py_isspace(t[i])) i++;
            we[nw++] = i;
            continue;
        }
        if (i == n) end_of_sentence = 1;
        else {
            int run = i;
            if (run > 0 && (t[run - 1] == '.' || t[run - 1] == '!' || t[run - 1] == '?')) end_of_sentence = 1;
            while (i < n && py_isspace(t[i])) i++;
        }
        if (end_of_sentence) {
            int a = sent_w0, b = nw;
            if (b - a <= MAX_WORDS) {
                if (b > a) { pieces[np].w0 = a; pieces[np].w1 = b; np++; }
            } else {
                int start = a, cnt = 0, w;
                for (w = a; w < b; w++) {
                    cnt++;
                    if ((t[we[w] - 1] == ',' && cnt >= 4) || cnt >= MAX_WORDS) {
                        pieces[np].w0 = start; pieces[np].w1 = w + 1; np++;
                        start = w + 1; cnt = 0;
                    }
                }
                if (cnt) { pieces[np].w0 = start; pieces[np].w1 = b; np++; }
            }
            sent_w0 = nw;
            if (i == n) break;
        }
    }
    for (k = 0; k < np; k++) {
        if (pack && no > 1 && (out[no - 1].w1 - out[no - 1].w0) + (pieces[k].w1 - pieces[k].w0) <= MAX_WORDS)
            out[no - 1].w1 = pieces[k].w1;
        else
            out[no++] = pieces[k];
    }
    free(pieces);
    return no;
}

/* ---- the driver's `if self._numbers: text = _numbers(text, unit.lang)`, between _clean and _lines: the caller's fn
   (bl_numbers.h) on the cleaned text as UTF-8, its answer decoded back.  *t replaced (malloc'd); returns the count, or
   -1 when out of memory (the text then goes on as it was) -------------------------------------------------------------- */
static int numbers(blv_numbers_fn fn, unsigned **t, int m, int encoding)
{
    unsigned char *u = (unsigned char *)malloc((size_t)m * 4 + 1);
    char *w;
    unsigned *r;
    int i, k = 0;
    if (!u) return -1;
    for (i = 0; i < m; i++) {
        unsigned c = (*t)[i];
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
    u[k] = 0;
    w = fn((const char *)u, encoding);
    free(u);
    if (!w) return -1;
    r = (unsigned *)malloc(sizeof(unsigned) * (strlen(w) + 1));
    if (!r) { free(w); return -1; }
    k = utf8_decode(w, r);
    free(w);
    free(*t);
    *t = r;
    return k;
}

/* ---- the text the driver hands unit.say(): _translit (translit.h; the driver's ssv_translit), currencies, _clean,
   (_numbers), _lines, encoded, each line \r ^F, one more \r ^F.  malloc'd into *out (the caller frees it); returns the number of lines (0: nothing to say) -------------- */
static int say_bytes(const char *utf8, int encoding, int pack, blv_numbers_fn fn, unsigned char **out, int *out_len)
{
    int n = (int)strlen(utf8), m, nl, k, w, len = 0;
    unsigned *cps, *cur, *t;
    int *ws = NULL, *we = NULL;
    span *ln = NULL;
    unsigned char *data = NULL;
    *out = NULL;
    *out_len = 0;
    cps = (unsigned *)malloc(sizeof(unsigned) * (size_t)(n + 1));
    if (!cps) return 0;
    m = utf8_decode(utf8, cps);
    /* translit.h first: the letters this unit's alphabet lacks, as base letters (or a lone one's words) */
    cur = (unsigned *)malloc(sizeof(unsigned) * (size_t)tl_room(m));
    if (!cur) { free(cps); return 0; }
    m = tl_apply(cps, m, encoding == BLV_CP850 ? known_cp850 : tl_known_ascii, cur);
    free(cps);
    cps = cur;
    /* sizes: currencies() writes at most 8 code points per one it reads ("£.5" -> "50 pence"), _clean 3 ("...") */
    cur = (unsigned *)malloc(sizeof(unsigned) * (size_t)(8 * m + 16));
    t = (unsigned *)malloc(sizeof(unsigned) * (size_t)(24 * m + 48));
    if (!cur || !t) {
        free(cps); free(cur); free(t);
        return 0;
    }
    /* numwords.currencies(value, unit.lang): the English unit only */
    if (encoding == BLV_LATIN1)
        m = currencies(cps, m, cur);
    else
        memcpy(cur, cps, sizeof(unsigned) * (size_t)m);
    m = clean(cur, m, encoding, t);
    if (fn) {
        int r = numbers(fn, &t, m, encoding);
        if (r >= 0) m = r;
    }
    ws = (int *)malloc(sizeof(int) * (size_t)(m + 1));
    we = (int *)malloc(sizeof(int) * (size_t)(m + 1));
    ln = (span *)malloc(sizeof(span) * (size_t)(m + 1));
    data = (unsigned char *)malloc((size_t)m * 3 + 16);
    if (!ws || !we || !ln || !data) {
        free(cps); free(cur); free(t); free(ws); free(we); free(ln); free(data);
        return 0;
    }
    nl = lines(t, m, pack, ws, we, ln);
    /* unit.say(lines): each line encoded ("replace" -> '?'), then \r ^F; one more \r ^F flushes */
    for (k = 0; k < nl; k++) {
        for (w = ln[k].w0; w < ln[k].w1; w++) {
            int i;
            if (w > ln[k].w0) data[len++] = ' ';
            for (i = ws[w]; i < we[w]; i++) {
                int b = encoding == BLV_CP850 ? cp850_byte(t[i]) : (t[i] < 256 ? (int)t[i] : -1);
                data[len++] = (unsigned char)(b < 0 ? '?' : b);
            }
        }
        data[len++] = '\r'; data[len++] = 0x06;
    }
    if (nl) {
        data[len++] = '\r'; data[len++] = 0x06;
    }
    free(cps); free(cur); free(t); free(ws); free(we); free(ln);
    *out = data;
    *out_len = len;
    return nl;
}

BL_API int blv_say_bytes(const char *utf8, int encoding, int pack, unsigned char *out, int cap)
{
    return blv_say_bytes_with(utf8, encoding, pack, NULL, out, cap);
}

BL_API int blv_say_bytes_with(const char *utf8, int encoding, int pack, blv_numbers_fn fn, unsigned char *out, int cap)
{
    unsigned char *data;
    int len, nl = say_bytes(utf8, encoding, pack, fn, &data, &len);
    (void)nl;
    if (data && len <= cap)
        memcpy(out, data, (size_t)len);
    free(data);
    return len;
}

/* ---- _speakSegment + _speakItems (one text item) ----------------------------------------------------------------- */
BL_API int blv_speak(bl_voice *v, const char *utf8)
{
    int r = unit_rate(v->rate), p = unit_pitch(v->pitch), tone = v->tone, nl, len;
    unsigned char *data;
    const double *y;
    if (r != v->sent_rate || p != v->sent_pitch || tone != v->sent_tone) {
        char cmd[48];
        int c = snprintf(cmd, sizeof cmd, "\x05%dE\x05%dP\x05%dT", r, p, tone);
        bh_send(v->host, (const unsigned char *)cmd, c);
        bh_run(v->host, 0.02, 0.0005, &y);
        v->sent_rate = r; v->sent_pitch = p; v->sent_tone = tone;
    }
    v->gain = MAKEUP * v->volume / 100.0;
    v->lead = 1;
    v->active = 0;
    /* unit.run_ahead: on with short pauses only (both Braille Lite voices are in the driver's RUN_AHEAD_TESTED); read
       by the host's next say, which runs the mode (run_ahead.h) -- nothing of it here */
    bh_set_int(v->host, "run_ahead", v->run_ahead && v->pack);
    if (v->fault) {                                  /* the explicit recovery: a cancel clears the host's fault */
        bh_cancel(v->host, 3.0, -1.0, -1.0);
        v->fault = 0;
    }
    nl = say_bytes(utf8, v->encoding, v->pack, v->numbers, &data, &len);
    if (nl) {
        /* the line-start lift (blv_set_line_lift): the first line of speech that interrupted other speech (or
           came first), as the unit lifts a line moved to; never speech queued behind other speech (say-all) */
        if (v->line_lift && (v->lift_next || (v->quiet_since >= 0.0 && blv_clock() - v->quiet_since >= LIFT_PAUSE_S)))
            bh_set_int(v->host, "line_lift", 1);
        v->lift_next = 0;
        v->quiet_since = -1.0;
        bh_set_int(v->host, "turbo_between_lines", v->pack);
        if (bh_say(v->host, data, len) < 0 && !blv_break_fault) {   /* refused: say so, never silently */
            v->fault = 1;
            free(data);
            return -1;
        }
        v->active = 1;
    }
    free(data);
    return nl;
}

BL_API int blv_render(bl_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0;
    *pcm = v->pcm;
    if (!v->active) { *done = 1; return 0; }
    n = bh_run(v->host, BLOCK_S, 0.0005, &y);
    if (v->lead) {                                   /* _trim_lead */
        int k;
        for (k = 0; k < n; k++)
            if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) break;
        if (k < n) { start = k > LEAD_PREROLL ? k - LEAD_PREROLL : 0; v->lead = 0; }
        else start = n;
    }
    if (n - start > v->pcm_cap) {
        short *b = (short *)realloc(v->pcm, sizeof(short) * (size_t)(n - start));
        if (!b) { *done = 1; v->active = 0; return 0; }
        v->pcm = b;
        v->pcm_cap = n - start;
    }
    if (n > start)
        ssi_pcm16(y + start, n - start, v->gain, v->pcm);
    *pcm = v->pcm;
    *done = 0;
    {
        int busy = bh_busy(v->host, 0.1, 3.0);
        if (busy < 0 && blv_break_fault)
            busy = 1;                                /* the control: the fault taken as busy, as before */
        if (busy < 0)                                /* a fault ends the utterance here, not silence until a cancel */
            v->fault = 1;
        if (busy <= 0) {
            v->active = 0;
            *done = 1;
            v->quiet_since = blv_clock();
        }
    }
    return n - start;
}

BL_API void blv_cancel(bl_voice *v)
{
    bh_cancel(v->host, 3.0, -1.0, -1.0);
    v->lift_next = 1;
    v->active = 0;
    v->fault = 0;
}

BL_API int blv_break_fault = 0;

BL_API int blv_fault(const bl_voice *v)
{
    return v->fault;
}
