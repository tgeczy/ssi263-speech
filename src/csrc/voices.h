/* voices.h -- every SSI-263 voice behind one small C interface: the voice table the front ends without Python share
 * (the SAPI 5 engine, sapi/ssi263_sapi.cpp; the speech-dispatcher module; Android), so each of them speaks a voice the
 * same way and none keeps its own copy of which firmware a voice needs or what its NVDA driver's defaults are.
 *
 * The voices are the NVDA add-ons' five and the Mockingboard, by the ids their SAPI tokens carry ("<driver
 * module>:<voice>"; the Mockingboard, new in 0.8, has no add-on of its own before the unified one):
 *
 *     blazie:blazie       Braille Lite 2000 (June 2003)   bl_voice.h (BL2ENG.BNS + its warm state)
 *     blazie:blazie_es    Braille Lite 2000 (español)     bl_voice.h (BL2SPA.BNS + its fresh state)
 *     speakout:speakout   Speak-Out                       so_voice.h (SPEAKOUT.HEX)       with SSV_HAVE_SPEAKOUT
 *     accentmini:mini     Accent-mini                     am_voice.h (SPKEMS.DVC)         with SSV_HAVE_ACCENTMINI
 *     accentmini:sa       Accent SA                       as_voice.h (u2, u3, u4)
 *     mockingboard:mockingboard  Mockingboard              mb_voice.h (mockingboard-tts-1.1.bin)  with SSV_HAVE_MOCKINGBOARD
 *
 * Each engine is a table of six functions -- create, set, speak, render, cancel, destroy -- over its voice's own API
 * (blv_, sov_, amv_, asv_, mbv_); a voice whose engine is not compiled in is still listed, and ssv_create says why it cannot
 * make it.  The firmware is never built in: it comes from a folder laid out as the repository's firmware/ (the paths
 * in ssv_info.files), the user's or the installer's.
 *
 * Settings come on NVDA's scales and with each driver's defaults (ssv_defaults), so a front end that maps its own
 * requests onto them speaks as the NVDA add-on does; sapi/test_native.py holds the SAPI path to the add-ons' drivers
 * byte for byte.  The bank (ssv_bank_*) keeps the units the way one NVDA driver instance keeps them: a unit per voice
 * for the Braille Lite, one card for both Accents (switching between them boots the other, as the Accent driver does),
 * all of them made again when a boot setting (sample rate, the Braille Lite's inflection or whine) changes.
 *
 * Not thread-safe per voice or per bank: one caller at a time.  MIT.
 */
#ifndef SSI263_VOICES_H
#define SSI263_VOICES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define SSV_API __declspec(dllexport)
#else
#define SSV_API __attribute__((visibility("default")))
#endif

#define SSV_BLAZIE 0               /* the Braille Lite 2000 (bl_voice.h) */
#define SSV_SPEAKOUT 1             /* GW Micro's Speak-Out (so_voice.h) */
#define SSV_ACCENT_MINI 2          /* Aicom's Accent-mini (am_voice.h) */
#define SSV_ACCENT_SA 3            /* Aicom's Accent SA (as_voice.h) */
#define SSV_MOCKINGBOARD 4         /* Sweet Micro Systems' Mockingboard (mockingboard/mb_voice.h) */

typedef struct {
    const char *id;                /* "blazie:blazie": the NVDA driver module and its voice, as the SAPI tokens carry it */
    const char *name;              /* the NVDA driver's name for it, UTF-8 */
    const char *lang;              /* "en", "es" */
    int engine;                    /* SSV_BLAZIE ... SSV_MOCKINGBOARD */
    int slot;                      /* voices of one slot share one unit in a bank (the two Accents: one card) */
    const char *files[4];          /* its firmware, relative to the firmware folder, "/"-separated; NULL after the last */
} ssv_info;

/* The table: every voice, in the order above. */
SSV_API int ssv_count(void);
SSV_API const ssv_info *ssv_voice_info(int i);          /* NULL outside 0..ssv_count()-1 */
SSV_API int ssv_find(const char *id);                   /* its index, or -1 */
/* 1 when the voice's engine is compiled into this library (so/am arrive with their sources), else 0. */
SSV_API int ssv_engine_built(int i);
/* 1 when the voice can be made from fwdir: its engine is built and every one of its files is there. */
SSV_API int ssv_available(int i, const char *fwdir);

/* What a unit is booted with; a change needs a new unit (the drivers reboot theirs). */
typedef struct {
    int sample_rate;               /* 11025, 22050 (the add-ons' default) or 44100; anything else is 22050 */
    int inflection;                /* the Braille Lite's own voice inflection, its status-menu on/off: 1 (default) */
    int whine;                     /* the Braille Lite's idle sound: 0 off (default), 1 hiss, 2 whine.  Its open channel
                                      after speech is never kept here (the NVDA driver's keepOpen is NVDA-only) */
} ssv_boot;
SSV_API void ssv_boot_defaults(ssv_boot *b);

/* An utterance's settings, NVDA's scales; ssv_defaults gives the voice's driver defaults. */
typedef struct {
    int rate, pitch, volume;       /* 0-100: 50, 50, 100 = each unit's factory rate and pitch, full volume (every voice
                                      takes a volume up to 200, louder and clipped at full scale: Android's slider) */
    int tone;                      /* the NVDA variant: the Braille Lite's tone 0-26 (7), the Speak-Out's A-Z as 0-25
                                      (8 = I), the Accent-mini's voice characteristic 0-9 (5); unused by the Accent SA */
    int pack;                      /* "Shorten pauses between sentences": the Braille Lite, the Speak-Out (1) */
    int join;                      /* "Join phrases": the Speak-Out's word delay (1) */
    int inflection;                /* the Accents' intonation 0-100 (100 = full) */
    int numbers;                   /* "Custom number processing": the Braille Lite, the Accents (1) */
    int run_ahead;                 /* the Braille Lite's "Run the unit ahead", EXPERIMENTAL (0) */
} ssv_settings;
SSV_API void ssv_defaults(int i, ssv_settings *s);

typedef struct ssv_voice ssv_voice;

/* Makes and boots voice i from fwdir (NULL: the current folder).  NULL on failure, the reason in err. */
SSV_API ssv_voice *ssv_create(int i, const char *fwdir, const ssv_boot *b, char *err, int errlen);

/* Where a voice's files come from when they are not laid out as the firmware folder: each of ssv_info.files, in its
   order, as a path of its own or as bytes in memory (data[k] wins when not NULL).  Android's: the user's imported
   firmware under names of the app's own, the Aicom ROMs and driver handed over from the APK.  The Braille Lite takes
   paths only (its unit reads its files itself). */
typedef struct {
    const char *path[4];
    const unsigned char *data[4];
    size_t size[4];
} ssv_source;
/* As ssv_create, from src. */
SSV_API ssv_voice *ssv_create_from(int i, const ssv_source *src, const ssv_boot *b, char *err, int errlen);
SSV_API void ssv_destroy(ssv_voice *v);
SSV_API int ssv_index(const ssv_voice *v);
SSV_API int ssv_sample_rate(const ssv_voice *v);
/* Starts one utterance (UTF-8) with these settings (NULL: the voice's defaults); only what changed reaches the unit,
   as in the drivers.  pitch_offset != 0 is NVDA's PitchCommand before the text (a capital), for the voices whose API
   takes one (the Speak-Out and the Accents); the Braille Lite ignores it.  1: there is audio to render; 0: nothing to
   say (render reports done at once); -1: the unit failed (the next speak recovers). */
SSV_API int ssv_speak(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset);
/* The next block (30 ms of the unit's time): 16-bit mono PCM at the sample rate, in a buffer valid until the next
   call; it may hold 0 samples while the unit is still reading.  *done = 1 at the utterance's end. */
SSV_API int ssv_render(ssv_voice *v, const short **pcm, int *done);
/* Stops the utterance; the next speak starts clean. */
SSV_API void ssv_cancel(ssv_voice *v);

/* A bank: the units of one front end, kept as one NVDA driver instance keeps them (see the top). */
typedef struct ssv_bank ssv_bank;
SSV_API ssv_bank *ssv_bank_new(const char *fwdir);
SSV_API void ssv_bank_free(ssv_bank *k);
/* The boot settings for the units from now on: when they differ from the ones the units were made with, every unit
   goes (the next ssv_bank_voice makes it again). */
SSV_API void ssv_bank_boot(ssv_bank *k, const ssv_boot *b);
/* Voice i's unit: the one its slot holds, or a new one (another voice of the slot is let go first).  NULL on failure,
   the reason in err. */
SSV_API ssv_voice *ssv_bank_voice(ssv_bank *k, int i, char *err, int errlen);

/* The accented-letter pass every voice's text path runs first (translit.h), on its own: n bytes of UTF-8 (n < 0: up
   to the NUL) in, UTF-8 out.  charset: what the firmware knows, SSV_ASCII (the English Braille Lite, the Speak-Out,
   the Accents) or SSV_CP850 (the Spanish Braille Lite).  Returns the length (no NUL), copying into out when it fits in
   cap; -1 when out of memory.  For the Braille Lite's NVDA driver (its front end is Python, its unit this library)
   and for the tests that hold the voices to 0.7.0's drivers, which predate the pass.  The library also exports
   translit.h's ssv_translit_break, the tests' control (nonzero: the pass off for every voice in it). */
#define SSV_ASCII 0
#define SSV_CP850 1
SSV_API int ssv_translit(const char *utf8, int n, int charset, char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif
