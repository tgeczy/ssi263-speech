/* test_mockingboard.c -- the Mockingboard host (mb_host.h) on Sweet Micro Systems' own firmware files.
 *
 *     test_mockingboard <folder holding mockingboard-tts-1.1.bin> [<folder holding mockingboard-tts-early.bin>]
 *
 * Each test runs on 1.1, then -- given the second folder -- on the earlier text-to-speech (disks 1 and 2), named
 * early_<test>.  Their golden frames differ (each version's own rules); 1.1's are py65's too (below), the early
 * one's are this core's, frozen 2026-10-10 when Tomi heard it.  In the early one's driver R3 is 70h | the amplitude,
 * in 1.1's 50h |.
 *
 *   golden_r0       "HELLO, MY NAME IS MOCKINGBOARD." gives the R0 frames below -- the same on py65 (an independent
 *                   6502, src/csrc/mockingboard/compare_py65.py) as on this core -- and the text is done
 *   streams         every frame writes R4, R3, R2, R1, R0 in that order (the IIE TTS DRIVER's handler), and the
 *                   settings reach them as INFLECTION makes its two kinds of frame: R4 = the filter or the filter + 2,
 *                   R3 = 50h | the amplitude, or that + 4, R2's high nibble = the rate or the rate + 2
 *   clean_core      no decimal-mode ADC/SBC, no undocumented opcode, no interrupt with the language card's RAM read
 *   settings        rate 13 ends sooner than rate 0 (14 and 15 wrap in INFLECTION's rate + 2: the firmware's own);
 *                   inflection 20 writes higher R1s than inflection 2
 *   busy_refused    a text while speaking is refused; empty and 254-character texts too (MBH_MAX_TEXT: at 254 the
 *                   firmware's index would wrap); 253 accepted
 *   overflow_guard  texts with more frames than the pages hold are refused before anything plays (MBH_FAULT_LONG):
 *                   253 characters of prose, and sixty digits on 1.1 (the early version reads them in 216 frames:
 *                   taken, and spoken to the end); the next text then speaks as golden, and the program's bytes are
 *                   a fresh unit's after the same text (nothing of it was overwritten)
 *   overflow_in_rules  253 characters of WXYZ, whose R0 frames overflow during the rules: refused by the guard
 *                   itself, no frame past its first byte (1.1: 8B00h; the early one: 6600h, its own variables)
 *   cancel          a cancel stops the frames within one more A/R, and the next text speaks normally
 *   wrong_image     the file with one byte changed is refused
 *
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1 (77
 * with a note when the firmware file is not there: the voice's files are not in every checkout).  MIT.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_host.h"

static int failures;
static const char *g_prefix = "";                    /* "early_" for the earlier version */
static const char *g_file = MB_FILE;
static const unsigned char *g_golden;
static int g_ngolden;
static int g_r3 = 0x50;                              /* the driver's R3: g_r3 | the amplitude (+ 4) */
static unsigned g_guard_lo = 0x8B00;                 /* the first byte past the R0 frames' room (mb_host.c) */
/* the program's own bytes below C000h (code, tables, rules), which no text may change */
static unsigned g_prog[3][2];

static void check(const char *name, int ok, const char *fmt, ...)
{
    va_list ap;
    printf("%s %s%s ", ok ? "ok" : "FAIL", g_prefix, name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!ok)
        failures++;
}

static const unsigned char GOLDEN[] = {
    0xC0, 0xAC, 0xCD, 0x0A, 0x22, 0x11, 0x16, 0x63, 0x00, 0x00, 0x37, 0x0E, 0xCB, 0x44, 0x78, 0x08, 0x45, 0x44, 0x37,
    0x07, 0x2F, 0x37, 0x0E, 0x29, 0xAD, 0xC0, 0x07, 0x39, 0xEE, 0xE6, 0x64, 0xEB, 0x11, 0x52, 0x5C, 0x25, 0xEB, 0xC0,
    0x00, 0x00, 0x00, 0x00};
static const unsigned char GOLDEN_EARLY[] = {
    0xC0, 0xAC, 0x0A, 0x4A, 0x60, 0x60, 0x11, 0x63, 0x00, 0x00, 0x37, 0x4E, 0x4E, 0x84, 0x78, 0x08, 0x04, 0x37, 0x47,
    0x2F, 0x37, 0x0E, 0x29, 0xAD, 0xC0, 0x07, 0x39, 0x64, 0x11, 0x63, 0x1D, 0x65, 0xC0, 0x00, 0x00, 0x00, 0x00};

static unsigned char *slurp(const char *dir, const char *file, long *n)
{
    char path[4096];
    FILE *f;
    unsigned char *d = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, file);
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

static mb_host *make(const char *dir)
{
    char err[256] = "";
    long n = 0;
    unsigned char *d = slurp(dir, g_file, &n);
    mb_host *h = d ? mbh_create(d, (size_t)n, NULL, 22050, err, (int)sizeof err) : NULL;
    free(d);
    if (!h) {
        printf("FAIL create %s\nFAILED\n", err);
        exit(1);
    }
    mbh_set_int(h, "log_writes", 1);
    return h;
}

/* speaks until done (at most `limit` seconds); returns the seconds of chip time */
static double speak(mb_host *h, const char *text, double limit)
{
    double t = 0.0;
    const double *a;
    if (!mbh_say(h, (const unsigned char *)text, (int)strlen(text)))
        return -1.0;
    while (mbh_busy(h) && t < limit) {
        mbh_run(h, 0.03, 0.0005, &a);
        t += 0.03;
    }
    return t;
}

static int r0s(mb_host *h, unsigned char *out, int cap)
{
    const mbh_write *w;
    int n = mbh_writes(h, &w), k, m = 0;
    for (k = 0; k < n; k++)
        if (w[k].reg == 0 && m < cap)
            out[m++] = (unsigned char)w[k].val;
    return m;
}

static void t_golden(const char *dir)
{
    mb_host *h = make(dir);
    unsigned char got[256];
    double t = speak(h, "HELLO, MY NAME IS MOCKINGBOARD.", 20.0);
    int n = r0s(h, got, 256), k, first = -1;
    for (k = 0; k < n && k < g_ngolden; k++)
        if (got[k] != g_golden[k] && first < 0)
            first = k;
    check("golden_r0", n == g_ngolden && first < 0 && !mbh_busy(h) && t > 1.0,
          "%d frames (want %d), first difference at %d, %.2f s, busy %d", n, g_ngolden, first, t, mbh_busy(h));
    mbh_destroy(h);
}

static void t_streams(const char *dir)
{
    mb_host *h = make(dir);
    const mbh_write *w;
    int n, k, bad_order = 0, bad_value = 0, frames = 0;
    mbh_set(h, 8, 11, 6, 200);
    speak(h, "TESTING ONE TWO THREE.", 20.0);
    n = mbh_writes(h, &w);
    /* after the driver's start (R3 80, R0 C0, R3 70): groups of five */
    for (k = 3; k + 4 < n; k += 5) {
        if (w[k].reg != 4 || w[k + 1].reg != 3 || w[k + 2].reg != 2 || w[k + 3].reg != 1 || w[k + 4].reg != 0) {
            bad_order++;
            break;
        }
        if ((w[k].val != 200 && w[k].val != 202) || (w[k + 1].val != (g_r3 | 6) && w[k + 1].val != (g_r3 | 6) + 4)
            || ((w[k + 2].val >> 4) != 11 && (w[k + 2].val >> 4) != 13))
            bad_value++;
        frames++;
    }
    check("streams", n > 20 && !bad_order && !bad_value && frames > 10,
          "%d writes, %d frames, order %d, values %d (first group R4 %02X R3 %02X R2 %02X)", n, frames, bad_order,
          bad_value, n > 5 ? w[3].val : 0, n > 5 ? w[4].val : 0, n > 5 ? w[5].val : 0);
    mbh_destroy(h);
}

static void t_clean_core(const char *dir)
{
    mb_host *h = make(dir);
    int i;
    static const char *texts[] = {"THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.", "Is it 10:30, or $4.50? Yes!",
                                  "WHO, WHAT, WHERE; WHEN / WHY!"};
    for (i = 0; i < 3; i++)
        speak(h, texts[i], 30.0);
    check("clean_core", mbh_get_int(h, "decimal") == 0 && mbh_get_int(h, "undocumented") == 0
          && mbh_get_int(h, "vector_reads_lc") == 0 && mbh_get_int(h, "vector_reads") > 50,
          "decimal %d, undocumented %d, vectors read %d (%d from the card's RAM)", mbh_get_int(h, "decimal"),
          mbh_get_int(h, "undocumented"), mbh_get_int(h, "vector_reads"), mbh_get_int(h, "vector_reads_lc"));
    mbh_destroy(h);
}

static int max_r1(mb_host *h)
{
    const mbh_write *w;
    int n = mbh_writes(h, &w), k, m = 0;
    for (k = 0; k < n; k++)
        if (w[k].reg == 1 && w[k].val > m)
            m = w[k].val;
    return m;
}

static void t_settings(const char *dir)
{
    const char *text = "SPEAKING SLOWLY AND QUICKLY.";
    mb_host *a = make(dir), *b = make(dir);
    double slow, fast;
    int lo, hi;
    mbh_set(a, 8, 0, 11, 230);
    mbh_set(b, 8, 13, 11, 230);
    slow = speak(a, text, 30.0);
    fast = speak(b, text, 30.0);
    mbh_destroy(a);
    mbh_destroy(b);
    a = make(dir);
    b = make(dir);
    mbh_set(a, 2, 8, 11, 230);
    mbh_set(b, 20, 8, 11, 230);
    speak(a, text, 30.0);
    speak(b, text, 30.0);
    lo = max_r1(a);
    hi = max_r1(b);
    check("settings", fast < slow && hi > lo, "rate 0 %.2f s, rate 13 %.2f s; highest R1 %02X (inflection 2), %02X (20)",
          slow, fast, lo, hi);
    mbh_destroy(a);
    mbh_destroy(b);
}

static void t_busy_refused(const char *dir)
{
    mb_host *h = make(dir);
    char long_text[300];
    int first, again, empty, too_long, longest;
    const double *a;
    double t = 0.0;
    first = mbh_say(h, (const unsigned char *)"ONE TWO THREE.", 14);
    mbh_run(h, 0.1, 0.0005, &a);
    again = mbh_say(h, (const unsigned char *)"FOUR.", 5);
    while (mbh_busy(h) && t < 20.0) {
        mbh_run(h, 0.03, 0.0005, &a);
        t += 0.03;
    }
    empty = mbh_say(h, (const unsigned char *)"", 0);
    memset(long_text, ' ', sizeof long_text);
    long_text[0] = 'A';
    too_long = mbh_say(h, (const unsigned char *)long_text, 254);
    longest = mbh_say(h, (const unsigned char *)long_text, 253);    /* a letter and spaces: three frames */
    check("busy_refused", first && !again && !empty && !too_long && longest,
          "first %d, while busy %d, empty %d, 254 %d, 253 %d", first, again, empty, too_long, longest);
    mbh_destroy(h);
}

static void t_overflow_guard(const char *dir)
{
    mb_host *h = make(dir);
    char text[300];
    unsigned char buf[512];
    int digits, digits_fault, spaces, spaces_fault, next;
    memset(text, 0, sizeof text);
    memcpy(text, "123456789012345678901234567890123456789012345678901234567890", 60);
    digits = mbh_say(h, (const unsigned char *)text, 60);
    digits_fault = mbh_get_int(h, "fault");
    if (digits) {             /* the early version reads digits in fewer frames (216 here): within the limit, spoken */
        const double *a;
        double t = 0.0;
        digits = mbh_get_int(h, "frames") <= MBH_MAX_FRAMES ? 2 : 1;
        while (mbh_busy(h) && t < 60.0) {
            mbh_run(h, 0.03, 0.0005, &a);
            t += 0.03;
        }
    }
    memset(text, 0, sizeof text);
    while (strlen(text) < 253)
        strcat(text, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. ");
    spaces = mbh_say(h, (const unsigned char *)text, 253);
    spaces_fault = mbh_get_int(h, "fault");
    /* a text whose R0 frames overflow DURING the rules (four frames a letter): stopped by the guard itself, so no
       frame went past its first byte -- without it the early program writes on into its own variables at 6600h */
    {
        int in_rules, in_rules_fault;
        unsigned end;
        memset(text, 0, sizeof text);
        while (strlen(text) < 253)
            strcat(text, "WXYZ");
        text[253] = 0;
        in_rules = mbh_say(h, (const unsigned char *)text, 253);
        in_rules_fault = mbh_get_int(h, "fault");
        end = (unsigned)(mbh_get_int(h, "mem:001B") << 8 | mbh_get_int(h, "mem:001A"));
        check("overflow_in_rules", !in_rules && in_rules_fault == MBH_FAULT_LONG && end <= g_guard_lo,
              "253 of WXYZ %d (fault %d); the frames ended at %04X (the guard's first byte %04X)", in_rules,
              in_rules_fault, end, g_guard_lo);
    }
    mbh_clear_writes(h);
    next = speak(h, "HELLO, MY NAME IS MOCKINGBOARD.", 20.0) > 1.0 && r0s(h, buf, 512) == g_ngolden
           && !memcmp(buf, g_golden, (size_t)g_ngolden);
    /* the program itself untouched: its bytes as a fresh unit's after the same text */
    {
        mb_host *fresh = make(dir);
        int r, differ = 0, first_addr = -1;
        unsigned a;
        speak(fresh, "HELLO, MY NAME IS MOCKINGBOARD.", 20.0);
        for (r = 0; r < 3; r++)
            for (a = g_prog[r][0]; a <= g_prog[r][1] && a; a++) {
                char name[16];
                snprintf(name, sizeof name, "mem:%04X", a);
                if (mbh_get_int(h, name) != mbh_get_int(fresh, name)) {
                    if (first_addr < 0)
                        first_addr = (int)a;
                    differ++;
                }
            }
        mbh_destroy(fresh);
        /* 60 digits: refused as too long (1.1), or (2) taken within MBH_MAX_FRAMES and spoken (early) */
        check("overflow_guard", ((!digits && digits_fault == MBH_FAULT_LONG) || (digits == 2 && g_r3 == 0x70))
              && !spaces && spaces_fault == MBH_FAULT_LONG
              && next && !differ, "60 digits %d (fault %d), 253 characters of prose %d (fault %d); the next text as "
              "golden: %d; the program's bytes as a fresh unit's: %d differ (first at %04X)", digits, digits_fault,
              spaces, spaces_fault, next, differ, first_addr < 0 ? 0 : first_addr);
    }
    mbh_destroy(h);
}

static void t_cancel(const char *dir)
{
    mb_host *h = make(dir);
    const double *a;
    double t = 0.0;
    int before, after, next;
    unsigned char buf[512];
    mbh_say(h, (const unsigned char *)"THIS IS A RATHER LONG SENTENCE THAT WILL NOT BE FINISHED.", 57);
    mbh_run(h, 0.6, 0.0005, &a);
    before = r0s(h, buf, 512);
    mbh_cancel(h);
    while (mbh_busy(h) && t < 2.0) {
        mbh_run(h, 0.03, 0.0005, &a);
        t += 0.03;
    }
    after = r0s(h, buf, 512);
    mbh_clear_writes(h);
    next = speak(h, "HELLO, MY NAME IS MOCKINGBOARD.", 20.0) > 1.0 && r0s(h, buf, 512) == g_ngolden
           && !memcmp(buf, g_golden, (size_t)g_ngolden);
    check("cancel", !mbh_busy(h) && t < 0.5 && after - before <= 2 && next,
          "%d frames before, %d after the cancel, done in %.2f s; the next text as golden: %d", before, after - before,
          t, next);
    mbh_destroy(h);
}

static void t_wrong_image(const char *dir)
{
    char path[4096], err[256] = "";
    FILE *f;
    unsigned char *d;
    long n;
    mb_host *h;
    snprintf(path, sizeof path, "%s/%s", dir, g_file);
    f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (unsigned char *)malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n)
        n = 0;
    fclose(f);
    h = mbh_create(d, (size_t)n, NULL, 22050, err, (int)sizeof err);
    check("wrong_image_control", h != NULL, "the file as it is: %s", h ? "accepted" : err);
    mbh_destroy(h);
    d[n / 2] ^= 1;
    err[0] = 0;
    h = mbh_create(d, (size_t)n, NULL, 22050, err, (int)sizeof err);
    check("wrong_image", h == NULL && strstr(err, "sha256") != NULL, "one byte changed: %s", h ? "accepted" : err);
    mbh_destroy(h);
    free(d);
}

int main(int argc, char **argv)
{
    char path[4096];
    FILE *f;
    if (argc < 2) {
        fprintf(stderr, "usage: test_mockingboard <firmware folder>\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s/%s", argv[1], MB_FILE);
    f = fopen(path, "rb");
    if (!f) {
        printf("skipped: no %s in %s\n", MB_FILE, argv[1]);
        return 77;
    }
    fclose(f);
    {
        /* 1.1's TEXT TO SPEECH, INFLECTION, IIE TTS DRIVER (the rules are in the language card) */
        static const unsigned prog11[3][2] = {{0x8C00, 0x8F97}, {0x9000, 0x92DB}, {0x9300, 0x93BE}};
        /* the early program but its settings (6A00h-6A03h, the host's), and its rules */
        static const unsigned prog_early[3][2] = {{0x6600, 0x69FF}, {0x6A04, 0x6DC3}, {0x6E00, 0x9356}};
        int pass;
        for (pass = 0; pass < 2; pass++) {
            const char *dir = pass == 0 ? argv[1] : argc > 2 ? argv[2] : NULL;
            if (!dir)
                break;
            if (pass == 1) {
                snprintf(path, sizeof path, "%s/%s", dir, MB_FILE_EARLY);
                f = fopen(path, "rb");
                if (!f) {
                    printf("skipped: no %s in %s\n", MB_FILE_EARLY, dir);
                    break;
                }
                fclose(f);
            }
            g_prefix = pass ? "early_" : "";
            g_file = pass ? MB_FILE_EARLY : MB_FILE;
            g_golden = pass ? GOLDEN_EARLY : GOLDEN;
            g_ngolden = pass ? (int)sizeof GOLDEN_EARLY : (int)sizeof GOLDEN;
            g_r3 = pass ? 0x70 : 0x50;
            g_guard_lo = pass ? 0x6600 : 0x8B00;
            memcpy(g_prog, pass ? prog_early : prog11, sizeof g_prog);
            t_golden(dir);
            t_streams(dir);
            t_clean_core(dir);
            t_settings(dir);
            t_busy_refused(dir);
            t_overflow_guard(dir);
            t_cancel(dir);
            t_wrong_image(dir);
        }
    }
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
