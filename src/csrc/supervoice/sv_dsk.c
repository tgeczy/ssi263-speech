/* sv_dsk.c -- see sv_dsk.h.  MIT.
 *
 * Disk BASIC's layout (Tandy's Color Computer Disk System manual; the same as every RS-DOS reader): 68 granules of
 * nine sectors, two to a track, track 17 (the directory's) skipped -- granule g is track g/2, plus one from granule
 * 34 on, sectors 1-9 for an even g and 10-18 for an odd one.  The allocation table's byte for a granule is the next
 * granule (0-67), or C0h + the sectors used in the last one; FFh is free.  A directory entry is 32 bytes: the name (8)
 * and extension (3), space-padded, the type, the ASCII flag, the first granule, and the bytes used in the last sector
 * (big-endian); a first byte of 00h is a deleted entry and FFh ends the directory.
 *
 * SV_DSK_BREAK, the tests' controls (tools/check_sv_voice.py): "track17" maps granules from 34 on onto track 17
 * itself, as if the directory's track were not skipped; "last" takes the whole last sector, not the directory's count.
 * Either way the file comes out wrong and is refused by its sha256.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sv_dsk.h"

#define SECTOR 256
#define PER_TRACK 18
#define DIR_TRACK 17
#define GRANULES 68
#define GRANULE_SECTORS 9

static int fail(char *err, int errlen, const char *why)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s", why);
    return 0;
}

static const unsigned char *sector(const unsigned char *img, size_t n, int track, int s)
{
    size_t at = ((size_t)track * PER_TRACK + (size_t)(s - 1)) * SECTOR;
    return at + SECTOR <= n ? img + at : NULL;
}

static int break_is(const char *what)
{
    const char *b = getenv("SV_DSK_BREAK");
    return b && !strcmp(b, what);
}

/* the first sector of granule g */
static void granule_at(int g, int *track, int *first)
{
    *track = g / 2 + (g >= 34 && !break_is("track17") ? 1 : 0);
    *first = (g & 1) * GRANULE_SECTORS + 1;
}

static int from_dsk(const unsigned char *img, size_t n, unsigned char **out, size_t *out_n, char *err, int errlen)
{
    const unsigned char *fat = sector(img, n, DIR_TRACK, 2), *e = NULL;
    unsigned char *buf;
    size_t len = 0;
    int s, k, g, steps, last_bytes, found = 0, ended = 0;
    if (!fat || n % ((size_t)PER_TRACK * SECTOR) != 0 || n < SV_DSK_SIZE)
        return fail(err, errlen, "not a Color Computer disk image (Disk BASIC's 35 tracks of 18 sectors)");
    for (s = 3; s <= 11 && !found && !ended; s++) {
        const unsigned char *d = sector(img, n, DIR_TRACK, s);
        for (k = 0; k < SECTOR / 32; k++) {
            e = d + 32 * k;
            if (e[0] == 0xFF) { ended = 1; break; }
            if (e[0] && !memcmp(e, "SUPERTOK", 8) && !memcmp(e + 8, "BIN", 3)) { found = 1; break; }
        }
    }
    if (!found)
        return fail(err, errlen, "the disk has no SUPERTOK.BIN (the Super Voice's program)");
    buf = (unsigned char *)malloc((size_t)GRANULES * GRANULE_SECTORS * SECTOR);
    if (!buf)
        return fail(err, errlen, "out of memory");
    last_bytes = (e[14] << 8) | e[15];
    if (last_bytes == 0 || last_bytes > SECTOR || break_is("last"))
        last_bytes = SECTOR;
    for (g = e[13], steps = 0; ; steps++) {
        int track, first, next, count, j;
        if (g >= GRANULES || steps >= GRANULES) {
            free(buf);
            return fail(err, errlen, "SUPERTOK.BIN's granules on the disk are broken");
        }
        next = fat[g];
        count = next >= 0xC0 ? (next & 0x3F) : GRANULE_SECTORS;
        if (count < 1 || count > GRANULE_SECTORS || (next < 0xC0 && next >= GRANULES)) {
            free(buf);
            return fail(err, errlen, "SUPERTOK.BIN's allocation table entry is broken");
        }
        granule_at(g, &track, &first);
        for (j = 0; j < count; j++) {
            const unsigned char *p = sector(img, n, track, first + j);
            int take = next >= 0xC0 && j == count - 1 ? last_bytes : SECTOR;
            if (!p) {
                free(buf);
                return fail(err, errlen, "SUPERTOK.BIN runs past the end of the disk image");
            }
            memcpy(buf + len, p, (size_t)take);
            len += (size_t)take;
        }
        if (next >= 0xC0)
            break;
        g = next;
    }
    if (!svh_is_known(buf, len)) {
        free(buf);
        return fail(err, errlen, "the disk's SUPERTOK.BIN is not the known program (unknown sha256)");
    }
    *out = buf;
    *out_n = len;
    return 1;
}

SV_API int sv_firmware_from(const unsigned char *data, size_t n, unsigned char **out, size_t *out_n, char *err,
                            int errlen)
{
    *out = NULL;
    *out_n = 0;
    if (!data || !n)
        return fail(err, errlen, "no bytes");
    if (svh_is_known(data, n)) {                    /* the program itself, as the disk holds it */
        if (!(*out = (unsigned char *)malloc(n)))
            return fail(err, errlen, "out of memory");
        memcpy(*out, data, n);
        *out_n = n;
        return 1;
    }
    if (n < SV_DSK_SIZE)                            /* too small for a disk: a program, but not the known one */
        return fail(err, errlen, "not Speech Systems' " SV_FILE " (unknown sha256), nor a disk image holding it");
    return from_dsk(data, n, out, out_n, err, errlen);
}

SV_API void sv_free(void *p) { free(p); }
