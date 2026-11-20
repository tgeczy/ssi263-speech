/* voices.h -- every SSI-263 voice behind one small C interface: the voice table the front ends without Python share
 * (the SAPI 5 engine, sapi/ssi263_sapi.cpp; the speech-dispatcher module; Android), so each of them speaks a voice the
 * same way and none keeps its own copy of which firmware a voice needs or what its NVDA driver's defaults are.
 *
 * The voices are the NVDA add-ons' five and the Mockingboard, by the ids their SAPI tokens carry ("<driver
 * module>:<voice>"; the Mockingboard, new in 0.8, has no add-on of its own before the unified one):
 *
 *     blazie:blazie       Braille Lite 2000 (June 2003)   bl_voice.h (BL2ENG.BNS + its warm state)
 *     blazie:blazie_es    Braille Lite 2000 (Spanish)     bl_voice.h (BL2SPA.BNS + its fresh state)
 *     speakout:speakout   Speak-Out                       so_voice.h (SPEAKOUT.HEX)       with SSV_HAVE_SPEAKOUT
 *     accentmini:mini     Accent-mini                     am_voice.h (SPKEMS.DVC)         with SSV_HAVE_ACCENTMINI
 *     accentmini:sa       Accent-PC                       as_voice.h (u2, u3, u4)
 *     mockingboard:mockingboard  Mockingboard              mb_voice.h (mockingboard-tts-1.1.bin)  with SSV_HAVE_MOCKINGBOARD
 *     mockingboard:early  Mockingboard, early             mb_voice.h (mockingboard-tts-early.bin)  with SSV_HAVE_MOCKINGBOARD
 *     speakout:soundingboard  Sounding Board (GW Micro)   so_voice.h (SBLOAD.COM + SB.COM 2.7: sov_create_sb)  with
 *                                                         SSV_HAVE_SOUNDINGBOARD (0.8.5; last in the table, after the
 *                                                         Braille Lite's release voices)
 *     smartalk:smartalk   SmarTalk (Automated Functions)  smartalk/st_voice.h (S_TALK.EXE 1.21)  with SSV_HAVE_SMARTALK
 *                                                         (after the Sounding Board; NVDA only so far)
 *
 * Each engine is a table of six functions -- create, set, speak, render, cancel, destroy -- over its voice's own API
 * (blv_, sov_, amv_, asv_, mbv_, stv_); a voice whose engine is not compiled in is still listed, and ssv_create says why it cannot
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
#include "marks.h"

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
#define SSV_ACCENT_SA 3            /* Aicom's Accent-PC (as_voice.h) */
#define SSV_MOCKINGBOARD 4         /* Sweet Micro Systems' Mockingboard (mockingboard/mb_voice.h) */
#define SSV_SOUNDINGBOARD 5        /* GW Micro's Sounding Board: the Speak-Out's voice on its DOS drivers (so_voice.h) */
#define SSV_SMARTALK 6             /* Automated Functions' SmarTalk: its DOS text-to-speech (smartalk/st_voice.h) */

typedef struct {
    const char *id;                /* "blazie:blazie": the NVDA driver module and its voice, as the SAPI tokens carry it */
    const char *name;              /* the NVDA driver's name for it, UTF-8 */
    const char *lang;              /* "en", "es", "de", "it", "sk" */
    int engine;                    /* SSV_BLAZIE ... SSV_SMARTALK */
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
                                      (8 = I), the Accent-mini's voice characteristic 0-9 (5), the SmarTalk's tone A-Z
                                      as 0-25 (8 = I); unused by the Accent-PC */
    int pack;                      /* "Shorten pauses between sentences": the Braille Lite, the Speak-Out (1) */
    int join;                      /* "Join phrases": the Speak-Out's word delay (1) */
    int inflection;                /* the Accents' intonation 0-100 (100 = full); the SmarTalk's inflection is on from
                                      50 (below: monotone) */
    int numbers;                   /* "Custom number processing": the Braille Lite, the Accents (1) */
    int run_ahead;                 /* the Braille Lite's "Run the unit ahead", EXPERIMENTAL (0) */
    int line_lift;                 /* the Braille Lite's "Lift line starts (as note-taking mode)" (0): the first
                                      line after a cancel lifted as the unit lifts a line moved to (bl_voice.h) */
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

/* Index marks (marks.h): places in the text a screen reader wants to be told of when speech reaches them.
   ssv_speak_marked: as ssv_speak, with the caller's marks (byte offsets into utf8, in rising order).  With none it
   IS ssv_speak: nothing is done for marks' sake, the audio the same sample for sample.
   ssv_reached: the marks speech reached in the block the last ssv_render returned, in the caller's order, each with
   its sample in that block (0 to the block's count) and whether its place is exact -- where the unit's own index
   mark says -- or estimated.  Read it after every ssv_render of a marked utterance, also one that returned 0 samples
   or reported done: a text with nothing to say (ssv_speak_marked returned 0) gives its marks with the one ssv_render
   that follows.
     own marks   a Braille Lite whose release was measured for them (bl_languages.txt's marks column; bl_voice.h),
                 not run ahead: each mark where the unit echoes its Ctrl-F, to the sample.  The Speak-Out and the
                 Sounding Board (so_voice.h), the Accent-PC (as_voice.h), the Accent-mini (am_voice.h) and the
                 SmarTalk (st_voice.h): where the unit or its driver says it passed its own mark.
     estimated   every other voice (the Mockingboard, the other Braille Lite releases, a Braille Lite run ahead): a
                 mark is put at its share of the text's characters, as a share of the
                 utterance's samples.  The utterance's length must be known for that, so the voice is rendered ahead
                 of the caller (8 blocks for each one returned, the first returned at once), and a mark is reported
                 once the unit is done and the returned audio has reached its place.  A mark in the first eighth or
                 so of an utterance can therefore come late, by at most the time the unit takes to finish ahead of
                 the caller; none comes early by more than the estimate is off.
   Every mark is reported exactly once: at the utterance's end the ones still left come with its last block
   (estimated), so a caller waiting on one is never left waiting.  After ssv_cancel none of the cancelled text's
   marks is reported. */
SSV_API int ssv_speak_marked(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset,
                             const ssv_mark *marks, int n_marks);
SSV_API int ssv_reached(ssv_voice *v, const ssv_reach **out);
/* 1 when ssv_speak_marked with these settings would use the unit's own marks, 0 when it would estimate.  For a
   caller that wants marks only where they cost no rendering ahead (the Android app: a phone has no speed to spare
   for it).  It gives the unit the settings, as ssv_speak does. */
SSV_API int ssv_marks_exact(ssv_voice *v, const ssv_settings *s);

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
   the Accents), SSV_CP850 (the Spanish, German and Italian Braille Lites), SSV_CP857 / 860 / 866 (the Turkish,
   Portuguese and Russian ones), SSV_CPHUN (the Hungarian one), SSV_CP852 (the Polish and Croatian ones) or
   SSV_CP852SK (the Slovak Braille 'n Speak 2000 and the Czech Braille Lite).  Returns the length (no NUL), copying into out when it fits in
   cap; -1 when out of memory.  For the Braille Lite's NVDA driver (its front end is Python, its unit this library)
   and for the tests that hold the voices to 0.7.0's drivers, which predate the pass.  The library also exports
   translit.h's ssv_translit_break, the tests' control (nonzero: the pass off for every voice in it). */
#define SSV_ASCII 0
#define SSV_CP850 1
#define SSV_CP852 2
#define SSV_CP857 3                  /* the Turkish Braille Lite (bl_voice.h BLV_CP857: the same ids) */
#define SSV_CP860 4                  /* the Portuguese ones */
#define SSV_CP866 5                  /* the Russian one */
#define SSV_CPHUN 6                  /* the Hungarian one (bl_voice.h BLV_CPHUN, bl_cphun.h) */
#define SSV_CP852SK 7                /* the Slovak and Czech ones (BLV_CP852SK: 852, o circumflex as its capital) */
/* 8 on: bl_voice.h's BLV_CP852PL ... BLV_CP864AR, the same ids (the Polish, Croatian, French, Danish and Norwegian,
   Swedish, Spanish, Italian, Turkish, Portuguese, Russian and Arabic (UAE) units) */
SSV_API int ssv_translit(const char *utf8, int n, int charset, char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif
