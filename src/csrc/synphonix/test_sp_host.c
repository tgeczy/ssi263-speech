/* test_sp_host.c -- the C SynPhonix host driven as tools/check_sp_host.py drives src/hosts/synphonix.py: boot (VEST's
 * own announcement), then each line of text said and run (0.05 s at a time) until the card is idle; every chip write
 * printed as "W <chip time> <reg> <value>", each line's end as "E <writes so far>", and a fault as "FAULT <reason>".
 * A line "^X" (the two characters) is the host's cancel instead, after 0.4 s of the text before it.
 *
 *     test_sp_host VEST.COM "text" ["text" ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sp_host.h"

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d = NULL;
    long len;
    *n = 0;
    if (!f)
        return NULL;
    if (!fseek(f, 0, SEEK_END) && (len = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET)
            && (d = (unsigned char *)malloc((size_t)len)) && fread(d, 1, (size_t)len, f) == (size_t)len)
        *n = (size_t)len;
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    char err[300];
    size_t n_com;
    unsigned char *com;
    sp_host *h;
    const sph_write *wl;
    int i, k, n, printed = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: test_sp_host VEST.COM text...\n");
        return 2;
    }
    com = slurp(argv[1], &n_com);
    h = sph_create(com, n_com, NULL, 44100.0, err, (int)sizeof err);
    free(com);
    if (!h) {
        printf("FAULT %s\n", err);
        return 1;
    }
    sph_set_int(h, "log_writes", 1);
    sph_boot(h, SPH_BOOT_LIMIT);
    for (i = 2; i < argc && !sph_fault(h); i++) {
        const double *y;
        if (!strcmp(argv[i], "^X")) {
            sph_cancel(h);
        } else {
            sph_say(h, (const unsigned char *)argv[i], (int)strlen(argv[i]), SPH_SAY_CX, SPH_SAY_DX);
            if (i + 1 < argc && !strcmp(argv[i + 1], "^X")) {
                sph_run(h, 0.4, 0.0005, &y);            /* the text before a cancel: 0.4 s of it */
            } else {
                sph_run(h, 0.05, 0.0005, &y);
                for (k = 0; k < 400 && sph_busy(h, 0.05) && !sph_fault(h); k++)
                    sph_run(h, 0.05, 0.0005, &y);
            }
        }
        n = sph_writes(h, &wl);
        for (; printed < n; printed++)
            printf("W %.9f %d %02X\n", wl[printed].t, wl[printed].reg, wl[printed].val);
        printf("E %d\n", n);
    }
    if (sph_fault(h))
        printf("FAULT %s\n", sph_error(h));
    sph_destroy(h);
    return 0;
}
