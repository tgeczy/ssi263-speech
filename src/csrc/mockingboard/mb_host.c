/* mb_host.c -- the Mockingboard host (mb_host.h).  MIT.
 */
#include "mb_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_board.h"

/* the toolkit's addresses (mb_host.h) */
#define A_TEXT 0x8500u                     /* MB$ GETTEXT's buffer */
#define A_LAST 0x8C03u                     /* the text's last index */
#define A_PTR 0x06u                        /* (06h): the text */
#define A_SPEAK 0x8C11u                    /* TEXT TO SPEECH, after its JSR to MB$ GETTEXT */
#define A_RULES_DONE 0x8C32u               /* its JSR to INFLECTION: the rules have made every R0 frame */
#define A_SPARE 0x8B00u                    /* 8B00-8BFF: no file's; the R0 frames reach it only past 256 */
#define A_SETTINGS 0x92DCu                 /* inflection, rate, amplitude, filter */
#define A_BUSY 0x1Eu
#define A_END 0x1Au                        /* (1Ah): one past the last R0 frame */
#define A_PLAYING 0xECu                    /* (ECh): the R0 frame the driver writes next */

#define CALL_LIMIT 50000000ULL             /* 6502 cycles a text's conversion may take: ~49 s on an Apple */

/* the six files, in MB_FILE's order: their load addresses */
static const unsigned ADDRS[6] = {0x8C00, 0x9000, 0x9300, 0xD000, 0xD100, 0xD200};
static const char *const SHA256_HEX = "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae";

struct mb_host {
    mb_board *b;
    ssi263 *chip;
    int own_chip;
    double out_rate;
    double cpu_hz, cycle_acc;
    int fault, frames;
    double *buf;
    int n_buf, cap_buf;
    int log_on;
    mbh_write *wl;
    int n_wl, cap_wl;
};

/* ---- the chip, as the board sees it ------------------------------------------------------------------------------ */

static void chip_write(void *ctx, int reg, int val)
{
    mb_host *h = (mb_host *)ctx;
    ssi263_write(h->chip, reg, val);
    if (h->log_on) {
        if (h->n_wl == h->cap_wl) {
            int cap = h->cap_wl ? h->cap_wl * 2 : 1024;
            mbh_write *w = (mbh_write *)realloc(h->wl, (size_t)cap * sizeof *w);
            if (w) { h->wl = w; h->cap_wl = cap; }
        }
        if (h->n_wl < h->cap_wl) {
            h->wl[h->n_wl].t = ssi263_time(h->chip);
            h->wl[h->n_wl].reg = reg;
            h->wl[h->n_wl].val = val;
            h->n_wl++;
        }
    }
    mb_board_ar(h->b, ssi263_request(h->chip));
}

/* ---- the firmware file ------------------------------------------------------------------------------------------- */

/* SHA-256 (FIPS 180-4), for the firmware's identity only */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR32(v, n) (((v) >> (n)) | ((v) << (32 - (n))))

static void sha256_block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], s[8], t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (i = 16; i < 64; i++)
        w[i] = (ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7]
             + (ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
    memcpy(s, h, sizeof s);
    for (i = 0; i < 64; i++) {
        t1 = s[7] + (ROR32(s[4], 6) ^ ROR32(s[4], 11) ^ ROR32(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + K256[i] + w[i];
        t2 = (ROR32(s[0], 2) ^ ROR32(s[0], 13) ^ ROR32(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        s[7] = s[6]; s[6] = s[5]; s[5] = s[4]; s[4] = s[3] + t1;
        s[3] = s[2]; s[2] = s[1]; s[1] = s[0]; s[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
        h[i] += s[i];
}

static void sha256(const unsigned char *d, size_t n, unsigned char out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char tail[128];
    size_t full = n / 64 * 64, rest = n - full, k;
    unsigned long long bits = (unsigned long long)n * 8;
    int i;
    for (k = 0; k < full; k += 64)
        sha256_block(h, d + k);
    memset(tail, 0, sizeof tail);
    memcpy(tail, d + full, rest);
    tail[rest] = 0x80;
    k = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++)
        tail[k - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha256_block(h, tail);
    if (k == 128)
        sha256_block(h, tail + 64);
    for (i = 0; i < 32; i++)
        out[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
}

static int hex_is(const unsigned char *d, const char *hex)
{
    static const char X[] = "0123456789abcdef";
    int k;
    for (k = 0; k < 32; k++)
        if (hex[2 * k] != X[d[k] >> 4] || hex[2 * k + 1] != X[d[k] & 15])
            return 0;
    return 1;
}

MB_API int mbh_is_known(const unsigned char *image, size_t n)
{
    unsigned char sum[32];
    sha256(image, n, sum);
    return hex_is(sum, SHA256_HEX);
}

/* the six files placed; 0 (the reason in err) if the image is not the set */
static int place(mb_board *b, const unsigned char *image, size_t n, char *err, int errlen)
{
    unsigned char sum[32];
    size_t at = 0;
    int k;
    sha256(image, n, sum);
    if (!hex_is(sum, SHA256_HEX)) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: not Sweet Micro Systems' text-to-speech 1.1 (unknown sha256)", MB_FILE);
        return 0;
    }
    for (k = 0; k < 6; k++) {
        unsigned addr, len;
        if (at + 4 > n)
            return 0;
        addr = image[at] | image[at + 1] << 8;
        len = image[at + 2] | image[at + 3] << 8;
        if (addr != ADDRS[k] || at + 4 + len > n || !mb_board_load(b, (uint16_t)addr, image + at + 4, len))
            return 0;
        at += 4 + len;
    }
    return at == n;
}

/* ---- creation ---------------------------------------------------------------------------------------------------- */

MB_API mb_host *mbh_create(const unsigned char *image, size_t n, ssi263 *chip, double out_rate, char *err, int errlen)
{
    mb_chip ops;
    mb_host *h = (mb_host *)calloc(1, sizeof *h);
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
    ssi263_set_xck(chip, MBH_XCK_HZ);        /* a fresh chip: exact */
    h->chip = chip;
    h->out_rate = out_rate;
    h->cpu_hz = MB_CPU_HZ;
    ops.ctx = h;
    ops.write = chip_write;
    h->b = mb_board_create(&ops);
    if (!h->b || !place(h->b, image, n, err, errlen)) {
        if (h->b && err && errlen > 0 && !err[0])
            snprintf(err, (size_t)errlen, "%s: not six DOS files at the toolkit's addresses", MB_FILE);
        mb_board_destroy(h->b);
        if (h->own_chip)
            ssi263_free(chip);
        free(h);
        return NULL;
    }
    mbh_set(h, 8, 8, 11, 230);                /* the demo's POKEs */
    return h;
}

MB_API mb_host *mbh_create_dir(const char *dir, ssi263 *chip, double out_rate, char *err, int errlen)
{
    char path[4096];
    FILE *f;
    long len;
    unsigned char *d;
    mb_host *h;
    snprintf(path, sizeof path, "%s/%s", dir, MB_FILE);
    f = fopen(path, "rb");
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot open", MB_FILE);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)
        || !(d = (unsigned char *)malloc(len ? (size_t)len : 1))) {
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot read", MB_FILE);
        return NULL;
    }
    if (fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: cannot read", MB_FILE);
        return NULL;
    }
    fclose(f);
    h = mbh_create(d, (size_t)len, chip, out_rate, err, errlen);
    free(d);
    return h;
}

MB_API void mbh_destroy(mb_host *h)
{
    if (!h)
        return;
    mb_board_destroy(h->b);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->buf);
    free(h->wl);
    free(h);
}

MB_API ssi263 *mbh_chip(mb_host *h)
{
    return h->chip;
}

/* ---- speaking ---------------------------------------------------------------------------------------------------- */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

MB_API void mbh_set(mb_host *h, int inflection, int rate, int amplitude, int filter)
{
    mb_board_poke(h->b, A_SETTINGS, (uint8_t)clamp(inflection, 0, 26));
    mb_board_poke(h->b, A_SETTINGS + 1, (uint8_t)clamp(rate, 0, 15));
    mb_board_poke(h->b, A_SETTINGS + 2, (uint8_t)clamp(amplitude, 0, 15));
    mb_board_poke(h->b, A_SETTINGS + 3, (uint8_t)clamp(filter, 0, 255));
}

MB_API int mbh_busy(const mb_host *h)
{
    return mb_board_peek(h->b, A_BUSY) != 0 || mb_board_get(h->b, "irq") > 0;
}

/* runs the 6502 until it reaches `pc`, the idle loop, a guarded write, or the limit; 1 if it reached `pc` */
static int run_to(mb_host *h, uint16_t pc, uint64_t start)
{
    while (mb_board_pc(h->b) != pc && !mb_board_idle(h->b) && !mb_board_get(h->b, "guard_writes")
           && mb_board_cycles(h->b) - start < CALL_LIMIT)
        mb_board_run(h->b, 1);
    return mb_board_pc(h->b) == pc;
}

MB_API int mbh_say(mb_host *h, const unsigned char *text, int n)
{
    uint64_t start;
    int k, marks = 0, raw;
    h->fault = 0;
    if (n <= 0 || n > MBH_MAX_TEXT || mbh_busy(h) || !mb_board_idle(h->b))
        return 0;
    for (k = 0; k < n; k++)
        marks += strchr("!,.?:;", text[k]) != NULL && text[k];
    mb_board_poke(h->b, A_TEXT, ' ');                       /* as MB$ GETTEXT: a space, the text, a space */
    for (k = 0; k < n; k++)
        mb_board_poke(h->b, (uint16_t)(A_TEXT + 1 + k), text[k]);
    mb_board_poke(h->b, (uint16_t)(A_TEXT + 1 + n), ' ');
    mb_board_poke(h->b, A_LAST, (uint8_t)(n + 1));
    mb_board_poke(h->b, A_PTR, A_TEXT & 0xFF);
    mb_board_poke(h->b, A_PTR + 1, A_TEXT >> 8);
    mb_board_call(h->b, A_SPEAK, 0, 0, 0);
    mb_board_guard(h->b, A_SPARE, A_SPARE + 0xFF);
    start = mb_board_cycles(h->b);                           /* the chip's time stands still (mb_host.h) */
    /* the rules: every R0 frame made, before INFLECTION adds a punctuation's phonemes (up to 4 each) */
    if (!run_to(h, A_RULES_DONE, start)) {
        h->fault = mb_board_get(h->b, "guard_writes") ? MBH_FAULT_LONG : MBH_FAULT_STUCK;
        mb_board_abort(h->b);
        mb_board_guard(h->b, 0xFFFF, 0);
        return 0;
    }
    raw = (mb_board_peek(h->b, A_END + 1) - 0x8A) * 256 + mb_board_peek(h->b, A_END);
    if (raw + 4 * marks > MBH_MAX_FRAMES) {
        h->fault = MBH_FAULT_LONG;
        mb_board_abort(h->b);
        mb_board_guard(h->b, 0xFFFF, 0);
        return 0;
    }
    /* INFLECTION and the driver's start */
    run_to(h, MB_IDLE, start);
    mb_board_guard(h->b, 0xFFFF, 0);
    if (!mb_board_idle(h->b)) {
        h->fault = MBH_FAULT_STUCK;
        return 0;
    }
    h->frames = (mb_board_peek(h->b, A_END + 1) - 0x8A) * 256 + mb_board_peek(h->b, A_END);
    return 1;
}

MB_API void mbh_cancel(mb_host *h)
{
    if (!mbh_busy(h))
        return;
    mb_board_poke(h->b, A_END, mb_board_peek(h->b, A_PLAYING));
    mb_board_poke(h->b, A_END + 1, mb_board_peek(h->b, A_PLAYING + 1));
}

static int reserve(mb_host *h, long more)
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

MB_API int mbh_run(mb_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0;
    h->n_buf = 0;
    *audio = h->buf;
    while (t < seconds) {
        double before, dt;
        long n, got;
        before = ssi263_time(h->chip);
        if (!ssi263_request(h->chip)) {
            n = (long)(step * h->out_rate);
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run_until_request(h->chip, n, h->buf + h->n_buf);
        } else {
            n = (long)nearbyint(step / 4 * h->out_rate);
            if (!reserve(h, n + 1))
                return -1;
            got = ssi263_run(h->chip, n, h->buf + h->n_buf);
        }
        h->n_buf += (int)got;
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;
        mb_board_ar(h->b, ssi263_request(h->chip));
        h->cycle_acc += h->cpu_hz * dt;
        if (h->cycle_acc >= 1.0) {
            uint64_t want = (uint64_t)h->cycle_acc;
            h->cycle_acc -= (double)mb_board_run(h->b, want);
        }
        t += dt;
    }
    *audio = h->buf;
    return h->n_buf;
}

/* ---- state ------------------------------------------------------------------------------------------------------- */

MB_API int mbh_get_int(const mb_host *h, const char *name)
{
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "frames")) return h->frames;
    if (!strcmp(name, "fault")) return h->fault;
    return (int)mb_board_get(h->b, name);
}

MB_API void mbh_set_int(mb_host *h, const char *name, int v)
{
    if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
}

MB_API int mbh_writes(const mb_host *h, const mbh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

MB_API void mbh_clear_writes(mb_host *h)
{
    h->n_wl = 0;
}
