/* test_android_native.c -- the app's native part without a phone: the front end the JNI bridge calls (ssa_engine.c,
 * ssa_map.c, ssa_import.c), linked with the same chip, boards, hosts and voices, driven the way SsiTtsService drives
 * it -- an Android request's rate and pitch percentages on top of the app's sliders, the audio pulled in chunks, a
 * stop between chunks and a cancel after it -- and each case's PCM hashed.
 *
 *     test_android_native <data folder> [<aicom folder> [<SPKEMS.DVC> [<the Mockingboards' built-in files' folder>]]]
 *                                           one line per case: "<name> <samples> <fnv-1a 64 of the PCM bytes>";
 *                                           with the Aicom folder (u2/u3/u4.BIN), the Accent SA's cases first; with
 *                                           the Accent-mini's driver ("-": none), its cases; the Speak-Out's when the
 *                                           data folder has SPEAKOUT.HEX, each Mockingboard's when it has its file
 *                                           (mockingboard-tts-1.1.bin, mockingboard-tts-early.bin), and with the last
 *                                           folder the built-in copies' cases; the Braille Lite's, lockstep and run
 *                                           ahead, and its number words on and off (English, and Spanish when there)
 *     test_android_native --texts           stdin's lines as the Accent SA's front end sends them
 *     test_android_native --so-direct <SPEAKOUT.HEX>
 *                                           the Speak-Out's reference: so_voice driven directly (sov_create, sov_set,
 *                                           sov_speak, sov_render, sov_cancel), one unit as the app keeps one; stdin:
 *                                           "<name> <sample rate> <rate> <pitch> <tone> <volume> <join> <short>
 *                                           <pitch offset> <blocks before a cancel> <text, hex>" per case
 *     test_android_native --am-direct <SPKEMS.DVC>
 *                                           the Accent-mini's reference: am_voice driven directly (amv_create,
 *                                           amv_set, amv_speak, amv_render, amv_cancel), one unit as the app keeps
 *                                           one, made again when the boot key changes; stdin: "<name> <boot key>
 *                                           <sample rate> <rate> <pitch> <inflection> <volume> <numbers> <voice>
 *                                           <pitch offset> <blocks before a cancel> <text, hex>" per case
 *     test_android_native --mb-direct <mockingboard-tts-1.1.bin or mockingboard-tts-early.bin>
 *                                           a Mockingboard's reference: mb_voice driven directly (mbv_create,
 *                                           mbv_set, mbv_speak, mbv_render, mbv_cancel), one unit as the app keeps
 *                                           one, made again at a new sample rate; stdin: "<name> <sample rate> <rate>
 *                                           <pitch> <volume> <numbers> <pitch offset> <blocks before a cancel>
 *                                           <text, hex>" per case
 *     test_android_native --import <file> <out>
 *                                           the import's judgement (ssa_import_firmware, as nativeImportFirmware):
 *                                           "import <code> <message>"
 *
 * test_android_native.py compares the hashes with bl_voice driven directly, the way the speech-dispatcher module
 * maps SSIP (the reference), and the Accent SA's with the NVDA Accent driver itself (accent_reference.py), on the
 * desktop and over adb.  SSI263_ANDROID_TEST_BREAK in the environment puts a bug back, the controls: 1 breaks the rate
 * mapping (ssa_map.h), so the "fast" cases must differ; accent-pitch, accent-glide, accent-reuse and accent-step are
 * ssa_engine.h's ssa_accent_break 1, 2, 3 and 4; speakout-pitch, speakout-settings, run-ahead, numbers,
 * mockingboard-pitch, mockingboard-numbers, mockingboard-import-ignored, mockingboard-builtin-ignored and
 * mockingboard-variant its ssa_voice_break 1 to 9; import-hash and import-dsk ssa_import.h's
 * ssa_import_break 1 and 2.
 *
 * Built by the desktop compiler (test_android_native.py) and by build_android.sh --test (static, for a device).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssa_engine.h"
#include "ssa_import.h"
#include "ssa_map.h"
#include "accentsa/as_voice.h"
#include "speakout/so_voice.h"
#include "accentmini/am_voice.h"
#include "mockingboard/mb_voice.h"

static const char *HELLO = "Hello there. This is the Braille Lite, speaking on a phone.";
static const char *LONG = "This is a long message for the stop test, with a comma or two, that keeps going well "
                          "past the moment the harness says stop. It has a second sentence as well.";
/* the Braille Lite's number words: English's, and Spain's ("1.234.567" one number, "3,5" tres coma cinco) */
static const char *NUM_EN = "It is 1,234,567 steps, 3.5 miles and $12.50.";
static const char *NUM_ES = "Son 1.234.567 pasos y 3,5 kilos.";

typedef struct {
    unsigned long long h;
    long samples;
} digest;

static void add(digest *d, const short *pcm, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned v = (unsigned short)pcm[i];
        d->h = (d->h ^ (v & 0xFF)) * 1099511628211ULL;        /* little-endian bytes, as the app hands them over */
        d->h = (d->h ^ (v >> 8)) * 1099511628211ULL;
    }
    d->samples += n;
}

static void report(const char *name, const digest *d)
{
    printf("%s %ld %016llx\n", name, d->samples, d->h);
}

/* One utterance through the app's path, pulled `chunk` samples at a time.  stop_after > 0: stop once the unit has
   rendered that many non-empty blocks, check the stop lands before another block, and cancel.  -1 on a failure. */
static int speak(ssa_engine *e, int voice, const char *text, const ssa_settings *s, int req_rate, int req_pitch,
                 int chunk, int stop_after, digest *d)
{
    short buf[8192];
    int r = ssa_start(e, voice, text, s, req_rate, req_pitch), n;
    d->h = 1469598103934665603ULL;
    d->samples = 0;
    if (r < 0) { fprintf(stderr, "ssa_start failed on %s\n", text); return -1; }
    if (r == 1) return 0;
    while ((n = ssa_pull(e, buf, chunk)) > 0) {
        add(d, buf, n);
        if (stop_after > 0 && ssa_blocks(e) == stop_after) {
            int blocks = ssa_blocks(e);
            ssa_stop(e);
            /* what is left of the current block may still come; no new block may be rendered */
            while ((n = ssa_pull(e, buf, chunk)) > 0) add(d, buf, n);
            if (n != -2 || ssa_blocks(e) != blocks) {
                fprintf(stderr, "the stop did not land: pull %d, blocks %d -> %d\n", n, blocks, ssa_blocks(e));
                return -1;
            }
            ssa_cancel(e);
            return 0;
        }
    }
    if (n < 0) { fprintf(stderr, "pull returned %d\n", n); return -1; }
    return 0;
}

/* ---- the Accent SA (the built-in voice): the ROMs in memory, as the app hands them over ------------------------- */

static unsigned char *slurp(const char *dir, const char *name, size_t *n)
{
    char p[1200];
    FILE *f;
    unsigned char *d;
    long len;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (!(f = fopen(p, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
    fclose(f);
    *n = (size_t)len;
    return d;
}

static const char *HELLO_A = "Hello there. This is the Accent SA, speaking on a phone.";
static const char *LONG_A = "This is a long message for the stop test, with a comma or two, that keeps going well past "
                            "the moment the harness says stop. It has a second sentence as well.";
static const char *NUMBERS_A = "You owe $1234.50 for 3 items: 100 percent, the 21st of 1,000,000 and -2.5 degrees.";
static const char *TEXT_A = "It\xe2\x80\x99s \xe2\x80\x9cquoted\xe2\x80\x9d \xe2\x80\x93 see ~/code\xe2\x80\xa6 "
                            "\xc2\xa3" "2.63, 5 \xe2\x82\xac and caf\xc3\xa9\tend \xf0\x9f\x8e\x89";
static const char *CAP_A = "B";

/* test_android_native.py's ACCENT_CASES, in the same order: name, text, the app's rate and pitch sliders, the
   request's rate and pitch percentages, the engine volume, inflection, sample rate, pull size, blocks before a stop */
typedef struct {
    const char *name, *text;
    int rate, pitch, req_rate, req_pitch, volume, inflection, sample_rate, chunk, stop;
} accent_case;

static const accent_case *accent_cases(int *n)
{
    static accent_case c[24];
    int k = 0;
#define A(nm, tx, r, p, rr, rp, v, inf, sr, ch, st) \
    do { accent_case x = {nm, tx, r, p, rr, rp, v, inf, sr, ch, st}; c[k++] = x; } while (0)
    A("a-default", HELLO_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-default-97", HELLO_A, 50, 50, 100, 100, 100, 1, 22050, 97, 0);
    A("a-fast", HELLO_A, 50, 50, 200, 100, 100, 1, 22050, 4096, 0);
    A("a-slow-low", HELLO_A, 30, 50, 100, 50, 100, 1, 22050, 4096, 0);
    A("a-sliders", HELLO_A, 70, 80, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-numbers", NUMBERS_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-text", TEXT_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-pitch-100", CAP_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-pitch-150", CAP_A, 50, 50, 100, 150, 100, 1, 22050, 4096, 0);
    A("a-pitch-75", CAP_A, 50, 50, 100, 75, 100, 1, 22050, 4096, 0);
    A("a-pitch-120", CAP_A, 50, 50, 100, 120, 100, 1, 22050, 4096, 0);
    A("a-pitch-100-again", CAP_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-stopped", LONG_A, 50, 50, 100, 100, 100, 1, 22050, 4096, 5);
    A("a-after-stop", "Next message.", 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    A("a-volume-150", HELLO_A, 50, 50, 100, 100, 150, 1, 22050, 4096, 0);
    A("a-monotone", HELLO_A, 50, 50, 100, 100, 100, 0, 22050, 4096, 0);
    A("a-11k", HELLO_A, 50, 50, 100, 100, 100, 1, 11025, 4096, 0);
#undef A
    *n = k;
    return c;
}

static int accent(const char *aicom)
{
    size_t n2, n3, n4;
    unsigned char *u2 = slurp(aicom, "u2.BIN", &n2), *u3 = slurp(aicom, "u3.BIN", &n3), *u4 = slurp(aicom, "u4.BIN", &n4);
    ssa_engine *e = ssa_new(".");
    const accent_case *c;
    char err[256];
    digest d;
    int n, i, rc = 0;
    if (!u2 || !u3 || !u4 || !e || !ssa_set_accent_roms(e, u2, n2, u3, n3, u4, n4)) {
        fprintf(stderr, "the Accent SA's ROMs in %s: missing or the wrong size\n", aicom);
        return 1;
    }
    free(u2); free(u3); free(u4);                      /* the engine keeps its own copy */
    if (!ssa_has_voice(e, SSA_ACCENT_SA)) { fprintf(stderr, "no Accent SA\n"); return 1; }
    c = accent_cases(&n);
    for (i = 0; i < n && !rc; i++) {
        ssa_settings s = {c[i].rate, c[i].pitch, 7, c[i].volume, 1};
        ssa_configure(e, c[i].sample_rate, c[i].inflection, 0);
        if (ssa_load(e, SSA_ACCENT_SA, err, sizeof err) != 0) { fprintf(stderr, "boot: %s\n", err); rc = 1; break; }
        if (speak(e, SSA_ACCENT_SA, c[i].text, &s, c[i].req_rate, c[i].req_pitch, c[i].chunk, c[i].stop, &d))
            rc = 1;
        else
            report(c[i].name, &d);
    }
    ssa_free(e);
    return rc;
}

/* --texts: each line of stdin (a text's UTF-8, in hex) as the Accent SA's front end would send it, hex: "text <line>
   <hex>" */
static int texts(void)
{
    char line[8192], text[4096], out[65536];
    int k = 0;
    while (fgets(line, sizeof line, stdin)) {
        int n, i;
        unsigned b;
        line[strcspn(line, "\r\n")] = 0;
        for (n = 0; line[2 * n] && line[2 * n + 1] && n < (int)sizeof text - 1; n++) {
            sscanf(line + 2 * n, "%2x", &b);
            text[n] = (char)b;
        }
        text[n] = 0;
        n = asv_say_bytes(text, 1, out, (int)sizeof out);
        if (n < 0 || n >= (int)sizeof out) { fprintf(stderr, "say_bytes failed on line %d\n", k); return 1; }
        printf("text %d ", k++);
        for (i = 0; i < n; i++) printf("%02x", (unsigned char)out[i]);
        printf("\n");
    }
    return 0;
}

/* --level <data folder> <aicom folder> <voice> <engine volume> [<SPKEMS.DVC>]: each line of stdin (hex UTF-8) spoken
   through the app's path at the app's default settings and that volume: "level <line> <peak> <clipped> <sum of
   squares> <samples over -40 dBFS>" (test_volume_headroom.py) */
static int level(const char *data, const char *aicom, int voice, int volume, const char *dvc_path)
{
    size_t n2, n3, n4;
    unsigned char *u2 = slurp(aicom, "u2.BIN", &n2), *u3 = slurp(aicom, "u3.BIN", &n3), *u4 = slurp(aicom, "u4.BIN", &n4);
    ssa_engine *e = ssa_new(data);
    ssa_settings s;
    char line[8192], text[4096];
    short buf[4096];
    int k = 0;
    ssa_default_settings(&s);
    s.volume = volume;
    if (dvc_path) {                          /* the Accent-mini's driver, in memory as the app hands it over */
        FILE *f = fopen(dvc_path, "rb");
        unsigned char *dvc = NULL;
        long len = 0;
        if (f) {
            fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
            dvc = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
            if (dvc && fread(dvc, 1, (size_t)len, f) != (size_t)len) len = 0;
            fclose(f);
        }
        if (e && dvc && len > 0) ssa_set_accent_mini(e, dvc, (size_t)len);
        free(dvc);
    }
    if (!e || !u2 || !u3 || !u4 || !ssa_set_accent_roms(e, u2, n2, u3, n3, u4, n4) || !ssa_has_voice(e, voice)) {
        fprintf(stderr, "voice %d cannot speak\n", voice);
        return 1;
    }
    while (fgets(line, sizeof line, stdin)) {
        int n, i, peak = 0;
        long clipped = 0, loud = 0;
        double sq = 0;
        unsigned b;
        line[strcspn(line, "\r\n")] = 0;
        for (n = 0; line[2 * n] && line[2 * n + 1] && n < (int)sizeof text - 1; n++) {
            sscanf(line + 2 * n, "%2x", &b);
            text[n] = (char)b;
        }
        text[n] = 0;
        if (ssa_start(e, voice, text, &s, 100, 100) < 0) { fprintf(stderr, "start failed\n"); return 1; }
        while ((n = ssa_pull(e, buf, 4096)) > 0)
            for (i = 0; i < n; i++) {
                int v = buf[i] < 0 ? -buf[i] : buf[i];
                if (v > peak) peak = v;
                clipped += v >= 32767;
                if (v > 328) { sq += (double)v * v; loud++; }       /* over -40 dBFS: speech, not the pauses */
            }
        printf("level %d %d %ld %.0f %ld\n", k++, peak, clipped, sq, loud);
    }
    ssa_free(e);
    free(u2); free(u3); free(u4);
    return 0;
}

/* ---- the Speak-Out (imported): one unit kept across the cases, as the app keeps it ---------------------------- */

static const char *HELLO_S = "Hello there. This is the Speak-Out, speaking on a phone.";
static const char *SETTINGS_S = "One. Two, three! Four? Five, $3.50.";
static const char *CAP_S = "B";

/* test_android_native.py's SPEAKOUT_CASES, in the same order: name, text, the app's rate and pitch sliders, the
   request's rate and pitch percentages, volume, tone, join, short pauses, sample rate, pull size, blocks before a
   stop */
typedef struct {
    const char *name, *text;
    int rate, pitch, req_rate, req_pitch, volume, tone, join, shrt, sample_rate, chunk, stop;
} speakout_case;

static const speakout_case *speakout_cases(int *n)
{
    static speakout_case c[24];
    int k = 0;
#define S(nm, tx, r, p, rr, rp, v, t, j, sh, sr, ch, st) \
    do { speakout_case x = {nm, tx, r, p, rr, rp, v, t, j, sh, sr, ch, st}; c[k++] = x; } while (0)
    S("s-default", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-default-97", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 97, 0);
    S("s-fast", HELLO_S, 50, 50, 200, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-sliders", HELLO_S, 70, 80, 100, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-settings", SETTINGS_S, 50, 50, 100, 100, 100, 0, 0, 0, 22050, 4096, 0);
    S("s-pitch-100", CAP_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-pitch-150", CAP_S, 50, 50, 100, 150, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-pitch-75", CAP_S, 50, 50, 100, 75, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-pitch-120", CAP_S, 50, 50, 100, 120, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-pitch-100-again", CAP_S, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-stopped", LONG, 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 8);
    S("s-after-stop", "Next message.", 50, 50, 100, 100, 100, 8, 1, 1, 22050, 4096, 0);
    S("s-volume-150", HELLO_S, 50, 50, 100, 100, 150, 8, 1, 1, 22050, 4096, 0);
    S("s-11k", HELLO_S, 50, 50, 100, 100, 100, 8, 1, 1, 11025, 4096, 0);
#undef S
    *n = k;
    return c;
}

/* SSI263_ANDROID_TEST_ONLY (a comma list of test_android_native.py's blocks): runs only those; every block without it */
static int block(const char *name)
{
    const char *only = getenv("SSI263_ANDROID_TEST_ONLY");
    size_t n = strlen(name);
    const char *p;
    if (!only || !*only) return 1;
    for (p = only; (p = strstr(p, name)) != NULL; p += n)
        if ((p == only || p[-1] == ',') && (p[n] == ',' || !p[n])) return 1;
    return 0;
}

static int speakout(const char *data)
{
    ssa_engine *e = ssa_new(data);
    const speakout_case *c;
    char err[256];
    digest d;
    int n, i, rc = 0;
    if (!e) return 1;
    if (!ssa_has_voice(e, SSA_SPEAKOUT)) { ssa_free(e); return 0; }      /* no SPEAKOUT.HEX: no cases (said by the .py) */
    c = speakout_cases(&n);
    for (i = 0; i < n && !rc; i++) {
        ssa_settings s;
        ssa_default_settings(&s);
        s.rate = c[i].rate; s.pitch = c[i].pitch; s.volume = c[i].volume;
        s.so_tone = c[i].tone; s.so_join = c[i].join; s.so_short = c[i].shrt;
        ssa_configure(e, c[i].sample_rate, 1, 0);
        if (ssa_load(e, SSA_SPEAKOUT, err, sizeof err) != 0) { fprintf(stderr, "boot: %s\n", err); rc = 1; break; }
        if (speak(e, SSA_SPEAKOUT, c[i].text, &s, c[i].req_rate, c[i].req_pitch, c[i].chunk, c[i].stop, &d))
            rc = 1;
        else
            report(c[i].name, &d);
    }
    ssa_free(e);
    return rc;
}

static int unhex(const char *h, char *out, int cap)
{
    int n;
    unsigned b;
    for (n = 0; h[2 * n] && h[2 * n + 1] && n < cap - 1; n++) {
        sscanf(h + 2 * n, "%2x", &b);
        out[n] = (char)b;
    }
    out[n] = 0;
    return n;
}

/* --so-direct: the reference -- so_voice itself, driven as so_voice.h says, with the settings the .py computed */
static int so_direct(const char *hex)
{
    char line[16384], name[64], texthex[8192], text[4096], err[256];
    so_voice *v = NULL;
    int cur_rate = 0;
    while (fgets(line, sizeof line, stdin)) {
        int sr, rate, pitch, tone, volume, join, shrt, offset, stop, done = 0, blocks = 0, n;
        const short *pcm;
        digest d = {1469598103934665603ULL, 0};
        texthex[0] = 0;
        if (sscanf(line, "%63s %d %d %d %d %d %d %d %d %d %8191s", name, &sr, &rate, &pitch, &tone, &volume, &join,
                   &shrt, &offset, &stop, texthex) < 10)
            continue;
        unhex(texthex, text, sizeof text);
        if (!v || sr != cur_rate) {          /* the app boots a new unit at a new sample rate (ssa_configure) */
            sov_destroy(v);
            v = sov_create(hex, (double)sr, err, sizeof err);
            if (!v) { fprintf(stderr, "sov_create: %s\n", err); return 1; }
            cur_rate = sr;
        }
        sov_set(v, rate, pitch, tone, volume, join, shrt);
        if (sov_speak(v, text, offset))
            while (!done) {
                n = sov_render(v, &pcm, &done);
                if (n > 0) {
                    add(&d, pcm, n);
                    if (stop > 0 && ++blocks == stop && !done) {
                        sov_cancel(v);
                        break;
                    }
                }
            }
        report(name, &d);
    }
    sov_destroy(v);
    return 0;
}

/* ---- the Accent-mini (built in): its driver in memory, one unit kept, as the app keeps it -------------------------- */

static const char *HELLO_M = "Hello there. This is the Accent-mini, speaking on a phone.";

/* test_android_native.py's MINI_CASES, in the same order: name, text, the app's rate and pitch sliders, the request's
   rate and pitch percentages, volume, inflection (the app's boot setting), blocks before a stop */
typedef struct {
    const char *name, *text;
    int rate, pitch, req_rate, req_pitch, volume, inflection, stop;
} mini_case;

static const mini_case *mini_cases(int *n)
{
    static mini_case c[16];
    int k = 0;
#define M(nm, tx, r, p, rr, rp, v, inf, st) \
    do { mini_case x = {nm, tx, r, p, rr, rp, v, inf, st}; c[k++] = x; } while (0)
    M("m-default", HELLO_M, 50, 50, 100, 100, 100, 1, 0);
    M("m-fast", HELLO_M, 50, 50, 200, 100, 100, 1, 0);
    M("m-sliders", HELLO_M, 70, 80, 100, 100, 150, 1, 0);
    M("m-pitch-100", "B", 50, 50, 100, 100, 100, 1, 0);
    M("m-pitch-150", "B", 50, 50, 100, 150, 100, 1, 0);
    M("m-pitch-75", "B", 50, 50, 100, 75, 100, 1, 0);
    M("m-stopped", LONG, 50, 50, 100, 100, 100, 1, 8);
    M("m-after-stop", "Next message, $3.50.", 50, 50, 100, 100, 100, 1, 0);
    M("m-monotone", HELLO_M, 50, 50, 100, 100, 100, 0, 0);
#undef M
    *n = k;
    return c;
}

static int mini(const char *dvc_path)
{
    size_t n;
    unsigned char *dvc = slurp(".", dvc_path, &n);
    ssa_engine *e = ssa_new(".");
    const mini_case *c;
    char err[256];
    digest d;
    int k, i, rc = 0;
    if (!dvc) {                              /* an absolute path */
        FILE *f = fopen(dvc_path, "rb");
        long len;
        if (f) {
            fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
            dvc = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
            n = dvc ? fread(dvc, 1, (size_t)len, f) : 0;
            fclose(f);
        }
    }
    if (!e || !dvc || !ssa_set_accent_mini(e, dvc, n)) {
        fprintf(stderr, "the Accent-mini's driver %s: missing, or this build has no Accent-mini\n", dvc_path);
        return 1;
    }
    free(dvc);                               /* the engine keeps its own copy */
    c = mini_cases(&k);
    for (i = 0; i < k && !rc; i++) {
        ssa_settings s;
        ssa_default_settings(&s);
        s.rate = c[i].rate; s.pitch = c[i].pitch; s.volume = c[i].volume;
        ssa_configure(e, 22050, c[i].inflection, 0);
        if (ssa_load(e, SSA_ACCENT_MINI, err, sizeof err) != 0) { fprintf(stderr, "boot: %s\n", err); rc = 1; break; }
        if (speak(e, SSA_ACCENT_MINI, c[i].text, &s, c[i].req_rate, c[i].req_pitch, 4096, c[i].stop, &d))
            rc = 1;
        else
            report(c[i].name, &d);
    }
    ssa_free(e);
    return rc;
}

/* --am-direct: the reference -- am_voice itself, driven as am_voice.h says, with the settings the .py computed */
static int am_direct(const char *dvc)
{
    char line[16384], name[64], texthex[8192], text[4096], err[256];
    am_voice *v = NULL;
    int cur_boot = -1;
    while (fgets(line, sizeof line, stdin)) {
        int boot, sr, rate, pitch, infl, volume, numbers, voice, offset, stop, done = 0, blocks = 0, n;
        const short *pcm;
        digest d = {1469598103934665603ULL, 0};
        texthex[0] = 0;
        if (sscanf(line, "%63s %d %d %d %d %d %d %d %d %d %d %8191s", name, &boot, &sr, &rate, &pitch, &infl, &volume,
                   &numbers, &voice, &offset, &stop, texthex) < 11)
            continue;
        unhex(texthex, text, sizeof text);
        if (!v || boot != cur_boot) {        /* the app boots its units again when a boot setting changes */
            amv_destroy(v);
            v = amv_create(dvc, (double)sr, err, sizeof err);
            if (!v) { fprintf(stderr, "amv_create: %s\n", err); return 1; }
            cur_boot = boot;
        }
        amv_set(v, rate, pitch, infl, volume, numbers, voice);
        if (amv_speak(v, text, offset) > 0)
            while (!done) {
                n = amv_render(v, &pcm, &done);
                if (n > 0) {
                    add(&d, pcm, n);
                    if (stop > 0 && ++blocks == stop && !done) {
                        amv_cancel(v);
                        break;
                    }
                }
            }
        report(name, &d);
    }
    amv_destroy(v);
    return 0;
}

/* ---- the Mockingboard (imported): one unit kept across the cases, as the app keeps it ---------------------------- */

static const char *HELLO_MB = "Hello there. This is the Mockingboard, speaking on a phone.";

/* test_android_native.py's MB_CASES, in the same order: name, text, the app's rate and pitch sliders, the request's
   rate and pitch percentages, volume, numbers, sample rate, pull size, blocks before a stop */
typedef struct {
    const char *name, *text;
    int rate, pitch, req_rate, req_pitch, volume, numbers, sample_rate, chunk, stop;
} mb_case;

static const mb_case *mb_cases(int *n)
{
    static mb_case c[24];
    int k = 0;
#define B(nm, tx, r, p, rr, rp, v, nu, sr, ch, st) \
    do { mb_case x = {nm, tx, r, p, rr, rp, v, nu, sr, ch, st}; c[k++] = x; } while (0)
    B("mb-default", HELLO_MB, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-default-97", HELLO_MB, 50, 50, 100, 100, 100, 1, 22050, 97, 0);
    B("mb-fast", HELLO_MB, 50, 50, 200, 100, 100, 1, 22050, 4096, 0);
    B("mb-sliders", HELLO_MB, 70, 80, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-pitch-100", "B", 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-pitch-150", "B", 50, 50, 100, 150, 100, 1, 22050, 4096, 0);
    B("mb-pitch-75", "B", 50, 50, 100, 75, 100, 1, 22050, 4096, 0);
    B("mb-pitch-120", "B", 50, 50, 100, 120, 100, 1, 22050, 4096, 0);
    B("mb-pitch-100-again", "B", 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-num", NUM_EN, 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-num-off", NUM_EN, 50, 50, 100, 100, 100, 0, 22050, 4096, 0);
    B("mb-stopped", LONG, 50, 50, 100, 100, 100, 1, 22050, 4096, 8);
    B("mb-after-stop", "Next message.", 50, 50, 100, 100, 100, 1, 22050, 4096, 0);
    B("mb-volume-150", HELLO_MB, 50, 50, 100, 100, 150, 1, 22050, 4096, 0);
    B("mb-11k", HELLO_MB, 50, 50, 100, 100, 100, 1, 11025, 4096, 0);
#undef B
    *n = k;
    return c;
}

/* The Mockingboard's cases on one voice (SSA_MOCKINGBOARD, or SSA_MOCKINGBOARD_EARLY with "mbe" for "mb" in the
   names), its file imported in the data folder */
static int mockingboard(const char *data, int voice)
{
    ssa_engine *e = ssa_new(data);
    const mb_case *c;
    char err[256], name[64];
    digest d;
    int n, i, rc = 0;
    if (!e) return 1;
    if (!ssa_has_voice(e, voice)) { ssa_free(e); return 0; }   /* no firmware: no cases (said by the .py) */
    c = mb_cases(&n);
    for (i = 0; i < n && !rc; i++) {
        ssa_settings s;
        ssa_default_settings(&s);
        s.rate = c[i].rate; s.pitch = c[i].pitch; s.volume = c[i].volume; s.numbers = c[i].numbers;
        ssa_configure(e, c[i].sample_rate, 1, 0);
        if (ssa_load(e, voice, err, sizeof err) != 0) { fprintf(stderr, "boot: %s\n", err); rc = 1; break; }
        if (speak(e, voice, c[i].text, &s, c[i].req_rate, c[i].req_pitch, c[i].chunk, c[i].stop, &d))
            rc = 1;
        else {
            snprintf(name, sizeof name, "%s%s", voice == SSA_MOCKINGBOARD_EARLY ? "mbe" : "mb", c[i].name + 2);
            report(name, &d);
        }
    }
    ssa_free(e);
    return rc;
}

/* The built-in copies (the APK's, handed over in memory: ssa_set_mockingboard), from the folder holding both files:
   each voice's "Hello." at the defaults with no imported copy ("mb-builtin", "mbe-builtin"); where each voice's file
   comes from, built in alone and beside an imported copy in data ("mb-source <voice> <builtin> <imported>": 2, then
   1 -- the import wins); and each voice refusing the other's file ("mb-refused <voice> <1 when refused>"). */
static int mockingboard_builtin(const char *data, const char *dir)
{
    static const char *const FILES[2] = {SSA_MOCKINGBOARD_FILE, SSA_MOCKINGBOARD_EARLY_FILE};
    static const char *const NAMES[2] = {"mb-builtin", "mbe-builtin"};
    unsigned char *bin[2];
    size_t n[2];
    int k, rc = 0;
    for (k = 0; k < 2; k++)
        bin[k] = slurp(dir, FILES[k], &n[k]);
    for (k = 0; k < 2 && !rc; k++) {
        int voice = SSA_MOCKINGBOARD + k;
        ssa_engine *e = ssa_new("no-such-folder"), *f = ssa_new(data);
        ssa_settings s;
        digest d;
        if (!bin[k]) { ssa_free(e); ssa_free(f); continue; }
        ssa_default_settings(&s);
        printf("mb-refused %d %d\n", voice, !(bin[1 - k] && ssa_set_mockingboard(e, voice, bin[1 - k], n[1 - k])));
        if (!ssa_set_mockingboard(e, voice, bin[k], n[k]) || !ssa_set_mockingboard(f, voice, bin[k], n[k])) {
            fprintf(stderr, "voice %d refused its own built-in file\n", voice);
            rc = 1;
        } else {
            printf("mb-source %d %d %d\n", voice, ssa_mockingboard_source(e, voice), ssa_mockingboard_source(f, voice));
            if (speak(e, voice, "Hello.", &s, 100, 100, 4096, 0, &d))
                d.samples = 0;                 /* it could not speak: reported as silence, which the .py refuses */
            report(NAMES[k], &d);
        }
        ssa_free(e);
        ssa_free(f);
    }
    free(bin[0]);
    free(bin[1]);
    return rc;
}

/* --mb-direct: the reference -- mb_voice itself, driven as mb_voice.h says, with the settings the .py computed, on
   the firmware file given (either known one: mbv_create takes the version by its sha256) */
static int mb_direct(const char *file)
{
    char line[16384], name[64], texthex[8192], text[4096], err[256];
    mb_voice *v = NULL;
    int cur_rate = 0;
    size_t fn = 0;
    unsigned char *fw = slurp(".", file, &fn);
    if (!fw) {                               /* an absolute path: slurp joins it to "." */
        FILE *f = fopen(file, "rb");
        long len;
        if (f) {
            fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
            fw = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
            fn = fw ? fread(fw, 1, (size_t)len, f) : 0;
            fclose(f);
        }
    }
    if (!fw) { fprintf(stderr, "cannot read %s\n", file); return 1; }
    while (fgets(line, sizeof line, stdin)) {
        int sr, rate, pitch, volume, numbers, offset, stop, done = 0, blocks = 0, n;
        const short *pcm;
        digest d = {1469598103934665603ULL, 0};
        texthex[0] = 0;
        if (sscanf(line, "%63s %d %d %d %d %d %d %d %8191s", name, &sr, &rate, &pitch, &volume, &numbers, &offset,
                   &stop, texthex) < 8)
            continue;
        unhex(texthex, text, sizeof text);
        if (!v || sr != cur_rate) {          /* the app boots a new unit at a new sample rate (ssa_configure) */
            mbv_destroy(v);
            v = mbv_create(fw, fn, (double)sr, err, sizeof err);
            if (!v) { fprintf(stderr, "mbv_create: %s\n", err); free(fw); return 1; }
            cur_rate = sr;
        }
        mbv_set(v, rate, pitch, volume, numbers);
        if (mbv_speak(v, text, offset) > 0)
            while (!done) {
                n = mbv_render(v, &pcm, &done);
                if (n > 0) {
                    add(&d, pcm, n);
                    if (stop > 0 && ++blocks == stop && !done) {
                        mbv_cancel(v);
                        break;
                    }
                }
            }
        report(name, &d);
    }
    mbv_destroy(v);
    free(fw);
    return 0;
}

/* --import: the import's judgement on a file, as the app's nativeImportFirmware gives it */
static int import(const char *file, const char *out)
{
    size_t n;
    unsigned char *data = slurp(".", file, &n);
    char msg[512];
    int r;
    if (!data) {                             /* an absolute path: slurp joins it to "." */
        FILE *f = fopen(file, "rb");
        long len;
        if (!f) { fprintf(stderr, "cannot open %s\n", file); return 1; }
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
        n = data ? fread(data, 1, (size_t)len, f) : 0;
        fclose(f);
    }
    r = ssa_import_firmware(data, (long)n, out, msg, sizeof msg);
    printf("import %d %s\n", r, msg);
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    ssa_engine *e;
    ssa_settings s, slow, ra;
    digest d;
    char err[256];
    const char *brk = getenv("SSI263_ANDROID_TEST_BREAK");
    ssa_default_settings(&s);
    slow = s;
    slow.rate = 30;
    ra = s;
    ra.run_ahead = 1;
    ssa_map_break = brk && !strcmp(brk, "1");
    ssa_accent_break = !brk ? 0 : !strcmp(brk, "accent-pitch") ? 1 : !strcmp(brk, "accent-glide") ? 2
                     : !strcmp(brk, "accent-reuse") ? 3 : !strcmp(brk, "accent-step") ? 4 : 0;
    ssa_voice_break = !brk ? 0 : !strcmp(brk, "speakout-pitch") ? 1 : !strcmp(brk, "speakout-settings") ? 2
                    : !strcmp(brk, "run-ahead") ? 3 : !strcmp(brk, "numbers") ? 4
                    : !strcmp(brk, "mockingboard-pitch") ? 5 : !strcmp(brk, "mockingboard-numbers") ? 6
                    : !strcmp(brk, "mockingboard-import-ignored") ? 7 : !strcmp(brk, "mockingboard-builtin-ignored") ? 8
                    : !strcmp(brk, "mockingboard-variant") ? 9 : 0;
    ssa_import_break = !brk ? 0 : !strcmp(brk, "import-hash") ? 1 : !strcmp(brk, "import-dsk") ? 2 : 0;
    if (argc >= 2 && !strcmp(argv[1], "--texts")) return texts();
    if (argc >= 6 && !strcmp(argv[1], "--level"))
        return level(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), argc >= 7 ? argv[6] : NULL);
    if (argc >= 3 && !strcmp(argv[1], "--so-direct")) return so_direct(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "--am-direct")) return am_direct(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "--mb-direct")) return mb_direct(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "--import")) return import(argv[2], argv[3]);
    if (argc < 2) { fprintf(stderr, "usage: test_android_native <data folder> [<aicom folder>] | --texts\n"); return 2; }
    if (argc >= 3 && block("accent") && accent(argv[2])) return 1;
    if (argc >= 4 && strcmp(argv[3], "-") && block("mini") && mini(argv[3])) return 1;
    if (block("so") && speakout(argv[1])) return 1;
    if (block("mb") && (mockingboard(argv[1], SSA_MOCKINGBOARD) || mockingboard(argv[1], SSA_MOCKINGBOARD_EARLY)))
        return 1;
    if (argc >= 5 && block("mb") && mockingboard_builtin(argv[1], argv[4])) return 1;
    /* the mapping itself: Android 100% is the slider; a doubling is SSIP 50 */
    if (ssa_rate(50, 100) != 50 || ssa_pitch(50, 50) != 25 || ssa_pitch(50, 400) != 100
            || ssa_ssip_from_percent(200) != 50 || ssa_to100(-100) != 0) {
        fprintf(stderr, "the mapping is off\n");
        return 1;
    }

    if (block("bl")) {
        e = ssa_new(argv[1]);
        if (!e || !ssa_has_voice(e, SSA_ENGLISH)) { fprintf(stderr, "no English unit in %s\n", argv[1]); return 2; }
        ssa_configure(e, 22050, 1, 0);
        if (ssa_load(e, SSA_ENGLISH, err, sizeof err) != 0) { fprintf(stderr, "boot: %s\n", err); return 2; }
        if (speak(e, SSA_ENGLISH, HELLO, &s, 100, 100, 4096, 0, &d)) return 1;
        report("default", &d);
        if (speak(e, SSA_ENGLISH, HELLO, &s, 100, 100, 97, 0, &d)) return 1;
        report("default-97", &d);
        if (speak(e, SSA_ENGLISH, HELLO, &s, 200, 100, 4096, 0, &d)) return 1;
        report("fast", &d);
        if (speak(e, SSA_ENGLISH, HELLO, &slow, 100, 50, 4096, 0, &d)) return 1;
        report("slow-low", &d);
        if (speak(e, SSA_ENGLISH, LONG, &s, 100, 100, 4096, 5, &d)) return 1;
        report("stopped", &d);
        if (speak(e, SSA_ENGLISH, "Next message.", &s, 100, 100, 4096, 0, &d)) return 1;
        report("after-stop", &d);
        if (ssa_has_voice(e, SSA_SPANISH)) {
            if (speak(e, SSA_SPANISH, "Ma\xc3\xb1" "ana, \xc2\xbfqu\xc3\xa9 tal? \xc3\x89l est\xc3\xa1 aqu\xc3\xad.",
                      &s, 100, 100, 4096, 0, &d))
                return 1;
            report("spanish", &d);
        }
        ssa_free(e);
    }

    /* run ahead (EXPERIMENTAL, the Braille Lite's), on a unit of its own: the setting reaches bl_voice */
    if (block("ra")) {
        e = ssa_new(argv[1]);
        ssa_configure(e, 22050, 1, 0);
        if (speak(e, SSA_ENGLISH, HELLO, &ra, 100, 100, 4096, 0, &d)) return 1;
        report("ra-default", &d);
        if (speak(e, SSA_ENGLISH, HELLO, &ra, 200, 100, 4096, 0, &d)) return 1;
        report("ra-fast", &d);
        if (speak(e, SSA_ENGLISH, LONG, &ra, 100, 100, 4096, 5, &d)) return 1;
        report("ra-stopped", &d);
        if (speak(e, SSA_ENGLISH, "Next message.", &ra, 100, 100, 4096, 0, &d)) return 1;
        report("ra-after-stop", &d);
        ssa_free(e);
    }

    /* the number words ("Read numbers as words", the driver's default: on), each case on a fresh unit -- so on and
       off differ by the setting alone: the setting reaches bl_voice, on and off, English and Spanish */
    if (block("num")) {
        static const int voices[2] = {SSA_ENGLISH, SSA_SPANISH};
        static const char *names[2][2] = {{"num-en", "num-en-off"}, {"num-es", "num-es-off"}};
        ssa_settings off = s;
        int k, on;
        off.numbers = 0;
        for (k = 0; k < 2; k++)
            for (on = 1; on >= 0; on--) {
                e = ssa_new(argv[1]);
                ssa_configure(e, 22050, 1, 0);
                if (ssa_has_voice(e, voices[k])) {
                    if (speak(e, voices[k], k ? NUM_ES : NUM_EN, on ? &s : &off, 100, 100, 4096, 0, &d)) return 1;
                    report(names[k][!on], &d);
                }
                ssa_free(e);
            }
    }

    /* the import's check (ssa_probe): a unit of its own speaks the first case again, as the first case did; and the
       Speak-Out's, the import's "Hello." */
    if (block("bl")) {
        unsigned long long h;
        long n = ssa_probe(argv[1], SSA_ENGLISH, HELLO, &h, err, sizeof err);
        if (n < 0) { fprintf(stderr, "probe: %s\n", err); return 1; }
        printf("probe %ld %016llx\n", n, h);
    }
    if (block("so")) {
        unsigned long long h;
        long n;
        int has;
        e = ssa_new(argv[1]);
        has = e && ssa_has_voice(e, SSA_SPEAKOUT);
        ssa_free(e);
        if (has) {
            n = ssa_probe(argv[1], SSA_SPEAKOUT, "Hello.", &h, err, sizeof err);
            if (n <= 0) { fprintf(stderr, "Speak-Out probe: %ld %s\n", n, err); return 1; }
            printf("s-probe %ld %016llx\n", n, h);
        }
    }
    if (block("mb")) {                       /* each Mockingboard's import check: its "Hello." on a unit of its own */
        static const int voices[2] = {SSA_MOCKINGBOARD, SSA_MOCKINGBOARD_EARLY};
        static const char *const names[2] = {"mb-probe", "mbe-probe"};
        int k;
        for (k = 0; k < 2; k++) {
            unsigned long long h;
            long n;
            int has;
            e = ssa_new(argv[1]);
            has = e && ssa_has_voice(e, voices[k]);
            ssa_free(e);
            if (has) {
                n = ssa_probe(argv[1], voices[k], "Hello.", &h, err, sizeof err);
                if (n <= 0) { fprintf(stderr, "Mockingboard probe: %ld %s\n", n, err); return 1; }
                printf("%s %ld %016llx\n", names[k], n, h);
            }
        }
    }
    return 0;
}
