/* ssp_ssml.c -- see ssp_ssml.h. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ssp_ssml.h"

int ssp_ssml_break = 0;

/* SSML break strengths, ms (the SSML default is medium) */
static const struct { const char *name; int ms; } STRENGTHS[] = {
    {"none", 0}, {"x-weak", 100}, {"weak", 200}, {"medium", 350}, {"strong", 500}, {"x-strong", 800},
};

int ssp_pause_scale(int pause_mode)
{
    if (ssp_ssml_break == 2) return 100;   /* the control: the mode ignored, every pause long */
    return pause_mode == SSP_PAUSE_OFF ? 0 : pause_mode == SSP_PAUSE_LONG ? 100 : 50;
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- a growing byte buffer ----------------------------------------------------------------------------------- */
typedef struct { char *p; size_t n, cap; int oom; } buf;

static void put(buf *b, const char *s, size_t n)
{
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 64;
        char *p;
        while (cap < b->n + n + 1) cap *= 2;
        if (!(p = (char *)realloc(b->p, cap))) { b->oom = 1; return; }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void putc_(buf *b, char c) { put(b, &c, 1); }

/* ---- tags and attributes --------------------------------------------------------------------------------------- */

/* The tag between '<' and '>' (exclusive) is the element name, case-insensitive, closing or not as asked. */
static int is_tag(const char *t, size_t n, const char *name, int closing)
{
    size_t k = 0, m = strlen(name);
    while (k < n && isspace((unsigned char)t[k])) k++;
    if (closing) {
        if (k >= n || t[k] != '/') return 0;
        k++;
        while (k < n && isspace((unsigned char)t[k])) k++;
    } else if (k < n && t[k] == '/') {
        return 0;
    }
    if (n - k < m) return 0;
    for (size_t i = 0; i < m; i++)
        if (tolower((unsigned char)t[k + i]) != name[i]) return 0;
    k += m;
    return k == n || isspace((unsigned char)t[k]) || t[k] == '/' || t[k] == '>';
}

/* An attribute's value inside the tag, single- or double-quoted, into out (cap bytes); 1 when found. */
static int attr(const char *t, size_t n, const char *name, char *out, size_t cap)
{
    size_t m = strlen(name);
    for (size_t i = 1; i + m < n; i++) {
        size_t k;
        char q;
        if (!isspace((unsigned char)t[i - 1])) continue;
        if (strncasecmp(t + i, name, m)) continue;
        k = i + m;
        while (k < n && isspace((unsigned char)t[k])) k++;
        if (k >= n || t[k] != '=') continue;
        k++;
        while (k < n && isspace((unsigned char)t[k])) k++;
        if (k >= n || (t[k] != '"' && t[k] != '\'')) continue;
        q = t[k++];
        {
            size_t j = 0;
            while (k < n && t[k] != q) {
                if (j + 1 < cap) out[j++] = (char)tolower((unsigned char)t[k]);
                k++;
            }
            out[j] = 0;
            /* trim */
            while (j && isspace((unsigned char)out[j - 1])) out[--j] = 0;
            {
                size_t s = 0;
                while (out[s] && isspace((unsigned char)out[s])) s++;
                if (s) memmove(out, out + s, strlen(out + s) + 1);
            }
        }
        return 1;
    }
    return 0;
}

/* A number at the start of s, the rest in *end; 0 when there is none. */
static int number(const char *s, double *v, const char **end)
{
    char *e;
    *v = strtod(s, &e);
    if (e == s) return 0;
    while (isspace((unsigned char)*e)) e++;
    *end = e;
    return 1;
}

static int scaled(int ms, int pct)
{
    ms = clampi(ms, 0, SSP_BREAK_MAX_MS);
    if (ssp_ssml_break == 1 || pct <= 0 || ms <= 0) return 0;
    {
        int s = ms * pct / 100, floor_ = ms < SSP_PAUSE_FLOOR_MS ? ms : SSP_PAUSE_FLOOR_MS;
        return clampi(s > floor_ ? s : floor_, 0, SSP_BREAK_MAX_MS);
    }
}

/* <break>: time= ("800.0ms", "0.8s", a bare number of ms), else strength=, else medium; scaled. */
static int break_ms(const char *t, size_t n, int pct)
{
    char v[64];
    int ms = 350;
    double d;
    const char *e;
    if (attr(t, n, "time", v, sizeof v)) {
        if (number(v, &d, &e)) {
            if (!strcmp(e, "ms") || !*e) ms = (int)floor(d + 0.5);
            else if (!strcmp(e, "s")) ms = (int)floor(d * 1000.0 + 0.5);
        }
    } else if (attr(t, n, "strength", v, sizeof v)) {
        for (size_t i = 0; i < sizeof STRENGTHS / sizeof *STRENGTHS; i++)
            if (!strcmp(v, STRENGTHS[i].name)) ms = STRENGTHS[i].ms;
    }
    return scaled(ms, pct);
}

static int sentence_ms(int pct)
{
    int ms;
    if (ssp_ssml_break == 1 || pct <= 0) return 0;
    ms = SSP_SENTENCE_MS * pct / 100;
    return ms > SSP_PAUSE_FLOOR_MS ? ms : SSP_PAUSE_FLOOR_MS;
}

/* prosody rate: keywords (dt-iPhone's VoiceOverRateMapper), "150%", "1.5", "1.5x" -> percent */
static int rate_percent(const char *v, int *out)
{
    static const struct { const char *k; int p; } K[] = {
        {"x-slow", 50}, {"slow", 75}, {"medium", 100}, {"default", 100}, {"fast", 150}, {"x-fast", 250}};
    double d;
    const char *e;
    for (size_t i = 0; i < sizeof K / sizeof *K; i++)
        if (!strcmp(v, K[i].k)) { *out = K[i].p; return 1; }
    if (!number(v, &d, &e) || d <= 0) return 0;
    if (!strcmp(e, "%")) *out = (int)floor(d + 0.5);
    else if (!*e || !strcmp(e, "x")) *out = (int)floor(d * 100.0 + 0.5);
    else return 0;
    *out = clampi(*out, 1, 1000);
    return 1;
}

/* prosody pitch, relative: keywords, "+10%" / "-10%" / "10%" (TGSpeechBox's: a change), "+2st" -> percent */
static int pitch_percent(const char *v, int *out)
{
    static const struct { const char *k; int p; } K[] = {
        {"x-low", 70}, {"low", 85}, {"medium", 100}, {"default", 100}, {"high", 115}, {"x-high", 130}};
    double d;
    const char *e;
    for (size_t i = 0; i < sizeof K / sizeof *K; i++)
        if (!strcmp(v, K[i].k)) { *out = K[i].p; return 1; }
    if (!number(v, &d, &e)) return 0;
    if (!strcmp(e, "%")) *out = (int)floor(100.0 + d + 0.5);
    else if (!strcmp(e, "st")) *out = (int)floor(100.0 * pow(2.0, d / 12.0) + 0.5);
    else return 0;                         /* Hz, an absolute pitch: the voice's own stays */
    *out = clampi(*out, 1, 1000);
    return 1;
}

/* prosody volume -> amplitude 0..2: keywords, "-6.0206003dB" (VoiceOver's rotor), "50%", a number */
static int volume_factor(const char *v, double *out)
{
    static const struct { const char *k; double f; } K[] = {
        {"silent", 0.0}, {"x-soft", 0.25}, {"soft", 0.5}, {"medium", 1.0}, {"default", 1.0}, {"loud", 1.5},
        {"x-loud", 2.0}};
    double d;
    const char *e;
    for (size_t i = 0; i < sizeof K / sizeof *K; i++)
        if (!strcmp(v, K[i].k)) { *out = K[i].f; return 1; }
    if (!number(v, &d, &e)) return 0;
    if (!strcmp(e, "db")) d = pow(10.0, d / 20.0);
    else if (!strcmp(e, "%")) d /= 100.0;
    else if (*e) return 0;
    *out = d < 0 ? 0 : d > 2 ? 2 : d;
    return 1;
}

/* ---- text: entities decoded, white space collapsed --------------------------------------------------------------- */

static void utf8(buf *b, unsigned long c)
{
    char s[4];
    if (c == 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return;
    if (c < 0x80) { s[0] = (char)c; put(b, s, 1); }
    else if (c < 0x800) { s[0] = (char)(0xC0 | c >> 6); s[1] = (char)(0x80 | (c & 0x3F)); put(b, s, 2); }
    else if (c < 0x10000) {
        s[0] = (char)(0xE0 | c >> 12); s[1] = (char)(0x80 | ((c >> 6) & 0x3F)); s[2] = (char)(0x80 | (c & 0x3F));
        put(b, s, 3);
    } else {
        s[0] = (char)(0xF0 | c >> 18); s[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((c >> 6) & 0x3F)); s[3] = (char)(0x80 | (c & 0x3F));
        put(b, s, 4);
    }
}

/* raw text (tags already spaces) -> the segment's text */
static char *clean(const char *raw, size_t n)
{
    static const struct { const char *e; const char *c; } NAMED[] = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}};
    buf d = {0}, o = {0};
    size_t i = 0;
    put(&d, "", 0);
    while (i < n) {
        if (raw[i] == '&') {
            size_t j = i + 1;
            while (j < n && j - i < 12 && raw[j] != ';' && raw[j] != '&' && !isspace((unsigned char)raw[j])) j++;
            if (j < n && raw[j] == ';') {
                const char *name = raw + i + 1;
                size_t m = j - i - 1;
                int done = 0;
                if (m >= 2 && name[0] == '#') {
                    char tmp[16];
                    unsigned long c;
                    char *e;
                    if (m - 1 < sizeof tmp) {
                        memcpy(tmp, name + 1, m - 1);
                        tmp[m - 1] = 0;
                        c = (tmp[0] == 'x' || tmp[0] == 'X') ? strtoul(tmp + 1, &e, 16) : strtoul(tmp, &e, 10);
                        if (!*e) { utf8(&d, c); done = 1; }
                    }
                } else {
                    for (size_t k = 0; k < sizeof NAMED / sizeof *NAMED; k++)
                        if (strlen(NAMED[k].e) == m && !strncmp(name, NAMED[k].e, m)) {
                            put(&d, NAMED[k].c, strlen(NAMED[k].c));
                            done = 1;
                        }
                }
                if (done) { i = j + 1; continue; }
            }
        }
        putc_(&d, raw[i++]);
    }
    /* white space collapsed and trimmed */
    put(&o, "", 0);
    {
        int space = 0;
        for (size_t k = 0; k < d.n; k++) {
            unsigned char c = (unsigned char)d.p[k];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { space = 1; continue; }
            if (space && o.n) putc_(&o, ' ');
            space = 0;
            putc_(&o, (char)c);
        }
    }
    free(d.p);
    if (d.oom || o.oom) { free(o.p); return NULL; }
    return o.p;
}

/* ---- the request ------------------------------------------------------------------------------------------------- */

typedef struct { ssp_segment *seg; int n, cap; int oom; } list;

static void add(list *l, char *text, int pause)
{
    if (!text) { l->oom = 1; return; }
    if (l->oom) { free(text); return; }
    /* a boundary with no text before it folds into the one before: the longer pause wins */
    if (!*text && l->n) {
        if (pause > l->seg[l->n - 1].pause_ms) l->seg[l->n - 1].pause_ms = pause;
        free(text);
        return;
    }
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        ssp_segment *p = (ssp_segment *)realloc(l->seg, sizeof *p * (size_t)cap);
        if (!p) { free(text); l->oom = 1; return; }
        l->seg = p;
        l->cap = cap;
    }
    l->seg[l->n].text = text;
    l->seg[l->n].pause_ms = pause;
    l->n++;
}

int ssp_parse(const char *ssml, int pause_mode, ssp_request *out)
{
    int pct = ssp_pause_scale(pause_mode);
    int have_rate = 0, have_pitch = 0, have_volume = 0;
    list l = {0};
    buf raw = {0};
    size_t i = 0, n = strlen(ssml);
    memset(out, 0, sizeof *out);
    out->rate = 100;
    out->pitch = 100;
    out->volume = 1.0;
    put(&raw, "", 0);
    while (i < n) {
        const char *t;
        size_t tn, j;
        if (ssml[i] != '<') { putc_(&raw, ssml[i++]); continue; }
        for (j = i + 1; j < n && ssml[j] != '>'; j++) {}
        if (j >= n) { put(&raw, ssml + i, n - i); break; }   /* no '>': text */
        t = ssml + i + 1;
        tn = j - i - 1;
        i = j + 1;
        if (is_tag(t, tn, "break", 0) || is_tag(t, tn, "s", 1) || is_tag(t, tn, "p", 1)) {
            int pause = is_tag(t, tn, "break", 0) ? break_ms(t, tn, pct) : sentence_ms(pct);
            add(&l, clean(raw.p ? raw.p : "", raw.n), pause);
            raw.n = 0;
            if (raw.p) raw.p[0] = 0;
            continue;
        }
        if (is_tag(t, tn, "prosody", 0)) {
            char v[64];
            int p;
            double d;
            if (!have_rate && attr(t, tn, "rate", v, sizeof v) && rate_percent(v, &p)) { out->rate = p; have_rate = 1; }
            if (!have_pitch && attr(t, tn, "pitch", v, sizeof v) && pitch_percent(v, &p)) {
                out->pitch = p;
                have_pitch = 1;
            }
            if (!have_volume && attr(t, tn, "volume", v, sizeof v) && volume_factor(v, &d)) {
                out->volume = d;
                have_volume = 1;
            }
        }
        putc_(&raw, ' ');                  /* any other tag: a space, so words on either side stay apart */
    }
    add(&l, clean(raw.p ? raw.p : "", raw.n), 0);
    free(raw.p);
    if (raw.oom || l.oom) {
        for (int k = 0; k < l.n; k++) free(l.seg[k].text);
        free(l.seg);
        memset(out, 0, sizeof *out);
        return -1;
    }
    /* the request's end, when it ends in speech: the gap before VoiceOver's next request */
    if (l.n && *l.seg[l.n - 1].text && pct > 0 && ssp_ssml_break != 1) {
        int tail = SSP_REQUEST_END_MS * pct / 100;
        if (tail > l.seg[l.n - 1].pause_ms) l.seg[l.n - 1].pause_ms = tail;
    }
    out->seg = l.seg;
    out->n = l.n;
    return 0;
}

void ssp_request_free(ssp_request *r)
{
    if (!r) return;
    for (int k = 0; k < r->n; k++) free(r->seg[k].text);
    free(r->seg);
    r->seg = NULL;
    r->n = 0;
}

int ssp_has_text(const ssp_request *r)
{
    for (int k = 0; k < r->n; k++)
        if (r->seg[k].text[0]) return 1;
    return 0;
}

int ssp_total_pause_ms(const ssp_request *r)
{
    int t = 0;
    for (int k = 0; k < r->n; k++) t += r->seg[k].pause_ms;
    return t;
}
