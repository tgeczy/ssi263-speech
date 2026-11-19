/* voices.c -- see voices.h.  One engine table per voice API; the defaults are each NVDA driver's (__init__), named
 * where they come from -- change them together. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voices.h"
#include "translit.h"
#include "blazie/bl_voice.h"
#include "blazie/bl_firmware.h"            /* blv_release_of, blv_release_text: a release's own encoding */
#include "voices_blazie.h"
#include "blazie/bl_numbers.h"
#include "accentsa/as_voice.h"
#ifdef SSV_HAVE_SPEAKOUT
#include "speakout/so_voice.h"
#endif
#if defined(SSV_HAVE_SOUNDINGBOARD) && !(defined(SSV_HAVE_SPEAKOUT) && defined(SO_HAVE_SOUNDINGBOARD))
#error "the Sounding Board is the Speak-Out's voice on its own drivers: build it with SSV_HAVE_SPEAKOUT and so_voice.c with SO_HAVE_SOUNDINGBOARD (and sb_host.c, pc86)"
#endif
#ifdef SSV_HAVE_ACCENTMINI
#include "accentmini/am_voice.h"
#endif
#ifdef SSV_HAVE_MOCKINGBOARD
#include "mockingboard/mb_voice.h"
#endif
#ifdef SSV_HAVE_SMARTALK
#include "smartalk/st_voice.h"
#endif

/* ---- the table ---------------------------------------------------------------------------------------------------- */
/* the slots after the release voices' (a bank holds one unit per slot: SLOTS below counts them from these) */
#define SLOT_SOUNDINGBOARD (9 + SSV_BLAZIE_RELEASE_VOICES)
#define SLOT_SMARTALK (10 + SSV_BLAZIE_RELEASE_VOICES)
static const ssv_info VOICES[] = {
    {"blazie:blazie", "Braille Lite 2000 (June 2003)", "en", SSV_BLAZIE, 0,
     {"blazie/BL2ENG.BNS", "blazie/bl2_2003_warm.state", NULL}},
    {"blazie:blazie_es", "Braille Lite 2000 (Spanish)", "es", SSV_BLAZIE, 1,
     {"blazie/spanish/BL2SPA.BNS", "blazie/spanish/bl2spa_fresh.state", NULL}},
    {"speakout:speakout", "Speak-Out", "en", SSV_SPEAKOUT, 2, {"gw-micro-speakout/SPEAKOUT.HEX", NULL}},
    {"accentmini:mini", "Accent-mini", "en", SSV_ACCENT_MINI, 3, {"aicom-accent-mini/SPKEMS.DVC", NULL}},
    {"accentmini:sa", "Accent SA", "en", SSV_ACCENT_SA, 3,
     {"aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN", NULL}},
    {"mockingboard:mockingboard", "Mockingboard (Sweet Micro Systems)", "en", SSV_MOCKINGBOARD, 4,
     {"sweet-micro-mockingboard/mockingboard-tts-1.1.bin", NULL}},
    /* the earlier text-to-speech of Mockingboard disks 1 and 2: its own rules ("Mocking-bo-wrd", "dough-lars") */
    {"mockingboard:early", "Mockingboard, early (Sweet Micro Systems)", "en", SSV_MOCKINGBOARD, 5,
     {"sweet-micro-mockingboard/mockingboard-tts-early.bin", NULL}},
    /* 0.8: the Braille Lite 2000 in German and Italian (Blazie's September 2000 update, an anonymous donation) and
       the Braille 'n Speak 2000 in Slovak -- each unit's own rules, through the same voice (bl_encoding) */
    {"blazie:blazie_de", "Braille Lite 2000 (German)", "de", SSV_BLAZIE, 6,
     {"blazie/german/Lite2000.BNS", "blazie/german/lite2000_de_fresh.state", NULL}},
    {"blazie:blazie_it", "Braille Lite 2000 (Italian)", "it", SSV_BLAZIE, 7,
     {"blazie/italian/Lite2000.BNS", "blazie/italian/lite2000_it_fresh.state", NULL}},
    {"blazie:blazie_sk", "Braille 'n Speak 2000 (Slovak)", "sk", SSV_BLAZIE, 8,
     {"blazie/bns2000/BS2SLL.BNS", "blazie/bns2000/bs2sll_fresh.state", NULL}},
    /* 0.8: the Braille Lite 2000 in 17 more languages (Blazie's updates, an anonymous donation), one voice per
       language, from the release table (blazie/bl_languages.txt, generated: voices_blazie.h) */
    SSV_BLAZIE_RELEASE_ROWS
    /* 0.8.5: GW Micro's Sounding Board, the Speak-Out's front end in the card's own DOS drivers (version 2.7), for
       those who want them -- imported only, never shipped.  Last, so no earlier voice's index or slot moves */
    {"speakout:soundingboard", "Sounding Board (GW Micro)", "en", SSV_SOUNDINGBOARD, SLOT_SOUNDINGBOARD,
     {"gw-micro-sounding-board/SBLOAD.COM", "gw-micro-sounding-board/SB.COM", NULL}},
    /* Automated Functions' SmarTalk (1995): the box's own DOS text-to-speech, S_TALK.EXE 1.21, on the Accent-mini's
       pc86.  After the Sounding Board, so no earlier voice's index or slot moves */
    {"smartalk:smartalk", "SmarTalk (Automated Functions)", "en", SSV_SMARTALK, SLOT_SMARTALK,
     {"automated-functions-smartalk/S_TALK.EXE", NULL}},
};
#define NVOICES ((int)(sizeof VOICES / sizeof VOICES[0]))

SSV_API int ssv_count(void) { return NVOICES; }

SSV_API const ssv_info *ssv_voice_info(int i) { return i >= 0 && i < NVOICES ? &VOICES[i] : NULL; }

SSV_API int ssv_find(const char *id)
{
    int i;
    for (i = 0; id && i < NVOICES; i++)
        if (!strcmp(VOICES[i].id, id)) return i;
    return -1;
}

static char *join_path(const char *dir, const char *rel)
{
    size_t a = dir && *dir ? strlen(dir) : 0, b = strlen(rel);
    char *p = (char *)malloc(a + b + 2);
    if (!p) return NULL;
    if (a) {
        memcpy(p, dir, a);
        if (dir[a - 1] != '/' && dir[a - 1] != '\\') p[a++] = '/';
    }
    memcpy(p + a, rel, b + 1);
    return p;
}

static unsigned char *read_path(const char *path, size_t *n)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    unsigned char *data = NULL;
    long len;
    *n = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (data = (unsigned char *)malloc((size_t)len)) != NULL) {
        if (fread(data, 1, (size_t)len, f) == (size_t)len) *n = (size_t)len;
        else { free(data); data = NULL; }
    }
    fclose(f);
    return data;
}

/* File k of a source: its bytes (in memory, or read from its path: *owned then says to free them), NULL if neither. */
static const unsigned char *source_bytes(const ssv_source *src, int k, size_t *n, unsigned char **owned)
{
    *owned = NULL;
    if (src->data[k]) { *n = src->size[k]; return src->data[k]; }
    return *owned = read_path(src->path[k], n);
}

/* ---- the engines: create, set, speak, render, cancel, destroy -------------------------------------------------------
   A unit is the voice API's own object; set takes the utterance's settings just before speak. */
typedef struct {
    void *(*create)(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen);
    void (*set)(void *u, const ssv_settings *s);
    int (*speak)(void *u, const char *utf8, int pitch_offset);
    int (*render)(void *u, const short **pcm, int *done);
    void (*cancel)(void *u);
    void (*destroy)(void *u);
    /* the unit's own index marks (marks.h), for an engine that has them; NULL: its marks are estimated.
       speak_marked: as speak, or -2 when the unit cannot take marks for this utterance (estimated then) */
    int (*speak_marked)(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks);
    int (*reached)(void *u, const ssv_reach **out);
    int (*marks_own)(void *u);                      /* after set: would speak_marked take the marks? */
} ssv_engine;

/* the Braille Lite: blazie.py (rate, pitch, volume, tone, short pauses, number words, run ahead, line-start lift;
   inflection and whine are the unit's boot) */
/* each Blazie unit's text: the English unit's 7-bit, the Spanish, German and Italian units' DOS code page 850, the
   Slovak unit's 852 with o circumflex as its capital (read from what each says for the accented letters of its
   language) */
static int bl_encoding(const char *lang)
{
    if (!strcmp(lang, "sk")) return BLV_CP852SK;
    if (!strcmp(lang, "es")) return BLV_CP850ES;
    if (!strcmp(lang, "it")) return BLV_CP850IT;
    if (!strcmp(lang, "de")) return BLV_CP850;
    return BLV_LATIN1;
}

/* "Custom number processing" (bl_numbers): English and Spain's Spanish only; every other unit reads its numbers
   itself (blazie.py NUMBER_LANGS), except that past twelve digits one whose reader folds a trillion into billions
   gets them a digit at a time (bl_trillions; ssv_voice.folds, from the release table's trillions column) */
static int bl_has_numbers(const char *lang) { return !strcmp(lang, "en") || !strcmp(lang, "es"); }

/* the Italian Braille Lite and the Slovak Braille 'n Speak never echo ^F (XON/XOFF only): their end comes from the
   chip alone (bl_host.c "no_echo"; blazie.py NO_ECHO) */
static int bl_no_echo(const char *lang) { return !strcmp(lang, "it") || !strcmp(lang, "sk"); }

static void *eng_bl_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    bl_voice *v;
    int release, encoding;
    if (!src->path[0] || !src->path[1]) { snprintf(err, errlen, "the Braille Lite's files: no path"); return NULL; }
    /* the text encoding the firmware's own release names (bl_languages.txt), else by the language as before -- the
       shipped voices' rows name the same ones */
    release = blv_release_of(src->path[0]);
    encoding = release >= 0 ? blv_release_text(release) : bl_encoding(info->lang);
    v = blv_create(src->path[0], src->path[1], encoding,
                   (double)b->sample_rate, b->inflection != 0, b->whine, err, errlen);
    if (v && bl_no_echo(info->lang))
        bh_set_int(blv_host(v), "no_echo", 1);
    return v;
}
static void eng_bl_set(void *u, const ssv_settings *s)
{
    blv_set((bl_voice *)u, s->rate, s->pitch, s->tone, s->volume, s->pack);
    blv_set_run_ahead((bl_voice *)u, s->run_ahead);
    blv_set_line_lift((bl_voice *)u, s->line_lift);
    blv_set_numbers((bl_voice *)u, s->numbers ? bl_numbers : NULL);
}
static int eng_bl_speak(void *u, const char *utf8, int pitch_offset)
{
    int n = blv_speak((bl_voice *)u, utf8);
    (void)pitch_offset;
    return n < 0 ? -1 : n > 0;
}
static int eng_bl_speak_marked(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    int n;
    (void)pitch_offset;
    if (!blv_marks_exact((bl_voice *)u))             /* (after eng_bl_set: run ahead and the packing are this text's) */
        return -2;
    n = blv_speak_marked((bl_voice *)u, utf8, marks, n_marks);
    return n < 0 ? -1 : n > 0;
}
static int eng_bl_reached(void *u, const ssv_reach **out) { return blv_reached((bl_voice *)u, out); }
static int eng_bl_marks_own(void *u) { return blv_marks_exact((bl_voice *)u); }
static int eng_bl_render(void *u, const short **pcm, int *done) { return blv_render((bl_voice *)u, pcm, done); }
static void eng_bl_cancel(void *u) { blv_cancel((bl_voice *)u); }
static void eng_bl_destroy(void *u) { blv_destroy((bl_voice *)u); }

/* the Accent SA: accentmini.py's "sa" voice (rate, pitch, inflection, volume, number words) */
static void *eng_as_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    size_t n[3];
    const unsigned char *rom[3];
    unsigned char *owned[3];
    as_voice *v = NULL;
    int k;
    (void)info;
    for (k = 0; k < 3; k++)
        rom[k] = source_bytes(src, k, &n[k], &owned[k]);
    if (rom[0] && rom[1] && rom[2])
        v = asv_create(rom[0], n[0], rom[1], n[1], rom[2], n[2], (double)b->sample_rate, err, errlen);
    else
        snprintf(err, errlen, "could not read the Accent SA's ROMs");
    for (k = 0; k < 3; k++)
        free(owned[k]);
    return v;
}
static void eng_as_set(void *u, const ssv_settings *s)
{
    asv_set((as_voice *)u, s->rate, s->pitch, s->inflection, s->volume, s->numbers);
}
static int eng_as_speak(void *u, const char *utf8, int pitch_offset) { return asv_speak((as_voice *)u, utf8, pitch_offset) > 0; }
static int eng_as_speak_marked(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    return asv_speak_marked((as_voice *)u, utf8, pitch_offset, marks, n_marks) > 0;
}
static int eng_as_reached(void *u, const ssv_reach **out) { return asv_reached((as_voice *)u, out); }
static int eng_as_marks_own(void *u) { return asv_marks_exact((as_voice *)u); }
static int eng_as_render(void *u, const short **pcm, int *done) { return asv_render((as_voice *)u, pcm, done); }
static void eng_as_cancel(void *u) { asv_cancel((as_voice *)u); }
static void eng_as_destroy(void *u) { asv_destroy((as_voice *)u); }

#ifdef SSV_HAVE_SPEAKOUT
/* the Speak-Out: speakout.py (rate, pitch, tone A-Z, volume, join phrases, short pauses) */
static void *eng_so_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    (void)info;
    if (src->data[0])
        return sov_create_hex((const char *)src->data[0], src->size[0], (double)b->sample_rate, err, errlen);
    if (!src->path[0]) { snprintf(err, errlen, "the Speak-Out's SPEAKOUT.HEX: no path"); return NULL; }
    return sov_create(src->path[0], (double)b->sample_rate, err, errlen);
}
static void eng_so_set(void *u, const ssv_settings *s)
{
    sov_set((so_voice *)u, s->rate, s->pitch, s->tone, s->volume, s->join, s->pack);
}
static int eng_so_speak(void *u, const char *utf8, int pitch_offset) { return sov_speak((so_voice *)u, utf8, pitch_offset) > 0; }
static int eng_so_speak_marked(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    if (!sov_marks_exact((so_voice *)u))
        return -2;
    return sov_speak_marked((so_voice *)u, utf8, pitch_offset, marks, n_marks) > 0;
}
static int eng_so_reached(void *u, const ssv_reach **out) { return sov_reached((so_voice *)u, out); }
static int eng_so_marks_own(void *u) { return sov_marks_exact((so_voice *)u); }
static int eng_so_render(void *u, const short **pcm, int *done) { return sov_render((so_voice *)u, pcm, done); }
static void eng_so_cancel(void *u) { sov_cancel((so_voice *)u); }
static void eng_so_destroy(void *u) { sov_destroy((so_voice *)u); }
#endif

#ifdef SSV_HAVE_SOUNDINGBOARD
/* the Sounding Board: the Speak-Out's voice (its settings, set, speak, render, cancel) on the card's DOS drivers */
static void *eng_sb_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    size_t n[2];
    unsigned char *owned[2];
    const unsigned char *d[2];
    so_voice *v = NULL;
    int k;
    for (k = 0; k < 2; k++)
        d[k] = source_bytes(src, k, &n[k], &owned[k]);
    if (d[0] && d[1])
        v = sov_create_sb(d[0], n[0], d[1], n[1], (double)b->sample_rate, err, errlen);
    else
        snprintf(err, errlen, "could not read the Sounding Board's %s and %s", info->files[0], info->files[1]);
    for (k = 0; k < 2; k++)
        free(owned[k]);
    return v;
}
#endif

#ifdef SSV_HAVE_ACCENTMINI
/* the Accent-mini: accentmini.py's "mini" voice (rate, pitch, inflection, volume, number words, voice characteristic) */
static void *eng_am_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    (void)info;
    if (src->data[0])
        return amv_create_mem(src->data[0], src->size[0], (double)b->sample_rate, err, errlen);
    if (!src->path[0]) { snprintf(err, errlen, "the Accent-mini's SPKEMS.DVC: no path"); return NULL; }
    return amv_create(src->path[0], (double)b->sample_rate, err, errlen);
}
static void eng_am_set(void *u, const ssv_settings *s)
{
    amv_set((am_voice *)u, s->rate, s->pitch, s->inflection, s->volume, s->numbers, s->tone);
}
static int eng_am_speak(void *u, const char *utf8, int pitch_offset)
{
    int r = amv_speak((am_voice *)u, utf8, pitch_offset);
    return r < 0 ? -1 : r > 0;
}
static int eng_am_speak_marked(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    int r;
    if (!amv_marks_exact((am_voice *)u))                     /* a driver whose count is not known: estimated */
        return -2;
    r = amv_speak_marked((am_voice *)u, utf8, pitch_offset, marks, n_marks);
    return r < 0 ? -1 : r > 0;
}
static int eng_am_reached(void *u, const ssv_reach **out) { return amv_reached((am_voice *)u, out); }
static int eng_am_marks_own(void *u) { return amv_marks_exact((am_voice *)u); }
static int eng_am_render(void *u, const short **pcm, int *done) { return amv_render((am_voice *)u, pcm, done); }
static void eng_am_cancel(void *u) { amv_cancel((am_voice *)u); }
static void eng_am_destroy(void *u) { amv_destroy((am_voice *)u); }
#endif

#ifdef SSV_HAVE_MOCKINGBOARD
/* the Mockingboard: mb_voice.h (rate, pitch, volume, number words) */
static void *eng_mb_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    size_t n;
    unsigned char *owned;
    const unsigned char *d = source_bytes(src, 0, &n, &owned);
    mb_voice *v = NULL;
    if (d)
        v = mbv_create(d, n, (double)b->sample_rate, err, errlen);
    else
        snprintf(err, errlen, "could not read the Mockingboard's %s", info->files[0]);
    free(owned);
    return v;
}
static void eng_mb_set(void *u, const ssv_settings *s) { mbv_set((mb_voice *)u, s->rate, s->pitch, s->volume, s->numbers); }
static int eng_mb_speak(void *u, const char *utf8, int pitch_offset) { return mbv_speak((mb_voice *)u, utf8, pitch_offset) > 0; }
static int eng_mb_render(void *u, const short **pcm, int *done) { return mbv_render((mb_voice *)u, pcm, done); }
static void eng_mb_cancel(void *u) { mbv_cancel((mb_voice *)u); }
static void eng_mb_destroy(void *u) { mbv_destroy((mb_voice *)u); }
#endif

#ifdef SSV_HAVE_SMARTALK
/* the SmarTalk: st_voice.h (rate, pitch, tone A-Z, volume, inflection on or off, number words) */
static void *eng_st_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    size_t n;
    unsigned char *owned;
    const unsigned char *d = source_bytes(src, 0, &n, &owned);
    st_voice *v = NULL;
    if (d)
        v = stv_create(d, n, (double)b->sample_rate, err, errlen);
    else
        snprintf(err, errlen, "could not read the SmarTalk's %s", info->files[0]);
    free(owned);
    return v;
}
static void eng_st_set(void *u, const ssv_settings *s)
{
    stv_set((st_voice *)u, s->rate, s->pitch, s->tone, s->volume, s->inflection >= 50, s->numbers);
}
static int eng_st_speak(void *u, const char *utf8, int pitch_offset)
{
    int r = stv_speak((st_voice *)u, utf8, pitch_offset);
    return stv_fault((st_voice *)u) ? -1 : r > 0;
}
static int eng_st_speak_marked(void *u, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    int r = stv_speak_marked((st_voice *)u, utf8, pitch_offset, marks, n_marks);
    return stv_fault((st_voice *)u) ? -1 : r > 0;
}
static int eng_st_reached(void *u, const ssv_reach **out) { return stv_reached((st_voice *)u, out); }
static int eng_st_marks_own(void *u) { return stv_marks_exact((st_voice *)u); }
static int eng_st_render(void *u, const short **pcm, int *done) { return stv_render((st_voice *)u, pcm, done); }
static void eng_st_cancel(void *u) { stv_cancel((st_voice *)u); }
static void eng_st_destroy(void *u) { stv_destroy((st_voice *)u); }
#endif

static const ssv_engine *engine(int kind)
{
    static const ssv_engine bl = {eng_bl_create, eng_bl_set, eng_bl_speak, eng_bl_render, eng_bl_cancel, eng_bl_destroy,
                                  eng_bl_speak_marked, eng_bl_reached, eng_bl_marks_own};
    static const ssv_engine as = {eng_as_create, eng_as_set, eng_as_speak, eng_as_render, eng_as_cancel, eng_as_destroy,
                                  eng_as_speak_marked, eng_as_reached, eng_as_marks_own};
#ifdef SSV_HAVE_SPEAKOUT
    static const ssv_engine so = {eng_so_create, eng_so_set, eng_so_speak, eng_so_render, eng_so_cancel, eng_so_destroy,
                                  eng_so_speak_marked, eng_so_reached, eng_so_marks_own};
#endif
#ifdef SSV_HAVE_ACCENTMINI
    static const ssv_engine am = {eng_am_create, eng_am_set, eng_am_speak, eng_am_render, eng_am_cancel, eng_am_destroy,
                                  eng_am_speak_marked, eng_am_reached, eng_am_marks_own};
#endif
#ifdef SSV_HAVE_MOCKINGBOARD
    static const ssv_engine mb = {eng_mb_create, eng_mb_set, eng_mb_speak, eng_mb_render, eng_mb_cancel, eng_mb_destroy};
#endif
#ifdef SSV_HAVE_SOUNDINGBOARD
    static const ssv_engine sb = {eng_sb_create, eng_so_set, eng_so_speak, eng_so_render, eng_so_cancel, eng_so_destroy,
                                  eng_so_speak_marked, eng_so_reached, eng_so_marks_own};
#endif
#ifdef SSV_HAVE_SMARTALK
    static const ssv_engine st = {eng_st_create, eng_st_set, eng_st_speak, eng_st_render, eng_st_cancel, eng_st_destroy,
                                  eng_st_speak_marked, eng_st_reached, eng_st_marks_own};
#endif
    switch (kind) {
    case SSV_BLAZIE: return &bl;
    case SSV_ACCENT_SA: return &as;
#ifdef SSV_HAVE_SPEAKOUT
    case SSV_SPEAKOUT: return &so;
#endif
#ifdef SSV_HAVE_ACCENTMINI
    case SSV_ACCENT_MINI: return &am;
#endif
#ifdef SSV_HAVE_MOCKINGBOARD
    case SSV_MOCKINGBOARD: return &mb;
#endif
#ifdef SSV_HAVE_SOUNDINGBOARD
    case SSV_SOUNDINGBOARD: return &sb;
#endif
#ifdef SSV_HAVE_SMARTALK
    case SSV_SMARTALK: return &st;
#endif
    default: return NULL;
    }
}

SSV_API int ssv_engine_built(int i)
{
    return i >= 0 && i < NVOICES && engine(VOICES[i].engine) != NULL;
}

SSV_API int ssv_available(int i, const char *fwdir)
{
    int k;
    if (!ssv_engine_built(i)) return 0;
    for (k = 0; VOICES[i].files[k]; k++) {
        char *path = join_path(fwdir, VOICES[i].files[k]);
        FILE *f = path ? fopen(path, "rb") : NULL;
        free(path);
        if (!f) return 0;
        fclose(f);
    }
    return 1;
}

/* ---- the defaults: each driver's __init__ ------------------------------------------------------------------------ */
SSV_API void ssv_boot_defaults(ssv_boot *b)
{
    b->sample_rate = 22050;        /* ssi263_rates.DEFAULT */
    b->inflection = 1;             /* blazie.py: self._infl = True */
    b->whine = 0;                  /* blazie.py: self._whine = "off" */
}

SSV_API void ssv_defaults(int i, ssv_settings *s)
{
    int kind = i >= 0 && i < NVOICES ? VOICES[i].engine : SSV_BLAZIE;
    s->rate = 50;
    s->pitch = 50;
    s->volume = 100;
    s->pack = 1;                   /* blazie.py, speakout.py: self._short = True */
    s->join = 1;                   /* speakout.py: self._join = True */
    s->inflection = 100;           /* accentmini.py: self._inflection = 100 */
    s->numbers = 1;                /* blazie.py, accentmini.py: self._numbers = True */
    s->run_ahead = 0;              /* blazie.py: self._run_ahead = False */
    s->line_lift = 0;              /* blazie.py: self._line_lift = False */
    s->tone = kind == SSV_BLAZIE ? 7          /* DEFAULT_TONE */
            : kind == SSV_SPEAKOUT || kind == SSV_SOUNDINGBOARD ? 8   /* self._tone = "i" (the Sounding Board: its manual's too) */
            : kind == SSV_ACCENT_MINI ? 5     /* self._voice_char = "5" */
            : kind == SSV_SMARTALK ? 8        /* smartalk.py: self._tone = "i", the TSR's own */
            : 0;
}

/* ---- a voice ------------------------------------------------------------------------------------------------------- */
struct ssv_voice {
    int index, rate;
    const ssv_engine *e;
    void *u;
    int folds;                     /* a Blazie unit whose own reader folds a trillion (bl_firmware.h BLV_TRILLIONS_FOLD) */
    /* index marks (marks.h, voices.h): how this utterance's are found */
    int marks;                     /* MARKS_NONE (ssv_render passes the engine's blocks through), _OWN, _ESTIMATED */
    struct { int id; double share; } *mk;          /* estimated: those not yet reported, each its share of the text */
    int n_mk, cap_mk;
    short *q;                      /* estimated: the audio rendered ahead of the caller, */
    int q_len, q_cap, q_at;        /*   its samples, and where the caller is in them, */
    int *qb, n_qb, cap_qb, qb_at;  /*   block by block as the engine gave them */
    long total, fed;               /* samples rendered, samples returned */
    int eng_done, calls;
    ssv_reach *reached;          /* the marks the last block reached (ssv_reached) */
    int n_reached, cap_reached;
};
#define MARKS_NONE 0
#define MARKS_OWN 1
#define MARKS_ESTIMATED 2
#define AHEAD 8                    /* blocks rendered for each block returned, as the NVDA add-on's ssi263_flow.py */

static int valid_rate(int r) { return r == 11025 || r == 22050 || r == 44100 ? r : 22050; }

SSV_API ssv_voice *ssv_create(int i, const char *fwdir, const ssv_boot *b, char *err, int errlen)
{
    ssv_source src;
    ssv_voice *v;
    int k;
    if (i < 0 || i >= NVOICES) { snprintf(err, errlen, "no voice %d", i); return NULL; }
    memset(&src, 0, sizeof src);
    for (k = 0; k < 4 && VOICES[i].files[k]; k++)
        if (!(src.path[k] = join_path(fwdir, VOICES[i].files[k]))) { snprintf(err, errlen, "out of memory"); break; }
    v = k == 4 || !VOICES[i].files[k] ? ssv_create_from(i, &src, b, err, errlen) : NULL;
    for (k = 0; k < 4; k++)
        free((char *)src.path[k]);
    return v;
}

SSV_API ssv_voice *ssv_create_from(int i, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    ssv_boot d;
    ssv_voice *v;
    const ssv_engine *e;
    if (i < 0 || i >= NVOICES) { snprintf(err, errlen, "no voice %d", i); return NULL; }
    e = engine(VOICES[i].engine);
    if (!e) { snprintf(err, errlen, "%s: its engine is not in this library", VOICES[i].id); return NULL; }
    if (!b) { ssv_boot_defaults(&d); b = &d; }
    d = *b;
    d.sample_rate = valid_rate(b->sample_rate);
    v = (ssv_voice *)calloc(1, sizeof *v);
    if (!v) { snprintf(err, errlen, "out of memory"); return NULL; }
    v->index = i;
    v->rate = d.sample_rate;
    v->e = e;
    v->u = e->create(&VOICES[i], src, &d, err, errlen);
    if (!v->u) { free(v); return NULL; }
    if (VOICES[i].engine == SSV_BLAZIE)    /* its firmware's release, as measured (bl_languages.txt's trillions) */
        v->folds = blv_release_trillions(blv_release_of(src->path[0])) == BLV_TRILLIONS_FOLD;
    return v;
}

SSV_API void ssv_destroy(ssv_voice *v)
{
    if (!v) return;
    v->e->destroy(v->u);
    free(v->mk);
    free(v->q);
    free(v->qb);
    free(v->reached);
    free(v);
}

SSV_API int ssv_index(const ssv_voice *v) { return v->index; }
SSV_API int ssv_sample_rate(const ssv_voice *v) { return v->rate; }

SSV_API int ssv_marks_exact(ssv_voice *v, const ssv_settings *s)
{
    ssv_settings d;
    if (!v->e->marks_own || getenv("SSV_MARKS_ESTIMATE"))
        return 0;
    if (!s) { ssv_defaults(v->index, &d); s = &d; }
    if (VOICES[v->index].engine == SSV_BLAZIE && !bl_has_numbers(VOICES[v->index].lang)
            && (s->numbers || s->run_ahead)) {
        d = *s;                    /* (as ssv_speak_marked gives this unit its settings) */
        d.numbers = 0;
        d.run_ahead = 0;
        s = &d;
    }
    v->e->set(v->u, s);
    return v->e->marks_own(v->u) != 0;
}

SSV_API int ssv_speak_marked(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset,
                             const ssv_mark *marks, int n_marks)
{
    ssv_settings d;
    int guard = 0;
    if (!s) { ssv_defaults(v->index, &d); s = &d; }
    if (VOICES[v->index].engine == SSV_BLAZIE && !bl_has_numbers(VOICES[v->index].lang)
            && (s->numbers || s->run_ahead)) {
        guard = s->numbers && v->folds;
        d = *s;                    /* the unit reads its own numbers: no number words of ours; and run ahead is */
        d.numbers = 0;             /* tested on the English and Spanish units only (blazie.py RUN_AHEAD_TESTED; */
        d.run_ahead = 0;           /* without ^F echoes it cannot even tell its end) */
        s = &d;
    }
    v->e->set(v->u, s);
    if (guard)                     /* but past twelve digits a folding unit gets them a digit at a time (blazie.py) */
        blv_set_numbers((bl_voice *)v->u, bl_trillions);
    if (!utf8) utf8 = "";
    v->marks = MARKS_NONE;
    v->n_mk = v->n_reached = 0;
    v->q_len = v->q_at = v->n_qb = v->qb_at = 0;
    v->total = v->fed = 0;
    v->eng_done = v->calls = 0;
    if (marks && n_marks > 0) {
        int r, i, at = 0, cp = 0, all = 0;
        /* (SSV_MARKS_ESTIMATE: the tests' switch, every voice estimated -- what a unit without marks of its own
           gets, a Braille Lite release not measured for them, and one run ahead) */
        if (v->e->speak_marked && !getenv("SSV_MARKS_ESTIMATE")
                && (r = v->e->speak_marked(v->u, utf8, pitch_offset, marks, n_marks)) != -2) {
            if (r >= 0) v->marks = MARKS_OWN;
            return r;
        }
        /* estimated: each mark's share of the text's characters (UTF-8: the bytes that start one) */
        if (n_marks > v->cap_mk) {
            void *q = realloc(v->mk, sizeof *v->mk * (size_t)n_marks);
            if (q) { v->mk = q; v->cap_mk = n_marks; }
        }
        if (n_marks <= v->cap_mk) {
            for (i = 0; utf8[i]; i++) all += ((unsigned char)utf8[i] & 0xC0) != 0x80;
            for (i = 0; i < n_marks; i++) {
                for (; utf8[at] && at < marks[i].at; at++) cp += ((unsigned char)utf8[at] & 0xC0) != 0x80;
                v->mk[i].id = marks[i].id;
                v->mk[i].share = all ? (double)cp / all : 0.0;
            }
            v->n_mk = n_marks;
            v->marks = MARKS_ESTIMATED;
        }
    }
    {
        int r = v->e->speak(v->u, utf8, pitch_offset);
        if (r < 0) { v->marks = MARKS_NONE; v->n_mk = 0; }      /* the unit failed: nothing was said, none reached */
        return r;
    }
}

SSV_API int ssv_speak(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset)
{
    return ssv_speak_marked(v, s, utf8, pitch_offset, NULL, 0);
}

static int reached_room(ssv_voice *v, int n)
{
    if (n > v->cap_reached) {
        ssv_reach *r = (ssv_reach *)realloc(v->reached, sizeof(ssv_reach) * (size_t)n);
        if (!r) return 0;
        v->reached = r;
        v->cap_reached = n;
    }
    return 1;
}

/* estimated marks: the engine rendered ahead of the caller, so the utterance's length is known before the caller
   has heard much of it; then each mark at its share of that length */
static int render_estimated(ssv_voice *v, const short **pcm, int *done)
{
    int ahead = v->calls++ ? AHEAD : 1, n = 0, i, kept = 0;      /* the first block at once: no wait for a first sound */
    const char *brk = getenv("SSV_MARKS_BREAK");                 /* the tests' controls: "queue" returns the buffer's
                                                                    start for every block; anything else reports
                                                                    every mark at once */
    int brk_queue = brk && !strcmp(brk, "queue"), brk_now = brk && !brk_queue;
    if (v->qb_at == v->n_qb)                                     /* the caller has it all: from the buffer's start */
        v->q_len = v->q_at = v->n_qb = v->qb_at = 0;
    while (!v->eng_done && ahead-- > 0) {
        const short *p;
        int d = 0, got = v->e->render(v->u, &p, &d);
        if (got < 0) got = 0;
        if (v->q_len + got > v->q_cap) {
            int cap = v->q_cap ? v->q_cap : 16384;
            short *b;
            while (cap < v->q_len + got) cap *= 2;
            b = (short *)realloc(v->q, sizeof(short) * (size_t)cap);
            if (!b) { v->eng_done = 1; break; }
            v->q = b;
            v->q_cap = cap;
        }
        if (v->n_qb == v->cap_qb) {
            int cap = v->cap_qb ? v->cap_qb * 2 : 256, *b = (int *)realloc(v->qb, sizeof(int) * (size_t)cap);
            if (!b) { v->eng_done = 1; break; }
            v->qb = b;
            v->cap_qb = cap;
        }
        if (got) memcpy(v->q + v->q_len, p, sizeof(short) * (size_t)got);
        v->q_len += got;
        v->qb[v->n_qb++] = got;
        v->total += got;
        v->eng_done = d;
    }
    if (v->qb_at < v->n_qb) {
        n = v->qb[v->qb_at++];
        *pcm = v->q + (brk_queue ? 0 : v->q_at);
        v->q_at += n;
    } else
        *pcm = v->q;
    *done = v->eng_done && v->qb_at == v->n_qb;
    v->n_reached = 0;
    if (!reached_room(v, v->n_mk)) v->n_mk = 0;
    for (i = 0; i < v->n_mk; i++) {
        double at = v->mk[i].share * (double)v->total - (double)v->fed;      /* its place, in this block's samples */
        int now = brk_now || v->mk[i].share <= 0.0 || *done || (v->eng_done && at <= (double)n);
        if (!now || kept) {                                      /* (in the caller's order: none passes one kept) */
            v->mk[kept++] = v->mk[i];
            continue;
        }
        v->reached[v->n_reached].id = v->mk[i].id;
        v->reached[v->n_reached].sample = v->mk[i].share <= 0.0 || at < 0.0 ? 0 : at > (double)n ? n : (int)at;
        v->reached[v->n_reached].exact = 0;
        if (v->n_reached && v->reached[v->n_reached].sample < v->reached[v->n_reached - 1].sample)
            v->reached[v->n_reached].sample = v->reached[v->n_reached - 1].sample;
        v->n_reached++;
    }
    v->n_mk = kept;
    v->fed += n;
    return n;
}

SSV_API int ssv_render(ssv_voice *v, const short **pcm, int *done)
{
    int n;
    if (v->marks == MARKS_ESTIMATED)
        return render_estimated(v, pcm, done);
    n = v->e->render(v->u, pcm, done);
    v->n_reached = 0;
    if (v->marks == MARKS_OWN) {                     /* the engine's, kept here: one place to read them */
        const ssv_reach *r;
        int k = v->e->reached(v->u, &r);
        if (k > 0 && reached_room(v, k)) {
            memcpy(v->reached, r, sizeof(ssv_reach) * (size_t)k);
            v->n_reached = k;
        }
    }
    return n;
}

SSV_API int ssv_reached(ssv_voice *v, const ssv_reach **out)
{
    *out = v->reached;
    return v->n_reached;
}

SSV_API void ssv_cancel(ssv_voice *v)
{
    v->e->cancel(v->u);
    v->marks = MARKS_NONE;                           /* none of a cancelled text's marks is reported */
    v->n_mk = v->n_reached = 0;
    v->q_len = v->q_at = v->n_qb = v->qb_at = 0;
}

/* ---- a bank ---------------------------------------------------------------------------------------------------------- */
/* the table's slots 0..8, one per release voice, the Sounding Board's and the SmarTalk's: the last slot + 1.  (It was
   the Sounding Board's + 1 when the SmarTalk came, so the SmarTalk's unit sat past the end of the bank and was never
   let go on a boot change: the SAPI engine's test caught it.) */
#define SLOTS (SLOT_SMARTALK + 1)

struct ssv_bank {
    char *fwdir;
    ssv_boot boot;
    ssv_voice *slot[SLOTS];
};

SSV_API ssv_bank *ssv_bank_new(const char *fwdir)
{
    ssv_bank *k = (ssv_bank *)calloc(1, sizeof *k);
    if (!k) return NULL;
    k->fwdir = join_path(fwdir ? fwdir : "", "");
    if (!k->fwdir) { free(k); return NULL; }
    ssv_boot_defaults(&k->boot);
    return k;
}

static void bank_clear(ssv_bank *k)
{
    int s, n = getenv("SSV_BANK_BREAK") ? SLOTS - 1 : SLOTS;   /* the tests' control: the last slot kept, as it was */
    for (s = 0; s < n; s++) {
        ssv_destroy(k->slot[s]);
        k->slot[s] = NULL;
    }
}

SSV_API void ssv_bank_free(ssv_bank *k)
{
    if (!k) return;
    bank_clear(k);
    free(k->fwdir);
    free(k);
}

SSV_API void ssv_bank_boot(ssv_bank *k, const ssv_boot *b)
{
    ssv_boot d = *b;
    d.sample_rate = valid_rate(b->sample_rate);
    d.inflection = b->inflection != 0;
    if (d.sample_rate != k->boot.sample_rate || d.inflection != k->boot.inflection || d.whine != k->boot.whine)
        bank_clear(k);
    k->boot = d;
}

SSV_API ssv_voice *ssv_bank_voice(ssv_bank *k, int i, char *err, int errlen)
{
    int s;
    if (i < 0 || i >= NVOICES) { snprintf(err, errlen, "no voice %d", i); return NULL; }
    s = VOICES[i].slot;
    if (s < 0 || s >= SLOTS) { snprintf(err, errlen, "voice %d's slot %d is not in the bank", i, s); return NULL; }
    if (k->slot[s] && k->slot[s]->index != i) {
        ssv_destroy(k->slot[s]);
        k->slot[s] = NULL;
    }
    if (!k->slot[s])
        k->slot[s] = ssv_create(i, k->fwdir, &k->boot, err, errlen);
    return k->slot[s];
}

/* ---- the accented-letter pass on its own (translit.h) ---------------------------------------------------------- */
SSV_API int ssv_translit(const char *utf8, int n, int charset, char *out, int cap)
{
    unsigned *in, *t;
    unsigned char *u;
    int k, m, len;
    if (n < 0) n = (int)strlen(utf8);
    in = (unsigned *)malloc(sizeof(unsigned) * (size_t)(n + 1));
    if (!in) return -1;
    k = tl_utf8_decode(utf8, n, in);
    t = (unsigned *)malloc(sizeof(unsigned) * (size_t)tl_room(k));
    if (!t) { free(in); return -1; }
    /* the voice's own alphabet for its encoding (SSV_CP* are bl_voice.h's BLV_* ids; SSV_ASCII is BLV_LATIN1's) */
    m = tl_apply(in, k, blv_text_known(charset), t);
    free(in);
    u = (unsigned char *)malloc((size_t)m * 4 + 1);
    if (!u) { free(t); return -1; }
    len = tl_utf8_encode(t, m, u);
    free(t);
    if (out && len <= cap) memcpy(out, u, (size_t)len);
    free(u);
    return len;
}
