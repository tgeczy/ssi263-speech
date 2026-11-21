/* bl_voice.h -- the Braille Lite as a voice: nvda/blazie/synthDrivers/blazie.py's front end in C, line for line
 * (its boot, _clean, _lines, the unit's rate/pitch/tone commands, the speak loop with the lead trim and PCM, and
 * cancel), around the C host (bl_host.h) and a chip made from the built-in defaults (ssi263_default_params).
 *
 * For front ends without Python: the Linux speech-dispatcher module, Android.  nvda/tools/voice_equiv.py gates it:
 * the real NVDA driver's PCM and this one's, byte for byte, on the same text and settings.
 *
 * The driver's number words (its "Custom number processing") come in through blv_set_numbers (bl_numbers.h), off
 * until set: without them the firmware reads numbers itself up to 999,999,999,999.  Pitch commands inside an
 * utterance (capitals) are not part of this API.
 */
#ifndef BL_VOICE_H
#define BL_VOICE_H

#include "bl_host.h"
#include "../marks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bl_voice bl_voice;

#define BLV_LATIN1 0               /* the English unit (BL2ENG.BNS): 7-bit text */
#define BLV_CP850 1                /* the Spanish, German and Italian units: DOS code page 850 */
#define BLV_CP852 2                /* DOS code page 852: the Polish and Croatian Braille Lite 2000 */
#define BLV_CP857 3                /* DOS code page 857: the Turkish Braille Lite 2000 (bl_languages.txt) */
#define BLV_CP860 4                /* DOS code page 860: the Portuguese and Brazilian Portuguese ones */
#define BLV_CP866 5                /* DOS code page 866: the Russian one */
#define BLV_CPHUN 6                /* the Hungarian one's letters as its speech rules take them (bl_cphun.h) */
#define BLV_CP852SK 7              /* 852 with o circumflex sent as its capital: the Slovak and Czech units (bl_voice.c) */
#define BLV_CP852PL 8              /* 852, Z dot above sent as its lower case, o circumflex as o: the Polish unit */
#define BLV_CP852HR 9              /* 852, C acute and D stroke as their lower case, o circumflex as o: the Croatian */
#define BLV_CP850FR 10             /* 850, the accented capitals the unit skips as their lower case: the French unit */
#define BLV_CP850DANO 11           /* only the letters ae, o slash, a ring of 850: the Danish and Norwegian units */
#define BLV_CP850SV 12             /* only a ring, a and o diaeresis, u diaeresis, E acute (e acute as it): Swedish */
#define BLV_CP850ES 13             /* 850, A and I acute as their lower case: the Spanish unit */
#define BLV_CP850IT 14             /* 850, the accented capitals as their lower case, i circumflex as i: the Italian */
#define BLV_CP857TR 15             /* 857, C cedilla as c cedilla, dotted I as i, a/i/u circumflex plain: Turkish */
#define BLV_CP860PT 16             /* 860, the accented capitals as their lower case, o circumflex as o: Portuguese */
#define BLV_CP866RU 17             /* 866, Cyrillic capital U as its lower case: the Russian unit */
#define BLV_CP864AR 18             /* 864, the 27 letters the Arabic (UAE) unit speaks, every form of each sent as its
                                      spoken one; vowel marks, tatweel and a bare hamza dropped (bl_cp864ar.h) */
/* (BLV_CP850ES is Spain's Spanish for bl_numbers.c, as BLV_CP850 was) */

/* Boots the unit as the driver does (punctuation none, full numbers, inflection on or off, volume 6).
   whine: 0 off, 1 hiss, 2 whine.  NULL on failure, the reason in err. */
BL_API bl_voice *blv_create(const char *firmware, const char *state, int encoding, double out_rate, int inflection,
                            int whine, char *err, int errlen);
/* The tests' controls (an app never sets it): 1 boots every firmware with the fast keys, 2 ignores the release's
   echo (bl_firmware.h blv_release_slow_boot, blv_release_echo), 3 leaves out a non-echoing unit's own end (bl_host.c
   no_echo_finished: done a second after its last sound again). */
BL_API extern int blv_voice_break;
BL_API void blv_destroy(bl_voice *v);

/* NVDA's scales: rate, pitch and volume 0-100 (50, 50, 100 = the unit's factory rate 11 and pitch 16, full volume);
   tone 0-26 (7 = factory); pack = the driver's "short pauses" (sentences packed onto one line from the second on). */
BL_API void blv_set(bl_voice *v, int rate, int pitch, int tone, int volume, int pack);
/* The driver's "Run the unit ahead" (runAhead; EXPERIMENTAL, off by default): nonzero runs the unit ahead of its chip
   from the next blv_speak, as the driver does, with packing on only (blv_set's pack).  The host carries the whole
   mode (bl_host.h "run_ahead", run_ahead.h): its completion in blv_render's done, its faults in blv_speak's -1 and
   blv_fault, its settle before a cancel in blv_cancel.  Off: the lockstep, byte for byte as before. */
BL_API void blv_set_run_ahead(bl_voice *v, int on);
/* "Lift line starts (as note-taking mode)", off by default.  In note-taking mode the unit lifts the pitch at the
   start of a line it reads because the user moved to it (dot 4 or dot 1 with space): the first slots of the line
   (four phonemes, or fewer and a word gap) at the pitch byte + 1Bh, sliding up, then back.  Reading on (its say-
   all) never does, and speech-box lines never do.  On, the first line of an utterance that follows a cancel (or
   the voice's first), or that starts after half a second of silence, is lifted that way: moving by line
   interrupts speech, or speaks into a pause; say-all queues the next line at once.  With the unit's voice
   inflection off (blv_create) there is no lift, as on the unit.  blv_clock: the pause's clock, in seconds (a
   monotonic one; the tests may set their own). */
BL_API extern double (*blv_clock)(void);
BL_API void blv_set_line_lift(bl_voice *v, int on);
BL_API int blv_get_line_lift(const bl_voice *v);
/* The listener interrupted while the voice was saying nothing (a stop between utterances, the last perhaps still
   playing): the next utterance is lifted as after a cancel, and the unit left as it is (blazie.py's cancel() when
   idle). */
BL_API void blv_interrupted(bl_voice *v);
/* The driver's "Custom number processing" (numberWords; the driver's default is on, this voice's off until set): fn
   rewrites the cleaned text before it is cut into lines, as the driver's _numbers -- bl_numbers.h's bl_numbers is it.
   A function rather than a flag so this file links without the number words (bl.dll has none).  fn gets the text as
   UTF-8 and the voice's encoding and returns a malloc'd UTF-8 string (NULL: left as it was).  NULL turns it off. */
typedef char *(*blv_numbers_fn)(const char *utf8, int encoding);
BL_API void blv_set_numbers(bl_voice *v, blv_numbers_fn fn);

/* Starts one utterance (UTF-8).  Returns the number of lines sent to the unit (0: nothing to say), or -1 when the
   host refused it (a run-ahead fault); the next blv_speak recovers with a cancel first. */
BL_API int blv_speak(bl_voice *v, const char *utf8);
/* The next block of the utterance: 16-bit mono PCM at out_rate in an internal buffer valid until the next call
   (possibly 0 samples while the unit is still silent).  *done = 1 once the unit has finished, or on a host fault
   (blv_fault says which). */
BL_API int blv_render(bl_voice *v, const short **pcm, int *done);
/* Stops the utterance: the unit drops what it has not spoken (its audio is discarded); the next blv_speak starts
   clean. */
BL_API void blv_cancel(bl_voice *v);
/* Nonzero after a host fault ended or refused the last utterance (cleared by the next blv_speak's recovery). */
BL_API int blv_fault(const bl_voice *v);

/* Index marks (../marks.h).  The unit has its own: a Ctrl-F anywhere in a line is echoed when speech reaches it.
   blv_speak_marked: as blv_speak, each mark's Ctrl-F glued to the front of the word it stands before.  The text is
   prepared whole, exactly as without marks; marks that fall on one word are one Ctrl-F.
   blv_reached: the marks speech reached in the block the last blv_render returned, in order, each with the sample
   at which its echo came.  A mark before the first word comes with the first block; one after the last word with the
   last block, after its last sample (the utterance's end, when its last sound has died away: the last line's own
   echo comes before that); whatever else is left when the utterance is over comes with the last block too, not
   exact (so no caller is left waiting), and nothing after a cancel.
   blv_marks_exact: 1 only for a release measured for it (bl_languages.txt's marks column, bl_firmware.h
   blv_release_marks) that echoes and is not run ahead of the listener (its echoes then come ahead of the audio).
   Otherwise blv_speak_marked speaks the text without marks and reports none: the voice table estimates their
   places (voices.c).

   MEASURED through this path on the emulated units (2026-11-19: a mark before every word of five texts against the
   same text without marks, rates 0, 50 and 100, short pauses off and on):
   - the English releases (June 2003, September and January 2000), the Danish, the Norwegian and the Swedish: every
     mark echoed in order where its word begins (in the pause before it, as the audio shows); the phonemes and their
     R2, R3, R4 the same; the times between phoneme loads the same to 1 ms at rates 0 and 50; the text spoken after a
     marked one as before.  A mark adds one pause frame at its word boundary (R0 = C0h with the registers as last
     written), inside the pause already there.
   - what a mark DOES change there: the unit starts a sentence's final fall (R1 lowered for its last phonemes) one
     phoneme late when a mark stands where the fall begins -- R1 on one phoneme of some sentences, 24 phonemes in
     the 60 marked utterances of each release.  And at rate 100 the unit's pauses move by up to 55 ms with marks
     (up to 18 ms from one unmarked run to the next): it is busy there.  Both are the firmware's own doing with its
     own index marks; the Ctrl-F before the space, as a word of its own, or at the end of the line before measured
     the same.
   - the Spanish releases: marks change the speech itself.  Through the shipped Spanish voice with the number words
     on, the unit steady from one unmarked run to the next: three sentences 52 phoneme loads and 4.64 s without
     marks, 59 loads and 5.45 s with one before every word (short pauses on); a list of words with commas between
     them came out the same.
   - the German releases: a mark changes a phoneme of some sentences.  Their marks, and the Spanish ones', are
     estimated.
   - the units that never echo Ctrl-F: estimated.
   The host takes a mark's echo for a line's end (its "preparing", bl_host.c) until the next phoneme, which the echo
   comes with: no write moved in the measurement above. */
BL_API int blv_marks_exact(const bl_voice *v);
BL_API int blv_speak_marked(bl_voice *v, const char *utf8, const ssv_mark *marks, int n_marks);
BL_API int blv_reached(bl_voice *v, const ssv_reach **out);
/* The bytes blv_speak_marked would send (as blv_say_bytes), for tests. */
BL_API int blv_say_bytes_marked(const char *utf8, int encoding, int pack, const ssv_mark *marks, int n_marks,
                                unsigned char *out, int cap);
BL_API extern int blv_marks_break;          /* the tests' controls (bl_voice.c) */
/* A test's control (blv_fault_test.py sets it; nothing else does): nonzero puts back the 0.7 draft's handling --
   a refused say taken as said, a fault taken as busy -- which must fail that test. */
BL_API extern int blv_break_fault;

/* The bytes blv_speak would send the unit for this text (currencies, clean-up, lines, encoding), for tests: returns
   the length, and copies them into out when they fit in cap. */
BL_API int blv_say_bytes(const char *utf8, int encoding, int pack, unsigned char *out, int cap);
/* The same with the number words (fn as blv_set_numbers; NULL: as blv_say_bytes). */
BL_API int blv_say_bytes_with(const char *utf8, int encoding, int pack, blv_numbers_fn fn, unsigned char *out, int cap);
/* ... and as a release whose own reader repeats digits around commas is sent it (commas nonzero: bl_firmware.h
   BLV_COMMAS_REPEAT, which blv_create reads for its firmware): after the number words, no comma touches a digit --
   one between two digits is a space (the unit says "3,5" as "3 5": measured), one beside a digit gets a space
   on that side, so that it keeps its pause.  MEASURED on the emulated Spanish unit (2026-11-19): every text
   that set the fault off, said three times running, the same length each time with the guard; 300 random texts
   of digits, letters, commas and other punctuation, each said twice, the same; "3 , 5" 1.01 s for the
   unguarded "3, 5"'s 1.35 s (which said the 5 twice).  Not checked on a real unit, nor on another CPU core. */
BL_API int blv_say_bytes_release(const char *utf8, int encoding, int pack, blv_numbers_fn fn, int commas,
                                 unsigned char *out, int cap);
BL_API extern int blv_commas_break;         /* a test's control: nonzero, the commas are left as they are */
/* ... and as a release whose own reader names symbols with its punctuation set to none is sent it (symbols: the
   ones it names, bl_firmware.h blv_release_symbols, which blv_create reads for its firmware; NULL or "" none):
   after the number words each of them is a space, a "$" before a digit kept (#68).  MEASURED on the emulated
   Spanish unit (2026-11-21, test_symbols.py): "alpha (beta) gamma", "a-b" and "50%" said with the phonemes
   of "alpha beta gamma", "a b" and "50"; "$5" with its dollar.  Not checked on a real unit. */
BL_API int blv_say_bytes_guarded(const char *utf8, int encoding, int pack, blv_numbers_fn fn, int commas,
                                 const char *symbols, unsigned char *out, int cap);
BL_API extern int blv_symbols_break;        /* a test's control: nonzero, the symbols are left as they are */
/* The unit's byte for one code point in that encoding (below 80h itself), or -1 when the unit has none: the NVDA
   driver's codec for BLV_CPHUN, which Python has no codec for. */
BL_API int blv_text_byte(int encoding, unsigned code_point);
/* The letters translit.h keeps for that encoding (the rest become their base letters): voices.c's ssv_translit, the
   NVDA driver's pass, uses the voice's own. */
typedef int (*blv_known_fn)(unsigned code_point);
BL_API blv_known_fn blv_text_known(int encoding);

BL_API bl_host *blv_host(bl_voice *v);
BL_API ssi263 *blv_chip(bl_voice *v);

#ifdef __cplusplus
}
#endif
#endif
