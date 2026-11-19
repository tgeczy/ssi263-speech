/* sv_voice.c -- see sv_voice.h.  MIT.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_voice.h"
#include "../accent_text.h"                /* at_say_text: the shared 7-bit text path (translit, currencies, numbers) */

#define BLOCK_S 0.03
#define RATE_DEFAULT 11                    /* the program's own: its opening writes R2 = A8h */
#define PITCH_DEFAULT 14                   /* "^IT14,3" (the manual, p.22) */
#define PITCH_SPEED 3

struct sv_voice {
    sv_host *h;
    double out_rate, gain;
    int rate, pitch, volume, numbers;
    int offset;                             /* this utterance's capital pitch offset */
    char **parts;
    int n_parts, next, active;
    short *pcm;
    int pcm_cap;
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

SV_API int svv_rate_step(int rate)
{
    rate = clamp(rate, 0, 100);
    return rate <= 50 ? 1 + (int)(rate * 10 / 50.0 + 0.5) : RATE_DEFAULT + (int)((rate - 50) * 5 / 50.0 + 0.5);
}

SV_API int svv_pitch_step(int pitch)
{
    pitch = clamp(pitch, 0, 100);
    return pitch <= 50 ? 1 + (int)(pitch * 13 / 50.0 + 0.5) : PITCH_DEFAULT + (int)((pitch - 50) * 18 / 50.0 + 0.5);
}

/* ---- the text ----------------------------------------------------------------------------------------------------- */

/* The shared text path, then this program's rules (sv_voice.h): its control character and its "quote" taken out,
   a hyphen not in front of a digit made a space, a space put before a period that follows a digit and is not a
   decimal point.  malloc'd; NULL when out of memory. */
static char *prepare(const char *utf8, int numbers)
{
    char *t = at_say_text(utf8 ? utf8 : "", numbers), *o;
    size_t n, i, m = 0;
    if (!t)
        return NULL;
    n = strlen(t);
    o = (char *)malloc(2 * n + 1);
    if (!o) {
        free(t);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        char c = t[i];
        if (c == '^' || c == '"')
            c = ' ';
        else if (c == '-' && !isdigit((unsigned char)t[i + 1]))
            c = ' ';
        else if (c == '.' && i > 0 && isdigit((unsigned char)t[i - 1]) && !isdigit((unsigned char)t[i + 1])
                 && m > 0 && o[m - 1] != ' ')
            o[m++] = ' ';
        if (c == ' ' && (m == 0 || o[m - 1] == ' '))
            continue;
        o[m++] = c;
    }
    while (m > 0 && o[m - 1] == ' ')
        m--;
    o[m] = 0;
    free(t);
    return o;
}

static void free_parts(sv_voice *v)
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
    for (;;) {
        int k;
        while (n > 0 && s[0] == ' ') { s++; n--; }       /* a part is measured from its first character */
        if (n <= max)
            break;
        k = cut_after(s, n, max, ".!?");
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

/* the voice's text for utf8, as parts (a malloc'd array of malloc'd strings in *out); the count, -1 out of memory */
static int make_parts(const char *utf8, int numbers, char ***out)
{
    list l = {NULL, 0, 0};
    char *t = prepare(utf8, numbers);
    if (!t)
        return -1;
    if (!split(&l, t, (int)strlen(t), SVV_PART)) {
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

SV_API int svv_parts(const char *utf8, int numbers, char *out, int cap)
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

/* ---- the voice ---------------------------------------------------------------------------------------------------- */

SV_API sv_voice *svv_create(const unsigned char *image, size_t n, double out_rate, char *err, int errlen)
{
    sv_voice *v = (sv_voice *)calloc(1, sizeof *v);
    if (!v) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    v->h = svh_create(image, n, NULL, out_rate, err, errlen);
    if (!v->h) {
        free(v);
        return NULL;
    }
    v->out_rate = out_rate;
    svv_set(v, 50, 50, 100, 0);
    return v;
}

SV_API sv_voice *svv_create_dir(const char *dir, double out_rate, char *err, int errlen)
{
    char path[4096];
    FILE *f;
    long len;
    unsigned char *d = NULL;
    sv_voice *v = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, SV_FILE);
    f = fopen(path, "rb");
    if (f && fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (d = (unsigned char *)malloc((size_t)len)) != NULL && fread(d, 1, (size_t)len, f) == (size_t)len)
        v = svv_create(d, (size_t)len, out_rate, err, errlen);
    else if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s: cannot read", SV_FILE);
    if (f)
        fclose(f);
    free(d);
    return v;
}

SV_API void svv_destroy(sv_voice *v)
{
    if (!v)
        return;
    free_parts(v);
    svh_destroy(v->h);
    free(v->pcm);
    free(v);
}

SV_API sv_host *svv_host(sv_voice *v) { return v->h; }

SV_API void svv_set(sv_voice *v, int rate, int pitch, int volume, int numbers)
{
    v->rate = rate;
    v->pitch = pitch;
    v->volume = volume < 0 ? 0 : volume;
    v->numbers = numbers != 0;
}

SV_API int svv_speak(sv_voice *v, const char *utf8, int pitch_offset)
{
    char **p = NULL;
    int n;
    free_parts(v);
    v->active = 0;
    svh_cancel(v->h);                       /* a text still being said is cut: the host rests the chip before the next */
    n = make_parts(utf8, v->numbers, &p);
    if (n <= 0) {
        free(p);
        return 0;
    }
    v->parts = p;
    v->n_parts = n;
    v->offset = pitch_offset;
    v->gain = v->volume / 100.0;
    v->active = 1;
    return 1;
}

/* the next part to the program, its settings' control codes in front (none at the program's own defaults); 0 when
   none is left */
static int say_next(sv_voice *v)
{
    int rate = svv_rate_step(v->rate);
    int pitch = clamp(svv_pitch_step(v->pitch)
                      + (v->offset ? (int)(v->offset * 18 / 50.0 + (v->offset > 0 ? 0.5 : -0.5)) : 0), 1, 32);
    while (v->next < v->n_parts) {
        char text[SVH_MAX_TEXT + 1];
        int len = 0;
        if (rate != RATE_DEFAULT)
            len += snprintf(text + len, sizeof text - (size_t)len, "^S%d ", rate);
        if (pitch != PITCH_DEFAULT)
            len += snprintf(text + len, sizeof text - (size_t)len, "^IT%d,%d ", pitch, PITCH_SPEED);
        len += snprintf(text + len, sizeof text - (size_t)len, "%s", v->parts[v->next]);
        v->next++;
        if (svh_say(v->h, (const unsigned char *)text, len))
            return 1;                       /* (a part the host refuses is skipped: none is expected) */
    }
    return 0;
}

SV_API int svv_render(sv_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active) {
        *done = 1;
        return 0;
    }
    if (!svh_busy(v->h) && !say_next(v)) {
        v->active = 0;                      /* the parts stay until the next speak: svv_get_int counts them */
        *done = 1;
        return 0;
    }
    n = svh_run(v->h, BLOCK_S, &y);
    if (n < 0) {
        svv_cancel(v);
        *done = 1;
        return 0;
    }
    if (n > v->pcm_cap) {
        short *q = (short *)realloc(v->pcm, (size_t)n * sizeof(short));
        if (!q) {
            svv_cancel(v);
            *done = 1;
            return 0;
        }
        v->pcm = q;
        v->pcm_cap = n;
    }
    *pcm = v->pcm;
    if (n > 0)
        ssi_pcm16(y, n, v->gain, v->pcm);
    return n;
}

SV_API void svv_cancel(sv_voice *v)
{
    free_parts(v);
    v->active = 0;
    svh_cancel(v->h);
}

SV_API int svv_get_int(const sv_voice *v, const char *name)
{
    if (!strcmp(name, "parts")) return v->n_parts;
    return -1;
}
