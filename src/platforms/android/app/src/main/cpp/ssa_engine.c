/* ssa_engine.c -- see ssa_engine.h.  The voices' engines are src/csrc/voices.c's table (the one the SAPI engine
 * speaks through: each voice's API, its NVDA driver's defaults), units made with ssv_create_from -- the imported
 * firmware (the Braille Lite's, the Speak-Out's, the Mockingboard's) by path under the app's own names, the Aicom
 * ROMs and driver in memory.  What is Android's own stays here:
 * the request's rate and pitch on the sliders (a capital's pitch moved at least one step), the Accent-PC's fresh unit
 * per utterance, and the pull with a stop between blocks.  The bank (ssv_bank) is not used: it keeps one card for
 * both Accents, and the Accent-PC here is a fresh unit per utterance.
 *
 * The kept units' flow is sd_ssi263.c's speak(): set, speak, render until done, a stop between blocks and a cancel
 * after it.  The Accent-PC's is the NVDA driver's _speakJob (as_voice.h) on a unit of the utterance's own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voices.h"
#include "voices_blazie.h"             /* SSV_BLAZIE_RELEASE_VOICES */
#include "blazie/bl_firmware.h"        /* the release table: blv_release_*; blv_sha256 */
#include "smartalk/st_host.h"          /* STH_SHA256 */
#include "accentsa/as_voice.h"
#include "speakout/so_voice.h"
#ifdef SSV_HAVE_MOCKINGBOARD
#include "mockingboard/mb_voice.h"
#endif
#ifdef SSV_HAVE_SMARTALK
#include "smartalk/st_voice.h"         /* stv_step */
#endif
#include "ssa_engine.h"
#include "ssa_map.h"

#define ACCENT_PITCH 50                /* the settings' pitch the Accent-PC's units keep: the rest goes as a capital's */

int ssa_accent_break = 0;
int ssa_voice_break = 0;

/* ---- the app's voices: voices.c's, by id, and where each one's files are on the phone ---------------------------- */
typedef struct {
    const char *id;                    /* voices.h's */
    const char *files[3];              /* imported, in the data folder (ssv_info.files' order); none: in memory */
} voice_def;

static const voice_def VOICES[SSA_RELEASE_FIRST] = {
    {"blazie:blazie", {"BL2ENG.BNS", "bl2_2003_warm.state", NULL}},
    {"blazie:blazie_es", {"BL2SPA.BNS", "bl2spa_fresh.state", NULL}},
    {"accentmini:sa", {NULL}},
    {"speakout:speakout", {SSA_SPEAKOUT_FILE, NULL}},
    {"accentmini:mini", {NULL}},
    {"mockingboard:mockingboard", {SSA_MOCKINGBOARD_FILE, NULL}},
    {"mockingboard:early", {SSA_MOCKINGBOARD_EARLY_FILE, NULL}},
    {"blazie:blazie_de", {"Lite2000_de.BNS", "lite2000_de_fresh.state", NULL}},     /* the add-on's flat names */
    {"blazie:blazie_it", {"Lite2000_it.BNS", "lite2000_it_fresh.state", NULL}},
    {"blazie:blazie_sk", {"BS2SLL.BNS", "bs2sll_fresh.state", NULL}},
};
/* the voices after the release table's (ssa_engine.h, 0.8.5), from SSA_VOICES */
static const voice_def AFTER_RELEASES[SSA_ALL_VOICES - SSA_VOICES] = {
    {"speakout:soundingboard", {SSA_SBLOAD_FILE, SSA_SB_FILE, NULL}},
    {"smartalk:smartalk", {SSA_SMARTALK_FILE, NULL}},
};
static const size_t ROM_SIZES[3] = {0x10000, 0x8000, 0x8000};

/* ssa_engine.h's count of the release voices is the generated table's, the Sounding Board comes right after them and
   the SmarTalk after it */
typedef char release_voices_match[SSA_RELEASE_VOICES == SSV_BLAZIE_RELEASE_VOICES ? 1 : -1];
typedef char soundingboard_after_releases[SSA_SOUNDINGBOARD == SSA_VOICES ? 1 : -1];
typedef char smartalk_after_soundingboard[SSA_SMARTALK == SSA_SOUNDINGBOARD + 1 ? 1 : -1];

static int after_releases(int voice) { return voice >= SSA_VOICES && voice < SSA_ALL_VOICES; }

int ssa_voice_release(int voice)
{
    int k, n = voice - SSA_RELEASE_FIRST;
    if (voice < SSA_RELEASE_FIRST || voice >= SSA_VOICES) return -1;
    for (k = 0; k < blv_firmware_count(); k++)
        if (blv_release_voice_key(k) && n-- == 0) return k;
    return -1;
}

int ssa_release_voice(int release)
{
    int k, voice = SSA_RELEASE_FIRST;
    if (release < 0 || release >= blv_firmware_count() || !blv_release_voice_key(release)) return -1;
    for (k = 0; k < release; k++)
        if (blv_release_voice_key(k)) voice++;
    return voice < SSA_VOICES ? voice : -1;
}

int ssa_voice_file(int voice, int i, char *out, int cap)
{
    int k = ssa_voice_release(voice);
    const char *name = NULL;
    if (voice >= 0 && voice < SSA_RELEASE_FIRST && i >= 0 && i < 3)
        name = VOICES[voice].files[i];
    else if (after_releases(voice) && i >= 0 && i < 3)
        name = AFTER_RELEASES[voice - SSA_VOICES].files[i];
    else if (k >= 0 && i == 0)
        name = blv_release_file(k);
    else if (k >= 0 && i == 1) {
        snprintf(out, (size_t)cap, "%s.state", blv_release_id(k));
        return 1;
    }
    if (!name) return 0;
    snprintf(out, (size_t)cap, "%s", name);
    return 1;
}

static int ssv_of(int voice)
{
    char id[80];
    int k = ssa_voice_release(voice);
    if (voice >= 0 && voice < SSA_RELEASE_FIRST) return ssv_find(VOICES[voice].id);
    if (after_releases(voice)) return ssv_find(AFTER_RELEASES[voice - SSA_VOICES].id);
    if (k < 0) return -1;
    snprintf(id, sizeof id, "blazie:%s", blv_release_voice_key(k));
    return ssv_find(id);
}

struct ssa_engine {
    char *datadir;
    int sample_rate, inflection, whine;
    ssv_voice *units[SSA_ALL_VOICES];  /* the kept units (every voice but the Accent-PC) */
    unsigned char *rom[3];             /* the Accent-PC's u2, u3, u4 */
    unsigned char *mini;               /* the Accent-mini's SPKEMS.DVC */
    size_t mini_n;
    unsigned char *mb[2];              /* the Mockingboards' built-in files (1.1, early), from ssa_set_mockingboard */
    size_t mb_n[2];
    unsigned char *st;                 /* the SmarTalk's built-in S_TALK.EXE, from ssa_set_smartalk */
    size_t st_n;
    ssv_voice *spare;                 /* the Accent-PC's next unit: booted, not yet spoken on */
    ssv_voice *accent;                 /* the unit speaking the current utterance */
    int cur;                           /* the utterance's voice; -1 when none is running */
    const short *block;                /* the last block rendered, valid until the next render */
    int block_n, block_at, done, blocks;
    long frames;                       /* samples of the utterance rendered before the current block */
    int *reach;                        /* ssa_reached: (mark, frame) pairs not yet given */
    int n_reach, cap_reach;
    int stop;                          /* ssa_stop's flag: set from any thread, read with __atomic */
};

void ssa_default_settings(ssa_settings *s)
{
    memset(s, 0, sizeof *s);
    s->rate = 50;
    s->pitch = 50;
    s->tone = 7;
    s->volume = 100;
    s->pack = 1;
    s->run_ahead = 0;
    s->line_lift = 0;
    s->so_tone = 8;
    s->so_join = 1;
    s->so_short = 1;
    s->numbers = 1;                    /* blazie.py: self._numbers = True (numberWords, defaultVal=True) */
}

static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

static void path(const ssa_engine *e, const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s", e->datadir, name);
}

static int readable(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int is_mb(int voice) { return voice == SSA_MOCKINGBOARD || voice == SSA_MOCKINGBOARD_EARLY; }

/* Every one of the voice's files is in the data folder (imported). */
static int imported(const ssa_engine *e, int voice)
{
    char name[256], p[1200];
    int i;
    if (!ssa_voice_file(voice, 0, name, sizeof name)) return 0;
    for (i = 0; ssa_voice_file(voice, i, name, sizeof name); i++) {
        path(e, name, p, sizeof p);
        if (!readable(p)) return 0;
    }
    return 1;
}

int ssa_mockingboard_source(const ssa_engine *e, int voice)
{
    if (!is_mb(voice)) return 0;
    if (imported(e, voice) && ssa_voice_break != 7) return 1;
    return e->mb[voice - SSA_MOCKINGBOARD] && ssa_voice_break != 8 ? 2 : 0;
}

int ssa_smartalk_source(const ssa_engine *e)
{
    if (imported(e, SSA_SMARTALK) && ssa_voice_break != 13) return 1;
    return e->st && ssa_voice_break != 14 ? 2 : 0;
}

/* A unit of the voice, made by voices.c from where the app keeps its files. */
static ssv_voice *make_unit(ssa_engine *e, int voice, char *err, int errlen)
{
    char paths[3][1200], name[256];
    ssv_source src;
    ssv_boot b;
    int k;
    memset(&src, 0, sizeof src);
    if (voice == SSA_ACCENT_SA)
        for (k = 0; k < 3; k++) { src.data[k] = e->rom[k]; src.size[k] = ROM_SIZES[k]; }
    else if (voice == SSA_ACCENT_MINI) {
        src.data[0] = e->mini;
        src.size[0] = e->mini_n;
    } else if (is_mb(voice) && ssa_mockingboard_source(e, voice) == 2) {   /* no imported copy: the built-in one */
        src.data[0] = e->mb[voice - SSA_MOCKINGBOARD];
        src.size[0] = e->mb_n[voice - SSA_MOCKINGBOARD];
    } else if (voice == SSA_SMARTALK && ssa_smartalk_source(e) == 2) {     /* no imported copy: the built-in one */
        src.data[0] = e->st;
        src.size[0] = e->st_n;
    } else
        for (k = 0; k < 3 && ssa_voice_file(voice, k, name, sizeof name); k++) {
            path(e, name, paths[k], sizeof paths[k]);
            src.path[k] = paths[k];
        }
    ssv_boot_defaults(&b);
    b.sample_rate = e->sample_rate;
    b.inflection = e->inflection;
    b.whine = e->whine;
    return ssv_create_from(ssv_of(voice), &src, &b, err, errlen);
}

/* The utterance's settings on voices.h's scales, and the capital's pitch offset (*offset), from the app's settings
   and the request's percentages. */
static void settings_for(const ssa_engine *e, int voice, const ssa_settings *s, int request_rate, int request_pitch,
                         ssv_settings *o, int *offset)
{
    ssv_defaults(ssv_of(voice), o);
    o->rate = ssa_rate(s->rate, request_rate);
    o->volume = s->volume;
    *offset = 0;
    switch (voice) {
    case SSA_ENGLISH:
    case SSA_SPANISH:                  /* the Braille Lite: the request's pitch as its setting (32 steps of its own) */
        o->pitch = ssa_pitch(s->pitch, request_pitch);
        o->tone = s->tone;
        o->pack = s->pack;
        o->run_ahead = s->run_ahead && ssa_voice_break != 3;
        o->line_lift = s->line_lift && ssa_voice_break != 10;  /* bl_voice.h blv_set_line_lift */
        o->numbers = s->numbers && ssa_voice_break != 4;    /* bl_numbers (voices.c), English or Spain's */
        break;
    default:
        if (ssa_voice_release(voice) < 0)
            break;
        /* fall through: the release voices (bl_languages.txt's), as the three below */
    case SSA_GERMAN:
    case SSA_ITALIAN:
    case SSA_SLOVAK:                   /* 0.8's Blazie units: as the two above, less what they do not have -- each
                                          reads its own numbers, and run ahead is tested on English and Spanish
                                          alone (voices.c's ssv_speak drops both for them whatever is sent); the
                                          line-start lift is sent as to every Blazie unit, and does nothing on
                                          them (their pitch writes are the same with it on and off) */
        o->pitch = ssa_pitch(s->pitch, request_pitch);
        o->tone = s->tone;
        o->pack = s->pack;
        o->line_lift = s->line_lift && ssa_voice_break != 10;
        o->numbers = 0;
        o->run_ahead = 0;
        break;
    case SSA_SPEAKOUT:                 /* the slider's pitch as the setting, the request's as a capital's offset */
    case SSA_SOUNDINGBOARD:            /* (its drivers' front end is the Speak-Out's: the same voice) */
        o->pitch = clamp100(s->pitch);
        *offset = ssa_speakout_pitch(s->pitch, ssa_voice_break == 1 ? 100 : request_pitch) - o->pitch;
        if (ssa_voice_break != 2) {    /* the control: its own settings dropped, the driver's defaults sent */
            o->tone = s->so_tone;
            o->join = s->so_join;
            o->pack = s->so_short;
        }
        break;
    case SSA_ACCENT_SA:
    case SSA_ACCENT_MINI: {            /* the Accents: as the Accent-PC has always been spoken here */
        int pitch = ssa_accent_pitch(s->pitch, ssa_accent_break == 1 ? 100 : request_pitch);
        o->inflection = e->inflection ? 100 : 0;
        o->volume = s->volume * SSA_ACCENT_LEVEL / 100;
        o->numbers = 1;
        if (voice == SSA_ACCENT_SA) {
            o->pitch = ACCENT_PITCH;   /* a fresh unit each time: every pitch said as a capital's, snapped to */
            if (ssa_accent_break == 2) /* the control: the pitch as a setting, glided to */
                o->pitch = pitch;
            else
                *offset = pitch - ACCENT_PITCH;
        } else {
            o->pitch = clamp100(s->pitch);
            *offset = pitch - o->pitch;
        }
        break;
    }
    case SSA_MOCKINGBOARD:
    case SSA_MOCKINGBOARD_EARLY:       /* as the Speak-Out: the slider's pitch as the setting, the request's as a
                                          capital's offset; the number words from the settings, as the Braille Lite */
        o->pitch = clamp100(s->pitch);
        *offset = ssa_mockingboard_pitch(s->pitch, ssa_voice_break == 5 ? 100 : request_pitch) - o->pitch;
        o->numbers = s->numbers && ssa_voice_break != 6;
        break;
    case SSA_SMARTALK:                 /* as the Mockingboard, with the TSR's ten pitch steps for the capital's move;
                                          its tone the TSR's own (I), its inflection the app's (the Accents') */
        o->pitch = clamp100(s->pitch);
        *offset = ssa_smartalk_pitch(s->pitch, ssa_voice_break == 11 ? 100 : request_pitch) - o->pitch;
        o->inflection = e->inflection ? 100 : 0;
        o->numbers = s->numbers && ssa_voice_break != 12;
        break;
    }
}

int ssa_speakout_pitch(int slider, int request) { return ssa_step_pitch(slider, request, sov_pitch_step); }

int ssa_mockingboard_pitch(int slider, int request)
{
#ifdef SSV_HAVE_MOCKINGBOARD
    return ssa_step_pitch(slider, request, mbv_pitch_step);
#else
    return ssa_pitch(slider, request);
#endif
}

int ssa_smartalk_pitch(int slider, int request)
{
#ifdef SSV_HAVE_SMARTALK
    return ssa_step_pitch(slider, request, stv_step);
#else
    return ssa_pitch(slider, request);
#endif
}

/* ---- the engine -------------------------------------------------------------------------------------------------- */

ssa_engine *ssa_new(const char *datadir)
{
    ssa_engine *e = (ssa_engine *)calloc(1, sizeof(ssa_engine));
    if (!e) return NULL;
    e->datadir = (char *)malloc(strlen(datadir) + 1);
    if (!e->datadir) { free(e); return NULL; }
    strcpy(e->datadir, datadir);
    e->sample_rate = 22050;
    e->inflection = 1;
    e->whine = 0;
    e->cur = -1;
    e->done = 1;
    return e;
}

static void shut_down(ssa_engine *e)
{
    int i;
    for (i = 0; i < SSA_ALL_VOICES; i++) {
        ssv_destroy(e->units[i]);
        e->units[i] = NULL;
    }
    ssv_destroy(e->spare);
    ssv_destroy(e->accent);
    e->spare = e->accent = NULL;
    e->cur = -1;                       /* nothing may point into a unit that is gone */
    e->block = NULL;
    e->block_n = e->block_at = 0;
    e->done = 1;
}

void ssa_free(ssa_engine *e)
{
    int i;
    if (!e) return;
    shut_down(e);
    for (i = 0; i < 3; i++) free(e->rom[i]);
    free(e->mini);
    free(e->mb[0]);
    free(e->mb[1]);
    free(e->st);
    free(e->reach);
    free(e->datadir);
    free(e);
}

int ssa_set_accent_roms(ssa_engine *e, const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                        const unsigned char *u4, size_t n4)
{
    const unsigned char *src[3] = {u2, u3, u4};
    size_t n[3] = {n2, n3, n4};
    int i;
    for (i = 0; i < 3; i++)
        if (!src[i] || n[i] != ROM_SIZES[i]) return 0;
    ssv_destroy(e->spare);             /* a unit made from other ROMs is not this voice any more */
    e->spare = NULL;
    for (i = 0; i < 3; i++) {
        unsigned char *p = (unsigned char *)malloc(n[i]);
        if (!p) return 0;
        memcpy(p, src[i], n[i]);
        free(e->rom[i]);
        e->rom[i] = p;
    }
    return 1;
}

int ssa_voice_built(int voice) { return ssv_engine_built(ssv_of(voice)); }

int ssa_set_accent_mini(ssa_engine *e, const unsigned char *dvc, size_t n)
{
    unsigned char *p;
    if (!ssa_voice_built(SSA_ACCENT_MINI) || !dvc || !n) return 0;
    if (!(p = (unsigned char *)malloc(n))) return 0;
    memcpy(p, dvc, n);
    if (e->units[SSA_ACCENT_MINI]) {   /* a unit made from another driver is not this voice any more */
        if (e->cur == SSA_ACCENT_MINI) ssa_cancel(e);
        ssv_destroy(e->units[SSA_ACCENT_MINI]);
        e->units[SSA_ACCENT_MINI] = NULL;
    }
    free(e->mini);
    e->mini = p;
    e->mini_n = n;
    return 1;
}

int ssa_set_mockingboard(ssa_engine *e, int voice, const unsigned char *bin, size_t n)
{
    unsigned char *p;
    int k = voice - SSA_MOCKINGBOARD;
    if (!is_mb(voice) || !ssa_voice_built(voice) || !bin || !n) return 0;
#ifdef SSV_HAVE_MOCKINGBOARD
    if (ssa_voice_break != 9 && mbh_variant(bin, n) != (voice == SSA_MOCKINGBOARD ? MBH_V11 : MBH_VEARLY))
        return 0;                      /* not this voice's file: the other one, or none */
#endif
    if (!(p = (unsigned char *)malloc(n))) return 0;
    memcpy(p, bin, n);
    if (e->units[voice]) {             /* a unit made from another copy is let go: the next use makes it again */
        if (e->cur == voice) ssa_cancel(e);
        ssv_destroy(e->units[voice]);
        e->units[voice] = NULL;
    }
    free(e->mb[k]);
    e->mb[k] = p;
    e->mb_n[k] = n;
    return 1;
}

int ssa_set_smartalk(ssa_engine *e, const unsigned char *exe, size_t n)
{
    static const char HEXD[] = "0123456789abcdef";
    unsigned char *p, digest[32];
    char got[65];
    int i;
    if (!ssa_voice_built(SSA_SMARTALK) || !exe || !n) return 0;
    blv_sha256(exe, (long)n, digest);
    for (i = 0; i < 32; i++) {
        got[2 * i] = HEXD[digest[i] >> 4];
        got[2 * i + 1] = HEXD[digest[i] & 15];
    }
    got[64] = 0;
    if (strcmp(got, STH_SHA256)) return 0;             /* not S_TALK.EXE 1.21 */
    if (!(p = (unsigned char *)malloc(n))) return 0;
    memcpy(p, exe, n);
    if (e->units[SSA_SMARTALK]) {      /* a unit made from another copy is let go: the next use makes it again */
        if (e->cur == SSA_SMARTALK) ssa_cancel(e);
        ssv_destroy(e->units[SSA_SMARTALK]);
        e->units[SSA_SMARTALK] = NULL;
    }
    free(e->st);
    e->st = p;
    e->st_n = n;
    return 1;
}

int ssa_has_voice(const ssa_engine *e, int voice)
{
    if (!ssa_voice_built(voice)) return 0;
    if (voice == SSA_ACCENT_SA) return e->rom[0] && e->rom[1] && e->rom[2];
    if (voice == SSA_ACCENT_MINI) return e->mini != NULL;
    if (is_mb(voice)) return ssa_mockingboard_source(e, voice) != 0;
    if (voice == SSA_SMARTALK) return ssa_smartalk_source(e) != 0;
    return imported(e, voice);
}

void ssa_configure(ssa_engine *e, int sample_rate, int inflection, int whine)
{
    if (sample_rate != 11025 && sample_rate != 22050 && sample_rate != 44100)
        sample_rate = 22050;
    inflection = inflection != 0;
    whine = whine < 0 || whine > 2 ? 0 : whine;
    if (sample_rate == e->sample_rate && inflection == e->inflection && whine == e->whine)
        return;
    shut_down(e);
    e->sample_rate = sample_rate;
    e->inflection = inflection;
    e->whine = whine;
}

int ssa_sample_rate(const ssa_engine *e) { return e->sample_rate; }

int ssa_load(ssa_engine *e, int voice, char *err, int errlen)
{
    if (voice == SSA_ACCENT_SA) {
        if (e->spare) return 0;
        if (!ssa_has_voice(e, voice)) { snprintf(err, (size_t)errlen, "the Accent-PC's ROMs are not set"); return -1; }
        e->spare = make_unit(e, voice, err, errlen);
        return e->spare ? 0 : -1;
    }
    if (!ssa_voice_built(voice)) { snprintf(err, (size_t)errlen, "no voice %d in this build", voice); return -1; }
    if (e->units[voice]) return 0;
    if (voice == SSA_ACCENT_MINI && !e->mini) {
        snprintf(err, (size_t)errlen, "the Accent-mini's SPKEMS.DVC is not set");
        return -1;
    }
    if (is_mb(voice) && !ssa_mockingboard_source(e, voice)) {
        snprintf(err, (size_t)errlen, "the Mockingboard's %s is neither imported nor built in",
                 VOICES[voice].files[0]);
        return -1;
    }
    if (voice == SSA_SMARTALK && !ssa_smartalk_source(e)) {
        snprintf(err, (size_t)errlen, "the SmarTalk's %s is neither imported nor built in", SSA_SMARTALK_FILE);
        return -1;
    }
    e->units[voice] = make_unit(e, voice, err, errlen);
    return e->units[voice] ? 0 : -1;
}

/* The Accent-PC's unit is done with: let go, and the next one booted now, so the next utterance need not wait for
   it (under the test's control 3, kept instead: its state carries into the next utterance). */
static void accent_release(ssa_engine *e)
{
    char err[256];
    if (!e->accent) return;
    if (ssa_accent_break == 3) {
        ssv_destroy(e->spare);
        e->spare = e->accent;
    } else {
        ssv_destroy(e->accent);
    }
    e->accent = NULL;
    if (!e->spare)
        ssa_load(e, SSA_ACCENT_SA, err, sizeof err);
}

void ssa_cancel(ssa_engine *e)
{
    e->n_reach = 0;                    /* no mark of a cancelled utterance is given */
    if (e->cur == SSA_ACCENT_SA) {
        if (ssa_accent_break == 3 && e->accent && !e->done)
            ssv_cancel(e->accent);     /* a kept unit must drop what it has not spoken */
        accent_release(e);
    } else if (e->cur >= 0 && !e->done && e->units[e->cur]) {
        ssv_cancel(e->units[e->cur]);
    }
    e->cur = -1;
    e->done = 1;
    e->block_n = e->block_at = 0;
}

int ssa_step_pitch(int slider, int request, int (*step)(int))
{
    int p = ssa_pitch(slider, request), base = step(clamp100(slider));
    if (ssa_accent_break == 4)         /* the control: the plain mapping */
        return p;
    if (request > 100)
        while (p < 100 && step(p) == base) p++;
    else if (request > 0 && request < 100)
        while (p > 0 && step(p) == base) p--;
    return p;
}

int ssa_accent_pitch(int slider, int request) { return ssa_step_pitch(slider, request, asv_pitch_step); }

/* ssv_speak; or, where the unit's own index marks would be used, ssv_speak_marked with the service's offsets as
   marks numbered from 0 */
static int say(ssv_voice *u, const ssv_settings *o, const char *utf8, int offset, const int *ats, int n_ats)
{
    ssv_mark *m;
    int i, r;
    if (!ats || n_ats <= 0 || (!ssv_marks_exact(u, o) && ssa_voice_break != 16)
            || !(m = (ssv_mark *)malloc(sizeof *m * (size_t)n_ats)))
        return ssv_speak(u, o, utf8, offset);
    for (i = 0; i < n_ats; i++) {
        m[i].at = ats[i];
        m[i].id = i;
    }
    r = ssv_speak_marked(u, o, utf8, offset, m, n_ats);
    free(m);
    return r;
}

static int start_accent(ssa_engine *e, const char *utf8, const ssa_settings *s, int request_rate, int request_pitch,
                        const int *ats, int n_ats)
{
    char err[256];
    ssv_settings o;
    int offset;
    if (ssa_load(e, SSA_ACCENT_SA, err, sizeof err) != 0)
        return -1;
    e->accent = e->spare;
    e->spare = NULL;
    settings_for(e, SSA_ACCENT_SA, s, request_rate, request_pitch, &o, &offset);
    e->blocks = 0;
    if (say(e->accent, &o, utf8, offset, ats, n_ats) <= 0) {
        e->cur = SSA_ACCENT_SA;        /* released at once: nothing to say */
        e->done = 1;
        accent_release(e);
        e->cur = -1;
        return 1;
    }
    e->cur = SSA_ACCENT_SA;
    e->done = 0;
    return 0;
}

int ssa_start(ssa_engine *e, int voice, const char *utf8, const ssa_settings *s, int request_rate,
              int request_pitch)
{
    return ssa_start_marked(e, voice, utf8, s, request_rate, request_pitch, NULL, 0);
}

int ssa_start_marked(ssa_engine *e, int voice, const char *utf8, const ssa_settings *s, int request_rate,
                     int request_pitch, const int *ats, int n_ats)
{
    char err[256];
    ssv_settings o;
    int offset, r;
    ssa_cancel(e);                     /* an utterance still running is abandoned: its leftovers must not follow */
    __atomic_store_n(&e->stop, 0, __ATOMIC_SEQ_CST);
    e->frames = 0;
    e->n_reach = 0;
    if (voice == SSA_ACCENT_SA)
        return start_accent(e, utf8, s, request_rate, request_pitch, ats, n_ats);
    if (ssa_load(e, voice, err, sizeof err) != 0)
        return -1;
    e->blocks = 0;
    settings_for(e, voice, s, request_rate, request_pitch, &o, &offset);
    r = say(e->units[voice], &o, utf8, offset, ats, n_ats);
    if (r < 0)                         /* the unit refused it: a synthesis error, not silence */
        return -1;
    if (!r)
        return 1;
    e->cur = voice;
    e->done = 0;
    return 0;
}

/* the next block, and the marks it reached: each at its frame in the utterance's audio */
static int render(ssa_engine *e)
{
    ssv_voice *u = e->cur == SSA_ACCENT_SA ? e->accent : e->units[e->cur];
    const ssv_reach *r;
    int n = ssv_render(u, &e->block, &e->done), k = ssv_reached(u, &r), i;
    if (k > 0 && e->n_reach + k > e->cap_reach) {
        int *q = (int *)realloc(e->reach, sizeof(int) * 2 * (size_t)(e->n_reach + k));
        if (q) { e->reach = q; e->cap_reach = e->n_reach + k; } else k = 0;
    }
    for (i = 0; i < k; i++) {
        int s = r[i].sample < 0 || n <= 0 ? 0 : r[i].sample > n ? n : r[i].sample;
        e->reach[2 * e->n_reach] = r[i].id;
        e->reach[2 * e->n_reach + 1] = (int)((ssa_voice_break == 15 ? 0 : e->frames) + s);    /* (the control) */
        e->n_reach++;
    }
    if (n > 0)
        e->frames += n;
    return n;
}

int ssa_reached(ssa_engine *e, int *out, int cap)
{
    int n = e->n_reach < cap ? e->n_reach : cap;
    if (n <= 0)
        return 0;
    memcpy(out, e->reach, sizeof(int) * 2 * (size_t)n);
    memmove(e->reach, e->reach + 2 * n, sizeof(int) * 2 * (size_t)(e->n_reach - n));
    e->n_reach -= n;
    return n;
}

int ssa_pull(ssa_engine *e, short *out, int cap)
{
    int n = 0;
    while (n < cap) {
        if (e->block_at < e->block_n) {
            int k = e->block_n - e->block_at;
            if (k > cap - n) k = cap - n;
            memcpy(out + n, e->block + e->block_at, sizeof(short) * (size_t)k);
            e->block_at += k;
            n += k;
            continue;
        }
        if (n)                         /* hand over what there is; the next block comes on the next pull */
            break;
        if (e->cur < 0)
            return 0;
        if (e->done) {                 /* finished, and all of it handed over */
            if (e->cur == SSA_ACCENT_SA)
                accent_release(e);
            e->cur = -1;
            return 0;
        }
        if (__atomic_load_n(&e->stop, __ATOMIC_SEQ_CST))
            return -2;
        e->block_n = render(e);
        e->block_at = 0;
        if (e->block_n > 0)
            e->blocks++;
    }
    return n;
}

void ssa_stop(ssa_engine *e)
{
    __atomic_store_n(&e->stop, 1, __ATOMIC_SEQ_CST);
}

int ssa_blocks(const ssa_engine *e) { return e->blocks; }

long ssa_probe(const char *datadir, int voice, const char *utf8, unsigned long long *fnv, char *err, int errlen)
{
    ssa_settings s;
    ssa_engine *e = ssa_new(datadir);
    short buf[4096];
    unsigned long long h = 1469598103934665603ULL;
    long samples = 0;
    int n, i;
    ssa_default_settings(&s);
    if (!e) { snprintf(err, (size_t)errlen, "out of memory"); return -1; }
    if (ssa_load(e, voice, err, errlen) != 0) { ssa_free(e); return -1; }
    if (ssa_start(e, voice, utf8, &s, 100, 100) == 0)
        while ((n = ssa_pull(e, buf, 4096)) > 0) {
            for (i = 0; i < n; i++) {
                unsigned v = (unsigned short)buf[i];
                h = (h ^ (v & 0xFF)) * 1099511628211ULL;
                h = (h ^ (v >> 8)) * 1099511628211ULL;
            }
            samples += n;
        }
    ssa_free(e);
    if (fnv) *fnv = h;
    return samples;
}
