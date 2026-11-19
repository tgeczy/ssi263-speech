/* sv_host.c -- the host of Speech Systems' Super Voice (sv_host.h).  MIT.
 */
#include "sv_host.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpu/cpu.h"

#define PROG_LO 0x5800u                    /* SUPERTOK's one LOADM segment: the program, then its dictionary (7400h) */
#define PROG_HI 0x7FF0u
#define ENTRY 0x5805u                      /* "multi-line input": one line spoken, then back (the manual, p.19) */
#define CHIP_LO 0xFF80u                    /* the SSI-263's five registers */
#define CHIP_HI 0xFF84u
/* the host's choices (sv_host.h): any RAM the program does not use would do */
#define TEXT_AT 0x3000u
#define DESC_AT 0x3100u
#define STACK_TOP 0x3FFEu
#define RET_AT 0x0002u                     /* the return address the call finds on its stack; nothing runs there */

#define START_LIMIT 5000000ULL             /* E cycles a text may run before its first wait: 5.6 s on the machine
                                              (the most seen, a fresh program and 240 characters: under 0.2 s) */
#define WAIT_LIMIT 4474431ULL              /* 5 s without a write to the chip while speaking: its longest phoneme
                                              (rate 0, duration 00) is under a third of a second */
#define REST_S 0.35                        /* longer than any pause lasts: ssi263_skip stops at the pause's end (rest) */

struct sv_host {
    uint8_t mem[65536];
    uint8_t loaded[65536];                 /* as svh_create left it: what every text starts from */
    m6809 *cpu;
    ssi263 *chip;
    int own_chip;
    double out_rate;
    uint64_t clock;                        /* the time, in E cycles, the 6809 has reached */
    double until;                          /* the time svh_run has been asked for, in E cycles */
    uint64_t made;                         /* the samples the chip has made */
    uint64_t last_write;                   /* clock at the last chip write */
    int active;                            /* the call has not returned */
    int cut;                               /* the last call was left part way: the next text rests the chip first */
    int waited;                            /* it has read the chip's status and found no request */
    int phonemes;                          /* R0 writes of this call: its opening's pause, then the text's */
    int skip_start;
    int fault;
    uint64_t start_cycles;
    double *buf;
    int n_buf, cap_buf;
    int oom;
    int log_on;
    svh_write *wl;
    int n_wl, cap_wl;
};

/* ---- the chip, in step ------------------------------------------------------------------------------------------- */

/* the chip brought up to the 6809's time: its audio appended */
static void chip_sync(sv_host *h)
{
    uint64_t want = (uint64_t)((double)h->clock / SVH_E_HZ * h->out_rate);
    long due;
    if (want <= h->made)
        return;
    due = (long)(want - h->made);
    if (h->n_buf + due + 1 > h->cap_buf) {
        int cap = h->cap_buf ? h->cap_buf : 4096;
        double *b;
        while (cap < h->n_buf + due + 1)
            cap *= 2;
        b = (double *)realloc(h->buf, (size_t)cap * sizeof(double));
        if (!b) {
            h->oom = 1;
            return;
        }
        h->buf = b;
        h->cap_buf = cap;
    }
    due = ssi263_run(h->chip, due, h->buf + h->n_buf);
    h->n_buf += (int)due;
    h->made += (uint64_t)due;
}

static void chip_write(sv_host *h, int reg, int val)
{
    chip_sync(h);
    /* Until the text's first phoneme its pitch lands at once: the opening's R1 and R2, and a pitch code at the
       text's head.  The chip glides from where its counter was -- the bottom of the range on a new chip -- and the
       first word climbed there (heard, 2026-11-19: "the first sound starts at a lower pitch, then it glides up", as
       the Mockingboard's did before the same remedy, mb_host.c).  A HOST feature (sv_host.h). */
    if (reg == 0 && ++h->phonemes == 2)
        ssi263_set_snap_pitch(h->chip, 0);
    else if (h->phonemes < 2 && (reg == 1 || reg == 2))
        ssi263_set_snap_pitch(h->chip, 1);
    ssi263_write(h->chip, reg, val);
    h->last_write = h->clock;
    if (h->log_on) {
        if (h->n_wl == h->cap_wl) {
            int cap = h->cap_wl ? h->cap_wl * 2 : 1024;
            svh_write *w = (svh_write *)realloc(h->wl, (size_t)cap * sizeof *w);
            if (w) { h->wl = w; h->cap_wl = cap; }
        }
        if (h->n_wl < h->cap_wl) {
            h->wl[h->n_wl].t = ssi263_time(h->chip);
            h->wl[h->n_wl].reg = reg;
            h->wl[h->n_wl].val = val;
            h->n_wl++;
        }
    }
}

/* ---- the machine: RAM and the chip, nothing else ----------------------------------------------------------------- */

static uint8_t bus_read(void *ctx, uint32_t a)
{
    sv_host *h = (sv_host *)ctx;
    a &= 0xFFFF;
    if (a >= CHIP_LO && a <= CHIP_HI) {
        int request;
        chip_sync(h);
        request = ssi263_request(h->chip);
        if (!request)
            h->waited = 1;
        return request ? 0x80 : 0x00;        /* D7 "When read, high is an active request for new data" (Byte,
                                                March 1984, the SSI 263's pin list); the program waits on BPL */
    }
    if (a >= 0xFF00)
        return 0;                            /* the PIAs, the SAM, the vectors: not here (the core's don't-care
                                                cycles read FFFFh) */
    return h->mem[a];
}

static void bus_write(void *ctx, uint32_t a, uint8_t v)
{
    sv_host *h = (sv_host *)ctx;
    a &= 0xFFFF;
    if (a >= CHIP_LO && a <= CHIP_HI) {
        chip_write(h, (int)(a - CHIP_LO), v);
        return;
    }
    if (a >= 0x8000)
        return;                              /* ROM's place and the I/O page: the program's three PIA writes */
    h->mem[a] = v;
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

SV_API int svh_is_known(const unsigned char *image, size_t n)
{
    static const char X[] = "0123456789abcdef";
    unsigned char sum[32];
    int k;
    sha256(image, n, sum);
    for (k = 0; k < 32; k++)
        if (SV_SHA256[2 * k] != X[sum[k] >> 4] || SV_SHA256[2 * k + 1] != X[sum[k] & 15])
            return 0;
    return 1;
}

/* the LOADM file's segments placed (Color BASIC's format: 00, length, address, the bytes; FF, 0000, the entry);
   1 when it is the one segment the host expects */
static int place(sv_host *h, const unsigned char *image, size_t n)
{
    size_t at = 0;
    int segments = 0;
    while (at + 5 <= n) {
        unsigned len = (unsigned)image[at + 1] << 8 | image[at + 2];
        unsigned addr = (unsigned)image[at + 3] << 8 | image[at + 4];
        if (image[at] == 0xFF)
            return segments == 1 && len == 0 && at + 5 == n;
        if (image[at] != 0x00 || addr != PROG_LO || addr + len - 1 != PROG_HI || at + 5 + len > n)
            return 0;
        memcpy(h->mem + addr, image + at + 5, len);
        at += 5 + len;
        segments++;
    }
    return 0;
}

/* ---- creation ---------------------------------------------------------------------------------------------------- */

static void fail(char *err, int errlen, const char *why)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", why);
}

SV_API sv_host *svh_create(const unsigned char *image, size_t n, ssi263 *chip, double out_rate, char *err, int errlen)
{
    cpu_bus bus;
    sv_host *h;
    if (!svh_is_known(image, n)) {
        fail(err, errlen, "not Speech Systems' " SV_FILE " (unknown sha256)");
        return NULL;
    }
    h = (sv_host *)calloc(1, sizeof *h);
    if (!h) {
        fail(err, errlen, "out of memory");
        return NULL;
    }
    if (!place(h, image, n)) {
        free(h);
        fail(err, errlen, SV_FILE ": not one LOADM segment at 5800h");
        return NULL;
    }
    memcpy(h->loaded, h->mem, sizeof h->loaded);
    if (!chip) {
        ssi263_params p;
        ssi263_default_params(&p);
        chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
        if (!chip) {
            free(h);
            fail(err, errlen, "ssi263_new failed");
            return NULL;
        }
        h->own_chip = 1;
    }
    ssi263_set_xck(chip, SVH_E_HZ);          /* a fresh chip: exact */
    h->chip = chip;
    h->out_rate = out_rate;
    h->skip_start = 1;
    memset(&bus, 0, sizeof bus);
    bus.ctx = h;
    bus.read = bus_read;
    bus.write = bus_write;
    h->cpu = m6809_create(&bus, SVH_E_HZ);
    if (!h->cpu) {
        if (h->own_chip)
            ssi263_free(chip);
        free(h);
        fail(err, errlen, "m6809_create failed");
        return NULL;
    }
    return h;
}

SV_API sv_host *svh_create_dir(const char *dir, ssi263 *chip, double out_rate, char *err, int errlen)
{
    char path[4096];
    FILE *f;
    long len;
    unsigned char *d;
    sv_host *h;
    snprintf(path, sizeof path, "%s/%s", dir, SV_FILE);
    f = fopen(path, "rb");
    if (!f) {
        fail(err, errlen, SV_FILE ": cannot open");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)
        || !(d = (unsigned char *)malloc(len ? (size_t)len : 1))) {
        fclose(f);
        fail(err, errlen, SV_FILE ": cannot read");
        return NULL;
    }
    if (fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        fclose(f);
        fail(err, errlen, SV_FILE ": cannot read");
        return NULL;
    }
    fclose(f);
    h = svh_create(d, (size_t)len, chip, out_rate, err, errlen);
    free(d);
    return h;
}

SV_API void svh_destroy(sv_host *h)
{
    if (!h)
        return;
    m6809_destroy(h->cpu);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->buf);
    free(h->wl);
    free(h);
}

SV_API ssi263 *svh_chip(sv_host *h)
{
    return h->chip;
}

/* ---- speaking ---------------------------------------------------------------------------------------------------- */

SV_API int svh_busy(const sv_host *h)
{
    return h->active;
}

/* one instruction of the call; its E cycles (0: the call returned, or left its code) */
static int step(sv_host *h)
{
    uint32_t pc = m6809_next_pc(h->cpu);
    if (pc == RET_AT) {
        h->active = 0;
        return 0;
    }
    if (pc < PROG_LO || pc > PROG_HI) {
        h->fault = SVH_FAULT_LEFT;
        h->active = 0;
        return 0;
    }
    return m6809_step(h->cpu);
}

/* After a cut, before the next text: the chip brought to rest.  The cut phoneme is still in the chip, silent only
   because cancel took the amplitude away; the program's opening gives the amplitude back, and the chip does not
   request until that phoneme's time is up.  MEASURED (35 cut times over a sentence, the next text 20 ms later): its
   first phoneme came up to 82 ms late at the default rate, the old phoneme sounding before it at up to 0.52 of full
   scale.  So the chip is put through CTL 1 and 0 with a pause, and its time run on, unheard, to that pause's end
   (ssi263_skip stops there): the chip is requesting and at rest, and the next text's first phoneme is written
   at once.  MEASURED (72 cut times, the next text at once): its level over its first 80 ms is the same at every
   cut, whatever was cut, and under 0.03 of full scale as on a new chip.  A HOST feature (sv_host.h). */
static void rest(sv_host *h)
{
    int r3 = ssi263_reg(h->chip, 3) & 0x70;      /* cancel left the amplitude 0 */
    chip_sync(h);
    ssi263_write(h->chip, 3, 0x80 | r3);         /* CTL 1: whatever the chip was holding is dropped */
    ssi263_write(h->chip, 0, 0xC0);              /* the mode the program uses, and a pause */
    ssi263_write(h->chip, 3, r3);                /* CTL 0 */
    ssi263_write(h->chip, 0, 0xC0);              /* the pause loaded */
    ssi263_skip(h->chip, REST_S);
    h->cut = 0;
}

SV_API int svh_say(sv_host *h, const unsigned char *text, int n)
{
    m6809_regs r;
    uint64_t start;
    if (n <= 0 || n > SVH_MAX_TEXT || h->active)
        return 0;
    h->fault = 0;
    if (h->cut)
        rest(h);
    memcpy(h->mem, h->loaded, sizeof h->mem);                /* every text finds the program as loaded (sv_host.h) */
    memcpy(h->mem + TEXT_AT, text, (size_t)n);
    h->mem[DESC_AT] = (uint8_t)n;                            /* the descriptor: length, a byte, address, a byte */
    h->mem[DESC_AT + 2] = TEXT_AT >> 8;
    h->mem[DESC_AT + 3] = TEXT_AT & 0xFF;
    h->mem[STACK_TOP] = RET_AT >> 8;
    h->mem[STACK_TOP + 1] = RET_AT & 0xFF;
    m6809_regs_get(h->cpu, &r);
    r.pc = ENTRY;
    r.s = STACK_TOP;
    r.x = DESC_AT;
    r.a = 0xFF;                                              /* USR's argument is a string */
    r.b = 0;
    r.u = r.y = 0;
    r.dp = 0;
    r.cc = 0;
    r.nmi_armed = 1;
    m6809_regs_set(h->cpu, &r);
    h->active = 1;
    h->waited = 0;
    h->phonemes = 0;
    h->last_write = h->clock;
    /* up to its first wait for the chip: the chip's time stands still (sv_host.h) */
    start = m6809_cycles(h->cpu);
    while (h->active && !h->waited && m6809_cycles(h->cpu) - start < START_LIMIT) {
        int c = step(h);
        if (!h->skip_start)
            h->clock += (uint64_t)c;
    }
    h->start_cycles = m6809_cycles(h->cpu) - start;
    if (!h->skip_start && h->until < (double)h->clock)
        h->until = (double)h->clock;                         /* that time has passed: svh_run goes on from it */
    if (h->active && !h->waited) {
        h->fault = SVH_FAULT_STUCK;
        h->active = 0;
    }
    return h->fault == 0;
}

SV_API void svh_cancel(sv_host *h)
{
    if (!h->active)
        return;
    h->active = 0;
    h->cut = 1;
    /* the program's own end (5976h): R3 with the amplitude 0, R4 = FFh */
    chip_write(h, 3, ssi263_reg(h->chip, 3) & 0x70);
    chip_write(h, 4, 0xFF);
}

SV_API int svh_run(sv_host *h, double seconds, const double **audio)
{
    h->n_buf = 0;
    h->oom = 0;
    h->until += seconds * SVH_E_HZ;
    while ((double)h->clock < h->until) {
        if (!h->active) {
            h->clock = (uint64_t)h->until;
            break;
        }
        h->clock += (uint64_t)step(h);
        if (h->active && h->clock - h->last_write > WAIT_LIMIT) {
            h->fault = SVH_FAULT_STUCK;
            svh_cancel(h);
        }
    }
    chip_sync(h);
    *audio = h->buf;
    return h->oom ? -1 : h->n_buf;
}

/* ---- state ------------------------------------------------------------------------------------------------------- */

SV_API int svh_get_int(const sv_host *h, const char *name)
{
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "skip_start")) return h->skip_start;
    if (!strcmp(name, "fault")) return h->fault;
    if (!strcmp(name, "start_cycles")) return (int)h->start_cycles;
    if (!strcmp(name, "undefined")) {
        m6809_regs r;
        m6809_regs_get(h->cpu, &r);
        return (int)r.undefined + (r.free_run || r.fault ? 1 : 0);
    }
    if (!strcmp(name, "pitch_gap")) {        /* the chip's pitch counter against its target, rounded */
        double st[64];
        ssi263_state(h->chip, st, 64);
        return (int)floor(fabs(st[2] - st[3]) + 0.5);
    }
    if (!strncmp(name, "mem:", 4))
        return h->mem[strtol(name + 4, NULL, 16) & 0xFFFF];
    return -1;
}

SV_API void svh_set_int(sv_host *h, const char *name, int v)
{
    if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
    else if (!strcmp(name, "skip_start"))
        h->skip_start = v != 0;
}

SV_API int svh_writes(const sv_host *h, const svh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

SV_API void svh_clear_writes(sv_host *h)
{
    h->n_wl = 0;
}
