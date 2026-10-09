/* ssp_speech.c -- see ssp_speech.h. */
#include <stdlib.h>
#include <string.h>

#include "ssp_speech.h"

int ssp_speech_break = 0;

struct ssp_speech {
    ssa_engine *e;
    int voice;
    ssa_settings s;
    int rate, pitch;
    ssp_segment *seg;                  /* the request's segments, copied */
    int n;
    int k;                             /* the segment under way; n when the request is done */
    int speaking;                      /* the unit is speaking segment k (else its pause, or the next to start) */
    long silence;                      /* zeros still to come after segment k */
    int errors;
    int stop;                          /* ssp_speech_stop's flag, any thread: __atomic */
};

ssp_speech *ssp_speech_new(ssa_engine *e)
{
    ssp_speech *p = (ssp_speech *)calloc(1, sizeof *p);
    if (p) p->e = e;
    return p;
}

static void drop(ssp_speech *p)
{
    for (int k = 0; k < p->n; k++) free(p->seg[k].text);
    free(p->seg);
    p->seg = NULL;
    p->n = p->k = 0;
    p->speaking = 0;
    p->silence = 0;
}

void ssp_speech_free(ssp_speech *p)
{
    if (!p) return;
    drop(p);
    free(p);
}

int ssp_speech_start(ssp_speech *p, int voice, const ssp_request *r, const ssa_settings *s)
{
    ssa_cancel(p->e);                  /* a request still running is abandoned */
    drop(p);
    __atomic_store_n(&p->stop, 0, __ATOMIC_SEQ_CST);
    p->voice = voice;
    p->s = *s;
    p->rate = r->rate;
    p->pitch = r->pitch;
    p->errors = 0;
    if (r->n <= 0) return 1;
    p->seg = (ssp_segment *)calloc((size_t)r->n, sizeof *p->seg);
    if (!p->seg) return 1;
    for (int k = 0; k < r->n; k++) {
        size_t m = strlen(r->seg[k].text) + 1;
        if (!(p->seg[k].text = (char *)malloc(m))) { p->n = k; drop(p); return 1; }
        memcpy(p->seg[k].text, r->seg[k].text, m);
        p->seg[k].pause_ms = r->seg[k].pause_ms;
    }
    p->n = r->n;
    p->k = -1;                         /* the first pull starts segment 0 */
    return ssp_has_text(r) || ssp_total_pause_ms(r) > 0 ? 0 : 1;
}

/* the next segment begun: its text on the unit (when it has any), its pause counted */
static void next_segment(ssp_speech *p)
{
    int r;
    p->k++;
    p->speaking = 0;
    p->silence = 0;
    if (p->k >= p->n) return;
    p->silence = ssp_speech_break == 1 ? 0 : (long)p->seg[p->k].pause_ms * ssa_sample_rate(p->e) / 1000;
    if (!p->seg[p->k].text[0]) return;
    r = ssa_start(p->e, p->voice, p->seg[p->k].text, &p->s, p->rate, p->pitch);
    if (r < 0) p->errors++;
    p->speaking = r == 0;
}

int ssp_speech_pull(ssp_speech *p, short *out, int cap)
{
    while (p->k < p->n) {
        if (__atomic_load_n(&p->stop, __ATOMIC_SEQ_CST)) {
            if (ssp_speech_break == 2) {   /* the control: the stop forgotten at the next segment */
                __atomic_store_n(&p->stop, 0, __ATOMIC_SEQ_CST);
                ssa_cancel(p->e);
                next_segment(p);
                continue;
            }
            ssa_cancel(p->e);
            return -2;
        }
        if (p->k < 0) { next_segment(p); continue; }
        if (p->speaking) {
            int got = ssa_pull(p->e, out, cap);
            if (got > 0) return got;
            if (got == -2) continue;       /* stopped: the flag above decides */
            p->speaking = 0;               /* the segment's audio is all handed over */
            continue;
        }
        if (p->silence > 0) {
            int z = p->silence < cap ? (int)p->silence : cap;
            memset(out, 0, sizeof(short) * (size_t)z);
            p->silence -= z;
            return z;
        }
        next_segment(p);
    }
    return 0;
}

void ssp_speech_stop(ssp_speech *p)
{
    __atomic_store_n(&p->stop, 1, __ATOMIC_SEQ_CST);
    ssa_stop(p->e);
}

int ssp_speech_errors(const ssp_speech *p) { return p->errors; }
