/* az_trace.c -- what the Accent SA writes to its chip, and when: the two ROMs from files, a text down the serial line,
 * every register write with its chip time.  For checks (test_az_firmware.py) and for laying a recording of a real
 * unit beside the same text.  The C API alone, no Python.
 *
 *   az_trace [options] <U5 file> <U4 file> ["text"]
 *     --greeting     do not flush the power-up greeting: let the unit say it (the trace then starts at power-up)
 *     --block S      the size of the host's blocks, seconds (default 0.05)
 *     --tail S       how long to go on after the unit is no longer busy (default 0.5)
 *     --watch        after each block a line "B <seconds> busy <0|1> speaking <0|1> work <instructions>": what a voice
 *                sees when it decides whether the text is done (../accentsa/as_voice.c)
 *     --wav FILE     the audio too, 16-bit mono
 *     --rate HZ      its sample rate (default 44100)
 *
 * One line a write, "W <seconds, 6 places> R<register> <value, 2 hex digits>"; then "END <seconds> writes <n> tx <n>
 * unmapped <in> <out> nmi <edges>".  Times are the chip's own, from power-up: a write lands where the chip then is,
 * within the host's slice (0.5 ms, less while a phoneme is asking for the next).  Without --greeting the unit is
 * booted first as the voices do (ash_boot: the greeting flushed) and the trace starts with the text.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "az_host.h"

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d;
    long len;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (unsigned char *)malloc((size_t)len + 1);
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return d;
}

static void le(FILE *f, unsigned long v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        fputc((int)(v >> (8 * i)) & 0xFF, f);
}

static double *all;
static long n_all, cap_all;

static int keep(const double *y, int n)
{
    if (n_all + n > cap_all) {
        cap_all = (n_all + n) * 2;
        all = (double *)realloc(all, (size_t)cap_all * sizeof(double));
        if (!all)
            return 0;
    }
    memcpy(all + n_all, y, (size_t)n * sizeof(double));
    n_all += n;
    return 1;
}

static long n_writes;

static void drain(as_host *h)
{
    const ash_write *w;
    int i, n = ash_writes(h, &w);
    for (i = 0; i < n; i++)
        printf("W %.6f R%d %02X\n", w[i].t, w[i].reg, w[i].val);
    n_writes += n;
    ash_clear_writes(h);
}

int main(int argc, char **argv)
{
    char err[256];
    const char *files[2] = {NULL, NULL}, *text = NULL, *wav = NULL;
    double block = 0.05, tail = 0.5, rate = 44100.0, quiet = 0.0;
    int greeting = 0, watch = 0, i, n_files = 0, blocks = 0;
    unsigned char *u5, *u4;
    size_t n5 = 0, n4 = 0;
    as_host *h;
    const double *y;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--greeting")) greeting = 1;
        else if (!strcmp(argv[i], "--watch")) watch = 1;
        else if (!strcmp(argv[i], "--block") && i + 1 < argc) block = atof(argv[++i]);
        else if (!strcmp(argv[i], "--tail") && i + 1 < argc) tail = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (n_files < 2) files[n_files++] = argv[i];
        else text = argv[i];
    }
    if (n_files < 2 || block <= 0.0) {
        fprintf(stderr, "usage: az_trace [--greeting] [--block S] [--tail S] [--wav FILE] [--rate HZ] <U5> <U4> [\"text\"]\n");
        return 2;
    }
    u5 = slurp(files[0], &n5);
    u4 = slurp(files[1], &n4);
    if (!u5 || !u4) {
        fprintf(stderr, "az_trace: cannot read %s\n", u5 ? files[1] : files[0]);
        return 1;
    }
    h = azh_create(u5, n5, u4, n4, NULL, rate, err, (int)sizeof err);
    free(u5);
    free(u4);
    if (!h) {
        fprintf(stderr, "az_trace: %s\n", err);
        return 1;
    }
    if (!greeting)
        ash_boot(h, 4.0);
    ash_set_int(h, "log_writes", 1);
    if (text) {
        size_t len = strlen(text);
        unsigned char *line = (unsigned char *)malloc(len + 1);
        memcpy(line, text, len);
        line[len] = '\r';
        ash_say(h, line, (int)len + 1, -1);
        free(line);
    }
    while (blocks++ < 200000) {            /* until it has been idle for `tail` */
        int n = ash_run(h, block, 0.0005, &y);
        if (n < 0 || (wav && !keep(y, n)))
            return 1;
        drain(h);
        if (watch)
            printf("B %.6f busy %d speaking %d work %d\n", ash_get_double(h, "time"), ash_busy(h, 0.03, 1.5),
                   ash_speaking(h), ash_get_int(h, "work"));
        quiet = ash_busy(h, 0.03, 1.5) ? 0.0 : quiet + block;
        if (quiet >= tail)
            break;
    }
    printf("END %.6f writes %ld tx %d unmapped %d %d nmi %d\n", ash_get_double(h, "time"), n_writes,
           ash_tx(h, (const unsigned char **)&y), ash_get_int(h, "unmapped_in"), ash_get_int(h, "unmapped_out"),
           ash_get_int(h, "nmi_edges"));
    if (wav) {
        short *pcm = (short *)malloc((size_t)(n_all ? n_all : 1) * sizeof(short));
        FILE *f = fopen(wav, "wb");
        long k;
        if (!pcm || !f) {
            fprintf(stderr, "az_trace: cannot write %s\n", wav);
            return 1;
        }
        ssi_pcm16(all, (int)n_all, 1.0, pcm);
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
        for (k = 0; k < n_all; k++)
            le(f, (unsigned short)pcm[k], 2);
        fclose(f);
        free(pcm);
    }
    free(all);
    ash_destroy(h);
    return 0;
}
