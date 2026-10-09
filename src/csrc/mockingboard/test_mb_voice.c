/* test_mb_voice.c -- the Mockingboard voice (mb_voice.h) on the real firmware file.
 *
 *     test_mb_voice <folder holding mockingboard-tts-1.1.bin>
 *
 *   steps          NVDA's rate and pitch onto the firmware's: 0, 50, 100 -> rate 0, 8, 13 and inflection 0, 8, 26
 *   parts_text     the shared text path: accented letters, currencies and number words ("café, 3 days, £2" ->
 *                  "cafe, three days, 2 pounds"); numbers off leaves the digits
 *   parts_split    a long text is cut at sentence ends first (twelve 66-character sentences: twelve parts, each a
 *                  sentence), then clause marks, then spaces; no part longer than MBV_PART; nothing lost
 *   speaks         a sentence through the voice: audio, done, the lead trimmed (speech within 300 samples: the host converts with the chip's time
 *                  standing still, so no lead trim is needed)
 *   long_text      600 characters of prose: every part said, done, about as long as its parts said one by one
 *   refused_split  sixty digits with numbers off (five frames a character): the host refuses the part, the voice
 *                  splits it and says it all
 *   cancel_next    a cancel in the middle, then another text: it speaks at once and completely
 *   volume         volume 50 is half the samples' level of volume 100
 *
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit 0/1 (77 without
 * the firmware file).  MIT.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_voice.h"

static int failures;
static const char *g_dir;

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

static mb_voice *make(void)
{
    char err[256] = "";
    mb_voice *v = mbv_create_dir(g_dir, 22050, err, (int)sizeof err);
    if (!v) {
        printf("FAIL create %s\nFAILED\n", err);
        exit(1);
    }
    return v;
}

typedef struct { long n; double peak, sum2; int done; long lead; int blocks; } said;

static said say(mb_voice *v, const char *text, int max_blocks)
{
    said s;
    memset(&s, 0, sizeof s);
    s.lead = -1;
    if (!mbv_speak(v, text, 0))
        return s;
    while (!s.done && s.blocks < max_blocks) {
        const short *p;
        int got = mbv_render(v, &p, &s.done), k;
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

static void t_steps(void)
{
    check("steps", mbv_rate_step(0) == 0 && mbv_rate_step(50) == 8 && mbv_rate_step(100) == 13
          && mbv_pitch_step(0) == 0 && mbv_pitch_step(50) == 8 && mbv_pitch_step(100) == 26,
          "rate %d %d %d, pitch %d %d %d", mbv_rate_step(0), mbv_rate_step(50), mbv_rate_step(100), mbv_pitch_step(0),
          mbv_pitch_step(50), mbv_pitch_step(100));
}

static void t_parts_text(void)
{
    char a[512], b[512];
    mbv_parts("caf\xc3\xa9, 3 days, \xc2\xa3" "2", 1, a, sizeof a);
    mbv_parts("caf\xc3\xa9, 3 days", 0, b, sizeof b);
    check("parts_text", strstr(a, "cafe") && strstr(a, "three days") && strstr(a, "pounds") && strstr(b, "3 days"),
          "numbers on \"%s\", off \"%s\"", a, b);
}

static void t_parts_split(void)
{
    static const char SENT[] = "This is a sentence, with a clause in it, that goes on for a while.";
    char text[2000], out[4000], joined[4000], first[200];
    int i, longest = 0, n = 1, cur = 0, ok;
    const char *bar;
    text[0] = 0;
    for (i = 0; i < 12; i++) {
        strcat(text, SENT);
        strcat(text, " ");
    }
    mbv_parts(text, 0, out, sizeof out);
    for (i = 0; out[i]; i++) {
        if (out[i] == '|') { n++; if (cur > longest) longest = cur; cur = 0; }
        else cur++;
    }
    if (cur > longest) longest = cur;
    bar = strchr(out, '|');
    snprintf(first, sizeof first, "%.*s", bar ? (int)(bar - out) : (int)strlen(out), out);
    strcpy(joined, out);
    for (i = 0; joined[i]; i++) if (joined[i] == '|') joined[i] = ' ';
    ok = longest <= MBV_PART && n == 12 && !strcmp(first, SENT) && strlen(joined) + 1 == strlen(text);
    check("parts_split", ok, "%d parts, the longest %d characters, the first \"%s\"", n, longest, first);
}

static void t_speaks(void)
{
    mb_voice *v = make();
    said s = say(v, "Hello, my name is Mockingboard.", 2000);
    check("speaks", s.done && s.n > 22050 && s.peak > 0.1 && s.lead >= 0 && s.lead <= 300,
          "%.2f s, peak %.2f, speech from sample %ld, done %d", s.n / 22050.0, s.peak, s.lead, s.done);
    mbv_destroy(v);
}

static void t_long(void)
{
    char text[700];
    mb_voice *v = make();
    said s;
    text[0] = 0;
    while (strlen(text) < 600)
        strcat(text, "The quick brown fox jumps over the lazy dog. ");
    s = say(v, text, 20000);
    check("long_text", s.done && mbv_get_int(v, "parts") >= 5 && s.n > 30 * 22050, "%d parts, %.1f s, done %d",
          mbv_get_int(v, "parts"), s.n / 22050.0, s.done);
    mbv_destroy(v);
}

static void t_refused_split(void)
{
    mb_voice *v = make();
    said s;
    mbv_set(v, 50, 50, 100, 0);
    s = say(v, "123456789012345678901234567890123456789012345678901234567890", 20000);
    check("refused_split", s.done && mbv_get_int(v, "split") >= 1 && s.n > 10 * 22050,
          "%d split, %d parts, %.1f s, done %d", mbv_get_int(v, "split"), mbv_get_int(v, "parts"), s.n / 22050.0, s.done);
    mbv_destroy(v);
}

static void t_cancel(void)
{
    mb_voice *v = make();
    said a, b;
    int k, done = 0;
    const short *p;
    mbv_speak(v, "This sentence is going to be cut off well before it is finished.", 0);
    for (k = 0; k < 20; k++)
        mbv_render(v, &p, &done);
    mbv_cancel(v);
    mbv_render(v, &p, &done);
    a.done = done;
    b = say(v, "Hello, my name is Mockingboard.", 2000);
    check("cancel_next", a.done && b.done && b.n > 22050 && b.lead >= 0 && b.lead <= 300,
          "done after cancel %d; next %.2f s, speech from sample %ld, done %d", a.done, b.n / 22050.0, b.lead, b.done);
    mbv_destroy(v);
}

static void t_volume(void)
{
    mb_voice *a = make(), *b = make();
    said x, y;
    mbv_set(b, 50, 50, 50, 1);
    x = say(a, "Hello there.", 2000);
    y = say(b, "Hello there.", 2000);
    check("volume", x.n == y.n && fabs(sqrt(y.sum2 / x.sum2) - 0.5) < 0.01, "level ratio %.3f (samples %ld, %ld)",
          sqrt(y.sum2 / (x.sum2 + 1e-30)), x.n, y.n);
    mbv_destroy(a);
    mbv_destroy(b);
}

int main(int argc, char **argv)
{
    char path[4096];
    FILE *f;
    if (argc < 2) {
        fprintf(stderr, "usage: test_mb_voice <firmware folder>\n");
        return 2;
    }
    g_dir = argv[1];
    snprintf(path, sizeof path, "%s/%s", g_dir, MB_FILE);
    f = fopen(path, "rb");
    if (!f) {
        printf("skipped: no %s in %s\n", MB_FILE, g_dir);
        return 77;
    }
    fclose(f);
    t_steps();
    t_parts_text();
    t_parts_split();
    t_speaks();
    t_long();
    t_refused_split();
    t_cancel();
    t_volume();
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
