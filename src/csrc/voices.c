/* voices.c -- see voices.h.  One engine table per voice API; the defaults are each NVDA driver's (__init__), named
 * where they come from -- change them together. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voices.h"
#include "translit.h"
#include "blazie/bl_cp850.h"
#include "blazie/bl_voice.h"
#include "blazie/bl_numbers.h"
#include "accentsa/as_voice.h"
#ifdef SSV_HAVE_SPEAKOUT
#include "speakout/so_voice.h"
#endif
#ifdef SSV_HAVE_ACCENTMINI
#include "accentmini/am_voice.h"
#endif
#ifdef SSV_HAVE_MOCKINGBOARD
#include "mockingboard/mb_voice.h"
#endif

/* ---- the table ---------------------------------------------------------------------------------------------------- */
static const ssv_info VOICES[] = {
    {"blazie:blazie", "Braille Lite 2000 (June 2003)", "en", SSV_BLAZIE, 0,
     {"blazie/BL2ENG.BNS", "blazie/bl2_2003_warm.state", NULL}},
    {"blazie:blazie_es", "Braille Lite 2000 (espa\xc3\xb1ol)", "es", SSV_BLAZIE, 1,
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
} ssv_engine;

/* the Braille Lite: blazie.py (rate, pitch, volume, tone, short pauses, number words, run ahead; inflection and whine
   are the unit's boot) */
static void *eng_bl_create(const ssv_info *info, const ssv_source *src, const ssv_boot *b, char *err, int errlen)
{
    if (!src->path[0] || !src->path[1]) { snprintf(err, errlen, "the Braille Lite's files: no path"); return NULL; }
    return blv_create(src->path[0], src->path[1], strcmp(info->lang, "es") ? BLV_LATIN1 : BLV_CP850,
                      (double)b->sample_rate, b->inflection != 0, b->whine, err, errlen);
}
static void eng_bl_set(void *u, const ssv_settings *s)
{
    blv_set((bl_voice *)u, s->rate, s->pitch, s->tone, s->volume, s->pack);
    blv_set_run_ahead((bl_voice *)u, s->run_ahead);
    blv_set_numbers((bl_voice *)u, s->numbers ? bl_numbers : NULL);
}
static int eng_bl_speak(void *u, const char *utf8, int pitch_offset)
{
    int n = blv_speak((bl_voice *)u, utf8);
    (void)pitch_offset;
    return n < 0 ? -1 : n > 0;
}
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
static int eng_so_render(void *u, const short **pcm, int *done) { return sov_render((so_voice *)u, pcm, done); }
static void eng_so_cancel(void *u) { sov_cancel((so_voice *)u); }
static void eng_so_destroy(void *u) { sov_destroy((so_voice *)u); }
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

static const ssv_engine *engine(int kind)
{
    static const ssv_engine bl = {eng_bl_create, eng_bl_set, eng_bl_speak, eng_bl_render, eng_bl_cancel, eng_bl_destroy};
    static const ssv_engine as = {eng_as_create, eng_as_set, eng_as_speak, eng_as_render, eng_as_cancel, eng_as_destroy};
#ifdef SSV_HAVE_SPEAKOUT
    static const ssv_engine so = {eng_so_create, eng_so_set, eng_so_speak, eng_so_render, eng_so_cancel, eng_so_destroy};
#endif
#ifdef SSV_HAVE_ACCENTMINI
    static const ssv_engine am = {eng_am_create, eng_am_set, eng_am_speak, eng_am_render, eng_am_cancel, eng_am_destroy};
#endif
#ifdef SSV_HAVE_MOCKINGBOARD
    static const ssv_engine mb = {eng_mb_create, eng_mb_set, eng_mb_speak, eng_mb_render, eng_mb_cancel, eng_mb_destroy};
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
    s->tone = kind == SSV_BLAZIE ? 7          /* DEFAULT_TONE */
            : kind == SSV_SPEAKOUT ? 8        /* self._tone = "i" */
            : kind == SSV_ACCENT_MINI ? 5     /* self._voice_char = "5" */
            : 0;
}

/* ---- a voice ------------------------------------------------------------------------------------------------------- */
struct ssv_voice {
    int index, rate;
    const ssv_engine *e;
    void *u;
};

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
    return v;
}

SSV_API void ssv_destroy(ssv_voice *v)
{
    if (!v) return;
    v->e->destroy(v->u);
    free(v);
}

SSV_API int ssv_index(const ssv_voice *v) { return v->index; }
SSV_API int ssv_sample_rate(const ssv_voice *v) { return v->rate; }

SSV_API int ssv_speak(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset)
{
    ssv_settings d;
    if (!s) { ssv_defaults(v->index, &d); s = &d; }
    v->e->set(v->u, s);
    return v->e->speak(v->u, utf8 ? utf8 : "", pitch_offset);
}

SSV_API int ssv_render(ssv_voice *v, const short **pcm, int *done) { return v->e->render(v->u, pcm, done); }
SSV_API void ssv_cancel(ssv_voice *v) { v->e->cancel(v->u); }

/* ---- a bank ---------------------------------------------------------------------------------------------------------- */
#define SLOTS 8

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
    int s;
    for (s = 0; s < SLOTS; s++) {
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
    if (k->slot[s] && k->slot[s]->index != i) {
        ssv_destroy(k->slot[s]);
        k->slot[s] = NULL;
    }
    if (!k->slot[s])
        k->slot[s] = ssv_create(i, k->fwdir, &k->boot, err, errlen);
    return k->slot[s];
}

/* ---- the accented-letter pass on its own (translit.h) ---------------------------------------------------------- */
static int known_cp850(unsigned c)                 /* bl_voice.c's cp850_byte(c) >= 0 */
{
    int i;
    if (c < 0x80) return 1;
    for (i = 0; i < 128; i++)
        if (bl_cp850_high[i] == c) return 1;
    return 0;
}

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
    m = tl_apply(in, k, charset == SSV_CP850 ? known_cp850 : tl_known_ascii, t);
    free(in);
    u = (unsigned char *)malloc((size_t)m * 4 + 1);
    if (!u) { free(t); return -1; }
    len = tl_utf8_encode(t, m, u);
    free(t);
    if (out && len <= cap) memcpy(out, u, (size_t)len);
    free(u);
    return len;
}
