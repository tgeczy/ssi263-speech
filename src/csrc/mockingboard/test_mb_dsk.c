/* test_mb_dsk.c -- the firmware file from a disk image (mb_dsk.h), on real disk images (not in the repository).
 *
 *     test_mb_dsk <the Developers Toolkit .dsk> <another Mockingboard .dsk (the Mockingboard C disk)>
 *
 *   dos_order      the toolkit image as it comes (DOS order): the known firmware file
 *   prodos_order   the same image rewritten in ProDOS order (.po): the same file
 *   other_version  the Mockingboard C disk (an earlier text-to-speech): refused, "not the ... version 1.1"
 *   damaged        the toolkit with one byte of its rule table changed: refused
 *   wrong_size     the image less one sector: refused
 *
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit 0/1 (77 when the
 * images are not given or not there).  MIT.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_dsk.h"

static int failures;

static void check(const char *name, int ok, const char *fmt, ...)
{
    va_list ap;
    printf("%s %s ", ok ? "ok" : "FAIL", name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!ok)
        failures++;
}

static unsigned char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d = NULL;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (*n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (d = (unsigned char *)malloc((size_t)*n)) != NULL && fread(d, 1, (size_t)*n, f) != (size_t)*n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    return d;
}

static const int HALVES[8][2] = {{0, 14}, {13, 12}, {11, 10}, {9, 8}, {7, 6}, {5, 4}, {3, 2}, {1, 15}};

static void to_prodos(const unsigned char *dos, unsigned char *po)
{
    int t, b, h;
    for (t = 0; t < 35; t++)
        for (b = 0; b < 8; b++)
            for (h = 0; h < 2; h++)
                memcpy(po + (t * 8 + b) * 512 + h * 256, dos + (t * 16 + HALVES[b][h]) * 256, 256);
}

int main(int argc, char **argv)
{
    long n1 = 0, n2 = 0;
    unsigned char *a, *b, *out = NULL, *po;
    size_t on = 0;
    char err[256];
    unsigned char probe[32];
    int ok, i, changed = 0, have_probe = 0;
    if (argc < 3 || !(a = slurp(argv[1], &n1)) || !(b = slurp(argv[2], &n2))) {
        printf("skipped: the disk images are not given or not there\n");
        return 77;
    }
    ok = mb_firmware_from_dsk(a, (size_t)n1, &out, &on, err, (int)sizeof err);
    check("dos_order", ok && on == 11948 && mbh_is_known(out, on), "%s, %lu bytes", ok ? "the known file" : err,
          (unsigned long)on);
    if (ok) {                              /* 32 bytes from inside MKB:RULE.TABLE, to find on the disk later */
        memcpy(probe, out + on - 2000, sizeof probe);
        have_probe = 1;
    }
    free(out);
    po = (unsigned char *)malloc((size_t)n1);
    to_prodos(a, po);
    ok = mb_firmware_from_dsk(po, (size_t)n1, &out, &on, err, (int)sizeof err);
    check("prodos_order", ok && on == 11948 && mbh_is_known(out, on), "%s", ok ? "the known file" : err);
    free(out);
    free(po);
    err[0] = 0;
    ok = mb_firmware_from_dsk(b, (size_t)n2, &out, &on, err, (int)sizeof err);
    check("other_version", !ok && (strstr(err, "version 1.1") || strstr(err, "missing")), "%s",
          ok ? "accepted" : err);
    free(out);
    /* one byte inside MKB:RULE.TABLE, found on the disk by 32 of its bytes, changed */
    for (i = 0; have_probe && i + (int)sizeof probe <= n1 && !changed; i++)
        if (!memcmp(a + i, probe, sizeof probe)) {
            a[i + 10] ^= 0x01;
            changed = 1;
        }
    err[0] = 0;
    ok = mb_firmware_from_dsk(a, (size_t)n1, &out, &on, err, (int)sizeof err);
    check("damaged", changed && !ok && strstr(err, "version 1.1"), "%s", !changed ? "the table not found on the disk"
          : ok ? "accepted" : err);
    free(out);
    err[0] = 0;
    ok = mb_firmware_from_dsk(a, (size_t)n1 - 256, &out, &on, err, (int)sizeof err);
    check("wrong_size", !ok && strstr(err, "140 KB"), "%s", ok ? "accepted" : err);
    free(out);
    free(a);
    free(b);
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
