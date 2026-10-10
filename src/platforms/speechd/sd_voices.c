/* sd_voices.c -- see sd_voices.h.  The voices' engines are src/csrc/voices.c's table, the one the SAPI engine and
 * Android speak through (each voice's API and its NVDA driver's defaults); units are made with ssv_create_from from
 * where this module keeps their files.  What is the module's own stays here: its names and languages for SSIP, the
 * data folder's layout, and its config keys put on voices.h's settings (settings_for).
 *
 * Test hooks, each a must-fail control of test_sd_ssi263.py: SD_SSI263_TEST_IGNORE_RUN_AHEAD=1 drops SSI263RunAhead
 * on its way to the Braille Lite, SD_SSI263_TEST_IGNORE_LINE_LIFT=1 drops SSI263LineLift likewise,
 * SD_SSI263_TEST_IGNORE_BL_NUMBERS=1 drops SSI263BrailleLiteNumbers (the Braille Lite
 * gets the driver's default, number words on), SD_SSI263_TEST_IGNORE_ACCENT_INFLECTION=1 drops SSI263AccentInflection
 * on its way to the Accents (they get the default, full intonation). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../csrc/voices.h"
#include "sd_voices.h"

void sdv_defaults(sd_settings *s)
{
    memset(s, 0, sizeof *s);
    s->sample_rate = 22050;
    s->inflection = 1;
    s->tone = 7;
    s->short_pauses = 1;
    s->numbers = 1;                    /* blazie.py: self._numbers = True (numberWords, defaultVal=True) */
    s->accent_inflection = 100;
    s->accent_numbers = 1;
    s->accent_voice = 5;
    s->speakout_tone = 8;
    s->speakout_join = 1;
    s->speakout_short_pauses = 1;
}

/* ---- the table: voices.c's voices by id, with this module's names, languages and files ------------------------- */
typedef struct {
    const char *id;                    /* voices.h's */
    const char *name, *language, *engine;
    const char *files[4];              /* in the data folder, ssv_info.files' order, NULL-terminated */
    long size[4];                      /* each one's size when it must be exactly that (the Accent SA's ROMs), else 0 */
} voice_def;

static const voice_def ALL[] = {
    {"blazie:blazie", "Braille Lite 2000", "en-US", "braille-lite", {"BL2ENG.BNS", "bl2_2003_warm.state", NULL}},
    {"blazie:blazie_es", "Braille Lite 2000 (espa\xc3\xb1ol)", "es-ES", "braille-lite",
     {"BL2SPA.BNS", "bl2spa_fresh.state", NULL}},
    {"accentmini:sa", "Accent SA", "en-US", "accent-sa",
     {"aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN", NULL}, {0x10000, 0x8000, 0x8000}},
    {"accentmini:mini", "Accent-mini", "en-US", "accent-mini", {"aicom-accent-mini/SPKEMS.DVC", NULL}},
    {"speakout:speakout", "Speak-Out", "en-US", "speakout", {"gw-micro-speakout/SPEAKOUT.HEX", NULL}},
    {"mockingboard:mockingboard", "Mockingboard", "en-US", "mockingboard",
     {"sweet-micro-mockingboard/mockingboard-tts-1.1.bin", NULL}},
    {"mockingboard:early", "Mockingboard, early", "en-US", "mockingboard",      /* Mockingboard disk 1's program */
     {"sweet-micro-mockingboard/mockingboard-tts-early.bin", NULL}},
};
#define N_ALL ((int)(sizeof ALL / sizeof ALL[0]))

/* the voices whose engine is built in (build_linux.sh: the Accent-mini's and the Speak-Out's with their sources), in
   the table's order: the module's voice i is ALL[built[i]] */
static int built[N_ALL], n_built = -1;
static ssv_voice *units[N_ALL];

static void find_built(void)
{
    int k;
    if (n_built >= 0) return;
    for (n_built = 0, k = 0; k < N_ALL; k++)
        if (ssv_engine_built(ssv_find(ALL[k].id))) built[n_built++] = k;
}

static const voice_def *def(int i) { find_built(); return &ALL[built[i]]; }

int sdv_count(void) { find_built(); return n_built; }
const char *sdv_name(int i) { return def(i)->name; }
const char *sdv_language(int i) { return def(i)->language; }
const char *sdv_engine(int i) { return def(i)->engine; }

int sdv_find(const char *name)
{
    int i;
    for (i = 0; i < sdv_count(); i++)
        if (!strcmp(name, def(i)->name)) return i;
    return -1;
}

static void path(const sd_settings *s, const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s", s->datadir, name);
}

int sdv_available(int i, const sd_settings *s)
{
    const char *const *f;
    for (f = def(i)->files; *f; f++) {
        char p[1200];
        path(s, *f, p, sizeof p);
        if (access(p, R_OK) != 0) return 0;
    }
    return 1;
}

static long file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n = -1;
    if (f && fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (f) fclose(f);
    return n;
}

int sdv_load(int i, const sd_settings *s, char *err, int errlen)
{
    const voice_def *d = def(i);
    char paths[4][1200];
    ssv_source src;
    ssv_boot b;
    int k;
    if (units[built[i]]) return 0;
    memset(&src, 0, sizeof src);
    for (k = 0; k < 4 && d->files[k]; k++) {
        path(s, d->files[k], paths[k], sizeof paths[k]);
        if (d->size[k] && file_size(paths[k]) != d->size[k]) {
            snprintf(err, (size_t)errlen, "%s: not %ld bytes", paths[k], d->size[k]);
            return -1;
        }
        src.path[k] = paths[k];
    }
    ssv_boot_defaults(&b);
    b.sample_rate = s->sample_rate;
    b.inflection = s->inflection;
    b.whine = s->whine;
    units[built[i]] = ssv_create_from(ssv_find(d->id), &src, &b, err, errlen);
    return units[built[i]] ? 0 : -1;
}

/* The message's settings on voices.h's scales: SSIP's rate, pitch and volume, and the voice's own keys. */
static void settings_for(int i, const sd_settings *s, int rate, int pitch, int volume, ssv_settings *o)
{
    int v = ssv_find(def(i)->id);
    ssv_defaults(v, o);
    o->rate = rate;
    o->pitch = pitch;
    o->volume = volume;
    switch (ssv_voice_info(v)->engine) {
    case SSV_BLAZIE:                   /* blv_set's tone and short pauses; run ahead; the line-start lift; the number
                                          words */
        o->tone = s->tone;
        o->pack = s->short_pauses;
        o->run_ahead = s->run_ahead && !getenv("SD_SSI263_TEST_IGNORE_RUN_AHEAD");
        o->line_lift = s->line_lift && !getenv("SD_SSI263_TEST_IGNORE_LINE_LIFT");
        if (!getenv("SD_SSI263_TEST_IGNORE_BL_NUMBERS")) o->numbers = s->numbers;
        break;
    case SSV_ACCENT_SA:
    case SSV_ACCENT_MINI:              /* asv_set / amv_set: inflection, number processing, the mini's voice */
        o->inflection = getenv("SD_SSI263_TEST_IGNORE_ACCENT_INFLECTION") ? 100 : s->accent_inflection;
        o->numbers = s->accent_numbers;
        o->tone = s->accent_voice;
        break;
    case SSV_SPEAKOUT:                 /* sov_set: its tone, join phrases, short pauses */
        o->tone = s->speakout_tone;
        o->join = s->speakout_join;
        o->pack = s->speakout_short_pauses;
        break;
    case SSV_MOCKINGBOARD:             /* mbv_set: rate, pitch, volume; number words on (its defaults) */
        break;
    }
}

int sdv_speak(int i, const sd_settings *s, int rate, int pitch, int volume, const char *utf8, char *err, int errlen)
{
    ssv_settings o;
    if (sdv_load(i, s, err, errlen) != 0) return -1;
    settings_for(i, s, rate, pitch, volume, &o);
    /* 0 (nothing to say, or the unit refused it: the next speak recovers) still renders until done, as before */
    return ssv_speak(units[built[i]], &o, utf8, 0) > 0;
}

int sdv_render(int i, const short **pcm, int *done)
{
    if (!units[built[i]]) { *done = 1; return 0; }
    return ssv_render(units[built[i]], pcm, done);
}

void sdv_cancel(int i)
{
    if (units[built[i]]) ssv_cancel(units[built[i]]);
}

void sdv_destroy_all(void)
{
    int k;
    for (k = 0; k < N_ALL; k++) {
        ssv_destroy(units[k]);
        units[k] = NULL;
    }
}
