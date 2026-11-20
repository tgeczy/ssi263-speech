/* ssa_engine.h -- the Android front end in plain C: the voices, their settings, start, pull, stop and cancel.
 *
 * Every voice the NVDA add-ons have (Tomi, 0.7.5: "all voices, no exceptions"), through src/csrc/voices.h -- the voice
 * table the SAPI engine speaks through: each voice's engine and its NVDA driver's defaults -- with the files where the
 * app keeps them:
 *
 *   SSA_ENGLISH      Braille Lite 2000          bl_voice.h  <data>/BL2ENG.BNS + bl2_2003_warm.state   (imported)
 *   SSA_SPANISH      Braille Lite 2000 (es)     bl_voice.h  <data>/BL2SPA.BNS + bl2spa_fresh.state    (imported)
 *   SSA_ACCENT_SA    Aicom Accent-PC            as_voice.h  u2, u3, u4 in memory (the APK's assets/aicom)
 *   SSA_SPEAKOUT     GW Micro Speak-Out         so_voice.h  <data>/SPEAKOUT.HEX                       (imported)
 *   SSA_ACCENT_MINI  Aicom Accent-mini          am_voice.h  SPKEMS.DVC in memory (assets/aicom)       (when
 *                                                           voices.c has it: SSV_HAVE_ACCENTMINI, build_android.sh)
 *   SSA_MOCKINGBOARD Mockingboard               mb_voice.h  <data>/mockingboard-tts-1.1.bin           (imported,
 *                    (Sweet Micro Systems)                  else the APK's copy in memory (assets/sweet-micro);
 *   SSA_MOCKINGBOARD_EARLY  Mockingboard, early mb_voice.h  <data>/mockingboard-tts-early.bin, the same way
 *                                               (when voices.c has them: SSV_HAVE_MOCKINGBOARD, build_android.sh)
 *   SSA_GERMAN       Braille Lite 2000 (de)     bl_voice.h  <data>/Lite2000_de.BNS + lite2000_de_fresh.state (imported)
 *   SSA_ITALIAN      Braille Lite 2000 (it)     bl_voice.h  <data>/Lite2000_it.BNS + lite2000_it_fresh.state (imported)
 *   SSA_SLOVAK       Braille 'n Speak 2000 (sk) bl_voice.h  <data>/BS2SLL.BNS + bs2sll_fresh.state       (imported)
 *
 * 0.8's three Blazie units -- Blazie's September 2000 German and Italian Braille Lite 2000, the Slovak Braille 'n
 * Speak 2000 -- under the NVDA add-on's flat names for their files, as sd_voices.c has them (the German and Italian
 * update disks both call theirs Lite2000.BNS).  voices.c gives each its code page (850; the Slovak unit's 852), lets
 * the Italian and Slovak units end without ^F echoes, and drops the number words and run ahead for all three (they
 * read their own numbers; run ahead is tested on English and Spanish alone).
 *
 * The Aicom Accent-PC (the built-in voice, Tomi 2026-09-30: Aicom's ROMs ship in the APK, handed over in memory) runs
 * through as_voice.h, the NVDA Accent driver's front end in C.  Each utterance gets a unit of its own, booted as the
 * driver boots one and let go afterwards: so an utterance sounds the same whatever came before it (a capital's raised
 * pitch cannot linger, and a stop needs no flush), and every pitch -- the app's slider and the request's together --
 * is said the way the driver says a capital's, with snap_pitch, so it jumps rather than glides from the unit's
 * power-up pitch.  The next unit is booted as soon as one is let go.
 *
 * The other voices keep one unit each, booted on first use, as the speech-dispatcher module keeps them: the settings
 * sent before each utterance, the audio pulled block by block, a stop between blocks, and a cancel so the next
 * utterance starts clean.  The Speak-Out and the Accent-mini take a request's pitch as the NVDA driver takes a
 * capital's -- the slider's pitch as the setting, the difference as PitchCommand's offset (snapped to, and restored
 * after the utterance by the voice itself) -- moved at least one of the box's pitch steps, as the Accent-PC's is.
 * The Mockingboard takes it the same way (its inflection's steps, mbv_pitch_step), and from the settings its number
 * words, as the Braille Lite does.
 * The Braille Lite takes it as its pitch setting (bl_voice's own scale has 32 steps), and from the settings its
 * number words (the NVDA driver's "Custom number processing", bl_numbers through blv_set_numbers: on by default, as
 * there) and its experimental run ahead (blv_set_run_ahead).
 *
 * No JNI here (ssa_jni.c is the thin bridge), so the host-side test (src/platforms/android/test) runs this same code
 * on the desktop and over adb.  Not thread-safe: one caller at a time (the app holds a lock), except ssa_stop.
 */
#ifndef SSA_ENGINE_H
#define SSA_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ssa_engine ssa_engine;

#define SSA_ENGLISH 0                  /* BL2ENG.BNS + bl2_2003_warm.state */
#define SSA_SPANISH 1                  /* BL2SPA.BNS + bl2spa_fresh.state */
#define SSA_ACCENT_SA 2                /* Aicom's u2, u3, u4, from ssa_set_accent_roms */
#define SSA_SPEAKOUT 3                 /* GW Micro's SPEAKOUT.HEX, imported */
#define SSA_ACCENT_MINI 4              /* Aicom's SPKEMS.DVC, from ssa_set_accent_mini */
#define SSA_MOCKINGBOARD 5             /* Sweet Micro Systems' mockingboard-tts-1.1.bin, imported or built in */
#define SSA_MOCKINGBOARD_EARLY 6       /* ... and mockingboard-tts-early.bin (Mockingboard disk 1's), the same way */
#define SSA_GERMAN 7                   /* Lite2000_de.BNS + lite2000_de_fresh.state (0.8) */
#define SSA_ITALIAN 8                  /* Lite2000_it.BNS + lite2000_it_fresh.state */
#define SSA_SLOVAK 9                   /* the Braille 'n Speak 2000's BS2SLL.BNS + bs2sll_fresh.state */
/* Then the Braille Lite voices the release table gives (src/csrc/blazie/bl_languages.txt: Hungarian, Turkish, ...;
   voices.c's voices_blazie.h, voice ids blazie:blazie_hu ...), one per row that names a new voice, in the table's order
   from SSA_RELEASE_FIRST: each a Braille Lite 2000 unit as the three above, its files the one a person brings and its
   state, under the release's own names (ssa_voice_file: BL2HUN.BNS + bl2_hu_1999_07.state). */
#define SSA_RELEASE_FIRST 10
#define SSA_RELEASE_VOICES 17          /* = voices_blazie.h's SSV_BLAZIE_RELEASE_VOICES (checked when compiled) */
#define SSA_VOICES (SSA_RELEASE_FIRST + SSA_RELEASE_VOICES)
/* Then the voices after the release table's (0.8.5), so no voice saved before them moves (SsiSettings keeps the
   index): GW Micro's Sounding Board, the Speak-Out's voice on the card's DOS drivers SBLOAD.COM and SB.COM (2.7),
   imported.  Its index is SSA_VOICES (checked when compiled): a release voice added to the table moves it. */
#define SSA_SOUNDINGBOARD 27
/* Automated Functions' SmarTalk: its own DOS text-to-speech, S_TALK.EXE 1.21, on the Accent-mini's 8086 (st_voice.h),
   imported -- right after the Sounding Board, so no voice saved before it moves (checked when compiled). */
#define SSA_SMARTALK 28
#define SSA_ALL_VOICES (SSA_SMARTALK + 1)
/* A release voice's row in the release table (bl_firmware.h blv_release_*), or -1; and back: a row's voice, or -1
   (a release that names no new voice: a shipped voice's, or the emulator's only). */
int ssa_voice_release(int voice);
int ssa_release_voice(int release);
/* The voice's i-th file in the data folder (its firmware, then its state) into out: 1, or 0 past the last (and for
   the voices kept in memory, the Accents'). */
int ssa_voice_file(int voice, int i, char *out, int cap);

/* The imported files' names in the data folder (the Speak-Out's and the Mockingboard's: SsiImport writes them there;
   the Mockingboard's is mb_host.h's MB_FILE). */
#define SSA_SPEAKOUT_FILE "SPEAKOUT.HEX"
#define SSA_SBLOAD_FILE "SBLOAD.COM"           /* the Sounding Board's two drivers (0.8.5) */
#define SSA_SB_FILE "SB.COM"
#define SSA_MOCKINGBOARD_FILE "mockingboard-tts-1.1.bin"
#define SSA_MOCKINGBOARD_EARLY_FILE "mockingboard-tts-early.bin"
#define SSA_SMARTALK_FILE "S_TALK.EXE"

/* The Accents' level at the app's volume 100, in their drivers' percent (as_voice.h's asv_set): the engine volume
   times this over 100.  test_volume_headroom.py measures it against the Braille Lite's. */
#define SSA_ACCENT_LEVEL 100

/* The app's own settings, on the NVDA drivers' scales.  rate, pitch 0-100 (50 = the unit's factory rate and pitch);
   volume 0-200 (100 = the desktop voices' level), every voice's.  The Braille Lite's (bl_voice.h's blv_set): tone
   0-26 (7), pack = short pauses, run_ahead = "Run the unit ahead" (EXPERIMENTAL, off by default), line_lift = "Lift
   line starts (as note-taking mode)" (off by default; a request after a stop or a pause; sent to every Blazie
   unit, heard on the English one alone: the others lift no line in note-taking mode either),
   numbers = "Read numbers as words" (1, the driver's default; English, and Spain's Spanish for the Spanish unit;
   the German, Italian and Slovak units read their own).  The Speak-Out's
   (so_voice.h's sov_set): so_tone 0-25 = A-Z (8 = I), so_join = "Join phrases", so_short = "Shorten pauses between
   sentences".  The Accents take rate, pitch and volume; the Mockingboard (mb_voice.h's mbv_set) rate, pitch, volume
   and numbers. */
typedef struct {
    int rate, pitch, tone, volume, pack;
    int run_ahead;
    int line_lift;
    int so_tone, so_join, so_short;
    int numbers;
} ssa_settings;

/* The defaults above (the NVDA drivers'). */
void ssa_default_settings(ssa_settings *s);

/* datadir: the folder holding the imported firmware and state files. */
ssa_engine *ssa_new(const char *datadir);
void ssa_free(ssa_engine *e);

/* The Accent-PC's ROMs (u2 64 KB, u3 and u4 32 KB each), copied.  1, or 0 (wrong sizes, out of memory). */
int ssa_set_accent_roms(ssa_engine *e, const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                        const unsigned char *u4, size_t n4);

/* The Accent-mini's SPKEMS.DVC, copied.  1, or 0 (empty, out of memory, or this build has no Accent-mini). */
int ssa_set_accent_mini(ssa_engine *e, const unsigned char *dvc, size_t n);

/* A Mockingboard voice's built-in file (the APK's assets/sweet-micro: Tomi, 2026-10-10, the GitHub APK carries both
   until a store build exists), copied; used whenever the data folder has no imported copy, which wins.  1, or 0 (not
   a Mockingboard voice, not that voice's file -- mbh_variant's 1.1 for SSA_MOCKINGBOARD, the early one for
   SSA_MOCKINGBOARD_EARLY --, empty, out of memory, or this build has no Mockingboard). */
int ssa_set_mockingboard(ssa_engine *e, int voice, const unsigned char *bin, size_t n);

/* Where a Mockingboard voice's file comes from now: 1 imported (the data folder), 2 built in, 0 neither (or not a
   Mockingboard voice). */
int ssa_mockingboard_source(const ssa_engine *e, int voice);

/* The SmarTalk's built-in S_TALK.EXE (the GitHub APK's assets/smartalk: Tomi, 2026-10-24, as the Mockingboards' and
   the Accents' -- its makers let it be shared; the Apple apps carry none), copied; used whenever the data folder has
   no imported copy, which wins.  1, or 0 (not S_TALK.EXE 1.21 by its sha256, empty, out of memory, or this build has
   no SmarTalk).  And where its file comes from now: 1 imported, 2 built in, 0 neither. */
int ssa_set_smartalk(ssa_engine *e, const unsigned char *exe, size_t n);
int ssa_smartalk_source(const ssa_engine *e);

/* This build carries the voice's engine (every one but the Accent-mini and the Mockingboards always does). */
int ssa_voice_built(int voice);

/* The voice can speak: its files are in the data folder (the Braille Lite's two, the Speak-Out's HEX, a
   Mockingboard's file -- or its built-in copy is set); the Accents' ROMs are set. */
int ssa_has_voice(const ssa_engine *e, int voice);

/* The settings a unit is booted with: sample rate (11025, 22050 or 44100; anything else is 22050, as sd_ssi263),
   inflection 0/1 (the Braille Lite's status-menu setting; the Accents: full intonation or monotone), whine 0 off /
   1 hiss / 2 whine (the Braille Lite's).  A change shuts the booted units down; the next use boots them again with
   the new settings. */
void ssa_configure(ssa_engine *e, int sample_rate, int inflection, int whine);
int ssa_sample_rate(const ssa_engine *e);

/* Boots the voice's unit now if it is not yet (the first utterance otherwise waits for it).  0, or -1 with the
   reason in err. */
int ssa_load(ssa_engine *e, int voice, char *err, int errlen);

/* Starts an utterance (UTF-8): the app's settings with the request's rate and pitch percentages on top (ssa_map.h).
   Cancels an utterance still running.  0: there is audio to pull; 1: nothing to say; -1: the unit could not boot,
   or refused the text. */
int ssa_start(ssa_engine *e, int voice, const char *utf8, const ssa_settings *s, int request_rate,
              int request_pitch);

/* As ssa_start, with index marks (src/csrc/marks.h, through voices.h's ssv_speak_marked): ats, n_ats byte offsets
   into utf8, in the text's order.  The service gives the start of every word and tells the framework which word
   is being spoken (SynthesisCallback.rangeStart; TalkBack takes it).  The marks are used only where they are the
   unit's own (ssv_marks_exact): the estimate renders the unit ahead of the listener, eight blocks for one, which a
   phone has no speed to spare for -- such a voice is said as by ssa_start and reaches nothing.
   ssa_reached: the marks the audio rendered so far has reached that were not given yet -- for each its number in
   ats and its frame in the utterance's audio, counted from the first sample ssa_pull gave for it; out takes
   2 * cap ints, the pairs written are returned.  A mark's frame can lie in audio a later ssa_pull gives (a pull
   hands over part of a block): ask after every pull, before giving its audio on. */
int ssa_start_marked(ssa_engine *e, int voice, const char *utf8, const ssa_settings *s, int request_rate,
                     int request_pitch, const int *ats, int n_ats);
int ssa_reached(ssa_engine *e, int *out, int cap);

/* Up to cap 16-bit samples of the utterance into out: the count, 0 once it has finished, -2 when stopped. */
int ssa_pull(ssa_engine *e, short *out, int cap);

/* Any thread: makes a pull in progress return -2 before its next block. */
void ssa_stop(ssa_engine *e);

/* The synthesis thread, after a stop or an abandoned utterance: the unit drops what it has not spoken. */
void ssa_cancel(ssa_engine *e);

/* Non-empty blocks the unit has rendered for the current utterance (the test's stop point). */
int ssa_blocks(const ssa_engine *e);

/* The firmware import's last check, on a folder of its own: boots the voice's unit from the files in datadir (the
   defaults: 22050 Hz, inflection on, no idle sound), speaks utf8 at the app's default settings and returns the
   samples it made -- 0 when the unit stays silent -- with the FNV-1a 64 of their little-endian bytes in *fnv when
   fnv is not NULL; or -1 with the reason in err. */
long ssa_probe(const char *datadir, int voice, const char *utf8, unsigned long long *fnv, char *err, int errlen);

/* A request's pitch on NVDA's scale for a voice with pitch steps (the Accents' ten, ESC P 0-9; the Speak-Out's ten,
   ^E P0-9): ssa_pitch's, except that a request's pitch other than 100 % always moves the voice at least one step from
   the slider's -- the nearest pitch that does, in the request's direction.  A capital's raise of 110-120 % (a screen
   reader's) would otherwise land on the slider's own step and go unheard.  step: the voice's own (asv_pitch_step,
   sov_pitch_step). */
int ssa_step_pitch(int slider, int request, int (*step)(int));
int ssa_accent_pitch(int slider, int request);         /* ssa_step_pitch with the Accent-PC's steps */
int ssa_speakout_pitch(int slider, int request);       /* ... with the Speak-Out's */
int ssa_mockingboard_pitch(int slider, int request);   /* ... with the Mockingboard's inflection (0-26), in a build
                                                          that has it; else the plain mapping */
int ssa_smartalk_pitch(int slider, int request);       /* ... with the SmarTalk's ten (ESC P 0-9), in a build that has
                                                          it; else the plain mapping */

/* The tests' controls (test_android_native.c sets them; the app never does): each puts back one bug the cases must
   catch.  ssa_accent_break -- 1: the request's pitch dropped; 2: the pitch sent as a setting, so it glides instead of
   jumping (no snap_pitch); 3: one unit kept from utterance to utterance, so what came before is heard in what
   follows; 4: the plain mapping for the pitch (ssa_step_pitch's step rule gone), so a 120 % request sounds like
   100 % -- the Accent-PC's and the Speak-Out's alike.  ssa_voice_break -- 1: the Speak-Out's request pitch dropped
   (no capital offset); 2: the Speak-Out's own settings (tone, join, short pauses) dropped, the defaults sent; 3: the
   Braille Lite's run ahead dropped; 4: the Braille Lite's number words dropped (always off, as before 0.7.5); 5: the
   Mockingboard's request pitch dropped (no capital offset); 6: the Mockingboard's number words dropped (always off);
   7: a Mockingboard's imported copy ignored (the built-in one always used); 8: the built-in copies ignored (import
   only); 9: ssa_set_mockingboard takes any file for either voice (no mbh_variant check); 10: the Braille Lite's
   line-start lift dropped; 11: the SmarTalk's request pitch dropped (no capital offset); 12: the SmarTalk's number
   words dropped (always off); 13: the SmarTalk's imported copy ignored (the built-in one used); 14: its built-in copy
   ignored (import only); 15: a mark's frame counted from its block's start, not the utterance's; 16: marks sent to
   a voice that would estimate them. */
extern int ssa_accent_break;
extern int ssa_voice_break;

#ifdef __cplusplus
}
#endif
#endif
