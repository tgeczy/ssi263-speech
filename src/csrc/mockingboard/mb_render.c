/* mb_render.c -- the C API alone: the Mockingboard's firmware file and a text into a WAV, no Python.
 *
 *     mb_render <folder holding mockingboard-tts-1.1.bin> "text" out.wav [--log]
 *
 * 22,050 Hz, 16-bit mono.  The text is said as one mbh_say (at most 254 characters).  --log prints every chip write
 * (time, register, value).  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_host.h"

#define RATE 22050

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 255), f); fputc((int)(v >> 8 & 255), f); fputc((int)(v >> 16 & 255), f); fputc((int)(v >> 24 & 255), f);
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 255), f); fputc((int)(v >> 8 & 255), f);
}

int main(int argc, char **argv)
{
    char err[256] = "";
    mb_host *h;
    short *pcm = NULL;
    long n = 0, cap = 0, k;
    double quiet = 0.0;
    FILE *f;
    int log = argc > 4 && !strcmp(argv[4], "--log");
    if (argc < 4) {
        fprintf(stderr, "usage: mb_render <firmware folder> \"text\" out.wav [--log]\n");
        return 2;
    }
    h = mbh_create_dir(argv[1], NULL, RATE, err, (int)sizeof err);
    if (!h) {
        fprintf(stderr, "mb_render: %s\n", err);
        return 1;
    }
    mbh_set_int(h, "log_writes", log);
    if (!mbh_say(h, (const unsigned char *)argv[2], (int)strlen(argv[2]))) {
        fprintf(stderr, "mb_render: the text was refused (fault %d)\n", mbh_get_int(h, "fault"));
        return 1;
    }
    /* until the firmware is done and 0.2 s of the chip's tail is out (at most 60 s) */
    while (quiet < 0.2 && n < 60L * RATE) {
        const double *a;
        int got = mbh_run(h, 0.03, 0.0005, &a), i;
        if (got < 0)
            return 1;
        if (n + got > cap) {
            cap = (n + got) * 2;
            pcm = (short *)realloc(pcm, (size_t)cap * sizeof *pcm);
            if (!pcm)
                return 1;
        }
        ssi_pcm16(a, got, 1.0, pcm + n);
        n += got;
        (void)i;
        quiet = mbh_busy(h) ? 0.0 : quiet + 0.03;
    }
    if (log) {
        const mbh_write *w;
        int c = mbh_writes(h, &w), i;
        for (i = 0; i < c; i++)
            printf("%.6f R%d=%02X\n", w[i].t, w[i].reg, w[i].val);
    }
    fprintf(stderr, "frames %d, %.2f s, irqs %d, decimal %d, undocumented %d\n", mbh_get_int(h, "frames"),
            (double)n / RATE, mbh_get_int(h, "vector_reads"), mbh_get_int(h, "decimal"), mbh_get_int(h, "undocumented"));
    f = fopen(argv[3], "wb");
    if (!f)
        return 1;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + (unsigned long)n * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, RATE * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (unsigned long)n * 2);
    for (k = 0; k < n; k++)
        put16(f, (unsigned)(unsigned short)pcm[k]);
    fclose(f);
    free(pcm);
    mbh_destroy(h);
    return 0;
}
