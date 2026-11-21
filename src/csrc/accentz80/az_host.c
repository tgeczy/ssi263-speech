/* az_host.c -- the Accent SA's board given to the Accent host (az_host.h).  MIT. */
#include "az_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "az_board.h"
#include "../blazie/bl_fopen.h"

/* ---- the board as the host asks for it (as_host.h's ash_board) --------------------------------------------------- */

static void b_destroy(void *b) { az_board_destroy((az_board *)b); }
static void b_serial(void *b) { az_board_serial((az_board *)b); }
static void b_nmi(void *b) { az_board_nmi((az_board *)b); }
static unsigned long long b_run(void *b, unsigned long long n) { return az_board_run((az_board *)b, n); }
static int b_send(void *b, const unsigned char *p, int n) { return az_board_send((az_board *)b, p, n); }
static void b_drop(void *b) { az_board_drop_input((az_board *)b); }
static int b_tx(const void *b, const unsigned char **p) { return az_board_tx((const az_board *)b, p); }
static int b_get(const void *b, const char *name)
{
    if (!strcmp(name, "speaking"))                          /* A/R passed to /NMI: the latch's bit 4 */
        return (az_board_get((const az_board *)b, "latch") & 0x10) ? 1 : 0;
    return az_board_get((const az_board *)b, name);
}
static void b_set(void *b, const char *name, int v) { az_board_set((az_board *)b, name, v); }
static unsigned b_work(void *b) { return az_board_take_work((az_board *)b); }

static const ash_board SA_BOARD = {b_destroy, b_serial, b_nmi, NULL, b_run, b_send, b_drop, b_tx, b_get, b_set, b_work};

AS_API as_host *azh_create(const unsigned char *u5, size_t n5, const unsigned char *u4, size_t n4, ssi263 *chip,
                           double out_rate, char *err, int errlen)
{
    az_chip ops;
    az_board *b;
    as_host *h = ash_create_bare(chip, out_rate, AZ_CPU_HZ, err, errlen);
    if (!h)
        return NULL;
    ops.ctx = h;
    ops.write = ash_chip_write;
    ops.request = ash_chip_request;
    b = az_board_create(u5, n5, u4, n4, &ops, err, errlen);
    if (!b) {
        ash_destroy(h);
        return NULL;
    }
    ash_attach(h, &SA_BOARD, b);
    return h;
}

/* a whole file into memory; its size in *n.  NULL (the reason in err) if it can't be read. */
static unsigned char *slurp(const char *dir, const char *name, size_t *n, char *err, int errlen)
{
    char path[4096];
    unsigned char *d = NULL;
    long len = -1;
    int found;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = bl_fopen(path, "rb");
    found = f != NULL;
    if (f && !fseek(f, 0, SEEK_END))
        len = ftell(f);
    if (f && len >= 0 && !fseek(f, 0, SEEK_SET)) {
        d = (unsigned char *)malloc((size_t)len + 1);
        if (d && fread(d, 1, (size_t)len, f) != (size_t)len) {
            free(d);
            d = NULL;
        }
    }
    if (f)
        fclose(f);
    if (!d) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "%s: %s", name, found ? "cannot read" : "not found");
        return NULL;
    }
    *n = (size_t)len;
    return d;
}

AS_API as_host *azh_create_dir(const char *rom_dir, ssi263 *chip, double out_rate, char *err, int errlen)
{
    size_t n5 = 0, n4 = 0;
    unsigned char *u5 = slurp(rom_dir, AZH_U5_NAME, &n5, err, errlen), *u4 = NULL;
    as_host *h = NULL;
    if (u5)
        u4 = slurp(rom_dir, AZH_U4_NAME, &n4, err, errlen);
    if (u4)
        h = azh_create(u5, n5, u4, n4, chip, out_rate, err, errlen);
    free(u5);
    free(u4);
    return h;
}
