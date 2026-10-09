/* test_apple_speech.c -- the Apple front end's own C on the desktop: ssp_ssml (VoiceOver's SSML and the pauses),
 * ssp_speech (a request spoken segment by segment) and ssp_import (Aicom's files by sha256), driven by
 * test_apple_speech.py.
 *
 *     test_apple_speech --parse                 stdin: "<pause mode> <hex SSML>" lines; out: each request parsed
 *     test_apple_speech --speak <data folder> <aicom folder> [<SPKEMS.DVC>]
 *                                               every voice whose files are there: each request through ssp_speech
 *                                               against its reference, the segments through ssa_start/ssa_pull
 *                                               directly with the pauses' zeros between them, on an engine of its own
 *     test_apple_speech --import <file> <out>   ssp_import_firmware's judgement
 *
 * SSI263_APPLE_SPEECH_BREAK puts one bug back (test_apple_speech.py's controls): ssml-1, ssml-2 (ssp_ssml_break),
 * speech-1, speech-2 (ssp_speech_break), import (ssp_import_break).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssa_engine.h"
#include "ssp_import.h"
#include "ssp_speech.h"
#include "ssp_ssml.h"

#define CAP 4096

static unsigned char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (unsigned char *)malloc((size_t)*n + 1);
    if (p && fread(p, 1, (size_t)*n, f) != (size_t)*n) { free(p); p = NULL; }
    fclose(f);
    return p;
}

static void hex_out(const char *s)
{
    if (!*s) { printf("-"); return; }
    for (; *s; s++) printf("%02x", (unsigned char)*s);
}

/* ---- --parse ------------------------------------------------------------------------------------------------------ */
static int parse(void)
{
    char line[65536];
    while (fgets(line, sizeof line, stdin)) {
        int mode;
        char *h, *ssml;
        size_t k = 0;
        ssp_request r;
        if (sscanf(line, "%d", &mode) != 1 || !(h = strchr(line, ' '))) continue;
        h++;
        ssml = (char *)malloc(strlen(h) / 2 + 1);
        while (h[0] && h[1] && h[0] != '\n') {
            unsigned v;
            sscanf(h, "%2x", &v);
            ssml[k++] = (char)v;
            h += 2;
        }
        ssml[k] = 0;
        if (ssp_parse(ssml, mode, &r)) { printf("error\n"); free(ssml); continue; }
        printf("request %d %d %.4f %d %d %d\n", r.rate, r.pitch, r.volume, r.n, ssp_has_text(&r),
               ssp_total_pause_ms(&r));
        for (int s = 0; s < r.n; s++) {
            printf("seg ");
            hex_out(r.seg[s].text);
            printf(" %d\n", r.seg[s].pause_ms);
        }
        printf("end\n");
        ssp_request_free(&r);
        free(ssml);
    }
    return 0;
}

/* ---- --speak ------------------------------------------------------------------------------------------------------ */
typedef struct { unsigned long long h; long samples; } digest;

static void eat(digest *d, const short *pcm, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v = (unsigned short)pcm[i];
        d->h = (d->h ^ (v & 0xFF)) * 1099511628211ULL;
        d->h = (d->h ^ (v >> 8)) * 1099511628211ULL;
    }
    d->samples += n;
}

static void fresh(digest *d) { d->h = 1469598103934665603ULL; d->samples = 0; }

/* The reference: the request's segments through ssa_start/ssa_pull directly, the pauses' zeros between them.  With
   stop_samples >= 0, the samples stop there (whole blocks, in the first segment) and the unit is cancelled, as a stop
   leaves it. */
static void reference(ssa_engine *e, int voice, const ssp_request *r, const ssa_settings *s, long stop_samples,
                      digest *d)
{
    static short buf[CAP], zeros[CAP];
    fresh(d);
    for (int k = 0; k < r->n; k++) {
        long z;
        if (r->seg[k].text[0] && ssa_start(e, voice, r->seg[k].text, s, r->rate, r->pitch) == 0) {
            int n;
            while ((n = ssa_pull(e, buf, CAP)) > 0) {
                if (stop_samples >= 0 && d->samples + n > stop_samples) { ssa_cancel(e); return; }
                eat(d, buf, n);
                if (stop_samples >= 0 && d->samples == stop_samples) { ssa_cancel(e); return; }
            }
        }
        for (z = (long)r->seg[k].pause_ms * ssa_sample_rate(e) / 1000; z > 0; z -= CAP)
            eat(d, zeros, z < CAP ? (int)z : CAP);
    }
}

/* The same request through ssp_speech; stop_pulls >= 0: stopped after that many pulls.  The pulls' result after the
   stop goes in *after_stop (it must be -2, and stay so). */
static void spoken(ssp_speech *p, int voice, const ssp_request *r, const ssa_settings *s, int stop_pulls, digest *d,
                   int *after_stop)
{
    static short buf[CAP];
    int n, pulls = 0;
    fresh(d);
    *after_stop = 0;
    if (ssp_speech_start(p, voice, r, s) != 0) return;
    while ((n = ssp_speech_pull(p, buf, CAP)) > 0) {
        eat(d, buf, n);
        if (stop_pulls >= 0 && ++pulls == stop_pulls) {
            ssp_speech_stop(p);
            *after_stop = ssp_speech_pull(p, buf, CAP);
            if (*after_stop == -2) *after_stop = ssp_speech_pull(p, buf, CAP);
            return;
        }
    }
}

static const char *VOICE_NAMES[SSA_VOICES] = {"en", "es", "sa", "so", "mini"};

/* (name, SSML, pause mode, stop after this many pulls or -1) -- in this order on one engine per side */
static const struct { const char *name, *ssml; int mode, stop; } CASES[] = {
    {"phrases", "<speak><prosody rate=\"100%\" volume=\"+0.0dB\"><s>Settings</s><s>Button</s>"
                "<s><break time=\"800.0ms\"/></s><s>Double tap to open.</s></prosody></speak>", SSP_PAUSE_SHORT, -1},
    {"long", "<speak><s>Settings</s><s>Button</s><break time=\"800.0ms\"/>Double tap to open.</speak>",
     SSP_PAUSE_LONG, -1},
    {"off", "<speak><s>Settings</s><s>Button</s><break time=\"800.0ms\"/>Double tap to open.</speak>",
     SSP_PAUSE_OFF, -1},
    {"fast", "<speak><prosody rate=\"200%\">Rate two hundred.<break strength=\"strong\"/>And more.</prosody></speak>",
     SSP_PAUSE_SHORT, -1},
    {"stopped", "<speak>This is a long message for the stop test, with a comma or two, that keeps going well past "
                "the moment the harness says stop.<break time=\"500ms\"/>A second part that must never be heard."
                "</speak>", SSP_PAUSE_SHORT, 3},
    {"after-stop", "<speak>Next message.</speak>", SSP_PAUSE_SHORT, -1},
    {"spacer", "<speak><break time=\"400ms\"/></speak>", SSP_PAUSE_LONG, -1},
    {"capital", "<speak><prosody pitch=\"+50%\">B</prosody></speak>", SSP_PAUSE_SHORT, -1},
};
#define NCASES ((int)(sizeof CASES / sizeof *CASES))

static int load_aicom(ssa_engine *e, const char *aicom, const char *dvc)
{
    static const char *ROMS[3] = {"u2.BIN", "u3.BIN", "u4.BIN"};
    unsigned char *rom[3];
    long n[3];
    char path[1200];
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", aicom, ROMS[i]);
        rom[i] = slurp(path, &n[i]);
        if (!rom[i]) ok = 0;
    }
    if (ok) ok = ssa_set_accent_roms(e, rom[0], (size_t)n[0], rom[1], (size_t)n[1], rom[2], (size_t)n[2]);
    for (int i = 0; i < 3; i++) free(rom[i]);
    if (dvc) {
        long m;
        unsigned char *d = slurp(dvc, &m);
        if (d) ssa_set_accent_mini(e, d, (size_t)m);
        free(d);
    }
    return ok;
}

static int speak(const char *data, const char *aicom, const char *dvc)
{
    ssa_settings s;
    int bad = 0;
    ssa_default_settings(&s);
    s.volume = 150;                    /* the app's default engine volume, as Android's */
    for (int v = 0; v < SSA_VOICES; v++) {
        ssa_engine *mine = ssa_new(data), *ref = ssa_new(data);
        ssp_speech *p = ssp_speech_new(mine);
        load_aicom(mine, aicom, dvc);
        load_aicom(ref, aicom, dvc);
        if (!ssa_has_voice(mine, v)) {
            printf("skip %s\n", VOICE_NAMES[v]);
            ssp_speech_free(p);
            ssa_free(mine);
            ssa_free(ref);
            continue;
        }
        for (int c = 0; c < NCASES; c++) {
            ssp_request r;
            digest got, want;
            int after = 0;
            ssp_parse(CASES[c].ssml, CASES[c].mode, &r);
            spoken(p, v, &r, &s, CASES[c].stop, &got, &after);
            reference(ref, v, &r, &s, CASES[c].stop >= 0 ? got.samples : -1, &want);
            printf("case %s-%s %ld %016llx %ld %016llx %d %d\n", VOICE_NAMES[v], CASES[c].name, got.samples, got.h,
                   want.samples, want.h, after, ssp_speech_errors(p));
            ssp_request_free(&r);
        }
        ssp_speech_free(p);
        ssa_free(mine);
        ssa_free(ref);
    }
    return bad;
}

/* ---- --import ----------------------------------------------------------------------------------------------------- */
static int import(const char *file, const char *out)
{
    long n;
    char msg[512];
    unsigned char *d = slurp(file, &n);
    int r;
    if (!d) { printf("unreadable\n"); return 2; }
    r = ssp_import_firmware(d, n, out, msg, sizeof msg);
    printf("%d %s\n", r, msg);
    free(d);
    return 0;
}

int main(int argc, char **argv)
{
    const char *brk = getenv("SSI263_APPLE_SPEECH_BREAK");
    if (brk) {
        if (!strcmp(brk, "ssml-1")) ssp_ssml_break = 1;
        else if (!strcmp(brk, "ssml-2")) ssp_ssml_break = 2;
        else if (!strcmp(brk, "speech-1")) ssp_speech_break = 1;
        else if (!strcmp(brk, "speech-2")) ssp_speech_break = 2;
        else if (!strcmp(brk, "import")) ssp_import_break = 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "--parse")) return parse();
    if (argc >= 4 && !strcmp(argv[1], "--speak")) return speak(argv[2], argv[3], argc >= 5 ? argv[4] : NULL);
    if (argc >= 4 && !strcmp(argv[1], "--import")) return import(argv[2], argv[3]);
    fprintf(stderr, "usage: test_apple_speech --parse | --speak <data> <aicom> [<SPKEMS.DVC>] | --import <file> <out>\n");
    return 2;
}
