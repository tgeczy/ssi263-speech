/* as_voice.h -- the Accent SA as a voice: nvda/accent/synthDrivers/accentmini.py's front end for its "sa" voice, in C
 * (its boot, its settings commands, currencies, _clean and _numbers, a capital's pitch with snap_pitch, the speak loop
 * with the lead trim and PCM, and cancel), around the C host (as_host.h) and a chip made from the built-in defaults
 * (ssi263_default_params) -- as bl_voice.h is the Braille Lite's.  For front ends without Python: Android first.
 *
 * src/platforms/android/test/test_android_native.py holds it to the NVDA driver itself (under the stand-in NVDA, on the
 * desktop's C host), byte for byte, on the texts and settings of its Accent SA cases.
 *
 * The ROMs come from the caller (as ash_create): Aicom's u2/u3/u4, never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef AS_VOICE_H
#define AS_VOICE_H

#include <stddef.h>
#include "as_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct as_voice as_voice;

/* Creates the unit from the ROMs and boots it as the driver does (ash_boot: the greeting flushed; its settings are
   then the Accent's power-up ones: rate 5, pitch 5, voice 5, full intonation).  NULL on failure, the reason in err. */
AS_API as_voice *asv_create(const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                            const unsigned char *u4, size_t n4, double out_rate, char *err, int errlen);
AS_API void asv_destroy(as_voice *v);

/* The driver's settings, on NVDA's scales: rate and pitch 0-100 (50 = the Accent's 5), inflection 0-100 (100 = full
   intonation, ESC M0; 0 = monotone, M1), volume in percent (100 = the driver's full volume, the gain 1; above it is
   louder, clipped at full scale), numbers = the driver's custom number processing.  Sent before the next text, only
   what changed (the driver's _sent). */
AS_API void asv_set(as_voice *v, int rate, int pitch, int inflection, int volume, int numbers);

/* Starts one utterance (UTF-8).  pitch_offset != 0 is NVDA's PitchCommand(offset) before the text -- a capital: the
   pitch jumps (snap_pitch) to the settings' pitch plus the offset for this text, and is restored (snapped) once its
   audio is out.  Returns 1 when text was sent, 0 when there is nothing to say. */
AS_API int asv_speak(as_voice *v, const char *utf8, int pitch_offset);
/* The next 30 ms block: 16-bit mono PCM at out_rate in an internal buffer valid until the next call (0 samples while
   the unit is still silent: the driver's lead trim).  *done = 1 once the unit has finished: not busy, and its 8085
   back in its waiting loops (issue #8: between clauses it goes quiet while it reads the next one).  The blocks of
   such a pause come out together (one call, several blocks) when the speech goes on, so the audio is unbroken. */
AS_API int asv_render(as_voice *v, const short **pcm, int *done);
/* The longest such pause asv_render waits through (chip seconds, default 6) before it calls the text done anyway;
   0 = done at the first quiet block, as 0.7.5 and earlier did (the tests' must-fail control). */
AS_API void asv_set_settle(as_voice *v, double seconds);
/* 1 when the last text was called done at that limit with the firmware still at work: not a completion.  Its held
   audio is given out with the done and what the firmware was still making is flushed (a Ctrl-X, as a cancel), so the
   next text starts clean; a front end should log it.  0 for a text that ended (and before any). */
AS_API int asv_limit(const as_voice *v);
/* The tests' must-fail controls only, never set by a front end (0, the default: none).  1: a new text, a cancel or a
   job's end keeps a wait in progress (its held audio and pending done); 2: the limit gives a plain done and drops the
   held audio; 3: "at work" is the first revision's 2000 instructions a block. */
AS_API void asv_set_test_break(as_voice *v, int what);
/* The driver's cancel: the Accent's flush (Ctrl-X), then the pitch said again if a capital's restore was pending. */
AS_API void asv_cancel(as_voice *v);

/* The NVDA variant: the voice characteristic, ESC V0-9 (5, the default; anything else ignored).  Sent with the next
   utterance's settings when it changed, as the driver sends its _accent_settings. */
AS_API void asv_set_voice(as_voice *v, int voice);

/* A job: NVDA's whole speech sequence as the NVDA driver (nvda/accent/synthDrivers/accentmini.py, since 0.7.5 on
   this library) speaks it -- several texts, PitchCommands and IndexCommands in one utterance, which asv_speak cannot
   say byte for byte: the lead trim is armed once per job, and a capital's pitch stays until the next PitchCommand or
   the job's end.  As so_voice.h's sov_begin ... sov_flush, and am_voice.h's amv_begin ... amv_flush:
     asv_begin, asv_pitch(offset), asv_text(bytes, n) (asv_say_bytes' text with the carriage return added; then
     asv_render until *done), asv_end (the finally), asv_flush (after asv_end when the job was cancelled).
   A job that cannot go on (the watchdog: busy, speaking and silent 4 s; out of memory) ends its text with asv_fault
   set and each call returning -1; the next asv_begin (or asv_flush) restarts the unit, as the driver's _run does.
   asv_speak and asv_cancel keep their single-utterance behaviour. */
AS_API int asv_begin(as_voice *v);
AS_API int asv_pitch(as_voice *v, int pitch_offset);
AS_API int asv_text(as_voice *v, const unsigned char *bytes, int n);
AS_API int asv_end(as_voice *v);
AS_API int asv_flush(as_voice *v);
AS_API int asv_fault(const as_voice *v);

/* The driver's _accent_pitch: NVDA's pitch 0-100 on the Accent's ESC P 0-9 (50 -> 5). */
AS_API int asv_pitch_step(int pitch);

/* The text the driver would send for this utterance (currencies, _clean, strip, _numbers; the carriage return not
   included), for tests: returns the length, and copies it into out (NUL-terminated) when it fits in cap. */
AS_API int asv_say_bytes(const char *utf8, int numbers, char *out, int cap);

AS_API as_host *asv_host(as_voice *v);

#ifdef __cplusplus
}
#endif
#endif
