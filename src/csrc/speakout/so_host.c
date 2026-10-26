/* so_host.c -- src/hosts/speakout.py's SpeakOutV40 ("mame-steps") in C, line for line (so_host.h).  Every comparison,
 * every order of operations and every rounding is Python's; each function names the method it ports.  MIT.
 */
#include "so_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_board.h"
#include "../numwords.h"

static const unsigned char CTRL_X[] = {0x18};
/* The firmware's text ring: every byte received goes to [1A30h], 8000h-83FFh, and the rules read at [1A32h].  It never
   holds the sender back (a real Speak-Out's serial line did), so a text past 1 KB wrapped over text not yet read -- the
   end of a long text lost.  The host keeps the excess and feeds it as the ring empties (feed). */
#define TEXT_WRITE 0x1A30
#define TEXT_READ 0x1A32
#define TEXT_RING 0x400
#define FEED_BELOW 0x300            /* the ring and the serial queue together under this: text that fits goes at once */
#define PIECE 32                    /* otherwise this much at a time, once the serial queue is empty */
#ifndef SO_NO_CANCEL_CR
static const unsigned char CR[] = {0x0D};                  /* soh_cancel's line end */
#endif

struct so_host {
    so_board *b;
    ssi263 *chip;
    int own_chip;
    double out_rate;
    double cpu_ips;
    double last_speech;                    /* chip time of the last non-idle phoneme load */
    int preparing;                         /* text with words sent, none of it spoken yet */
    double say_time;
    double *buf;
    int n_buf, cap_buf;
    int log_on;                            /* "log_writes" */
    soh_write *wl;
    int n_wl, cap_wl;
    unsigned char *waiting;                /* text said and not yet sent (feed) */
    int n_wait, cap_wait;
    int pace_break;                        /* SSI263_SO_PACE_BREAK=1, the tests' control: all text at once, as it was */
    int tail_break;                        /* SSI263_SO_TAIL_BREAK=1, the tests' control: busy() without held() */
};

static void set_err(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", msg);
}

/* ---- __init__ ---------------------------------------------------------------------------------------------------- */

SO_API so_host *soh_create(const char *hex, size_t len, ssi263 *chip, double out_rate, char *err, int errlen)
{
    so_host *h = (so_host *)calloc(1, sizeof *h);
    if (!h) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    h->pace_break = getenv("SSI263_SO_PACE_BREAK") && !strcmp(getenv("SSI263_SO_PACE_BREAK"), "1");
    h->tail_break = getenv("SSI263_SO_TAIL_BREAK") && !strcmp(getenv("SSI263_SO_TAIL_BREAK"), "1");
    h->b = so_create();
    if (!h->b) {
        free(h);
        set_err(err, errlen, "so_create failed");
        return NULL;
    }
    if (so_load_hex(h->b, hex, len) < 0) {
        so_destroy(h->b);
        free(h);
        set_err(err, errlen, "malformed Intel HEX");
        return NULL;
    }
    so_power_on(h->b);
    if (!chip) {
        ssi263_params p;
        ssi263_default_params(&p);
        chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
        if (!chip) {
            so_destroy(h->b);
            free(h);
            set_err(err, errlen, "ssi263_new failed");
            return NULL;
        }
        h->own_chip = 1;
    }
    h->chip = chip;
    h->out_rate = out_rate;
    h->cpu_ips = SOH_CPU_IPS;
    h->last_speech = -1.0;
    return h;
}

SO_API so_host *soh_create_file(const char *path, ssi263 *chip, double out_rate, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    long len;
    char *text;
    so_host *h;
    if (!f) {
        set_err(err, errlen, "cannot open the firmware (SPEAKOUT.HEX)");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        set_err(err, errlen, "cannot read the firmware");
        return NULL;
    }
    text = (char *)malloc(len ? (size_t)len : 1);
    if (!text || fread(text, 1, (size_t)len, f) != (size_t)len) {
        free(text);
        fclose(f);
        set_err(err, errlen, "cannot read the firmware");
        return NULL;
    }
    fclose(f);
    h = soh_create(text, (size_t)len, chip, out_rate, err, errlen);
    free(text);
    return h;
}

SO_API void soh_destroy(so_host *h)
{
    if (!h)
        return;
    so_destroy(h->b);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->buf);
    free(h->wl);
    free(h->waiting);
    free(h);
}

SO_API ssi263 *soh_chip(so_host *h)
{
    return h->chip;
}

/* ---- _chip_write, _cpu (SpeakOutV40's) ------------------------------------------------------------------------- */

static void log_write(so_host *h, int reg, int v)
{
    if (h->n_wl == h->cap_wl) {
        int cap = h->cap_wl ? h->cap_wl * 2 : 256;
        soh_write *w = (soh_write *)realloc(h->wl, (size_t)cap * sizeof *w);
        if (!w)
            return;
        h->wl = w;
        h->cap_wl = cap;
    }
    h->wl[h->n_wl].t = ssi263_time(h->chip);
    h->wl[h->n_wl].reg = reg;
    h->wl[h->n_wl].val = v;
    h->n_wl++;
}

/* the chip still sounding the last phoneme it was given (not PA, powered up): a text goes on until that phoneme has
   played and the firmware has answered with PA.  At rate 0 one phoneme outlasts busy()'s `quiet`, and its rest played
   at the head of the next text, before any write of its own (a listener, typing letters; tools/check_so_tail.py) */
static int held(const so_host *h)
{
    return !h->tail_break && (ssi263_reg(h->chip, 0) & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80);
}

static void chip_write(so_host *h, int reg, int v)
{
    int sounding = reg == 0 && held(h);        /* a phoneme playing that this write ends */
    ssi263_write(h->chip, reg, v);
    if (h->log_on)
        log_write(h, reg, v);
    if (sounding)                              /* speech until now: busy()'s `quiet` lets its fall die away here */
        h->last_speech = ssi263_time(h->chip);
    /* Idle = the start-of-utterance routine (0x4318): PA/3 with R2 forced to rate F.  Speech never uses rate F, and
       every frame first primes R0 with 00. */
    if (reg == 0 && v != 0x00 && !(v == 0xC0 && (ssi263_reg(h->chip, 2) >> 4) == 0xF)) {
        h->last_speech = ssi263_time(h->chip);
        h->preparing = 0;
    }
}

/* _cpu(count): count steps as Unicorn counts instructions, then the slice's chip writes, in order */
static void run_cpu(so_host *h, double dt)
{
    long long n = (long long)(h->cpu_ips * dt);              /* int(): truncation */
    const so_write *w;
    int k, i;
    so_run_steps_unicorn(h->b, (uint64_t)(n > 200 ? n : 200));   /* max(200, ...) */
    k = so_writes(h->b, &w);
    for (i = 0; i < k; i++)
        chip_write(h, w[i].reg, w[i].val);
    so_clear_writes(h->b);
}

/* ---- say, input_pending, busy ------------------------------------------------------------------------------------ */

static int word_at(const so_host *h, uint32_t addr);

/* _feed: the waiting text to the serial line: all of it when it fits under FEED_BELOW with what the ring and the line
   already hold (any ordinary text, sent exactly as it always was), else a PIECE at a time, each once the line is empty
   and the ring has room for it */
static void feed(so_host *h)
{
    while (h->n_wait > 0) {
        int queued = so_input_count(h->b), held, len;
        held = (word_at(h, TEXT_WRITE) - word_at(h, TEXT_READ)) % TEXT_RING;
        if (held < 0)
            held += TEXT_RING;                               /* Python's % */
        held += queued;
        if (h->pace_break || held + h->n_wait < FEED_BELOW)
            len = h->n_wait;
        else if (!queued && held + PIECE < FEED_BELOW)
            len = h->n_wait < PIECE ? h->n_wait : PIECE;
        else
            return;
        so_send(h->b, h->waiting, len);
        memmove(h->waiting, h->waiting + len, (size_t)(h->n_wait - len));
        h->n_wait -= len;
    }
}

SO_API void soh_say(so_host *h, const unsigned char *bytes, int n)
{
    int i, speech = 0;
    for (i = 0; i < n && !speech; i++)                       /* the text's characters >= " ", any isalnum() */
        speech = bytes[i] >= 0x20 && nw_isalnum(bytes[i]);
    if (speech && bytes[0] != 0x05) {                        /* and not text.startswith("\x05") */
        h->preparing = 1;
        h->say_time = ssi263_time(h->chip);
    }
    if (n <= 0)
        return;
    if (h->n_wait + n > h->cap_wait) {
        int cap = h->cap_wait ? h->cap_wait : 4096;
        unsigned char *q;
        while (cap < h->n_wait + n)
            cap *= 2;
        q = (unsigned char *)realloc(h->waiting, (size_t)cap);
        if (!q)
            return;                                          /* out of memory: the text is not said */
        h->waiting = q;
        h->cap_wait = cap;
    }
    memcpy(h->waiting + h->n_wait, bytes, (size_t)n);
    h->n_wait += n;
    feed(h);
}

static int word_at(const so_host *h, uint32_t addr)
{
    uint8_t w[2];
    so_read(h->b, addr, w, 2);
    return w[0] | (w[1] << 8);
}

SO_API int soh_input_pending(const so_host *h)
{
    if (h->n_wait > 0 || so_input_queued(h->b))
        return 1;
    if (word_at(h, 0x1A32) != word_at(h, 0x1A30))
        return 1;
    /* the phoneme-frame ring the chip ISR consumes (0x1119: [0x243C] chases [0x243A]) */
    return word_at(h, 0x243C) != word_at(h, 0x243A);
}

SO_API int soh_busy(const so_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    if (soh_input_pending(h) || (now - h->last_speech) < quiet || (held(h) && (now - h->last_speech) < patience))
        return 1;
    return h->preparing && (now - h->say_time) < patience;
}

/* ---- run, skip --------------------------------------------------------------------------------------------------- */

static int reserve(so_host *h, long more)
{
    if (h->n_buf + more > h->cap_buf) {
        int cap = h->cap_buf ? h->cap_buf : 4096;
        double *b;
        while (cap < h->n_buf + more)
            cap *= 2;
        b = (double *)realloc(h->buf, (size_t)cap * sizeof(double));
        if (!b)
            return 0;
        h->buf = b;
        h->cap_buf = cap;
    }
    return 1;
}

SO_API int soh_run(so_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0;
    feed(h);
    h->n_buf = 0;
    *audio = h->buf;
    while (t < seconds) {
        double before, dt;
        long n, got;
        so_offer(h->b, ssi263_request(h->chip) ? 1 : 0);    /* _offer */
        before = ssi263_time(h->chip);
        if (!ssi263_request(h->chip)) {
            n = (long)(step * h->out_rate);                  /* int(): truncation */
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run_until_request(h->chip, n, h->buf + h->n_buf);
        } else {
            n = (long)nearbyint(step / 4 * h->out_rate);     /* round(): half to even */
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run(h->chip, n, h->buf + h->n_buf);
        }
        h->n_buf += (int)got;
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;                                       /* max(..., 1e-5) */
        run_cpu(h, dt);
        t += dt;
    }
    *audio = h->buf;
    return h->n_buf;
}

SO_API double soh_skip(so_host *h, double seconds, double step)
{
    double t = 0.0;
    feed(h);
    while (t < seconds - 1e-9) {
        double before, dt, s = seconds - t;
        so_offer(h->b, ssi263_request(h->chip) ? 1 : 0);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, step < s ? step : s);           /* min(step, seconds - t) */
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;
        run_cpu(h, dt);
        t += dt;
    }
    return t;
}

/* ---- boot, cancel ------------------------------------------------------------------------------------------------ */

SO_API void soh_boot(so_host *h, double seconds)
{
    const double *y;
    soh_run(h, seconds, 0.0005, &y);
    soh_cancel(h, 0.6);
    soh_run(h, 0.2, 0.0005, &y);
}

SO_API void soh_cancel(so_host *h, double limit)
{
    double t;
    so_drop_input(h->b);
    h->n_wait = 0;
    h->preparing = 0;
    so_send(h->b, CTRL_X, 1);                                /* past any waiting text, as soh_say sent it before */
    soh_skip(h, 0.02, 0.002);
    t = 0.02;                                                /* nominal, as speakout.py counts it */
    while (t < limit && soh_busy(h, SOH_QUIET, SOH_PATIENCE)) {
        soh_skip(h, 0.02, 0.002);
        t += 0.02;
    }
    /* 0.8: then a line's end.  The firmware's rules keep a cut word's context through the ^X: "Custom number
       processing" cut after K HF (or K HF UH), the next "Hello" was read "HF EH EH LF LF UH O O U", an extra UH
       (complete_fuzz speakout, step 12).  A CR ends the word: silent (the firmware loads nothing for it), and every
       cut point then reads the next text as alone; a space does not (test_so_cancel.c). */
#ifndef SO_NO_CANCEL_CR                                      /* test_so_cancel's must-fail control: as before 0.8 */
    soh_say(h, CR, 1);
#endif
}

SO_API int soh_fault(const so_host *h)
{
    uint32_t s[7];
    so_cpu_state(h->b, s);
    return s[3] || s[4] || s[5];                             /* halted, fault, undefined */
}

/* ---- state ------------------------------------------------------------------------------------------------------- */

SO_API double soh_get_double(const so_host *h, const char *name)
{
    if (!strcmp(name, "cpu_ips")) return h->cpu_ips;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

SO_API void soh_set_double(so_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_ips")) h->cpu_ips = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
}

SO_API int soh_get_int(const so_host *h, const char *name)
{
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    if (!strcmp(name, "steps")) return (int)so_steps(h->b);
    if (!strcmp(name, "log_writes")) return h->log_on;
    return -1;
}

SO_API void soh_set_int(so_host *h, const char *name, int v)
{
    if (!strcmp(name, "preparing"))
        h->preparing = v != 0;
    else if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
}

SO_API int soh_writes(const so_host *h, const soh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

SO_API void soh_clear_writes(so_host *h) { h->n_wl = 0; }
