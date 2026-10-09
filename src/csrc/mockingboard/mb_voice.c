/* mb_voice.c -- see mb_voice.h.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_voice.h"
#include "../accent_text.h"                /* at_say_text: the shared 7-bit text path (translit, currencies, numbers) */

#define BLOCK_S 0.03
#define STEP_S 0.0005
#define WAIT_LIMIT 3.0                      /* chip seconds a cancelled part may take to stop before the next text */
#define AMPLITUDE 11                        /* the toolkit demo's */
#define FILTER 230

struct mb_voice {
    mb_host *h;
    double out_rate, gain;
    int rate, pitch, volume, numbers;
    int offset;                             /* this utterance's capital pitch offset */
    char **parts;
    int n_parts, next, active, n_split;
    short *pcm;
    int pcm_cap;
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

MB_API int mbv_rate_step(int rate)
{
    rate = clamp(rate, 0, 100);
    return rate <= 50 ? (int)(rate * 8 / 50.0 + 0.5) : 8 + (int)((rate - 50) * 5 / 50.0 + 0.5);
}

MB_API int mbv_pitch_step(int pitch)
{
    pitch = clamp(pitch, 0, 100);
    return pitch <= 50 ? (int)(pitch * 8 / 50.0 + 0.5) : 8 + (int)((pitch - 50) * 18 / 50.0 + 0.5);
}

/* ---- the parts ---------------------------------------------------------------------------------------------------- */

static void free_parts(mb_voice *v)
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

/* the voice's text for utf8, as parts (a malloc'd array of malloc'd strings in *out); the count, -1 out of memory */
static int make_parts(const char *utf8, int numbers, char ***out)
{
    list l = {NULL, 0, 0};
    char *t = at_say_text(utf8 ? utf8 : "", numbers);
    int n;
    if (!t)
        return -1;
    n = (int)strlen(t);
    if (!split(&l, t, n, MBV_PART)) {
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

MB_API int mbv_parts(const char *utf8, int numbers, char *out, int cap)
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

MB_API mb_voice *mbv_create(const unsigned char *image, size_t n, double out_rate, char *err, int errlen)
{
    mb_voice *v = (mb_voice *)calloc(1, sizeof *v);
    if (!v) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    v->h = mbh_create(image, n, NULL, out_rate, err, errlen);
    if (!v->h) {
        free(v);
        return NULL;
    }
    v->out_rate = out_rate;
    mbv_set(v, 50, 50, 100, 1);
    return v;
}

MB_API mb_voice *mbv_create_dir(const char *dir, double out_rate, char *err, int errlen)
{
    char path[4096];
    FILE *f;
    long len;
    unsigned char *d = NULL;
    mb_voice *v = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, MB_FILE);
    f = fopen(path, "rb");
    if (f && fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (d = (unsigned char *)malloc((size_t)len)) != NULL && fread(d, 1, (size_t)len, f) == (size_t)len)
        v = mbv_create(d, (size_t)len, out_rate, err, errlen);
    else if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s: cannot read", MB_FILE);
    if (f)
        fclose(f);
    free(d);
    return v;
}

MB_API void mbv_destroy(mb_voice *v)
{
    if (!v)
        return;
    free_parts(v);
    mbh_destroy(v->h);
    free(v->pcm);
    free(v);
}

MB_API mb_host *mbv_host(mb_voice *v) { return v->h; }

MB_API void mbv_set(mb_voice *v, int rate, int pitch, int volume, int numbers)
{
    v->rate = rate;
    v->pitch = pitch;
    v->volume = volume < 0 ? 0 : volume;
    v->numbers = numbers != 0;
}

/* lets the firmware finish what it is saying (a cancelled part ends at its next frame), the audio dropped */
static void wait_idle(mb_voice *v)
{
    double t = 0.0;
    const double *a;
    while (mbh_busy(v->h) && t < WAIT_LIMIT) {
        mbh_run(v->h, BLOCK_S, STEP_S, &a);
        t += BLOCK_S;
    }
}

MB_API int mbv_speak(mb_voice *v, const char *utf8, int pitch_offset)
{
    char **p = NULL;
    int n;
    free_parts(v);
    v->active = 0;
    v->n_split = 0;
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
    wait_idle(v);
    return 1;
}

/* the next part to the firmware: said, or split in two when it would make too many frames; 0 when none is left */
static int say_next(mb_voice *v)
{
    int infl = mbv_pitch_step(v->pitch) + (v->offset ? (int)(v->offset * 18 / 50.0 + (v->offset > 0 ? 0.5 : -0.5)) : 0);
    mbh_set(v->h, clamp(infl, 0, 26), mbv_rate_step(v->rate), AMPLITUDE, FILTER);
    while (v->next < v->n_parts) {
        char *s = v->parts[v->next];
        int len = (int)strlen(s);
        if (mbh_say(v->h, (const unsigned char *)s, len)) {
            v->next++;
            return 1;
        }
        if (mbh_get_int(v->h, "fault") != MBH_FAULT_LONG || len < 2) {
            v->next++;                      /* a part the firmware cannot take at all: skipped */
            continue;
        }
        {                                   /* split it in two at the space nearest the middle, and try again */
            list l = {NULL, 0, 0};
            int mid = len / 2, k = -1, d, i;
            for (d = 0; d < mid && k < 0; d++) {
                if (s[mid - d] == ' ') k = mid - d;
                else if (mid + d < len && s[mid + d] == ' ') k = mid + d;
            }
            if (k <= 0) k = mid;
            if (!push(&l, s, k) || !push(&l, s + k, len - k) || l.n == 0) {
                for (i = 0; i < l.n; i++) free(l.p[i]);
                free(l.p);
                v->next++;
                continue;
            }
            {
                char **q = (char **)realloc(v->parts, (size_t)(v->n_parts + l.n - 1) * sizeof *q);
                if (!q) {
                    for (i = 0; i < l.n; i++) free(l.p[i]);
                    free(l.p);
                    v->next++;
                    continue;
                }
                v->parts = q;
            }
            memmove(v->parts + v->next + l.n, v->parts + v->next + 1,
                    (size_t)(v->n_parts - v->next - 1) * sizeof(char *));
            for (i = 0; i < l.n; i++)
                v->parts[v->next + i] = l.p[i];
            v->n_parts += l.n - 1;
            free(l.p);
            free(s);
            v->n_split++;
        }
    }
    return 0;
}

MB_API int mbv_render(mb_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0, count;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active) {
        *done = 1;
        return 0;
    }
    if (!mbh_busy(v->h) && !say_next(v)) {
        v->active = 0;                      /* the parts stay until the next speak: mbv_get_int counts them */
        *done = 1;
        return 0;
    }
    n = mbh_run(v->h, BLOCK_S, STEP_S, &y);
    if (n < 0) {
        mbv_cancel(v);
        *done = 1;
        return 0;
    }
    /* no lead trim: the host converts a text with the chip's time standing still, so speech starts within the first
       few hundred samples (test_mb_voice: speaks) */
    count = n - start;
    if (count > v->pcm_cap) {
        short *q = (short *)realloc(v->pcm, (size_t)count * sizeof(short));
        if (!q) {
            mbv_cancel(v);
            *done = 1;
            return 0;
        }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    return count > 0 ? count : 0;
}

MB_API void mbv_cancel(mb_voice *v)
{
    free_parts(v);
    v->active = 0;
    mbh_cancel(v->h);
}

MB_API int mbv_get_int(const mb_voice *v, const char *name)
{
    if (!strcmp(name, "parts")) return v->n_parts;
    if (!strcmp(name, "split")) return v->n_split;
    return -1;
}
