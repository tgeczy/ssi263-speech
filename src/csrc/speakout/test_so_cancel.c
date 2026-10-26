/* test_so_cancel.c -- the Speak-Out's text after a cancel reads as it does alone (so_host.c soh_cancel).
 *
 *   test_so_cancel SPEAKOUT.HEX
 *
 * "Custom number processing, check box, checked" is cut after its 1st, 2nd ... 8th spoken phoneme (soh_cancel, as
 * a screen reader interrupts it), then "Hello there, how are you today?" is said: its first phonemes must be those
 * it has after an utterance that was not cut.  Before 0.8 the firmware's rules kept the cut word's context through
 * the ^X, and after a cut at K HF or K HF UH "Hello" read HF EH EH LF LF UH O O U -- an extra UH (complete_fuzz
 * speakout, step 12, under the full suite's load, where its cuts land); soh_cancel now ends with a CR.
 *
 * Must fail: test_so_cancel_old.exe, so_host.c built with SO_NO_CANCEL_CR (the CR not sent): cuts 2 and 3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so_host.h"

#define PHONES 12
#define CUTS 8
#define WARM 400                    /* the warm-up's own room: it said 15 phonemes into got[12] (Astra, Reply 252) */

static const char *PH[64] = {"PA", "E", "E1", "Y", "YI", "AY", "IE", "I", "A", "AI", "EH", "EH1", "AE", "AE1", "AH",
    "AH1", "AW", "O", "OU", "OO", "IU", "IU1", "U", "U1", "UH", "UH1", "UH2", "UH3", "ER", "R", "R1", "R2", "L", "L1",
    "LF", "W", "B", "D", "KV", "P", "T", "K", "HV", "HVC", "HF", "HFC", "HN", "Z", "S", "J", "SCH", "V", "F", "THV",
    "TH", "M", "N", "NG", ":A", ":OH", ":U", ":UH", "E2", "LB"};
static const char CUT_TEXT[] = "Custom number processing, check box, checked\r";
static const char HELLO[] = "Hello there, how are you today?\r";

/* run in 2 ms steps until `want` spoken phonemes (loads at a nonzero amplitude, PA left out) or `secs`; their codes */
static int loads(so_host *h, int *r3, int want, double secs, int *out)
{
    const double *y;
    const soh_write *w;
    int got = 0, i, n;
    double t = 0;
    while (t < secs && got < want) {
        soh_run(h, 0.002, 0.0005, &y);
        t += 0.002;
        n = soh_writes(h, &w);
        for (i = 0; i < n; i++) {
            if (w[i].reg == 3)
                *r3 = w[i].val;
            else if (w[i].reg == 0 && (w[i].val & 0x3F) && (*r3 & 0x0F) && got < want)
                out[got++] = w[i].val & 0x3F;
        }
        soh_clear_writes(h);
    }
    return got;
}

static so_host *booted(const char *hex, int *r3)
{
    char err[256];
    so_host *h = soh_create_file(hex, NULL, 22050.0, err, sizeof err);
    if (!h) {
        printf("FAIL create: %s\n", err);
        exit(1);
    }
    soh_set_int(h, "log_writes", 1);
    soh_boot(h, 4.0);
    soh_clear_writes(h);
    *r3 = 0;
    return h;
}

static void show(const int *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        printf(" %s", PH[p[i]]);
}

int main(int argc, char **argv)
{
    int ref[PHONES], got[PHONES], cut[CUTS], warm[WARM], r3, n, k, i, bad = 0;
    so_host *h;
    if (argc < 2) {
        printf("usage: test_so_cancel SPEAKOUT.HEX\n");
        return 2;
    }
    /* the reference: "Hello ..." after an utterance that ran to its end */
    h = booted(argv[1], &r3);
    soh_say(h, (const unsigned char *)"Yes, it works.\r", 15);
    loads(h, &r3, WARM, 4.0, warm);         /* to its end; its phonemes not kept */
    soh_say(h, (const unsigned char *)HELLO, (int)strlen(HELLO));
    n = loads(h, &r3, PHONES, 5.0, ref);
    soh_destroy(h);
    printf("reference:   ");
    show(ref, n);
    printf("\n");
    if (n < PHONES) {
        printf("FAIL the reference spoke %d phonemes\n", n);
        return 1;
    }
    for (k = 1; k <= CUTS; k++) {
        int m, same;
        h = booted(argv[1], &r3);
        soh_say(h, (const unsigned char *)CUT_TEXT, (int)strlen(CUT_TEXT));
        loads(h, &r3, k, 5.0, cut);
        soh_cancel(h, 0.6);
        soh_clear_writes(h);
        soh_say(h, (const unsigned char *)HELLO, (int)strlen(HELLO));
        m = loads(h, &r3, PHONES, 5.0, got);
        soh_destroy(h);
        same = m == n;
        for (i = 0; same && i < n; i++)
            same = got[i] == ref[i];
        printf("%-4s cut after", same ? "ok" : "FAIL");
        show(cut, k);
        printf(":");
        show(got, m);
        printf("\n");
        bad += !same;
    }
    printf("cancel: %s\n", bad ? "FAILED" : "every cut point reads the next text as alone");
    return bad ? 1 : 0;
}
