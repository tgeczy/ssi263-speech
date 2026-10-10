/* mb_dsk.c -- see mb_dsk.h.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_dsk.h"

/* the sets of files a firmware file is made of (mb_host.h): the toolkit's 1.1, then the earlier one of disks 1-2 */
typedef struct {
    int n;
    const char *names[6];
    unsigned addrs[6];
} fileset;
static const fileset SETS[2] = {
    {6, {"TEXT TO SPEECH", "INFLECTION", "IIE TTS DRIVER", "MKB:RULE.INDEX", "MKB:RULE.LENGTH", "MKB:RULE.TABLE"},
     {0x8C00, 0x9000, 0x9300, 0xD000, 0xD100, 0xD200}},
    {4, {"TEXT TO SPEECH", "MKB:RULE.INDEX", "MKB:RULE.LENGTH", "MKB:RULE.TABLE"},
     {0x6600, 0x6E00, 0x6F00, 0x7000}},
};
/* ProDOS block k of a track (0-7): the DOS 3.3 sectors of its two halves */
static const int PRODOS_HALVES[8][2] = {{0, 14}, {13, 12}, {11, 10}, {9, 8}, {7, 6}, {5, 4}, {3, 2}, {1, 15}};

typedef struct {
    const unsigned char *d;
    int prodos;                            /* the image is in ProDOS order */
} disk;

static const unsigned char *sector(const disk *k, int t, int s)
{
    int b, h;
    if (t < 0 || t > 34 || s < 0 || s > 15)
        return NULL;
    if (!k->prodos)
        return k->d + (t * 16 + s) * 256;
    for (b = 0; b < 8; b++)
        for (h = 0; h < 2; h++)
            if (PRODOS_HALVES[b][h] == s)
                return k->d + (t * 8 + b) * 512 + h * 256;
    return NULL;
}

static int is_dos33(const disk *k)
{
    const unsigned char *v = sector(k, 17, 0);
    return v && v[3] == 3 && v[0x27] == 122 && v[1] <= 34 && v[2] <= 15;
}

/* the catalog entry named `name`: its type and first track/sector list; 0 if none */
static int find(const disk *k, const char *name, int *type, int *t0, int *s0)
{
    const unsigned char *v = sector(k, 17, 0);
    int t = v[1], s = v[2], guard = 0, i, j;
    while (t && guard++ < 64) {
        const unsigned char *c = sector(k, t, s);
        if (!c)
            return 0;
        for (i = 0; i < 7; i++) {
            const unsigned char *e = c + 0x0B + i * 35;
            char nm[31];
            if (e[0] == 0 || e[0] == 0xFF)
                continue;
            for (j = 0; j < 30; j++)
                nm[j] = (char)(e[3 + j] & 0x7F);
            nm[30] = 0;
            for (j = 29; j >= 0 && nm[j] == ' '; j--)
                nm[j] = 0;
            if (!strcmp(nm, name)) {
                *type = e[2] & 0x7F;
                *t0 = e[0];
                *s0 = e[1];
                return 1;
            }
        }
        t = c[1];
        s = c[2];
    }
    return 0;
}

/* a binary file as DOS stores it (address, length, bytes) appended to out; its length, or -1 */
static long binary_file(const disk *k, int t, int s, unsigned char *out, long room)
{
    unsigned char *buf = (unsigned char *)malloc(MB_DSK_SIZE);
    long got = 0, need, guard = 0;
    int i;
    if (!buf)
        return -1;
    while (t && guard++ < 64) {
        const unsigned char *ts = sector(k, t, s);
        if (!ts)
            break;
        for (i = 0; i < 122; i++) {
            const unsigned char *d = sector(k, ts[0x0C + 2 * i], ts[0x0D + 2 * i]);
            if ((ts[0x0C + 2 * i] || ts[0x0D + 2 * i]) && d && got + 256 <= MB_DSK_SIZE) {
                memcpy(buf + got, d, 256);
                got += 256;
            }
        }
        t = ts[1];
        s = ts[2];
    }
    need = got >= 4 ? 4 + (buf[2] | buf[3] << 8) : -1;
    if (need < 0 || need > got || need > room) {
        free(buf);
        return -1;
    }
    memcpy(out, buf, (size_t)need);
    free(buf);
    return need;
}

static int fail(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", msg);
    return 0;
}

enum { R_OK, R_MISSING, R_DAMAGED, R_OTHER };

/* a set's files read in one sector order, into res; R_OK (a known firmware file), or why not */
static int read_set(const disk *k, const fileset *set, unsigned char *res, long *at)
{
    int f;
    *at = 0;
    for (f = 0; f < set->n; f++) {
        int type, t, s;
        long len;
        if (!find(k, set->names[f], &type, &t, &s) || type != 4)
            return R_MISSING;
        len = binary_file(k, t, s, res + *at, 64 * 1024 - *at);
        if (len < 4)
            return R_DAMAGED;
        if ((unsigned)(res[*at] | res[*at + 1] << 8) != set->addrs[f])
            return R_OTHER;                /* there, but loading elsewhere: another version of the text-to-speech */
        *at += len;
    }
    return mbh_variant(res, (size_t)*at) ? R_OK : R_OTHER;
}

MB_API int mb_firmware_from_dsk(const unsigned char *image, size_t n, unsigned char **out, size_t *out_n, char *err,
                                int errlen)
{
    static const char *const WHY[] = {"", "not the Mockingboard Developers Toolkit (a file is missing)",
                                      "not the Mockingboard Developers Toolkit (a file is damaged)",
                                      "a Mockingboard disk, but not one of the text-to-speech versions these voices "
                                      "run (1.1, or the earlier one of disks 1 and 2)"};
    disk k;
    unsigned char *res;
    long at = 0;
    int order, set, best = R_MISSING, any = 0;
    *out = NULL;
    *out_n = 0;
    if (n != MB_DSK_SIZE)
        return fail(err, errlen, "not a 140 KB Apple II disk image");
    res = (unsigned char *)malloc(64 * 1024);
    if (!res)
        return fail(err, errlen, "out of memory");
    k.d = image;
    /* DOS order first, then ProDOS order: the catalog's first sector sits at the same place in both, so the order is
       the one whose files read */
    for (order = 0; order < 2; order++) {
        k.prodos = order;
        if (!is_dos33(&k))
            continue;
        any = 1;
        for (set = 0; set < 2; set++) {
            int r = read_set(&k, &SETS[set], res, &at);
            if (r == R_OK) {
                *out = res;
                *out_n = (size_t)at;
                return 1;
            }
            if (r > best || best == R_MISSING)
                best = r;
        }
    }
    free(res);
    return fail(err, errlen, any ? WHY[best] : "not a DOS 3.3 disk");
}
