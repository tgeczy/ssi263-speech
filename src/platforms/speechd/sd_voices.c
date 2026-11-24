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
#include "../../csrc/voices_blazie.h"   /* the release table's Braille Lite voices (blazie/bl_languages.txt) */
#include "sd_voices.h"
#include <errno.h>
#include <sys/stat.h>
#ifdef SSV_HAVE_SUPERVOICE
#include "../../csrc/supervoice/sv_dsk.h"
#endif

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
    long size[4];                      /* each one's size when it must be exactly that (the Accent-PC's ROMs), else 0 */
} voice_def;

static const voice_def ALL[] = {
    {"blazie:blazie", "Braille Lite 2000", "en-US", "braille-lite", {"BL2ENG.BNS", "bl2_2003_warm.state", NULL}},
    {"blazie:blazie_es", "Braille Lite 2000 (Spanish)", "es-ES", "braille-lite",
     {"BL2SPA.BNS", "bl2spa_fresh.state", NULL}},
    /* 0.8: the German and Italian Braille Lite 2000 (Blazie's September 2000 update) and the Slovak Braille 'n
       Speak 2000, under the NVDA add-on's names for their files */
    {"blazie:blazie_de", "Braille Lite 2000 (German)", "de-DE", "braille-lite",
     {"Lite2000_de.BNS", "lite2000_de_fresh.state", NULL}},
    {"blazie:blazie_it", "Braille Lite 2000 (Italian)", "it-IT", "braille-lite",
     {"Lite2000_it.BNS", "lite2000_it_fresh.state", NULL}},
    {"blazie:blazie_sk", "Braille 'n Speak 2000 (Slovak)", "sk-SK", "braille-n-speak",
     {"BS2SLL.BNS", "bs2sll_fresh.state", NULL}},
    {"accentmini:sa", "Accent-PC", "en-US", "accent-sa",
     {"aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN", NULL}, {0x10000, 0x8000, 0x8000}},
    {"accentmini:mini", "Accent-mini", "en-US", "accent-mini", {"aicom-accent-mini/SPKEMS.DVC", NULL}},
    {"speakout:speakout", "Speak-Out", "en-US", "speakout", {"gw-micro-speakout/SPEAKOUT.HEX", NULL}},
    /* 0.8.5: GW Micro's Sounding Board, the Speak-Out's voice on the card's DOS drivers -- last in voices.c, listed
       here right under the Speak-Out (Tomi, 2026-10-25; tools/check_uniform.py FOLLOWS) */
    {"speakout:soundingboard", "Sounding Board (GW Micro)", "en-US", "soundingboard",
     {"gw-micro-sounding-board/SBLOAD.COM", "gw-micro-sounding-board/SB.COM", NULL}},
    {"mockingboard:mockingboard", "Mockingboard", "en-US", "mockingboard",
     {"sweet-micro-mockingboard/mockingboard-tts-1.1.bin", NULL}},
    {"mockingboard:early", "Mockingboard, early", "en-US", "mockingboard",      /* Mockingboard disk 1's program */
     {"sweet-micro-mockingboard/mockingboard-tts-early.bin", NULL}},
    /* 0.8: the Braille Lite 2000 in 17 more languages (Blazie's updates, an anonymous donation), from the release
       table, each its image and state in the data folder's bl2000/ (tools/make_release_states.py) */
    SSV_BLAZIE_RELEASE_SD_ROWS
    /* Automated Functions' SmarTalk: its own DOS text-to-speech, S_TALK.EXE 1.21 (voices.c, after the Sounding Board) */
    {"smartalk:smartalk", "SmarTalk (Automated Functions)", "en-US", "smartalk",
     {"automated-functions-smartalk/S_TALK.EXE", NULL}},
    /* 0.9.5: the Artic SynPhonix, Artic's own VEST.COM 1.60 (voices.c, last) */
    {"synphonix:vest", "Artic SynPhonix", "en-US", "synphonix", {"artic-synphonix/VEST.COM", NULL}},
    /* 0.9.5: Speech Systems' Super Voice, its Color Computer program SUPERTOK.BIN (voices.c, last) -- never packaged:
       the user imports it (sd_ssi263 --import) into their own data folder, sdv_user_dir */
    {"supervoice:supertok", "Speech Systems Super Voice", "en-US", "supervoice",
     {"speech-systems-super-voice/SUPERTOK.BIN", NULL}},
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

/* the names these voices had until they were named in English, as the other languages are: a client that saved one
   (speech-dispatcher selects by name) keeps its voice */
static const char *const FORMER[][2] = {
    {"Braille Lite 2000 (espa\xc3\xb1ol)", "Braille Lite 2000 (Spanish)"},
    {"Braille Lite 2000 (Deutsch)", "Braille Lite 2000 (German)"},
    {"Braille Lite 2000 (italiano)", "Braille Lite 2000 (Italian)"},
    {"Braille 'n Speak 2000 (sloven\xc4\x8dina)", "Braille 'n Speak 2000 (Slovak)"},
    /* 0.9.5: the voice called "Accent SA" since 0.7.1 runs the Accent-PC card's ROMs, and is named so */
    {"Accent SA", "Accent-PC"},
};

int sdv_find(const char *name)
{
    int i;
    /* (SD_SSI263_TEST_NO_FORMER: the tests' control, a former name unknown) */
    for (i = 0; i < (int)(sizeof FORMER / sizeof FORMER[0]) && !getenv("SD_SSI263_TEST_NO_FORMER"); i++)
        if (!strcmp(name, FORMER[i][0])) name = FORMER[i][1];
    for (i = 0; i < sdv_count(); i++)
        if (!strcmp(name, def(i)->name)) return i;
    return -1;
}

int sdv_user_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(out, cap, "%s/ssi263-speech", xdg);
    else if (home && *home) snprintf(out, cap, "%s/.local/share/ssi263-speech", home);
    else return 0;
    return 1;
}

/* a voice's file: in the data folder, or -- a voice no package carries (voices.h SSV_USER_DATA), not found there --
   in this user's own (SD_SSI263_TEST_NO_USER_DIR: the tests' control, the user's folder never looked in) */
static void path(const sd_settings *s, const char *name, char *out, size_t cap)
{
    char user[1100];
    snprintf(out, cap, "%s/%s", s->datadir, name);
    if (access(out, R_OK) != 0 && !strncmp(name, "speech-systems-super-voice/", 27)
            && !getenv("SD_SSI263_TEST_NO_USER_DIR") && sdv_user_dir(user, sizeof user))
        snprintf(out, cap, "%s/%s", user, name);
}

/* every folder of `dir` made, as mkdir -p */
static int make_dirs(char *dir)
{
    char *p;
    for (p = dir + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(dir, 0755) != 0 && errno != EEXIST) { *p = '/'; return -1; }
            *p = '/';
        }
    return mkdir(dir, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

int sdv_import(const char *file, char *msg, int cap)
{
#ifdef SSV_HAVE_SUPERVOICE
    FILE *f = fopen(file, "rb");
    unsigned char *data = NULL, *prog = NULL;
    size_t prog_n = 0;
    long n = -1;
    char dir[1100], dest[1200], tmp[1300], why[300];
    int ok = 0;
    if (f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n <= (64L << 20) && fseek(f, 0, SEEK_SET) == 0
            && (data = (unsigned char *)malloc((size_t)n)) != NULL && fread(data, 1, (size_t)n, f) == (size_t)n)
        ok = 1;
    if (f) fclose(f);
    if (!ok) {
        free(data);
        snprintf(msg, (size_t)cap, "%s could not be read%s", file, n > (64L << 20) ? " (too large)" : "");
        return -1;
    }
    ok = sv_firmware_from(data, (size_t)n, &prog, &prog_n, why, sizeof why);
    free(data);
    if (!ok) {
        snprintf(msg, (size_t)cap, "%s: %s (a zip: unzip it first, then import SUPERV.DSK)", file, why);
        return -1;
    }
    if (!sdv_user_dir(dir, sizeof dir)) {
        sv_free(prog);
        snprintf(msg, (size_t)cap, "neither XDG_DATA_HOME nor HOME is set: no data folder to put it in");
        return -1;
    }
    strncat(dir, "/speech-systems-super-voice", sizeof dir - strlen(dir) - 1);
    snprintf(dest, sizeof dest, "%s/SUPERTOK.BIN", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", dest);
    f = make_dirs(dir) == 0 ? fopen(tmp, "wb") : NULL;
    ok = f && fwrite(prog, 1, prog_n, f) == prog_n;
    if (f && fclose(f) != 0) ok = 0;
    sv_free(prog);
    if (!ok || rename(tmp, dest) != 0) {
        remove(tmp);
        snprintf(msg, (size_t)cap, "%s could not be written", dest);
        return -1;
    }
    snprintf(msg, (size_t)cap, "the Super Voice's SUPERTOK.BIN is in %s", dest);
    return 0;
#else
    (void)file;
    snprintf(msg, (size_t)cap, "this module was built without the Super Voice");
    return -1;
#endif
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
    case SSV_ACCENT_PC:
    case SSV_ACCENT_MINI:              /* asv_set / amv_set: inflection, number processing, the mini's voice */
        o->inflection = getenv("SD_SSI263_TEST_IGNORE_ACCENT_INFLECTION") ? 100 : s->accent_inflection;
        o->numbers = s->accent_numbers;
        o->tone = s->accent_voice;
        break;
    case SSV_SPEAKOUT:                 /* sov_set: its tone, join phrases, short pauses */
    case SSV_SOUNDINGBOARD:            /* the same voice (its drivers' front end is the Speak-Out's) */
        o->tone = s->speakout_tone;
        o->join = s->speakout_join;
        o->pack = s->speakout_short_pauses;
        break;
    case SSV_MOCKINGBOARD:             /* mbv_set: rate, pitch, volume; number words on (its defaults) */
    case SSV_SMARTALK:                 /* stv_set: rate, pitch, volume; its tone I, inflection and number words on
                                          (its defaults) */
    case SSV_SYNPHONIX:                /* spv_set: rate, pitch, volume; inflection and number words on (its
                                          defaults) */
    case SSV_SUPERVOICE:               /* svv_set: rate, pitch, volume; its own numbers (number words off: its
                                          default) */
        break;
    }
}

int sdv_speak(int i, const sd_settings *s, int rate, int pitch, int volume, const char *utf8, char *err, int errlen)
{
    return sdv_speak_marked(i, s, rate, pitch, volume, utf8, NULL, 0, err, errlen);
}

int sdv_speak_marked(int i, const sd_settings *s, int rate, int pitch, int volume, const char *utf8,
                     const ssv_mark *marks, int n_marks, char *err, int errlen)
{
    ssv_settings o;
    if (sdv_load(i, s, err, errlen) != 0) return -1;
    settings_for(i, s, rate, pitch, volume, &o);
    /* 0 (nothing to say, or the unit refused it: the next speak recovers) still renders until done, as before */
    return ssv_speak_marked(units[built[i]], &o, utf8, 0, marks, n_marks) > 0;
}

int sdv_render(int i, const short **pcm, int *done)
{
    if (!units[built[i]]) { *done = 1; return 0; }
    return ssv_render(units[built[i]], pcm, done);
}

int sdv_reached(int i, const ssv_reach **out)
{
    *out = NULL;
    return units[built[i]] ? ssv_reached(units[built[i]], out) : 0;
}

void sdv_cancel(int i)
{
    if (units[built[i]]) ssv_cancel(units[built[i]]);
}

void sdv_interrupted(int i)
{
    if (units[built[i]]) ssv_interrupted(units[built[i]]);
}

void sdv_literal_next(int i)
{
    if (units[built[i]]) ssv_literal_next(units[built[i]]);
}

void sdv_destroy_all(void)
{
    int k;
    for (k = 0; k < N_ALL; k++) {
        ssv_destroy(units[k]);
        units[k] = NULL;
    }
}
