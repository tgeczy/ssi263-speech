/* so_voice.c -- see so_voice.h.  Each part names the driver function it ports (nvda/speakout/synthDrivers/
 * speakout.py); change them together.  nvda/tools/so_voice_equiv.py compares the two byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_voice.h"
#include "../numwords.h"
#include "../translit.h"
#ifdef SO_HAVE_SOUNDINGBOARD
#include "../soundingboard/sb_host.h"
#endif

/* the driver's constants */
#define BLOCK_S 0.03
#define STEP_S 0.0005
#define LEAD_THRESHOLD 0.003
#define LEAD_PREROLL 220
#define BOOT_S 0.3                      /* SpeakOut.boot()'s default */
#define CANCEL_LIMIT 0.6
#define DEFAULT_TONE 8                  /* "i" */
static const char TONES[] = "abcdefghijklmnopqrstuvwxyz";

typedef struct {
    int rate, pitch, tone, word, sentence;          /* _box_settings(): R, P, T, W, I */
} box_settings;

struct so_voice {
    so_host *h;
    ssi263 *chip;
    char *hex;                                       /* the firmware's text, kept for a reboot */
    size_t hex_len;
#ifdef SO_HAVE_SOUNDINGBOARD
    /* the Sounding Board instead (sov_create_sb): GW Micro's DOS drivers, the Speak-Out's front end byte for byte,
       in their own host (sb_host.h); then h is NULL and these are kept for a reboot */
    sb_host *sb;
    unsigned char *sbload, *sbcom;
    size_t sbload_len, sbcom_len;
#endif
    double out_rate;
    int rate, pitch, tone, volume, join, short_pauses;   /* NVDA's scales */
    box_settings sent;                               /* _sent */
    int sent_valid;                                  /* _sent is not None */
    int base_pitch, cur_pitch;                       /* settings[1], _cur_pitch */
    int pitch_dirty, snap_until_speech;
    int lead, active, fault;
    int settings_unread;                             /* sov_begin sent settings the box has not run a block since */
    int job;                                         /* inside sov_begin ... sov_end: the driver's whole sequence */
    double t_start, gain;
    short *pcm;
    int pcm_cap;
    /* index marks (so_voice.h) */
    struct { int id, value; } *mk; /* the utterance's marks not yet reached: value = the byte the box sends back
                                      when speech reaches it, MK_NOW at once, MK_END at the utterance's end */
    int n_mk, cap_mk;
    int next_value;                /* the next mark's byte (MARK_FIRST .. MARK_LAST, round and round) */
    int index_on;                  /* this box has been told to send its replies (Ctrl-E ] 1) */
    ssv_reach *reached;            /* the marks the last sov_render's block reached (sov_reached) */
    int n_reached, cap_reached;
};
#define MK_NOW 0
#define MK_END -1
/* the bytes used for marks: letters and digits, well away from the thirteen values the box does not send back as
   they are (05h, 18h, 19h, BAh-BFh, FAh, FCh, FDh, FEh: 18h is its cancel) */
#define MARK_FIRST 0x30
#define MARK_LAST 0x7A

/* ---- the box's host: the Speak-Out's (so_host.h), or the Sounding Board's (sb_host.h), which takes the same calls -- */
typedef struct {
    so_host *so;
#ifdef SO_HAVE_SOUNDINGBOARD
    sb_host *sb;
#endif
} box;

static box box_of(const so_voice *v)
{
    box b;
    b.so = v->h;
#ifdef SO_HAVE_SOUNDINGBOARD
    b.sb = v->sb;
#endif
    return b;
}

#ifdef SO_HAVE_SOUNDINGBOARD
#define ON_BOX(b, so_call, sb_call) ((b).sb ? (sb_call) : (so_call))
#else
#define ON_BOX(b, so_call, sb_call) (so_call)
#endif

static void box_say(box b, const unsigned char *bytes, int n) { ON_BOX(b, soh_say(b.so, bytes, n), sbh_say(b.sb, bytes, n)); }
static int box_run(box b, double s, double step, const double **y)
{
    return ON_BOX(b, soh_run(b.so, s, step, y), sbh_run(b.sb, s, step, y));
}
static void box_boot(box b, double s) { ON_BOX(b, soh_boot(b.so, s), sbh_boot(b.sb, s)); }
static void box_cancel(box b, double limit) { ON_BOX(b, soh_cancel(b.so, limit), sbh_cancel(b.sb, limit)); }
static int box_busy(box b, double quiet, double patience)
{
    return ON_BOX(b, soh_busy(b.so, quiet, patience), sbh_busy(b.sb, quiet, patience));
}
static int box_fault(box b) { return ON_BOX(b, soh_fault(b.so), sbh_fault(b.sb)); }
static double box_last_speech(box b)
{
    return ON_BOX(b, soh_get_double(b.so, "last_speech"), sbh_get_double(b.sb, "last_speech"));
}
static ssi263 *box_chip(box b) { return ON_BOX(b, soh_chip(b.so), sbh_chip(b.sb)); }
static void box_destroy(box b) { ON_BOX(b, soh_destroy(b.so), sbh_destroy(b.sb)); }

/* ---- _boot ---------------------------------------------------------------------------------------------------- */
static box boot(const so_voice *v, double out_rate, char *err, int errlen)
{
    static const unsigned char PUNCT_NONE[] = {0x05, 'M', 'n'};  /* punctuation: none -- NVDA speaks symbols itself */
    const double *y;
    box b;
    memset(&b, 0, sizeof b);
#ifdef SO_HAVE_SOUNDINGBOARD
    if (v->sbload) {
        b.sb = sbh_create(v->sbload, v->sbload_len, v->sbcom, v->sbcom_len, NULL, out_rate, err, errlen);
        if (!b.sb)
            return b;
    } else
#endif
    if (!(b.so = soh_create(v->hex, v->hex_len, NULL, out_rate, err, errlen)))
        return b;
    box_boot(b, BOOT_S);
    box_say(b, PUNCT_NONE, (int)sizeof PUNCT_NONE);
    box_run(b, 0.05, STEP_S, &y);
    return b;
}

static int booted(box b)
{
#ifdef SO_HAVE_SOUNDINGBOARD
    if (b.sb)
        return 1;
#endif
    return b.so != NULL;
}

/* the voice's box replaced by a new one (and the old freed) */
static void take_box(so_voice *v, box b)
{
    box_destroy(box_of(v));
    v->h = b.so;
#ifdef SO_HAVE_SOUNDINGBOARD
    v->sb = b.sb;
#endif
    v->chip = box_chip(b);
    v->index_on = 0;                        /* a new box: not told yet */
}

/* the box booted and the voice's defaults set; the voice freed on failure */
static so_voice *start(so_voice *v, double out_rate, char *err, int errlen)
{
    box b = boot(v, out_rate, err, errlen);
    if (!booted(b)) {
        sov_destroy(v);
        return NULL;
    }
    take_box(v, b);
    v->out_rate = out_rate;
    /* __init__'s defaults: rate 50 (box 5), pitch 50 (box 3), volume 100, tone i, join and short pauses on */
    v->rate = 50; v->pitch = 50; v->volume = 100; v->tone = DEFAULT_TONE; v->join = 1; v->short_pauses = 1;
    return v;
}

#ifdef SO_HAVE_SOUNDINGBOARD
SO_API so_voice *sov_create_sb(const unsigned char *sbload, size_t sbload_len, const unsigned char *sbcom,
                               size_t sbcom_len, double out_rate, char *err, int errlen)
{
    so_voice *v = (so_voice *)calloc(1, sizeof *v);
    if (!v || !(v->sbload = (unsigned char *)malloc(sbload_len ? sbload_len : 1))
            || !(v->sbcom = (unsigned char *)malloc(sbcom_len ? sbcom_len : 1))) {
        sov_destroy(v);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(v->sbload, sbload, sbload_len);
    v->sbload_len = sbload_len;
    memcpy(v->sbcom, sbcom, sbcom_len);
    v->sbcom_len = sbcom_len;
    return start(v, out_rate, err, errlen);
}
#endif

SO_API so_voice *sov_create_hex(const char *hex, size_t len, double out_rate, char *err, int errlen)
{
    so_voice *v = (so_voice *)calloc(1, sizeof *v);
    if (!v || !(v->hex = (char *)malloc(len ? len : 1))) {
        free(v);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(v->hex, hex, len);
    v->hex_len = len;
    return start(v, out_rate, err, errlen);
}

SO_API so_voice *sov_create(const char *firmware, double out_rate, char *err, int errlen)
{
    FILE *f = fopen(firmware, "rb");
    long len;
    char *text;
    so_voice *v;
    if (!f) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot open the firmware (SPEAKOUT.HEX)");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)
            || !(text = (char *)malloc(len ? (size_t)len : 1))) {
        fclose(f);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot read the firmware");
        return NULL;
    }
    if (fread(text, 1, (size_t)len, f) != (size_t)len) {
        free(text);
        fclose(f);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot read the firmware");
        return NULL;
    }
    fclose(f);
    v = sov_create_hex(text, (size_t)len, out_rate, err, errlen);
    free(text);
    return v;
}

SO_API void sov_destroy(so_voice *v)
{
    if (!v) return;
    box_destroy(box_of(v));
    free(v->hex);
#ifdef SO_HAVE_SOUNDINGBOARD
    free(v->sbload);
    free(v->sbcom);
#endif
    free(v->pcm);
    free(v->mk);
    free(v->reached);
    free(v);
}

SO_API so_host *sov_host(so_voice *v) { return v->h; }
#ifdef SO_HAVE_SOUNDINGBOARD
SO_API void *sov_sb_host(so_voice *v) { return v->sb; }
#endif
SO_API ssi263 *sov_chip(so_voice *v) { return v->chip; }
SO_API double sov_sample_rate(const so_voice *v) { return v->out_rate; }
SO_API int sov_fault(const so_voice *v) { return v->fault; }

/* ---- the setters: rate and pitch clamped to 0-100, volume to 0-200 (above 100 louder, ssi_pcm16 clipping at full
   scale, as asv_set and blv_set: Android's slider; the driver stops at 100), a tone outside A-Z ignored ------------ */
static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

SO_API void sov_set(so_voice *v, int rate, int pitch, int tone, int volume, int join, int short_pauses)
{
    v->rate = clamp100(rate);
    v->pitch = clamp100(pitch);
    if (tone >= 0 && tone < 26)
        v->tone = tone;
    v->volume = volume < 0 ? 0 : volume > 200 ? 200 : volume;
    v->join = join != 0;
    v->short_pauses = short_pauses != 0;
}

/* ---- _switch_rate: a new box at the new rate; every setting sent again ------------------------------------------- */
SO_API int sov_set_sample_rate(so_voice *v, double out_rate)
{
    char err[128];
    box b;
    if (out_rate == v->out_rate)
        return 0;
    if (v->active)
        sov_cancel(v);
    b = boot(v, out_rate, err, (int)sizeof err);
    if (!booted(b))
        return -1;
    take_box(v, b);
    v->out_rate = out_rate;
    v->sent_valid = 0;
    v->pitch_dirty = 0;                     /* (the driver leaves _snap_until_speech as it was) */
    v->fault = 0;
    return 0;
}

/* ---- _box_pitch, _box_settings (int() of a positive float = floor) ----------------------------------------------- */
static int box_pitch(int p)
{
    p = clamp100(p);
    return p <= 50 ? (int)(p * 3 / 50.0 + 0.5) : 3 + (int)((p - 50) * 6 / 50.0 + 0.5);   /* 50 -> 3 */
}

SO_API int sov_pitch_step(int pitch) { return box_pitch(pitch); }

static box_settings settings_of(const so_voice *v)
{
    box_settings s;
    s.rate = (int)(v->rate * 9 / 100.0 + 0.5);                 /* 50 -> 5, the default */
    s.pitch = box_pitch(v->pitch);
    s.tone = v->tone;
    s.word = v->join ? 0 : 1;                                  /* word delay 0 when joining */
    s.sentence = v->short_pauses ? 0 : 1;                      /* sentence delay 0 when shortening */
    return s;
}

/* ---- the text: _translit (translit.h), currencies, _clean, strip --------------------------------------------- */

/* _clean: Latin-1 text with no control characters (^E and ^X are the box's commands) */
static int clean(const unsigned *in, int n, unsigned char *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        const char *r;
        if (c < 32 || c == 127) { out[m++] = ' '; continue; }
        if (c < 256) { out[m++] = (unsigned char)c; continue; }
        switch (c) {
        case 0x2018: case 0x2019: r = "'"; break;
        case 0x201C: case 0x201D: r = "\""; break;
        case 0x2013: case 0x2014: r = "-"; break;
        case 0x2026: r = "..."; break;
        default: r = " "; break;
        }
        while (*r) out[m++] = (unsigned char)*r++;
    }
    return m;
}

/* str.strip() after _clean: the only whitespace left is the space, U+0085 and U+00A0 */
static int strip_space(unsigned char c) { return c == ' ' || c == 0x85 || c == 0xA0; }

/* the text as the box is to read it (translit.h, the currencies, _clean, str.strip()): malloc'd with room for a "\r"
   after it, the length in *len (0: nothing to say) */
static unsigned char *cleaned(const char *utf8, int *len)
{
    size_t n = strlen(utf8);
    unsigned *t = (unsigned *)malloc((n + 1) * sizeof(unsigned)), *u;
    unsigned char *c;
    int k, m, a, b;
    *len = 0;
    if (!t) return NULL;
    k = nw_utf8(utf8, t);
    /* translit.h first: the letters the box's 7-bit alphabet lacks (its firmware drops Latin-1's), as base letters
       (or a lone one's words) */
    u = (unsigned *)malloc((size_t)tl_room(k) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    k = tl_apply(t, k, tl_known_ascii, u);
    free(t);
    t = u;
    u = (unsigned *)malloc(((size_t)k * 16 + 64) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    m = nw_currencies(t, k, u);                                /* "£2.63": the firmware reads only "$" */
    free(t);
    c = (unsigned char *)malloc((size_t)m * 3 + 2);            /* "…" -> "..." */
    if (!c) { free(u); return NULL; }
    m = clean(u, m, c);
    free(u);
    for (a = 0; a < m && strip_space(c[a]); a++) ;
    for (b = m; b > a && strip_space(c[b - 1]); b--) ;
    if (b > a) {
        memmove(c, c + a, (size_t)(b - a));
        *len = b - a;
    }
    return c;
}

/* the bytes box.say() gets for one text item: the text, then "\r"; malloc'd, the length in *len (0: nothing to say) */
static unsigned char *say_text(const char *utf8, int *len)
{
    unsigned char *c = cleaned(utf8, len);
    if (c && *len)
        c[(*len)++] = '\r';
    return c;
}

/* ---- index marks (so_voice.h) -------------------------------------------------------------------------------------- */
/* the tests' controls (nvda/tools/so_marks.py; an app never sets it): 1 reports every mark with the first block; 2
   gives the box a mark for every one of the caller's, also those on the same word; 3 leaves the marks after a
   cancel; 4 puts the box's mark inside its word, after the first letter */
SO_API int sov_marks_break = 0;

/* the words of cleaned text: their starts and ends; their count */
static int words_of(const unsigned char *c, int n, int *ws, int *we)
{
    int nw = 0, i = 0;
    while (i < n) {
        if (strip_space(c[i])) { i++; continue; }
        ws[nw] = i;
        while (i < n && !strip_space(c[i])) i++;
        we[nw++] = i;
    }
    return nw;
}

/* The word of the whole cleaned text that a mark at byte `at` of the source stands before (0 to nw; nw: after the
   last), as bl_voice.c finds it: the source up to the mark -- from the start of the run of text it stands in --
   cleaned by itself, its words held against the whole's; where they stop agreeing is the mark's word. */
static int mark_word(const char *utf8, int at, const unsigned char *c, const int *ws, const int *we, int nw)
{
    int len = (int)strlen(utf8), pn, pnw, j = 0, *pws, *pwe;
    unsigned char *pc;
    char *prefix;
    if (at <= 0) return 0;
    if (at > len) at = len;
    if (at < len)
        while (at > 0 && (unsigned char)utf8[at] > ' ' && (unsigned char)utf8[at - 1] > ' ') at--;
    while (at > 0 && at < len && ((unsigned char)utf8[at] & 0xC0) == 0x80) at--;
    prefix = (char *)malloc((size_t)at + 1);
    if (!prefix) return 0;
    memcpy(prefix, utf8, (size_t)at);
    prefix[at] = 0;
    pc = cleaned(prefix, &pn);
    free(prefix);
    if (!pc) return 0;
    pws = (int *)malloc(sizeof(int) * (size_t)(pn + 1));
    pwe = (int *)malloc(sizeof(int) * (size_t)(pn + 1));
    if (pws && pwe) {
        pnw = words_of(pc, pn, pws, pwe);
        for (j = 0; j < pnw && j < nw; j++) {
            int a = pwe[j] - pws[j];
            if (a != we[j] - ws[j] || memcmp(pc + pws[j], c + ws[j], (size_t)a)) break;
        }
    }
    free(pc); free(pws); free(pwe);
    return j;
}

/* The bytes for a marked text: the cleaned text with the box's index mark, 05h 5Bh xx, at the front of each marked
   word -- after the space before it, where the box takes it for nothing but a word boundary (so_voice.h) -- then
   "\r".  values (n_marks of them): for each of the caller's marks MK_NOW (before the first word), MK_END (after the
   last) or the byte of its word's mark, taken from *next on.  malloc'd, the length in *len (0: nothing to say, every
   value MK_NOW). */
static unsigned char *marked_text(const char *utf8, const ssv_mark *marks, int n_marks, int *values, int *next, int *len)
{
    int n, nw, i, w, m = 0, last = 0;
    unsigned char *c = cleaned(utf8, &n), *out = NULL;
    int *ws = NULL, *we = NULL, *mv = NULL, *extra = NULL;
    *len = 0;
    for (i = 0; i < n_marks; i++) values[i] = MK_NOW;
    if (!c || !n) { free(c); return NULL; }
    ws = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    we = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    mv = (int *)calloc((size_t)(n + 2), sizeof(int));          /* per word: its mark's byte, 0 none */
    extra = (int *)calloc((size_t)(n + 2), sizeof(int));
    out = (unsigned char *)malloc((size_t)n + 3 * (size_t)n_marks + 2);
    if (!ws || !we || !mv || !extra || !out) {
        free(c); free(ws); free(we); free(mv); free(extra); free(out);
        return NULL;
    }
    nw = words_of(c, n, ws, we);
    for (i = 0; i < n_marks; i++) {
        w = mark_word(utf8, marks[i].at, c, ws, we, nw);
        if (w < last) w = last;                                /* in the caller's order, never back */
        last = w;
        if (w <= 0) continue;
        if (w >= nw) { values[i] = MK_END; continue; }
        if (!mv[w]) {
            mv[w] = *next;
            *next = *next >= MARK_LAST ? MARK_FIRST : *next + 1;
        } else if (sov_marks_break == 2)
            extra[w]++;                                        /* the control: a mark more for each on the word */
        values[i] = mv[w];
    }
    for (w = 0, i = 0; i < n; i++) {
        int at_word = w < nw && i == ws[w];
        if (at_word && mv[w] && sov_marks_break != 4) {
            int k;
            for (k = 0; k <= extra[w]; k++) { out[m++] = 0x05; out[m++] = 0x5B; out[m++] = (unsigned char)mv[w]; }
        }
        out[m++] = c[i];
        if (at_word && mv[w] && sov_marks_break == 4) { out[m++] = 0x05; out[m++] = 0x5B; out[m++] = (unsigned char)mv[w]; }
        if (at_word) w++;
    }
    out[m++] = '\r';
    free(c); free(ws); free(we); free(mv); free(extra);
    *len = m;
    return out;
}

SO_API int sov_say_bytes_marked(const char *utf8, const ssv_mark *marks, int n_marks, unsigned char *out, int cap)
{
    int n, next = MARK_FIRST, *values = (int *)malloc(sizeof(int) * (size_t)(n_marks > 0 ? n_marks : 1));
    unsigned char *text;
    if (!values) return 0;
    text = marked_text(utf8, marks, n_marks, values, &next, &n);
    if (text && n <= cap)
        memcpy(out, text, (size_t)n);
    free(text);
    free(values);
    return n;
}

SO_API int sov_marks_exact(const so_voice *v)
{
#ifdef SO_HAVE_SOUNDINGBOARD
    if (v->sb) return 1;                    /* the Sounding Board: its driver's own count of the marks crossed */
#endif
    return v->h != NULL;                    /* the Speak-Out: its replies on the serial line */
}

/* what the box has reported in the block just run: each mark crossed, its byte (-1: not known, the Sounding Board's
   driver keeps only the last of several crossed together) and the sample of the run it came at */
typedef struct { int value, sample; } crossed;
static int crossed_in_run(so_voice *v, crossed *out, int cap)
{
    int n = 0, k;
    if (v->h) {
        const soh_reply *rp;
        int nr = soh_replies(v->h, &rp);
        for (k = 0; k < nr && n < cap; k++, n++) { out[n].value = rp[k].value; out[n].sample = rp[k].sample; }
    }
#ifdef SO_HAVE_SOUNDINGBOARD
    else if (v->sb) {
        const sbh_reply *rp;
        int nr = sbh_replies(v->sb, &rp);
        for (k = 0; k < nr && n < cap; k++, n++) { out[n].value = rp[k].value; out[n].sample = rp[k].sample; }
    }
#endif
    return n;
}

/* The marks speech has reached in the block just run: those the box has reported, each at the sample its report came
   (less the `start` samples trimmed from the block's head); with `all` (the utterance is over) the rest after the
   block's last sample. */
static void report_marks(so_voice *v, int start, int count, int all)
{
    crossed cr[64];
    int nc, i, j, kept = 0, last = 0, upto = 0, *at;
    v->n_reached = 0;
    if (!v->n_mk) return;
    if (v->n_mk > v->cap_reached) {
        ssv_reach *r = (ssv_reach *)realloc(v->reached, sizeof(ssv_reach) * (size_t)v->n_mk);
        if (!r) { v->n_mk = 0; return; }
        v->reached = r;
        v->cap_reached = v->n_mk;
    }
    at = (int *)malloc(sizeof(int) * 2 * (size_t)v->n_mk);     /* per waiting mark: its sample (-1: not yet), exact */
    if (!at) { v->n_mk = 0; return; }
    for (i = 0; i < v->n_mk; i++) { at[2 * i] = -1; at[2 * i + 1] = 0; }
    nc = crossed_in_run(v, cr, (int)(sizeof cr / sizeof cr[0]));
    /* each report is the next waiting mark's: by its byte where the box says it (the marks before that one then count
       as reached with it: a report lost on the way), else the next in order.  The caller's marks on one word share
       the box's mark and are reached together. */
    for (j = 0; j < nc; j++) {
        int k = -1, s = cr[j].sample - start;
        while (upto < v->n_mk && v->mk[upto].value == MK_NOW) upto++;
        if (cr[j].value < 0) {
            if (upto < v->n_mk && v->mk[upto].value != MK_END) k = upto;
        } else
            for (i = upto; i < v->n_mk; i++)
                if (v->mk[i].value == cr[j].value) { k = i; break; }
        if (k < 0) continue;
        for (i = upto; i <= k; i++) { at[2 * i] = s < 0 ? 0 : s; at[2 * i + 1] = i == k; }
        upto = k + 1;
        while (upto < v->n_mk && v->mk[upto].value == v->mk[k].value) {
            at[2 * upto] = at[2 * k];
            at[2 * upto + 1] = 1;
            upto++;
        }
    }
    for (i = 0; i < v->n_mk; i++) {
        int val = v->mk[i].value, s = 0, exact = 0;
        if (sov_marks_break == 1 || val == MK_NOW) { s = 0; exact = val == MK_NOW; }
        else if (at[2 * i] >= 0) { s = at[2 * i]; exact = at[2 * i + 1]; }
        else if (all) { s = count; exact = val == MK_END; }
        else {                                   /* not yet: its report is still to come */
            v->mk[kept++] = v->mk[i];
            continue;
        }
        if (s < last) s = last;
        if (s > count) s = count;
        last = s;
        v->reached[v->n_reached].id = v->mk[i].id;
        v->reached[v->n_reached].sample = s;
        v->reached[v->n_reached].exact = exact;
        v->n_reached++;
    }
    free(at);
    v->n_mk = kept;
}

SO_API int sov_reached(so_voice *v, const ssv_reach **out)
{
    *out = v->reached;
    return v->n_reached;
}

static void say_str(so_voice *v, const char *s);

SO_API int sov_expect(so_voice *v, const ssv_mark *marks, int n_marks)
{
    int i;
    if (!sov_marks_exact(v) || !marks || n_marks <= 0)
        return 0;
    if (v->n_mk + n_marks > v->cap_mk) {
        void *q = realloc(v->mk, sizeof *v->mk * (size_t)(v->n_mk + n_marks));
        if (!q) return 0;
        v->mk = q;
        v->cap_mk = v->n_mk + n_marks;
    }
    for (i = 0; i < n_marks; i++) {
        v->mk[v->n_mk].id = marks[i].id;
        v->mk[v->n_mk++].value = marks[i].at;
    }
    if (!v->index_on) {                     /* (as sov_speak_marked: the box told to send its replies) */
        if (v->h) say_str(v, "\x05]1");
        v->index_on = 1;
    }
    return 1;
}

SO_API int sov_say_bytes(const char *utf8, unsigned char *out, int cap)
{
    int n;
    unsigned char *s = say_text(utf8, &n);
    if (!s) return -1;
    if (out && n <= cap) memcpy(out, s, (size_t)n);
    free(s);
    return n;
}

/* ---- _speakJob / _speakItems ----------------------------------------------------------------------------------- */
static void say_str(so_voice *v, const char *s)
{
    box_say(box_of(v), (const unsigned char *)s, (int)strlen(s));
}

static void say_pitch(so_voice *v, int p)                      /* box.chip.snap_pitch = True; box.say("\x05P%d") */
{
    char cmd[16];
    ssi263_set_snap_pitch(v->chip, 1);
    snprintf(cmd, sizeof cmd, "\x05P%d", p);
    say_str(v, cmd);
}

/* _speakJob's finally: the user's pitch again, only after the capital's audio exists */
static void restore_pitch(so_voice *v)
{
    if (v->cur_pitch != v->base_pitch) {
        say_pitch(v, v->base_pitch);
        v->cur_pitch = v->base_pitch;
        v->pitch_dirty = 1;
    }
}

/* a text's end: in a job the pitch stays as the sequence left it until sov_end (the driver's finally) */
static void finish(so_voice *v)
{
    v->active = 0;
    if (!v->job)
        restore_pitch(v);
}

/* the items: PitchCommand(offset) -- how NVDA marks a capital: an offset on the user's own 0-100 pitch */
static void pitch_item(so_voice *v, int pitch_offset)
{
    int want = box_pitch(v->pitch + pitch_offset);
    if (want != v->cur_pitch) {
        say_pitch(v, want);
        v->cur_pitch = want;
        v->pitch_dirty = 1;
    }
}

/* _speakJob's start: a faulted box rebooted, the settings sent when they changed, the lead trim armed */
static void begin(so_voice *v)
{
    box_settings s;
    if (v->active)
        sov_cancel(v);
    v->n_mk = v->n_reached = 0;
    if (v->fault) {                         /* the driver's "speech failed; rebooting the emulated box" */
        char err[128];
        box b = boot(v, v->out_rate, err, (int)sizeof err);
        if (booted(b)) {
            take_box(v, b);
            v->sent_valid = 0;
            v->fault = 0;
        }
    }
    s = settings_of(v);
    if (!v->sent_valid || memcmp(&s, &v->sent, sizeof s)) {
        char cmd[64];
        snprintf(cmd, sizeof cmd, "\x05R%d\x05P%d\x05T%c\x05W%d\x05I%d", s.rate, s.pitch, TONES[s.tone], s.word,
                 s.sentence);
        say_str(v, cmd);
        v->sent = s;
        v->sent_valid = 1;
        v->settings_unread = 1;
    }
    v->base_pitch = v->cur_pitch = s.pitch;
    v->gain = v->volume / 100.0;
    v->lead = 1;                            /* nothing audible fed yet in this utterance */
}

/* a text item: t_start = box.chip.time; box.say(text + "\r") */
static void text_item(so_voice *v, const unsigned char *bytes, int n)
{
    v->t_start = ssi263_time(v->chip);
    box_say(box_of(v), bytes, n);
    v->active = 1;
}

SO_API int sov_speak(so_voice *v, const char *utf8, int pitch_offset)
{
    unsigned char *text;
    int n;
    v->job = 0;
    begin(v);
    if (pitch_offset)
        pitch_item(v, pitch_offset);
    text = say_text(utf8, &n);
    if (!text || !n) {
        free(text);
        restore_pitch(v);
        return 0;
    }
    text_item(v, text, n);
    free(text);
    return 1;
}

SO_API int sov_speak_marked(so_voice *v, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    unsigned char *text;
    int n, i, *values;
    if (!marks || n_marks <= 0 || !sov_marks_exact(v))       /* (a caller asks sov_marks_exact first) */
        return sov_speak(v, utf8, pitch_offset);
    v->job = 0;
    begin(v);
    values = (int *)malloc(sizeof(int) * (size_t)n_marks);
    if (n_marks > v->cap_mk) {
        void *q = realloc(v->mk, sizeof *v->mk * (size_t)n_marks);
        if (q) { v->mk = q; v->cap_mk = n_marks; }
    }
    if (!values || n_marks > v->cap_mk) {                    /* out of memory: spoken without marks */
        free(values);
        n_marks = 0;
        values = NULL;
    }
    if (!v->index_on) {                                      /* the box told to send its replies: on at power-up as
                                                                measured, said once so as not to depend on that */
        if (v->h) say_str(v, "\x05]1");                      /* (the Sounding Board's driver needs nothing sent) */
        v->index_on = 1;
    }
    if (!v->next_value) v->next_value = MARK_FIRST;
    if (pitch_offset)
        pitch_item(v, pitch_offset);
    text = n_marks ? marked_text(utf8, marks, n_marks, values, &v->next_value, &n) : say_text(utf8, &n);
    for (i = 0; i < n_marks; i++) {
        v->mk[i].id = marks[i].id;
        v->mk[i].value = values[i];
    }
    v->n_mk = n_marks;
    free(values);
    if (!text || !n) {                                       /* nothing to say: its marks with the next render */
        free(text);
        restore_pitch(v);
        return 0;
    }
    text_item(v, text, n);
    free(text);
    return 1;
}

/* ---- a job: the NVDA driver's whole speech sequence (so_voice.h) ------------------------------------------------ */
SO_API void sov_begin(so_voice *v)
{
    v->job = 0;
    begin(v);
    v->job = 1;
}

SO_API void sov_pitch(so_voice *v, int pitch_offset)
{
    pitch_item(v, pitch_offset);
}

SO_API int sov_text(so_voice *v, const unsigned char *bytes, int n)
{
    if (n <= 0)
        return 0;
    text_item(v, bytes, n);
    return 1;
}

SO_API void sov_end(so_voice *v)
{
    v->active = 0;
    v->job = 0;
    restore_pitch(v);                       /* _speakJob's finally */
}

SO_API void sov_flush(so_voice *v)
{
    box_cancel(box_of(v), CANCEL_LIMIT);         /* _run: box.cancel() */
    /* #35: settings sent at this job's start that the box never ran a block to read went with the flush; sent
       again with the next job, or the box keeps the old ones while the record says the new (SSV_SETTINGS_BREAK:
       the tests' control, the record kept) */
    if (v->settings_unread && !getenv("SSV_SETTINGS_BREAK"))
        v->sent_valid = 0;
    v->settings_unread = 0;
    if (v->pitch_dirty && v->sent_valid) {  /* _resend_pitch (none after a reboot) */
        char cmd[16];
        ssi263_set_snap_pitch(v->chip, 1);
        v->snap_until_speech = 1;
        snprintf(cmd, sizeof cmd, "\x05P%d", v->sent.pitch);
        say_str(v, cmd);
        v->cur_pitch = v->sent.pitch;
    }
}

/* one pass of _speakItems' loop: box.run(BLOCK_S), _trim_lead, pcm16, the snap check, busy() */
SO_API int sov_render(so_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0, count;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active) {                                          /* nothing to say: its marks are reached at once */
        report_marks(v, 0, 0, 1);
        *done = 1;
        return 0;
    }
    n = box_run(box_of(v), BLOCK_S, STEP_S, &y);
    v->settings_unread = 0;                                    /* a block run: the box has read them (#35) */
    if (n < 0) {                    /* the host has faulted (the Sounding Board's does so): rebooted at the next begin */
        report_marks(v, 0, 0, 1);
        finish(v);
        v->fault = !getenv("SO_VOICE_FAULT_BREAK");     /* the tests' control: the fault unsaid, as it was */
        *done = 1;
        return 0;
    }
    if (v->lead) {                                             /* _trim_lead: the box's reading time is only delay */
        int k;
        start = n;
        for (k = 0; k < n; k++)
            if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) {
                start = k > LEAD_PREROLL ? k - LEAD_PREROLL : 0;
                v->lead = 0;
                break;
            }
    }
    count = n - start;
    if (count > v->pcm_cap) {
        short *q = (short *)realloc(v->pcm, (size_t)count * sizeof(short));
        if (!q) { report_marks(v, 0, 0, 1); finish(v); *done = 1; return 0; }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    if (v->snap_until_speech && box_last_speech(box_of(v)) >= v->t_start) {
        /* the first phoneme is set up; a leftover snap would flatten a glide later */
        ssi263_set_snap_pitch(v->chip, 0);
        v->snap_until_speech = 0;
    }
    if (!box_busy(box_of(v), SOH_QUIET, SOH_PATIENCE)) {
        v->pitch_dirty = 0;                                    /* the box has read everything sent so far */
        finish(v);
        *done = 1;
    } else if (box_fault(box_of(v))) {                              /* never in the driver: a faulted V40 stays busy */
        v->fault = 1;
        v->active = 0;
        *done = 1;
    }
    report_marks(v, start, count > 0 ? count : 0, *done);
    return count > 0 ? count : 0;
}

/* cancel(), then _run's flush: box.cancel(), and _resend_pitch when a pitch command may have been dropped.  With no
   utterance in progress the driver's worker is idle and a cancel leaves the box alone: so does this. */
SO_API void sov_cancel(so_voice *v)
{
    if (sov_marks_break != 3)
        v->n_mk = v->n_reached = 0;                            /* no mark of a cancelled text is reported */
    if (!v->active)
        return;
    sov_end(v);                                                /* _speakJob's finally */
    sov_flush(v);
}
