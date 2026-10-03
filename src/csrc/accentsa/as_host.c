/* as_host.c -- src/hosts/accent_sa.py's AccentSA in C, line for line (as_host.h).  Every comparison, every order of
 * operations and every rounding is Python's.  MIT.
 */
#include "as_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as_board.h"

static const unsigned char FLUSH_BOOT[] = {0x1B, '=', 'F', 0x1B, '=', 'M', 0x18};   /* ESC =F, ESC =M, Ctrl-X */
static const unsigned char CTRL_X[] = {0x18};

struct as_host {
    as_board *b;
    ssi263 *chip;
    int own_chip;
    double out_rate;
    double cpu_hz, turbo, tick_hz, tick_acc;
    int preparing;
    double last_speech, say_time;
    long work;                         /* instructions the firmware ran outside its waiting loops in the last ash_run */
    double *buf;
    int n_buf, cap_buf;
    int log_on;
    ash_write *wl;
    int n_wl, cap_wl;
};

/* ---- the chip, as the board sees it ------------------------------------------------------------------------------ */

static void chip_write(void *ctx, int reg, int val)
{
    as_host *h = (as_host *)ctx;
    ssi263_write(h->chip, reg, val);
    if (h->log_on) {
        if (h->n_wl == h->cap_wl) {
            int cap = h->cap_wl ? h->cap_wl * 2 : 1024;
            ash_write *w = (ash_write *)realloc(h->wl, (size_t)cap * sizeof *w);
            if (w) { h->wl = w; h->cap_wl = cap; }
        }
        if (h->n_wl < h->cap_wl) {
            h->wl[h->n_wl].t = ssi263_time(h->chip);
            h->wl[h->n_wl].reg = reg;
            h->wl[h->n_wl].val = val;
            h->n_wl++;
        }
    }
    if (reg == 0 && (val & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80)) {
        h->last_speech = ssi263_time(h->chip);     /* PA (00) is not speech */
        h->preparing = 0;
    }
}

static int chip_request(void *ctx)
{
    return ssi263_request(((as_host *)ctx)->chip) ? 1 : 0;
}

/* ---- creation ---------------------------------------------------------------------------------------------------- */

AS_API as_host *ash_create(const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                           const unsigned char *u4, size_t n4, ssi263 *chip, double out_rate, char *err, int errlen)
{
    as_chip ops;
    as_host *h = (as_host *)calloc(1, sizeof *h);
    if (!h) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    if (!chip) {
        ssi263_params p;
        ssi263_default_params(&p);
        chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
        if (!chip) {
            free(h);
            if (err && errlen > 0)
                snprintf(err, (size_t)errlen, "ssi263_new failed");
            return NULL;
        }
        h->own_chip = 1;
    }
    h->chip = chip;
    h->out_rate = out_rate;
    h->cpu_hz = ASH_CPU_HZ;
    h->turbo = ASH_TURBO;
    h->last_speech = -1.0;
    ops.ctx = h;
    ops.write = chip_write;
    ops.request = chip_request;
    h->b = as_board_create(u2, n2, u3, n3, u4, n4, &ops, err, errlen);
    if (!h->b) {
        if (h->own_chip)
            ssi263_free(chip);
        free(h);
        return NULL;
    }
    return h;
}

/* a whole file into memory; its size in *n.  NULL (the reason in err) if it can't be read. */
static unsigned char *slurp(const char *dir, const char *name, size_t *n, char *err, int errlen)
{
    char path[4096];
    FILE *f;
    long len;
    unsigned char *d;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot open", name);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot read", name);
        return NULL;
    }
    d = (unsigned char *)malloc(len ? (size_t)len : 1);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot read", name);
        return NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return d;
}

AS_API as_host *ash_create_dir(const char *rom_dir, ssi263 *chip, double out_rate, char *err, int errlen)
{
    size_t n2 = 0, n3 = 0, n4 = 0;
    unsigned char *u2 = slurp(rom_dir, "u2.BIN", &n2, err, errlen), *u3 = NULL, *u4 = NULL;
    as_host *h = NULL;
    if (u2)
        u3 = slurp(rom_dir, "u3.BIN", &n3, err, errlen);
    if (u3)
        u4 = slurp(rom_dir, "u4.BIN", &n4, err, errlen);
    if (u4)
        h = ash_create(u2, n2, u3, n3, u4, n4, chip, out_rate, err, errlen);
    free(u2);
    free(u3);
    free(u4);
    return h;
}

AS_API void ash_destroy(as_host *h)
{
    if (!h)
        return;
    as_board_destroy(h->b);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->buf);
    free(h->wl);
    free(h);
}

AS_API ssi263 *ash_chip(as_host *h)
{
    return h->chip;
}

/* ---- accent_sa.py ---------------------------------------------------------------------------------------------- */

/* Python's str.isalnum() on chr(0..255) */
static const unsigned char ALNUM[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0xFE, 0xFF, 0xFF, 0x07, 0xFE, 0xFF, 0xFF, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x2C, 0x76, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF};

static int upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
static int letter(unsigned char c) { return upper(c) || (c >= 'a' && c <= 'z'); }
static int digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* accent_sa.py's _ESC, "\x1b(?:[=+\-O][A-Za-z]|[A-Z][0-9A-Z]|\|~[^~]*~)": the length of the command at s, or 0 */
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

AS_API int ash_is_speech(const unsigned char *bytes, int n)
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

AS_API int ash_say(as_host *h, const unsigned char *bytes, int n, int speech)
{
    if (n <= 0)
        return 1;
    if (speech < 0)
        speech = ash_is_speech(bytes, n);
    if (!as_board_send(h->b, bytes, n))
        return 0;
    if (speech) {
        h->preparing = 1;
        h->say_time = ssi263_time(h->chip);
    }
    return 1;
}

/* _cpu(dt) */
static void run_cpu(as_host *h, double dt)
{
    double hz = h->cpu_hz * (h->preparing ? h->turbo : 1.0);
    long long n = (long long)(hz * dt);            /* int(): truncation */
    as_board_run(h->b, (uint64_t)(n > 20 ? n : 20));
    if (h->tick_hz != 0.0) {
        h->tick_acc += dt * h->tick_hz;
        while (h->tick_acc >= 1.0) {
            h->tick_acc -= 1.0;
            as_board_rst75(h->b);
        }
    }
}

static int reserve(as_host *h, long more)
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

AS_API int ash_run(as_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0;
    h->n_buf = 0;
    h->work = 0;
    *audio = h->buf;
    while (t < seconds) {
        double before, dt;
        long n, got;
        as_board_serial(h->b);
        as_board_trap(h->b);
        before = ssi263_time(h->chip);
        if (!ssi263_request(h->chip)) {
            n = (long)(step * h->out_rate);                /* int(): truncation */
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run_until_request(h->chip, n, h->buf + h->n_buf);
        } else {
            n = (long)nearbyint(step / 4 * h->out_rate);   /* round(): half to even */
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run(h->chip, n, h->buf + h->n_buf);
        }
        h->n_buf += (int)got;
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;                                     /* max(..., 1e-5) */
        run_cpu(h, dt);
        h->work += (long)as_board_take_work(h->b);
        t += dt;
    }
    *audio = h->buf;
    return h->n_buf;
}

AS_API double ash_skip(as_host *h, double seconds, double step)
{
    double t = 0.0;
    while (t < seconds - 1e-9) {
        double before, dt, s = seconds - t;
        as_board_serial(h->b);
        as_board_trap(h->b);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, step < s ? step : s);         /* min(step, seconds - t) */
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;
        run_cpu(h, dt);
        as_board_take_work(h->b);                          /* skipped time is no block's */
        t += dt;
    }
    return t;
}

AS_API int ash_speaking(const as_host *h)
{
    return (as_board_get(h->b, "latch40") & 0x10) ? 1 : 0;
}

AS_API int ash_busy(const as_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    if (as_board_get(h->b, "rx") || as_board_get(h->b, "rx_ready") || ash_speaking(h) || (now - h->last_speech) < quiet)
        return 1;
    return h->preparing && (now - h->say_time) < patience;
}

AS_API double ash_boot(as_host *h, double limit)
{
    double t = ash_skip(h, 0.12, 0.002);
    ash_say(h, FLUSH_BOOT, (int)sizeof FLUSH_BOOT, 0);
    t += ash_skip(h, 0.02, 0.002);
    while (t < limit && ash_busy(h, 0.03, 1.5))
        t += ash_skip(h, 0.02, 0.002);
    return t;
}

AS_API double ash_cancel(as_host *h, double limit)
{
    double t;
    h->preparing = 0;
    as_board_drop_input(h->b);
    ash_say(h, CTRL_X, 1, 0);
    t = ash_skip(h, 0.01, 0.002);
    while (t < limit && ash_busy(h, 0.03, 1.5))
        t += ash_skip(h, 0.01, 0.002);
    return t;
}

/* ---- state ------------------------------------------------------------------------------------------------------- */

AS_API double ash_get_double(const as_host *h, const char *name)
{
    if (!strcmp(name, "cpu_hz")) return h->cpu_hz;
    if (!strcmp(name, "turbo")) return h->turbo;
    if (!strcmp(name, "tick_hz")) return h->tick_hz;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

AS_API void ash_set_double(as_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_hz")) h->cpu_hz = v;
    else if (!strcmp(name, "turbo")) h->turbo = v;
    else if (!strcmp(name, "tick_hz")) h->tick_hz = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
}

AS_API int ash_get_int(const as_host *h, const char *name)
{
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "work")) return h->work > 0x7FFFFFFFL ? 0x7FFFFFFF : (int)h->work;
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    return as_board_get(h->b, name);
}

AS_API void ash_set_int(as_host *h, const char *name, int v)
{
    if (!strcmp(name, "preparing"))
        h->preparing = v != 0;
    else if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
    else
        as_board_set(h->b, name, v);
}

AS_API int ash_writes(const as_host *h, const ash_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

AS_API void ash_clear_writes(as_host *h)
{
    h->n_wl = 0;
}

AS_API int ash_tx(const as_host *h, const unsigned char **bytes)
{
    return as_board_tx(h->b, (const uint8_t **)bytes);
}
