/* sd_voices.h -- the speech-dispatcher module's voice table: each voice it offers, the engine behind it, and that
 * engine's create / set / speak / render / cancel / destroy.  sd_ssi263.c speaks the protocol and calls only this.
 *
 * The voices are the NVDA add-ons' (Tomi, 0.7.1: "uniform everywhere"), each through src/csrc/voices.h -- the voice
 * table the SAPI engine and Android speak through: each voice's engine and its NVDA driver's defaults:
 *   Braille Lite 2000            en-US  bl_voice.h  <data>/BL2ENG.BNS + bl2_2003_warm.state
 *   Braille Lite 2000 (espanol)  es-ES  bl_voice.h  <data>/BL2SPA.BNS + bl2spa_fresh.state
 *   Accent SA                    en-US  as_voice.h  <data>/aicom-accent-sa/u2.BIN, u3.BIN, u4.BIN
 *   Accent-mini                  en-US  am_voice.h  <data>/aicom-accent-mini/SPKEMS.DVC     (built with its engine)
 *   Speak-Out                    en-US  so_voice.h  <data>/gw-micro-speakout/SPEAKOUT.HEX   (built with its engine)
 *   Mockingboard                 en-US  mb_voice.h  <data>/sweet-micro-mockingboard/mockingboard-tts-1.1.bin
 *   Mockingboard, early          en-US  mb_voice.h  <data>/sweet-micro-mockingboard/mockingboard-tts-early.bin
 *                                                   (both built with their engine)
 * The data folder mirrors the repository's firmware/ folders.  A voice whose files are not there is not offered.
 * build_linux.sh compiles voices.c with SSV_HAVE_ACCENTMINI / SSV_HAVE_SPEAKOUT when those voices' sources are in the
 * tree; a voice whose engine is not built in is not listed.
 *
 * Each voice's unit is made on its first use and kept for the session (as the add-ons keep one card): its settings
 * are sent before each message, a stop cancels it.  Not thread-safe (the module is one thread).  MIT.
 */
#ifndef SD_VOICES_H
#define SD_VOICES_H

#ifdef __cplusplus
extern "C" {
#endif

/* The config's keys (sd_ssi263.c reads them; its header lists them).  Every engine takes rate, pitch and volume from SSIP. */
typedef struct {
    char datadir[1024];
    int sample_rate;                   /* SSI263SampleRate: 11025, 22050, 44100 */
    /* the Braille Lite's */
    int inflection;                    /* SSI263Inflection 0/1 */
    int whine;                         /* SSI263Whine: 0 off, 1 hiss, 2 whine */
    int tone;                          /* SSI263Tone 0-26 */
    int short_pauses;                  /* SSI263ShortPauses 0/1 */
    int run_ahead;                     /* SSI263RunAhead 0/1 (EXPERIMENTAL; the Braille Lite only) */
    int line_lift;                     /* SSI263LineLift 0/1 (the Braille Lite only; bl_voice.h blv_set_line_lift) */
    int numbers;                       /* SSI263BrailleLiteNumbers 0/1 (the driver's custom number processing: 1) */
    /* the Accents' (the SA and the mini) */
    int accent_inflection;             /* SSI263AccentInflection 0-100 (100 = full intonation) */
    int accent_numbers;                /* SSI263AccentNumbers 0/1 (the add-on's custom number processing) */
    int accent_voice;                  /* SSI263AccentMiniVoice 0-9 (ESC V; the mini only, 5 = its default) */
    /* the Speak-Out's */
    int speakout_tone;                 /* SSI263SpeakOutTone 0-25 = A-Z (8 = I, its default) */
    int speakout_join;                 /* SSI263SpeakOutJoin 0/1 */
    int speakout_short_pauses;         /* SSI263SpeakOutShortPauses 0/1 */
} sd_settings;

void sdv_defaults(sd_settings *s);

int sdv_count(void);
const char *sdv_name(int i);           /* as the NVDA add-ons name it */
const char *sdv_language(int i);       /* SSIP's: en-US, es-ES */
const char *sdv_engine(int i);         /* braille-lite, accent-sa, accent-mini, speakout */
int sdv_find(const char *name);        /* by name; -1 if none */
int sdv_available(int i, const sd_settings *s);    /* its files are in the data folder */

/* Makes the voice's unit if it has none yet.  0, or -1 with the reason in err. */
int sdv_load(int i, const sd_settings *s, char *err, int errlen);
/* One message (UTF-8): the settings sent, then the text.  rate, pitch, volume on NVDA's 0-100 (50, 50, 100 = the
   unit's own).  1: there is audio to render; 0: nothing to say; -1: the unit could not be made. */
int sdv_speak(int i, const sd_settings *s, int rate, int pitch, int volume, const char *utf8, char *err, int errlen);
/* The next block (16-bit mono at s->sample_rate, valid until the next call; 0 samples is possible), *done at the end */
int sdv_render(int i, const short **pcm, int *done);
/* The unit drops what it has not spoken, so the next message starts clean. */
void sdv_cancel(int i);
void sdv_destroy_all(void);

#ifdef __cplusplus
}
#endif
#endif
