/* test_sv_voice.c -- the voice of Speech Systems' Super Voice (sv_voice.h) on the cartridge's own SUPERTOK.BIN.
 *
 *     test_sv_voice <folder holding SUPERTOK.BIN>
 *
 *   steps          NVDA's rate and pitch onto the program's: 0, 50, 100 -> ^S 1, 11, 16 and ^IT 1, 14, 32
 *   parts_text     the shared text path: accented letters, currencies and number words ("café, 3 days, £2" ->
 *                  "cafe, three days, 2 pounds"); numbers off leaves the digits to the program
 *   parts_rules    the voice's own rules for this program: its control character ^ and its "quote" taken out, a
 *                  hyphen made a space but not in front of a digit, a space before a period that follows a digit
 *                  but not inside a decimal number
 *   parts_split    a long text is cut at sentence ends first (twelve 66-character sentences: four parts of three),
 *                  no part longer than SVV_PART, nothing lost
 *   default_plain  at rate 50, pitch 50 the chip is written exactly as by the host given the bare text: the voice
 *                  adds no control code
 *   settings       rate 100 writes the chip's rate 15 and ends sooner than rate 0; pitch 100 writes the inflection
 *                  level 32 (R1 FAh); a capital's offset of 50 at pitch 50 does the same
 *   speaks         a sentence through the voice: audio, done, speech within 400 samples (its first phoneme is
 *                  written at once; it is an h, which takes 14 ms to pass the 0.003 this looks for)
 *   long_text      600 characters of prose: every part said, done
 *   point          "Page 1." takes no longer than "Page 1" and a pause: the program's "point" is not said
 *   cancel_next    a cancel in the middle, then "HI THERE": done after the cancel; the next text is the golden
 *                  phonemes, starts within the program's opening pause and is whole
 *   speak_over     a text while another is being said, without a cancel: the new one is the golden phonemes alone
 *   volume         volume 50 is half the samples' level of volume 100
 *
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit 0/1 (77 without
 * the program file).  MIT.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_voice.h"

static int failures;
static const char *g_dir;
static const unsigned char GOLDEN[] = {0xC0, 0x6C, 0x4F, 0x4B, 0x43, 0xC0, 0x75, 0x4B, 0x48, 0x4B, 0x5E, 0xC0, 0xC0};

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

static sv_voice *make(void)
{
    char err[256] = "";
    sv_voice *v = svv_create_dir(g_dir, 22050, err, (int)sizeof err);
    if (!v) {
        printf("FAIL create %s\nFAILED\n", err);
        exit(1);
    }
    svh_set_int(svv_host(v), "log_writes", 1);
    return v;
}

typedef struct { long n; double peak, sum2; int done; long lead; int blocks; } said;

/* renders the utterance in progress to its end */
static said hear(sv_voice *v, int max_blocks)
{
    said s;
    memset(&s, 0, sizeof s);
    s.lead = -1;
    while (!s.done && s.blocks < max_blocks) {
        const short *p;
        int got = svv_render(v, &p, &s.done), k;
        for (k = 0; k < got; k++) {
            double x = p[k] / 32768.0;
            if (s.lead < 0 && fabs(x) > 0.003)
                s.lead = s.n + k;               /* where speech starts */
            if (fabs(x) > s.peak) s.peak = fabs(x);
            s.sum2 += x * x;
        }
        s.n += got;
        s.blocks++;
    }
    return s;
}

static said say(sv_voice *v, const char *text, int offset, int max_blocks)
{
    said s;
    memset(&s, 0, sizeof s);
    s.lead = -1;
    svh_clear_writes(svv_host(v));
    if (!svv_speak(v, text, offset))
        return s;
    return hear(v, max_blocks);
}

/* 1 when the host's R0 writes are the golden "HI THERE" */
static int golden(sv_voice *v)
{
    const svh_write *w;
    int n = svh_writes(svv_host(v), &w), k, m = 0;
    for (k = 0; k < n; k++)
        if (w[k].reg == 0) {
            if (m >= (int)sizeof GOLDEN || w[k].val != GOLDEN[m])
                return 0;
            m++;
        }
    return m == (int)sizeof GOLDEN;
}

/* how many times the host wrote `val` (masked) to `reg` */
static int wrote(sv_voice *v, int reg, int mask, int val)
{
    const svh_write *w;
    int n = svh_writes(svv_host(v), &w), k, c = 0;
    for (k = 0; k < n; k++)
        c += w[k].reg == reg && (w[k].val & mask) == val;
    return c;
}

static void t_steps(void)
{
    check("steps", svv_rate_step(0) == 1 && svv_rate_step(50) == 11 && svv_rate_step(100) == 16
          && svv_pitch_step(0) == 1 && svv_pitch_step(50) == 14 && svv_pitch_step(100) == 32,
          "rate %d %d %d, pitch %d %d %d", svv_rate_step(0), svv_rate_step(50), svv_rate_step(100), svv_pitch_step(0),
          svv_pitch_step(50), svv_pitch_step(100));
}

static void t_parts_text(void)
{
    char a[512], b[512];
    svv_parts("caf\xc3\xa9, 3 days, \xc2\xa3" "2", 1, a, sizeof a);
    svv_parts("caf\xc3\xa9, 3 days", 0, b, sizeof b);
    check("parts_text", strstr(a, "cafe") && strstr(a, "three days") && strstr(a, "pounds") && strstr(b, "3 days"),
          "numbers on \"%s\", off \"%s\"", a, b);
}

static void t_parts_rules(void)
{
    char a[512];
    svv_parts("He said \"hi\" ^V0 to a well-known user - on page 1. It is -5 or 3.5.", 0, a, sizeof a);
    check("parts_rules", !strcmp(a, "He said hi V0 to a well known user on page 1 . It is -5 or 3.5 ."), "\"%s\"", a);
}

static void t_parts_split(void)
{
    static const char SENT[] = "This is a sentence, with a clause in it, that goes on for a while.";
    char text[2000], out[4000], joined[4000];
    int i, longest = 0, n = 1, cur = 0, ends = 1, ok;
    text[0] = 0;
    for (i = 0; i < 12; i++) {
        strcat(text, SENT);
        strcat(text, " ");
    }
    svv_parts(text, 0, out, sizeof out);
    for (i = 0; out[i]; i++) {
        if (out[i] == '|') { n++; if (cur > longest) longest = cur; cur = 0; ends = ends && i > 0 && out[i - 1] == '.'; }
        else cur++;
    }
    if (cur > longest) longest = cur;
    strcpy(joined, out);
    for (i = 0; joined[i]; i++) if (joined[i] == '|') joined[i] = ' ';
    ok = longest <= SVV_PART && n == 4 && ends && strlen(joined) + 1 == strlen(text);
    check("parts_split", ok, "%d parts, the longest %d characters, each ending a sentence %d, %d of %d characters", n,
          longest, ends, (int)strlen(joined) + 1, (int)strlen(text));
}

static void t_default_plain(void)
{
    char err[64] = "";
    sv_voice *v = make();
    sv_host *h = svh_create_dir(g_dir, NULL, 22050, err, (int)sizeof err);
    const svh_write *a, *b;
    const double *y;
    int na, nb, k, same;
    double t = 0.0;
    say(v, "HI THERE", 0, 2000);
    svh_set_int(h, "log_writes", 1);
    svh_say(h, (const unsigned char *)"HI THERE", 8);
    while (svh_busy(h) && t < 10.0) {
        svh_run(h, 0.03, &y);
        t += 0.03;
    }
    na = svh_writes(svv_host(v), &a);
    nb = svh_writes(h, &b);
    same = na == nb && na > 10;
    for (k = 0; same && k < na; k++)
        same = a[k].reg == b[k].reg && a[k].val == b[k].val;
    check("default_plain", same, "the voice's %d chip writes, the host's %d for the bare text: %s", na, nb,
          same ? "the same" : "DIFFERENT");
    svh_destroy(h);
    svv_destroy(v);
}

static void t_settings(void)
{
    sv_voice *v = make();
    said slow, fast;
    int r15, r0, p32, cap32;
    svv_set(v, 100, 50, 100, 0);
    fast = say(v, "HI THERE", 0, 2000);
    r15 = wrote(v, 2, 0xF0, 0xF0);
    svv_set(v, 0, 50, 100, 0);
    slow = say(v, "HI THERE", 0, 2000);
    r0 = wrote(v, 2, 0xF0, 0x00);
    svv_set(v, 50, 100, 100, 0);
    say(v, "HI THERE", 0, 2000);
    p32 = wrote(v, 1, 0xFF, 0xFA);
    svv_set(v, 50, 50, 100, 0);
    say(v, "HI THERE", 50, 2000);
    cap32 = wrote(v, 1, 0xFF, 0xFA);
    check("settings", r15 >= 1 && r0 >= 1 && fast.done && slow.done && slow.n > 3 * fast.n && p32 >= 1 && cap32 >= 1,
          "rate 100: R2's rate 15 written %d time(s), %.2f s; rate 0: rate 0 %d time(s), %.2f s; pitch 100: R1 FAh %d "
          "time(s); a capital at pitch 50: %d time(s)", r15, fast.n / 22050.0, r0, slow.n / 22050.0, p32, cap32);
    svv_destroy(v);
}

static void t_speaks(void)
{
    sv_voice *v = make();
    said s = say(v, "Hello, my name is Super Voice.", 0, 2000);
    check("speaks", s.done && s.n > 22050 && s.peak > 0.1 && s.lead >= 0 && s.lead <= 400,
          "%.2f s, peak %.2f, speech from sample %ld, done %d", s.n / 22050.0, s.peak, s.lead, s.done);
    svv_destroy(v);
}

static void t_long(void)
{
    char text[700];
    sv_voice *v = make();
    said s;
    text[0] = 0;
    while (strlen(text) < 600)
        strcat(text, "The quick brown fox jumps over the lazy dog. ");
    s = say(v, text, 0, 20000);
    check("long_text", s.done && svv_get_int(v, "parts") >= 3 && s.n > 30 * 22050, "%d parts, %.1f s, done %d",
          svv_get_int(v, "parts"), s.n / 22050.0, s.done);
    svv_destroy(v);
}

static void t_point(void)
{
    sv_voice *v = make();
    said with = say(v, "Page 1.", 0, 2000), without = say(v, "Page 1", 0, 2000);
    double extra = (with.n - without.n) / 22050.0;
    check("point", with.done && without.done && extra < 0.3, "\"Page 1.\" %.2f s, \"Page 1\" %.2f s (the program's "
          "\"point\" is 0.69 s)", with.n / 22050.0, without.n / 22050.0);
    svv_destroy(v);
}

static void t_cancel(void)
{
    sv_voice *v = make();
    said b;
    int k, done = 0, after;
    const short *p;
    svv_speak(v, "This sentence is going to be cut off well before it is finished.", 0);
    for (k = 0; k < 20; k++)
        svv_render(v, &p, &done);
    svv_cancel(v);
    svv_render(v, &p, &done);
    after = done;
    b = say(v, "HI THERE", 0, 2000);
    /* the opening pause is 27.5 ms, 607 samples; the first phoneme, HF, takes a few ms more to pass 0.003 */
    check("cancel_next", after && b.done && golden(v) && b.lead >= 0 && b.lead <= 900 && b.n > 15000 && b.n < 22050,
          "done after the cancel %d; the next text golden %d, %.2f s, speech from sample %ld, done %d", after,
          golden(v), b.n / 22050.0, b.lead, b.done);
    svv_destroy(v);
}

static void t_speak_over(void)
{
    sv_voice *v = make();
    said b;
    int k, done = 0;
    const short *p;
    svv_speak(v, "This sentence is going to be spoken over well before it is finished.", 0);
    for (k = 0; k < 20; k++)
        svv_render(v, &p, &done);
    b = say(v, "HI THERE", 0, 2000);
    check("speak_over", b.done && golden(v) && b.n > 15000 && b.n < 22050 && b.lead <= 900,
          "the new text golden %d, %.2f s, speech from sample %ld, done %d", golden(v), b.n / 22050.0, b.lead, b.done);
    svv_destroy(v);
}

static void t_volume(void)
{
    sv_voice *a = make(), *b = make();
    said x, y;
    svv_set(b, 50, 50, 50, 0);
    x = say(a, "Hello there.", 0, 2000);
    y = say(b, "Hello there.", 0, 2000);
    check("volume", x.n == y.n && fabs(sqrt(y.sum2 / x.sum2) - 0.5) < 0.01, "level ratio %.3f (samples %ld, %ld)",
          sqrt(y.sum2 / (x.sum2 + 1e-30)), x.n, y.n);
    svv_destroy(a);
    svv_destroy(b);
}

int main(int argc, char **argv)
{
    char path[4096];
    FILE *f;
    if (argc < 2) {
        fprintf(stderr, "usage: test_sv_voice <firmware folder>\n");
        return 2;
    }
    g_dir = argv[1];
    snprintf(path, sizeof path, "%s/%s", g_dir, SV_FILE);
    f = fopen(path, "rb");
    if (!f) {
        printf("skipped: no %s in %s\n", SV_FILE, g_dir);
        return 77;
    }
    fclose(f);
    t_steps();
    t_parts_text();
    t_parts_rules();
    t_parts_split();
    t_default_plain();
    t_settings();
    t_speaks();
    t_long();
    t_point();
    t_cancel();
    t_speak_over();
    t_volume();
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
