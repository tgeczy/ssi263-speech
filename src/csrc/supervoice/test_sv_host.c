/* test_sv_host.c -- the host of Speech Systems' Super Voice (sv_host.h) on the cartridge's own SUPERTOK.BIN.
 *
 *     test_sv_host <folder holding SUPERTOK.BIN>
 *
 * The golden phonemes are this core's, frozen 2026-11-19: no second 6809 has run the program, and no listener had
 * passed the voice when they were frozen.  They are what guards the 6809's instruction results (../cpu's contract
 * tests check its cycles and its interrupts, not its arithmetic).
 *
 *   golden_r0       "HI THERE" gives the phonemes below (HF AH1 EH1 Y, a pause, THV EH1 A EH1 R1), between the
 *                   program's own opening (R3 80h, R0 C0h, R3 5Fh, R2 A8h, R4 E9h, R1 6Bh) and its end (R3 50h, R4
 *                   FFh); a sentence with money, digits, an apostrophe and a number sign gives the longer list; the
 *                   call returns; no opcode Motorola leaves unused was run
 *   starts_at_once  the first phoneme is written within 2 ms of svh_say (the chip's time stands still until the
 *                   program first waits for the chip); with "skip_start" 0 it comes after the 90 ms and more the
 *                   machine takes -- and the phonemes are the same either way
 *   each_text_fresh a text after another one is written register for register as on a host just made (the program
 *                   keeps its last phoneme and the control codes of the text before), its times within TICK: the
 *                   chip's phoneme timer counts in steps of 4096 clock cycles, 4.6 ms, and does not start again
 *                   with a text, so where a write falls in a step is the history's
 *   codes           the program's control codes reach the chip, the up arrow being 5Eh: ^V3 the amplitude 3, ^F200
 *                   the filter C8h, ^S16 ends sooner than ^S1; and the text after ^V3 has the default amplitude again
 *   cancel          a sentence cut at eleven times -- in stops, in vowels, in a long pause (R0 00h) -- and "HI
 *                   THERE" 20 ms later: after the cancel not busy, its last writes R3 with the amplitude 0 and R4
 *                   FFh; the next text is the golden one and returns (uncorrected, a cut in the long pause leaves
 *                   the chip in the mode without A/R and the call never returns); its first phoneme is written
 *                   at once (within 2 ms of rendered audio) and its first 20 ms are quiet, under 0.012 of full
 *                   scale as on a new chip (uncorrected: up to 82 ms late, the cut phoneme sounding first at up
 *                   to half of full scale)
 *   start_pitch     at a text's first phoneme the chip's pitch counter is on its target: on a new host, with a pitch
 *                   code at the text's head (^IT20,4), and for a text after one at another pitch; a pitch code
 *                   inside a text still glides
 *   in_step         the same text on a host run in 30 ms blocks and on one run in 7 ms blocks: the same writes at the
 *                   same times, to the microsecond (the chip follows the 6809 at every access, not at a block's
 *                   end); and at 11,025 Hz the same writes, within TICK
 *   stuck_guard     the chip put in its mode without A/R behind the program's back: the call is given up after 5 s
 *                   (fault SVH_FAULT_STUCK), and the next text speaks
 *   refusals        an empty text, 241 characters, and a text while one is speaking are refused; 240 characters are
 *                   taken and spoken to the end
 *   wrong_image     the file with one byte changed is refused
 *
 * Not tested: SVH_FAULT_LEFT (nothing on the text path leaves the program).
 *
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1 (77
 * with a note when the firmware file is not there: it is in no checkout).  MIT.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_host.h"

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

static const unsigned char GOLDEN[] = {0xC0, 0x6C, 0x4F, 0x4B, 0x43, 0xC0, 0x75, 0x4B, 0x48, 0x4B, 0x5E, 0xC0, 0xC0};
/* By the chip's phoneme names, pauses as /:  I I T / K AW AW S T S / T W EH1 EH1 UH3 L V  D AH1 UH3 L ER S  F I I I F
   T Y  S EH1 EH1 N T S / D UH1 / S N / T / I I T / D I I AW UH3 L / N AH1 EH1 Y N  HF UH1 UH2 N D R I D  EH1 L EH1
   EH1 V I N  N UH1 UH2 M B ER / W UH1 I N / P O UH1 AH1 I IE N T -- "it costs twelve dollars fifty cents, doesn't
   it, dial nine hundred eleven number one point": the program's number words, so its arithmetic on this core reads
   right.  (The period after a digit is its "point", at a sentence's end too: the manual, p.17.) */
#define TEXT2 "It costs $12.50, doesn't it? Dial 911 #1."
static const unsigned char GOLDEN2[] = {
    0xC0, 0x47, 0x47, 0x68, 0xC0, 0xC0, 0x69, 0x50, 0x50, 0x70, 0x68, 0x70, 0xC0, 0xC0, 0x68, 0x63, 0x4B, 0x4B, 0x5B,
    0x60, 0x73, 0x65, 0x4F, 0x5B, 0x60, 0x5C, 0x70, 0x74, 0x47, 0x47, 0x47, 0x74, 0x68, 0x43, 0x70, 0x4B, 0x4B, 0x78,
    0x68, 0x70, 0xC0, 0xC0, 0x65, 0x59, 0xC0, 0x70, 0x78, 0xC0, 0x68, 0xC0, 0xC0, 0x47, 0x47, 0x68, 0xC0, 0xC0, 0x65,
    0x47, 0x47, 0x50, 0x5B, 0x60, 0xC0, 0xC0, 0x78, 0x4F, 0x4B, 0x43, 0x78, 0xC0, 0x6C, 0x59, 0x5A, 0x78, 0x65, 0x5D,
    0x47, 0x65, 0x4B, 0x60, 0x4B, 0x4B, 0x73, 0x47, 0x78, 0x78, 0x59, 0x5A, 0x77, 0x64, 0x5C, 0xC0, 0x63, 0x59, 0x47,
    0x78, 0xC0, 0x67, 0x51, 0x59, 0x4F, 0x47, 0x46, 0x78, 0x68, 0xC0, 0xC0};
#define TICK 0.0047                        /* 4096 cycles of the chip's clock, and a little */
#define LONG_TEXT "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. IT COSTS 1984 DOLLARS, DOESN'T IT?"

static unsigned char *slurp(const char *dir, long *n)
{
    char path[4096];
    FILE *f;
    unsigned char *d = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, SV_FILE);
    f = fopen(path, "rb");
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

static sv_host *make_at(const char *dir, double rate)
{
    char err[256] = "";
    sv_host *h = svh_create_dir(dir, NULL, rate, err, (int)sizeof err);
    if (!h) {
        printf("FAIL create %s\nFAILED\n", err);
        exit(1);
    }
    svh_set_int(h, "log_writes", 1);
    return h;
}

static sv_host *make(const char *dir) { return make_at(dir, 22050); }

/* runs until the call returns (at most `limit` seconds) in blocks of `block`; the seconds run */
static double finish(sv_host *h, double limit, double block)
{
    double t = 0.0;
    const double *a;
    while (svh_busy(h) && t < limit) {
        svh_run(h, block, &a);
        t += block;
    }
    return t;
}

/* speaks to the end; the seconds it took, -1 when refused */
static double speak(sv_host *h, const char *text, double limit)
{
    if (!svh_say(h, (const unsigned char *)text, (int)strlen(text)))
        return -1.0;
    return finish(h, limit, 0.03);
}

static int r0s(sv_host *h, unsigned char *out, int cap)
{
    const svh_write *w;
    int n = svh_writes(h, &w), k, m = 0;
    for (k = 0; k < n; k++)
        if (w[k].reg == 0 && m < cap)
            out[m++] = (unsigned char)w[k].val;
    return m;
}

/* the index of the first difference between the R0 writes and a golden list, -1 for none; -2: another count */
static int against(sv_host *h, const unsigned char *golden, int ngolden)
{
    unsigned char got[512];
    int n = r0s(h, got, 512), k;
    if (n != ngolden)
        return -2;
    for (k = 0; k < n; k++)
        if (got[k] != golden[k])
            return k;
    return -1;
}

static void t_golden(const char *dir)
{
    static const int OPEN[6][2] = {{3, 0x80}, {0, 0xC0}, {3, 0x5F}, {2, 0xA8}, {4, 0xE9}, {1, 0x6B}};
    sv_host *h = make(dir);
    const svh_write *w;
    double t = speak(h, "HI THERE", 10.0), t2;
    int d = against(h, GOLDEN, (int)sizeof GOLDEN), n = svh_writes(h, &w), k, opening = n >= 8, d2;
    for (k = 0; opening && k < 6; k++)
        opening = w[k].reg == OPEN[k][0] && w[k].val == OPEN[k][1];
    opening = opening && w[n - 2].reg == 3 && w[n - 2].val == 0x50 && w[n - 1].reg == 4 && w[n - 1].val == 0xFF;
    svh_clear_writes(h);
    t2 = speak(h, TEXT2, 30.0);
    d2 = against(h, GOLDEN2, (int)sizeof GOLDEN2);
    check("golden_r0", d == -1 && opening && d2 == -1 && !svh_busy(h) && t > 0.5 && t < 2.0 && t2 > 4.0
          && svh_get_int(h, "undefined") == 0 && svh_get_int(h, "fault") == 0,
          "\"HI THERE\": first difference %d, opening and end %d, %.2f s; the sentence: first difference %d, %.2f s; "
          "busy %d, unused opcodes %d", d, opening, t, d2, t2, svh_busy(h), svh_get_int(h, "undefined"));
    svh_destroy(h);
}

/* the time of the n-th R0 write (from 0) */
static double r0_time(sv_host *h, int nth)
{
    const svh_write *w;
    int n = svh_writes(h, &w), k;
    for (k = 0; k < n; k++)
        if (w[k].reg == 0 && nth-- == 0)
            return w[k].t;
    return -1.0;
}

static void t_starts_at_once(const char *dir)
{
    sv_host *a = make(dir), *b = make(dir);
    double ta, tb;
    int da, db;
    svh_set_int(b, "skip_start", 0);
    speak(a, "HI THERE", 10.0);
    speak(b, "HI THERE", 10.0);
    ta = r0_time(a, 1);
    tb = r0_time(b, 1);
    da = against(a, GOLDEN, (int)sizeof GOLDEN);
    db = against(b, GOLDEN, (int)sizeof GOLDEN);
    check("starts_at_once", ta >= 0 && ta < 0.002 && tb > 0.09 && tb < 0.2 && da == -1 && db == -1
          && svh_get_int(a, "start_cycles") > 80000,
          "the first phoneme %.1f ms after the call (as the machine: %.1f ms); %d cycles skipped; golden %d and %d",
          ta * 1000, tb * 1000, svh_get_int(a, "start_cycles"), da, db);
    svh_destroy(a);
    svh_destroy(b);
}

/* the writes of two logs: 0 the same registers and values, their times (from each log's first) within tol */
static int same_writes(const svh_write *x, int nx, const svh_write *y, int ny, double tol, double *worst)
{
    int k;
    *worst = 0.0;
    if (nx != ny || nx == 0)
        return 1;
    for (k = 0; k < nx; k++) {
        double d = fabs((x[k].t - x[0].t) - (y[k].t - y[0].t));
        if (x[k].reg != y[k].reg || x[k].val != y[k].val)
            return 2;
        if (d > *worst)
            *worst = d;
    }
    return *worst > tol ? 3 : 0;
}

static void t_each_text_fresh(const char *dir)
{
    sv_host *a = make(dir), *b = make(dir);
    const svh_write *wa, *wb;
    double worst = 0.0;
    int na, nb, r;
    speak(a, "^V3 ^S14 " LONG_TEXT, 30.0);
    svh_clear_writes(a);
    speak(a, "HI THERE", 10.0);
    speak(b, "HI THERE", 10.0);
    na = svh_writes(a, &wa);
    nb = svh_writes(b, &wb);
    r = same_writes(wa, na, wb, nb, TICK, &worst);
    check("each_text_fresh", r == 0 && na > 10,
          "after another text: %d writes, on a new host %d; %s, times within %.3f ms", na, nb,
          r == 0 ? "the same" : r == 3 ? "the same values" : "DIFFERENT", worst * 1000);
    svh_destroy(a);
    svh_destroy(b);
}

static int last_of(sv_host *h, int reg)
{
    const svh_write *w;
    int n = svh_writes(h, &w), k, v = -1;
    for (k = 0; k < n; k++)
        if (w[k].reg == reg)
            v = w[k].val;
    return v;
}

static void t_codes(const char *dir)
{
    sv_host *h = make(dir);
    const svh_write *w;
    int n, k, amp3 = 0, amp_after = -1, filter;
    double slow, fast;
    speak(h, "^V3 HI", 10.0);
    n = svh_writes(h, &w);
    for (k = 0; k < n; k++)
        amp3 += w[k].reg == 3 && w[k].val == 0x53;
    svh_clear_writes(h);
    speak(h, "HI", 10.0);
    n = svh_writes(h, &w);
    for (k = 0; k + 2 < n; k++)                          /* the last R3 before the end's */
        if (w[k].reg == 3)
            amp_after = w[k].val & 15;
    svh_clear_writes(h);
    speak(h, "^F200 HI", 10.0);
    n = svh_writes(h, &w);
    filter = 0;
    for (k = 0; k < n; k++)
        filter += w[k].reg == 4 && w[k].val == 200;
    svh_clear_writes(h);
    slow = speak(h, "^S1 HI THERE", 20.0);
    fast = speak(h, "^S16 HI THERE", 20.0);
    check("codes", amp3 == 1 && amp_after == 15 && filter == 1 && fast > 0 && slow > 3 * fast,
          "^V3: R3 53h written %d time(s), the next text's amplitude %d; ^F200: R4 C8h %d time(s); ^S1 %.2f s, ^S16 "
          "%.2f s", amp3, amp_after, filter, slow, fast);
    svh_destroy(h);
}

/* "HI THERE" said now: the seconds of audio rendered before its first phoneme is written (the chip's own clock
   will not do: the rest after a cut runs it on), and the level, rms, of its first 20 ms (*head); then run to its
   end */
static double onset(sv_host *h, double *head)
{
    const double *a;
    double first = -1.0, t = 0.0, e = 0.0;
    long n = 0;
    svh_clear_writes(h);
    if (!svh_say(h, (const unsigned char *)"HI THERE", 8))
        return -1.0;
    if (r0_time(h, 1) >= 0)
        first = 0.0;                                     /* written before the call's first wait */
    while (svh_busy(h) && t < 10.0) {
        int got = svh_run(h, 0.001, &a), k;
        for (k = 0; k < got && n < 441; k++, n++)
            e += a[k] * a[k];
        t += 0.001;
        if (first < 0 && r0_time(h, 1) >= 0)
            first = t;
    }
    *head = sqrt(e / (double)(n ? n : 1));
    return first;
}

static void t_cancel(const char *dir)
{
    static const double CUTS[] = {0.20, 0.54, 0.71, 0.89, 1.57, 2.08, 2.59, 3.96, 5.50, 5.84, -1.0};
    double peak, worst_late = 0.0, worst_peak = 0.0;
    int k, r3 = -1, r4 = -1, in_pause = 0, bad_golden = 0, stuck = 0, late = 0, loud = 0, n = 0;
    for (k = 0; k < (int)(sizeof CUTS / sizeof CUTS[0]); k++) {
        sv_host *h = make(dir);
        const svh_write *w;
        const double *a;
        double t = 0.0, got;
        int nw;
        svh_say(h, (const unsigned char *)LONG_TEXT, (int)strlen(LONG_TEXT));
        if (CUTS[k] < 0) {                               /* in the sentence's long pause (R0 00h) */
            while (svh_busy(h) && t < 20.0 && last_of(h, 0) != 0x00) {
                svh_run(h, 0.01, &a);
                t += 0.01;
            }
            in_pause = last_of(h, 0) == 0x00;
        } else {
            while (t < CUTS[k]) {
                svh_run(h, 0.001, &a);
                t += 0.001;
            }
        }
        svh_cancel(h);
        nw = svh_writes(h, &w);
        if (CUTS[k] < 0) {
            r3 = nw >= 2 && w[nw - 2].reg == 3 ? w[nw - 2].val : -1;
            r4 = nw >= 2 && w[nw - 1].reg == 4 ? w[nw - 1].val : -1;
        }
        stuck += svh_busy(h);
        svh_run(h, 0.02, &a);                            /* the next text comes 20 ms later */
        got = onset(h, &peak);
        bad_golden += against(h, GOLDEN, (int)sizeof GOLDEN) != -1;
        stuck += svh_busy(h) || svh_get_int(h, "fault") != 0;
        late += got < 0 || got > 0.002;
        loud += peak > 0.012;
        if (got > worst_late)
            worst_late = got;
        if (peak > worst_peak)
            worst_peak = peak;
        n++;
        svh_destroy(h);
    }
    check("cancel", in_pause && r3 == 0x50 && r4 == 0xFF && !bad_golden && !stuck && !late && !loud,
          "%d cuts (stops, vowels, a long pause %d): after the cancel R3 %02X, R4 %02X; the next text other than "
          "golden %d, not returned %d, its first phoneme not at once at %d (the latest %.1f ms), its first 20 ms "
          "loud at %d (the loudest %.4f rms)", n, in_pause, r3 & 255, r4 & 255, bad_golden, stuck, late,
          worst_late * 1000, loud, worst_peak);
}

/* the text said to its first phoneme: the pitch counter's distance from its target there (-1: none came) */
static int gap_at_first_phoneme(sv_host *h, const char *text)
{
    const double *a;
    double t = 0.0;
    svh_clear_writes(h);
    if (!svh_say(h, (const unsigned char *)text, (int)strlen(text)))
        return -1;
    while (svh_busy(h) && t < 2.0 && r0_time(h, 1) < 0) {
        svh_run(h, 0.0005, &a);
        t += 0.0005;
    }
    return r0_time(h, 1) < 0 ? -1 : svh_get_int(h, "pitch_gap");
}

static void t_start_pitch(const char *dir)
{
    sv_host *h = make(dir);
    const double *a;
    int fresh, coded, back, later = 0, n = 0;
    fresh = gap_at_first_phoneme(h, "HI THERE");
    finish(h, 10.0, 0.03);
    coded = gap_at_first_phoneme(h, "^IT20,4 HI THERE");
    finish(h, 10.0, 0.03);
    back = gap_at_first_phoneme(h, "HI ^IT28,2 THERE YOU ARE");
    while (svh_busy(h) && n++ < 400) {                   /* the code inside the text: the pitch moves there */
        svh_run(h, 0.01, &a);
        later |= svh_get_int(h, "pitch_gap") > 0;
    }
    check("start_pitch", fresh == 0 && coded == 0 && back == 0 && later,
          "the counter from its target at the first phoneme: %d on a new host, %d with a pitch code at the head, %d "
          "after a text at another pitch; a code inside the text glides: %d", fresh, coded, back, later);
    svh_destroy(h);
}

static void t_in_step(const char *dir)
{
    sv_host *a = make(dir), *b = make(dir), *c = make_at(dir, 11025);
    const svh_write *wa, *wb, *wc;
    double worst = 0.0, worst_c = 0.0;
    int na, nb, nc, r, rc;
    svh_say(a, (const unsigned char *)LONG_TEXT, (int)strlen(LONG_TEXT));
    svh_say(b, (const unsigned char *)LONG_TEXT, (int)strlen(LONG_TEXT));
    svh_say(c, (const unsigned char *)LONG_TEXT, (int)strlen(LONG_TEXT));
    finish(a, 30.0, 0.03);
    finish(b, 30.0, 0.007);
    finish(c, 30.0, 0.03);
    na = svh_writes(a, &wa);
    nb = svh_writes(b, &wb);
    nc = svh_writes(c, &wc);
    r = same_writes(wa, na, wb, nb, 0.000001, &worst);
    rc = same_writes(wa, na, wc, nc, TICK, &worst_c);
    check("in_step", r == 0 && rc == 0 && na > 100,
          "%d writes; in 7 ms blocks %d, %s, times within %.4f ms; at 11,025 Hz %d, %s, within %.3f ms", na, nb,
          r == 0 ? "the same" : r == 3 ? "the same values, TIMES apart" : "DIFFERENT", worst * 1000, nc,
          rc == 0 ? "the same" : rc == 3 ? "the same values, TIMES apart" : "DIFFERENT", worst_c * 1000);
    svh_destroy(a);
    svh_destroy(b);
    svh_destroy(c);
}

static void t_stuck_guard(const char *dir)
{
    sv_host *h = make(dir);
    const double *a;
    ssi263 *chip = svh_chip(h);
    double t, after;
    int fault, d;
    svh_say(h, (const unsigned char *)"HI THERE", 8);
    svh_run(h, 0.1, &a);
    ssi263_write(chip, 3, 0x80);                         /* CTL 1, duration bits 00, CTL 0: the mode without A/R */
    ssi263_write(chip, 0, 0x00);
    ssi263_write(chip, 3, 0x0F);
    t = 0.1 + finish(h, 8.0, 0.03);
    fault = svh_get_int(h, "fault");
    svh_clear_writes(h);
    after = speak(h, "HI THERE", 10.0);
    d = against(h, GOLDEN, (int)sizeof GOLDEN);
    check("stuck_guard", fault == SVH_FAULT_STUCK && t > 4.9 && t < 5.3 && d == -1 && after > 0.5 && after < 2.0,
          "given up after %.2f s, fault %d; the next text: first difference %d, %.2f s", t, fault, d, after);
    svh_destroy(h);
}

static void t_refusals(const char *dir)
{
    sv_host *h = make(dir);
    char text[260];
    double t;
    int empty, too_long, busy, k;
    for (k = 0; k < 241; k++)
        text[k] = "ONE TWO THREE FOUR FIVE SIX "[k % 28];
    empty = svh_say(h, (const unsigned char *)text, 0);
    too_long = svh_say(h, (const unsigned char *)text, 241);
    k = svh_say(h, (const unsigned char *)text, 240);
    busy = svh_say(h, (const unsigned char *)"HI", 2);
    t = finish(h, 60.0, 0.03);
    check("refusals", !empty && !too_long && k && !busy && !svh_busy(h) && t > 5.0 && svh_get_int(h, "fault") == 0,
          "empty %d, 241 characters %d, 240 taken %d, a text while speaking %d; the 240 spoken in %.2f s, busy %d",
          empty, too_long, k, busy, t, svh_busy(h));
    svh_destroy(h);
}

static void t_wrong_image(const char *dir)
{
    char err[256] = "";
    long n = 0;
    unsigned char *d = slurp(dir, &n);
    sv_host *h;
    int known = d ? svh_is_known(d, (size_t)n) : -1;
    if (d)
        d[n / 2] ^= 1;
    h = d ? svh_create(d, (size_t)n, NULL, 22050, err, (int)sizeof err) : NULL;
    check("wrong_image", known == 1 && d && !h && err[0], "the file known %d; one byte changed: %s", known,
          h ? "TAKEN" : err);
    svh_destroy(h);
    free(d);
}

int main(int argc, char **argv)
{
    long n = 0;
    unsigned char *d;
    if (argc < 2) {
        fprintf(stderr, "usage: test_sv_host <folder holding %s>\n", SV_FILE);
        return 2;
    }
    d = slurp(argv[1], &n);
    if (!d) {
        printf("skipped: no %s in %s\n", SV_FILE, argv[1]);
        return 77;
    }
    free(d);
    t_golden(argv[1]);
    t_starts_at_once(argv[1]);
    t_each_text_fresh(argv[1]);
    t_codes(argv[1]);
    t_cancel(argv[1]);
    t_start_pitch(argv[1]);
    t_in_step(argv[1]);
    t_stuck_guard(argv[1]);
    t_refusals(argv[1]);
    t_wrong_image(argv[1]);
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
