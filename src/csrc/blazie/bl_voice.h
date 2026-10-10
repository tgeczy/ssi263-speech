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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bl_voice bl_voice;

#define BLV_LATIN1 0               /* the English unit (BL2ENG.BNS): 7-bit text */
#define BLV_CP850 1                /* the Spanish unit (BL2SPA.BNS): DOS code page 850 */

/* Boots the unit as the driver does (punctuation none, full numbers, inflection on or off, volume 6).
   whine: 0 off, 1 hiss, 2 whine.  NULL on failure, the reason in err. */
BL_API bl_voice *blv_create(const char *firmware, const char *state, int encoding, double out_rate, int inflection,
                            int whine, char *err, int errlen);
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
/* A test's control (blv_fault_test.py sets it; nothing else does): nonzero puts back the 0.7 draft's handling --
   a refused say taken as said, a fault taken as busy -- which must fail that test. */
BL_API extern int blv_break_fault;

/* The bytes blv_speak would send the unit for this text (currencies, clean-up, lines, encoding), for tests: returns
   the length, and copies them into out when they fit in cap. */
BL_API int blv_say_bytes(const char *utf8, int encoding, int pack, unsigned char *out, int cap);
/* The same with the number words (fn as blv_set_numbers; NULL: as blv_say_bytes). */
BL_API int blv_say_bytes_with(const char *utf8, int encoding, int pack, blv_numbers_fn fn, unsigned char *out, int cap);

BL_API bl_host *blv_host(bl_voice *v);
BL_API ssi263 *blv_chip(bl_voice *v);

#ifdef __cplusplus
}
#endif
#endif
