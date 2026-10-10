/* test_line_lift.c -- the line-start lift (bl_voice.h blv_set_line_lift) against the unit's own note-taking mode.
 *
 *   test_line_lift BL2ENG.BNS bl2_2003_warm.state
 *
 * The reference: the emulated Braille Lite in note-taking mode, its mini help file opened and read line by line with
 * dot 4 + space -- the unit lifts the start of each line moved to.  Then the voice (bl_voice) speaks the same lines:
 *   lifted        lift on, each line after a cancel (moving by line while speech goes on): every lifted line start's
 *                 first six phonemes, and the pitch byte at each, as note-taking mode's
 *   pause         lift on, each line after a second of silence and no cancel (moving by line in a pause): the same
 *   say_all       lift on, the lines queued with no cancel or pause between them: none lifted after the first
 *   off           lift off (the default): none lifted
 *   inflection    lift on, the unit's voice inflection off: none lifted (the firmware drops its markers)
 * Lines are matched by their first six phonemes (the voice's punctuation is none, note-taking mode's some: a line
 * whose opening differs is skipped); at least eight must match, and the reference must lift.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bl_host.h"
#include "bl_voice.h"
#include "ssi263.h"

#define RATE 22050
#define FIRST 6                 /* phonemes compared per line */
#define MAXL 64
#define KEY_GAP_S 2.5          /* the unit's time between keys */

typedef struct { int n; int ph[FIRST], r1[FIRST]; } opening;
enum { QUEUED, CANCEL, PAUSE };

static int failures;
static double g_now;                                     /* the voice's clock (blv_clock), moved by the test only */

static double fake_clock(void)
{
    return g_now;
}

static void check(const char *name, int ok, const char *detail)
{
    printf("%s %s %s\n", ok ? "ok" : "FAIL", name, detail);
    if (!ok)
        failures++;
}

/* the log's lines: a line starts at the first spoken phoneme after 150 ms without one; its first FIRST spoken
   phonemes and the pitch byte (R1) in force at each */
static int openings(const bh_write *w, int n, opening *out, int max)
{
    int k, lines = 0, r1 = -1;
    double last = -1.0;
    for (k = 0; k < n; k++) {
        if (w[k].reg == 1)
            r1 = w[k].val;
        else if (w[k].reg == 0 && (w[k].val & 0x3F)) {
            if (last < 0.0 || w[k].t - last > 0.15) {
                if (lines == max)
                    break;
                out[lines++].n = 0;
            }
            last = w[k].t;
            if (out[lines - 1].n < FIRST) {
                out[lines - 1].ph[out[lines - 1].n] = w[k].val & 0x3F;
                out[lines - 1].r1[out[lines - 1].n++] = r1;
            }
        }
    }
    return lines;
}

static int lifted(const opening *o, int base)
{
    int i;
    for (i = 0; i < o->n; i++)
        if (o->r1[i] != base)
            return 1;
    return 0;
}

/* the help file's lines 2-15 from the firmware (as note-taking mode reads them) */
static int help_lines(const char *fw, char lines[][128], int max)
{
    static char d[400000];
    FILE *f = fopen(fw, "rb");
    size_t n;
    char *p, *end;
    int k = 0;
    if (!f)
        return 0;
    n = fread(d, 1, sizeof d - 1, f);
    fclose(f);
    for (p = d; p + 40 < d + n && memcmp(p, "Braille Lite Two Thousand Mini Help File", 40); p++)
        ;
    if (p + 40 >= d + n)
        return 0;
    end = p + strlen(p);
    while (p < end && k < max + 1) {
        char *e = memchr(p, '\r', (size_t)(end - p));
        size_t len = e ? (size_t)(e - p) : (size_t)(end - p);
        if (len > 0 && len < 128) {
            if (k > 0) {                                  /* line 1 is the title, read by the opening key */
                memcpy(lines[k - 1], p, len);
                lines[k - 1][len] = 0;
            }
            k++;
        }
        p += len + 1;
    }
    return k > 0 ? k - 1 : 0;
}

/* the voice speaking the lines: how = QUEUED (one after the other), CANCEL (a cancel before each), PAUSE (a second
   of the voice's clock before each) */
static int voice_run(const char *fw, const char *st, int lift, int how, int inflection, char lines[][128], int nl,
                     opening *out, int *base)
{
    char err[256] = "";
    bl_voice *v = blv_create(fw, st, 0, RATE, inflection, 0, err, (int)sizeof err);
    const bh_write *w;
    int k, n;
    if (!v) {
        printf("FAIL voice: %s\n", err);
        exit(1);
    }
    blv_set(v, 50, 50, 7, 100, 1);
    blv_set_line_lift(v, lift);
    for (k = 0; k < nl; k++) {
        int done = 0, guard = 0;
        const short *pcm;
        if (how == CANCEL)
            blv_cancel(v);
        else if (how == PAUSE)
            g_now += 1.0;
        if (k == 1 || (k == 0 && how == CANCEL))
            bh_set_int(blv_host(v), "log_writes", 1);     /* from the second line (the first is always lifted) */
        blv_speak(v, lines[k]);
        while (!done && guard++ < 4000)
            blv_render(v, &pcm, &done);
    }
    n = bh_writes(blv_host(v), &w);
    *base = -1;
    for (k = 0; k < n && *base < 0; k++)
        if (w[k].reg == 1)
            *base = w[k].val;
    n = openings(w, n, out, MAXL);
    blv_destroy(v);
    return n;
}

/* each line start note-taking mode lifted, and the voice's stretch with the same phonemes (a pause inside a line also
   starts a stretch: only the lifted ones are the reference's line starts): how many were found, how many alike */
static int alike(const opening *ref, int nref, int base_ref, const opening *got, int ngot, int *tried)
{
    int j, k, same = 0;
    *tried = 0;
    for (j = 0; j < nref; j++) {
        if (ref[j].n < FIRST || !lifted(&ref[j], base_ref))
            continue;
        for (k = 0; k < ngot; k++)
            if (got[k].n == FIRST && !memcmp(got[k].ph, ref[j].ph, sizeof got[k].ph)) {
                (*tried)++;
                same += !memcmp(got[k].r1, ref[j].r1, sizeof got[k].r1);
                break;
            }
    }
    return same;
}

int main(int argc, char **argv)
{
    static char lines[MAXL][128];
    static opening ref[MAXL], got[MAXL];
    unsigned char keys[24];
    char err[256] = "", d[256];
    int nl, nk = 0, nref, ngot, k, base_ref = -1, base, same, tried, any;
    ssi263 *chip;
    bl_host *h;
    const bh_write *w;
    const double *y;
    if (argc < 3) {
        fprintf(stderr, "usage: test_line_lift BL2ENG.BNS bl2_2003_warm.state\n");
        return 2;
    }
    nl = help_lines(argv[1], lines, 14);
    if (nl < 10) {
        printf("FAIL the help file's lines not found in %s\nFAILED\n", argv[1]);
        return 1;
    }
    /* the reference: ER-chord reads the help file; a key stops it; dot 4 + space, line after line */
    keys[nk++] = 0x7B;
    keys[nk++] = 0x58;
    for (k = 0; k < nl + 1; k++)
        keys[nk++] = 0x48;
    {
        ssi263_params p;
        ssi263_default_params(&p);
        chip = ssi263_new(&p, ssi263_default_rom(), RATE);
    }
    h = bh_create(argv[1], argv[2], chip, RATE, 5000.0, NULL, NULL, 0, 3000000ULL, 1, err, (int)sizeof err);
    if (!h) {
        printf("FAIL unit: %s\nFAILED\n", err);
        return 1;
    }
    bh_run(h, 4.0, 0.0005, &y);                           /* its greeting */
    bh_clear_writes(h);
    for (k = 0; k < nk; k++) {                            /* each key live, then the unit's time to speak */
        bh_key(h, keys[k]);
        bh_run(h, KEY_GAP_S, 0.0005, &y);
    }
    nref = bh_writes(h, &w);
    for (k = 0; k < nref && base_ref < 0; k++)
        if (w[k].reg == 1)
            base_ref = w[k].val;
    nref = openings(w, nref, ref, MAXL);
    for (k = 0, any = 0; k < nref; k++)
        any += lifted(&ref[k], base_ref);
    snprintf(d, sizeof d, "%d lines heard, %d lifted (pitch byte %02X)", nref, any, base_ref);
    check("reference", nref >= 10 && any >= 8, d);

    blv_clock = fake_clock;
    ngot = voice_run(argv[1], argv[2], 1, CANCEL, 1, lines, nl, got, &base);
    same = alike(ref, nref, base_ref, got, ngot, &tried);
    snprintf(d, sizeof d, "%d of the %d lifted line starts found in the voice have note-taking mode's pitch bytes "
             "(%d stretches spoken)", same, tried, ngot);
    check("lifted", tried >= 8 && same == tried, d);

    ngot = voice_run(argv[1], argv[2], 1, PAUSE, 1, lines, nl, got, &base);
    same = alike(ref, nref, base_ref, got, ngot, &tried);
    snprintf(d, sizeof d, "%d of the %d lifted line starts found after a pause have note-taking mode's pitch bytes",
             same, tried);
    check("pause", tried >= 8 && same == tried, d);

    ngot = voice_run(argv[1], argv[2], 1, QUEUED, 1, lines, nl, got, &base);
    for (k = 0, any = 0; k < ngot; k++)
        any += lifted(&got[k], base);
    snprintf(d, sizeof d, "%d of %d queued lines lifted", any, ngot);
    check("say_all", ngot >= 8 && any == 0, d);

    ngot = voice_run(argv[1], argv[2], 0, CANCEL, 1, lines, nl, got, &base);
    for (k = 0, any = 0; k < ngot; k++)
        any += lifted(&got[k], base);
    snprintf(d, sizeof d, "%d of %d lines lifted with the setting off", any, ngot);
    check("off", ngot >= 8 && any == 0, d);

    ngot = voice_run(argv[1], argv[2], 1, CANCEL, 0, lines, nl, got, &base);
    for (k = 0, any = 0; k < ngot; k++)
        any += lifted(&got[k], base);
    snprintf(d, sizeof d, "%d of %d lines lifted with the unit's voice inflection off", any, ngot);
    check("inflection", ngot >= 8 && any == 0, d);

    bh_destroy(h);
    ssi263_free(chip);
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
