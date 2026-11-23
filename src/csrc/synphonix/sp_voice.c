/* sp_voice.c -- see sp_voice.h.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sp_voice.h"
#include "../accent_text.h"                /* at_say_text: the shared 7-bit text path (translit, currencies, clean-up) */
#include "../numwords.h"                   /* nw_normalise: the number words */

#define BLOCK_S 0.03
#define STEP_S 0.0005
#define QUIET 0.05                          /* synphonix.py's busy() */
#define STALL_S 4.0                         /* busy and silent this long: the card has stalled */
#define LEAD_THRESHOLD 0.003                /* the silence before the first phoneme left out (am_voice.c's) */
#define LEAD_PREROLL 220
#define DEF_RATE 2                          /* VEST's own rate offset: its start-up announcement's (SPH_SAY_CX) */
#define PITCH_STEPS 6                       /* the pitch offset's steps each way from VEST's own */
#define SPV_AMP 6                           /* VEST's amplitude offset: CL 6 and above are one level, its loudest
                                               (-21.4 dB against -25.4 at 0 on a sentence, no clipping) */
#define FILTER 0xE6                         /* VEST's own filter offset (SPH_SAY_DX) */
#define FLAT 0x40                           /* AH bit 6: flat inflection */

struct sp_voice {
    sp_host *h;
    unsigned char *com;                                     /* VEST.COM, kept to boot the card again after a fault */
    size_t n_com;
    double out_rate, gain;
    int rate, pitch, volume, inflection, numbers;
    int ah, cx, dx;                                         /* the utterance's AL=7 registers */
    char **parts;
    int n_parts, next, active, lead, fault;
    double t_start;
    short *pcm;
    int pcm_cap;
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* NVDA's rate as VEST's rate offset: 50 is its own (2), 0-50 onto 0-2 and 50-100 onto 2-5 */
SP_API int spv_rate_offset(int value)
{
    value = clamp(value, 0, 100);
    return value <= 50 ? (int)(value * DEF_RATE / 50.0 + 0.5)
                       : DEF_RATE + (int)((value - 50) * (5 - DEF_RATE) / 50.0 + 0.5);
}

/* NVDA's pitch as VEST's pitch offset byte: 50 is its own (0); steps of 8 on R1 up (DH 1-6) and down (DH 3Fh-3Ah:
   VEST takes DH from 20h on as a signed step, measured) */
SP_API int spv_pitch_offset(int value)
{
    int step;
    value = clamp(value, 0, 100);
    step = value >= 50 ? (int)((value - 50) * PITCH_STEPS / 50.0 + 0.5)
                       : -(int)((50 - value) * PITCH_STEPS / 50.0 + 0.5);
    if (step < 0 && getenv("SPV_PITCH_BREAK"))        /* the tests' control: 20h + the step taken as "down", as the
                                                         offset's description reads -- measured, it goes up */
        return 0x20 - step;
    return step >= 0 ? step : 0x40 + step;
}

/* ---- the parts (st_voice.c's) ---------------------------------------------------------------------------------------- */

static void free_parts(sp_voice *v)
{
    int i;
    for (i = 0; i < v->n_parts; i++)
        free(v->parts[i]);
    free(v->parts);
    v->parts = NULL;
    v->n_parts = v->next = 0;
}

typedef struct { char **p; int n, cap; } list;

static int push(list *l, const char *s, int len)
{
    char *c;
    while (len > 0 && s[0] == ' ') { s++; len--; }
    while (len > 0 && s[len - 1] == ' ') len--;
    if (len <= 0)
        return 1;
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        char **q = (char **)realloc(l->p, (size_t)cap * sizeof *q);
        if (!q) return 0;
        l->p = q;
        l->cap = cap;
    }
    c = (char *)malloc((size_t)len + 1);
    if (!c) return 0;
    memcpy(c, s, (size_t)len);
    c[len] = 0;
    l->p[l->n++] = c;
    return 1;
}

/* where to cut s (n > max) at most max characters in: after the last of `marks` followed by a space, else -1 */
static int cut_after(const char *s, int n, int max, const char *marks)
{
    int i;
    for (i = (max < n ? max : n) - 1; i > 0; i--)
        if (strchr(marks, s[i]) && (i + 1 >= n || s[i + 1] == ' '))
            return i + 1;
    return -1;
}

/* s into parts of at most `max` characters: sentence ends, then clause marks, then a space, then anywhere */
static int split(list *l, const char *s, int n, int max)
{
    while (n > max) {
        int k = cut_after(s, n, max, ".!?");
        if (k < max / 3)
            k = cut_after(s, n, max, ",;:");
        if (k < max / 3) {
            int i;
            k = -1;
            for (i = max - 1; i > 0; i--)
                if (s[i] == ' ') { k = i; break; }
        }
        if (k <= 0)
            k = max;
        if (!push(l, s, k))
            return 0;
        s += k;
        n -= k;
    }
    return push(l, s, n);
}

/* ---- dollars and percent (st_voice.c's): VEST reads "$" before the number and every digit one by one, so an amount
   is made words first, as the SmarTalk's ----------------------------------------------------------------------------- */

static int digit(char c) { return c >= '0' && c <= '9'; }
static int letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int alnum(char c) { return digit(c) || letter(c); }

static int put(char *out, int m, const char *s, int a, int b)
{
    memcpy(out + m, s + a, (size_t)(b - a));
    return m + b - a;
}

static int put_str(char *out, int m, const char *s) { return put(out, m, s, 0, (int)strlen(s)); }

/* "$395.00" -> "395 dollars", "$2.63" -> "2 dollars 63 cents", "$.5" -> "50 cents", "$2.635" -> "2.635 dollars",
   "$.00" -> "0 dollars"; "10%" and "10 %" -> "10 percent".  A malloc'd string, NULL when out of memory. */
static char *dollars_percent(const char *s)
{
    int n = (int)strlen(s), i = 0, m = 0;
    char *out = (char *)malloc((size_t)n * 8 + 64);
    if (!out)
        return NULL;
    while (i < n) {
        if (s[i] == '$') {
            int p = i + 1, w0, w1, f0 = -1, f1 = -1;
            if (p < n && s[p] == ' ')
                p++;
            w0 = p;
            if (p < n && digit(s[p]))
                while (p < n && (digit(s[p]) || (s[p] == ',' && p + 1 < n && digit(s[p + 1]))))
                    p++;
            w1 = p;
            if (p + 1 < n && s[p] == '.' && digit(s[p + 1])) {
                f0 = ++p;
                while (p < n && digit(s[p]))
                    p++;
                f1 = p;
            }
            if (w1 > w0 || f0 >= 0) {
                int whole_zero = 1, k, cents = 0;
                for (k = w0; k < w1; k++)
                    if (digit(s[k]) && s[k] != '0')
                        whole_zero = 0;
                if (m > 0 && alnum(out[m - 1]))
                    out[m++] = ' ';
                if (f0 >= 0 && f1 - f0 > 2) {                 /* read as a number: "2.635 dollars" */
                    m = w1 > w0 ? put(out, m, s, w0, w1) : put_str(out, m, "0");
                    out[m++] = '.';
                    m = put(out, m, s, f0, f1);
                    m = put_str(out, m, " dollars");
                } else {
                    if (f0 >= 0)
                        cents = (s[f0] - '0') * 10 + (f1 - f0 > 1 ? s[f0 + 1] - '0' : 0);
                    if (w1 > w0 && (!whole_zero || !cents)) {
                        m = put(out, m, s, w0, w1);
                        m = put_str(out, m, w1 - w0 == 1 && s[w0] == '1' ? " dollar" : " dollars");
                    } else if (w1 == w0 && !cents) {
                        m = put_str(out, m, "0 dollars");
                    }
                    if (cents) {
                        char c[24];
                        if (w1 > w0 && (!whole_zero || !cents))
                            out[m++] = ' ';
                        snprintf(c, sizeof c, "%d %s", cents, cents == 1 ? "cent" : "cents");
                        m = put_str(out, m, c);
                    }
                }
                i = p;
                continue;
            }
        } else if (s[i] == '%' && i > 0 && (digit(s[i - 1]) || (s[i - 1] == ' ' && i > 1 && digit(s[i - 2])))) {
            if (m > 0 && out[m - 1] != ' ')
                out[m++] = ' ';
            m = put_str(out, m, "percent");
            i++;
            continue;
        }
        out[m++] = s[i++];
    }
    out[m] = 0;
    return out;
}

/* ---- VEST's two misreadings (sp_voice.h), measured on the host 2026-11-23 -------------------------------------------
   An apostrophe between two letters stays (VEST's contractions are right, and wrong without it) unless it makes a
   possessive after a C at the word's end ("Artic's" -> "Artics"); any other apostrophe goes.  SPV_TEXT_BREAK=1 in the
   environment, the tests' control: the text as it is.  A malloc'd string, NULL when out of memory. */
static char *apostrophes(const char *s)
{
    int n = (int)strlen(s), i, m = 0;
    char *out = (char *)malloc((size_t)n + 1);
    if (!out)
        return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '\'' && !getenv("SPV_TEXT_BREAK")) {
            int inner = i > 0 && letter(s[i - 1]) && i + 1 < n && letter(s[i + 1]);
            int hard_c = inner && (s[i - 1] == 'c' || s[i - 1] == 'C') && (s[i + 1] == 's' || s[i + 1] == 'S')
                         && (i + 2 >= n || !letter(s[i + 2]));
            if (!inner) {                                  /* a quote mark: VEST would say "apostrophe" */
                if (m > 0 && out[m - 1] != ' ' && i + 1 < n && s[i + 1] != ' ')
                    out[m++] = ' ';
                continue;
            }
            if (hard_c)                                    /* "Artic's": VEST's soft C would say "Articus" */
                continue;
        }
        out[m++] = s[i];
    }
    out[m] = 0;
    return out;
}

/* VEST's in-text pitch steps, a word that is exactly ".+" or ".-", cannot come from the text: the point goes.  In
   place. */
static void no_pitch_steps(char *s)
{
    int n = (int)strlen(s), i;
    for (i = 0; i + 1 < n; i++)
        if (s[i] == '.' && (s[i + 1] == '+' || s[i + 1] == '-') && (i == 0 || s[i - 1] == ' ')
                && (i + 2 >= n || s[i + 2] == ' '))
            s[i] = ' ';
}

/* the text VEST is given for utf8: the shared path (no number words), the apostrophes, dollars and percent, then the
   number words; no pitch step */
static char *voice_text(const char *utf8, int numbers)
{
    char *a = at_say_text(utf8 ? utf8 : "", 0), *b, *c;
    if (!a)
        return NULL;
    c = apostrophes(a);
    free(a);
    if (!(a = c))
        return NULL;
    b = dollars_percent(a);
    free(a);
    if (!b)
        return NULL;
    if (numbers) {
        c = nw_normalise(b);
        free(b);
        if (!(b = c))
            return NULL;
    }
    no_pitch_steps(b);
    return b;
}

/* the voice's text for utf8, as parts (a malloc'd array of malloc'd strings in *out); the count, -1 out of memory */
static int make_parts(const char *utf8, int numbers, char ***out)
{
    list l = {NULL, 0, 0};
    char *t = voice_text(utf8, numbers);
    if (!t)
        return -1;
    if (!split(&l, t, (int)strlen(t), SPV_PART)) {
        int i;
        for (i = 0; i < l.n; i++) free(l.p[i]);
        free(l.p);
        free(t);
        return -1;
    }
    free(t);
    *out = l.p;
    return l.n;
}

SP_API int spv_parts(const char *utf8, int numbers, char *out, int cap)
{
    char **p = NULL;
    int n = make_parts(utf8, numbers, &p), i, len = 0;
    if (n < 0)
        return -1;
    for (i = 0; i < n; i++)
        len += (int)strlen(p[i]) + (i ? 1 : 0);
    if (out && len < cap) {
        out[0] = 0;
        for (i = 0; i < n; i++) {
            if (i) strcat(out, "|");
            strcat(out, p[i]);
        }
    }
    for (i = 0; i < n; i++) free(p[i]);
    free(p);
    return len;
}

/* ---- the voice ------------------------------------------------------------------------------------------------------ */

/* a card booted: VEST.COM loaded, its announcement played out silently; NULL with the reason in err */
static sp_host *boot(const unsigned char *com, size_t n, double out_rate, char *err, int errlen)
{
    sp_host *h = sph_create(com, n, NULL, out_rate, err, errlen);
    if (!h)
        return NULL;
    sph_boot(h, SPH_BOOT_LIMIT);
    if (sph_fault(h)) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "VEST.COM failed at boot: %s", sph_error(h));
        sph_destroy(h);
        return NULL;
    }
    return h;
}

SP_API sp_voice *spv_create(const unsigned char *com, size_t n, double out_rate, char *err, int errlen)
{
    sp_voice *v = (sp_voice *)calloc(1, sizeof *v);
    if (!v || !(v->com = (unsigned char *)malloc(n ? n : 1))) {
        free(v);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(v->com, com, n);
    v->n_com = n;
    v->h = boot(com, n, out_rate, err, errlen);
    if (!v->h) {
        free(v->com);
        free(v);
        return NULL;
    }
    v->out_rate = out_rate;
    spv_set(v, 50, 50, 100, 1, 1);
    return v;
}

/* after a fault: a new card from the kept VEST.COM (st_voice.c's restart); 0 when that failed too */
static int restart(sp_voice *v)
{
    sp_host *h = boot(v->com, v->n_com, v->out_rate, NULL, 0);
    if (!h)
        return 0;
    sph_destroy(v->h);
    v->h = h;
    v->fault = 0;
    return 1;
}

SP_API void spv_destroy(sp_voice *v)
{
    if (!v)
        return;
    free_parts(v);
    sph_destroy(v->h);
    free(v->com);
    free(v->pcm);
    free(v);
}

SP_API sp_host *spv_host(sp_voice *v) { return v->h; }

/* rate and pitch clamped to 0-100, volume to 0-200 (above 100 louder, ssi_pcm16 clipping at full scale) */
SP_API void spv_set(sp_voice *v, int rate, int pitch, int volume, int inflection, int numbers)
{
    v->rate = clamp(rate, 0, 100);
    v->pitch = clamp(pitch, 0, 100);
    v->volume = clamp(volume, 0, 200);
    v->inflection = inflection != 0;
    v->numbers = numbers != 0;
}

/* the next part to VEST, one AL=7 call with the utterance's offsets; 0 when none is left */
static int say_next(sp_voice *v)
{
    const char *s;
    if (v->next >= v->n_parts)
        return 0;
    s = v->parts[v->next++];
    sph_say_ah(v->h, (const unsigned char *)s, (int)strlen(s), v->ah, v->cx, v->dx);
    if (sph_fault(v->h))
        v->fault = 1;
    return 1;
}

SP_API int spv_speak(sp_voice *v, const char *utf8, int pitch_offset)
{
    char **p = NULL;
    int n;
    if (spv_fault(v) && !restart(v))
        return 0;
    if (v->active || sph_busy(v->h, QUIET))
        spv_cancel(v);
    free_parts(v);
    n = make_parts(utf8, v->numbers, &p);
    if (n <= 0) {
        free(p);
        return 0;
    }
    v->parts = p;
    v->n_parts = n;
    v->gain = v->volume / 100.0;
    v->ah = v->inflection ? 0 : FLAT;
    v->cx = spv_rate_offset(v->rate) << 8 | SPV_AMP;
    v->dx = spv_pitch_offset(v->pitch + pitch_offset) << 8 | FILTER;
    v->t_start = sph_get_double(v->h, "time");
    v->active = 1;
    v->lead = 1;
    say_next(v);
    /* SPV_QUEUE_BREAK, the tests' control: every part at once -- past VEST's queue (about 20 s) its call waits, the
       chip running, and the first block holds all that audio (a long silence before speech, no cancel meanwhile) */
    while (getenv("SPV_QUEUE_BREAK") && !v->fault && say_next(v))
        ;
    if (v->fault) {
        v->active = 0;
        return 0;
    }
    return 1;
}

SP_API int spv_render(sp_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0, count;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active || v->fault) {
        v->active = 0;
        *done = 1;
        return 0;
    }
    if (v->next < v->n_parts && sph_queued_frames(v->h) < SPV_LOW)     /* the next part while VEST still talks */
        say_next(v);
    n = sph_run(v->h, BLOCK_S, STEP_S, &y);
    if (n < 0 || v->fault) {
        v->fault = 1;
        v->active = 0;
        *done = 1;
        return 0;
    }
    if (v->lead) {                                    /* the silence before the first phoneme (am_voice.c) */
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
        if (!q) {
            v->fault = 1;
            v->active = 0;
            *done = 1;
            return 0;
        }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    if (v->next >= v->n_parts && !sph_busy(v->h, QUIET)) {
        v->active = 0;                                /* the parts stay until the next speak: spv_get_int counts them */
        *done = 1;
    } else {
        double last = sph_get_double(v->h, "last_speech");
        double now = sph_get_double(v->h, "time");
        if (now - (last > v->t_start ? last : v->t_start) > STALL_S) {
            v->fault = 1;                             /* busy and silent: the card has stalled */
            v->active = 0;
            *done = 1;
        }
    }
    return count > 0 ? count : 0;
}

SP_API void spv_cancel(sp_voice *v)
{
    free_parts(v);
    v->active = 0;
    if (!v->fault) {
        sph_cancel(v->h);
        if (sph_fault(v->h))
            v->fault = 1;
    }
}

SP_API int spv_fault(const sp_voice *v) { return v->fault || sph_fault(v->h); }

SP_API int spv_get_int(const sp_voice *v, const char *name)
{
    if (!strcmp(name, "parts")) return v->n_parts;
    if (!strcmp(name, "sent")) return v->next;
    return -1;
}
