/* as_render.c -- the Accent-PC reads a text into a WAV file, with no Python: the ROMs from a folder, the chip from the
 * built-in defaults (as_host.h).  A demonstration of the C API and a smoke test.
 *
 *   as_render <folder with u2.BIN u3.BIN u4.BIN> "text" out.wav [sample rate, default 44100]
 *
 * As accent_pc.py's __main__: power-up (the greeting flushed), the text and a carriage return, then 50 ms blocks until
 * the Accent is no longer busy.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as_host.h"

static void le(FILE *f, unsigned long v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        fputc((int)(v >> (8 * i)) & 0xFF, f);
}

int main(int argc, char **argv)
{
    char err[256];
    double rate = argc > 4 ? atof(argv[4]) : 44100.0;
    as_host *h;
    const double *y;
    double *all = NULL;
    long n_all = 0, cap = 0, i;
    int n, blocks = 0;
    short *pcm;
    unsigned char *text;
    size_t len;
    FILE *f;
    if (argc < 4) {
        fprintf(stderr, "usage: as_render <rom folder> \"text\" out.wav [rate]\n");
        return 2;
    }
    h = ash_create_dir(argv[1], NULL, rate, err, (int)sizeof err);
    if (!h) {
        fprintf(stderr, "as_render: %s\n", err);
        return 1;
    }
    ash_boot(h, 4.0);
    len = strlen(argv[2]);
    text = (unsigned char *)malloc(len + 1);
    memcpy(text, argv[2], len);
    text[len] = '\r';
    ash_say(h, text, (int)len + 1, -1);
    free(text);
    do {
        n = ash_run(h, 0.05, 0.0005, &y);
        if (n < 0)
            break;
        if (n_all + n > cap) {
            cap = (n_all + n) * 2;
            all = (double *)realloc(all, (size_t)cap * sizeof(double));
            if (!all)
                return 1;
        }
        memcpy(all + n_all, y, (size_t)n * sizeof(double));
        n_all += n;
        blocks++;
    } while (ash_busy(h, 0.03, 1.5) && blocks < 20 * 120);
    pcm = (short *)malloc((size_t)(n_all ? n_all : 1) * sizeof(short));
    ssi_pcm16(all, (int)n_all, 1.0, pcm);
    f = fopen(argv[3], "wb");
    if (!f) {
        fprintf(stderr, "as_render: cannot write %s\n", argv[3]);
        return 1;
    }
    fwrite("RIFF", 1, 4, f);
    le(f, 36 + 2 * (unsigned long)n_all, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    le(f, 16, 4);
    le(f, 1, 2);
    le(f, 1, 2);
    le(f, (unsigned long)rate, 4);
    le(f, 2 * (unsigned long)rate, 4);
    le(f, 2, 2);
    le(f, 16, 2);
    fwrite("data", 1, 4, f);
    le(f, 2 * (unsigned long)n_all, 4);
    for (i = 0; i < n_all; i++)
        le(f, (unsigned short)pcm[i], 2);
    fclose(f);
    printf("%.2f s of audio in %s\n", n_all / rate, argv[3]);
    free(pcm);
    free(all);
    ash_destroy(h);
    return 0;
}
