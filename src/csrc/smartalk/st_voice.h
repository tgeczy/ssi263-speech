/* st_voice.h -- the SmarTalk as a voice: Automated Functions' S_TALK.EXE (st_host.h) with NVDA's settings, the shared
 * text path and the box's buffer taken care of, for the voice table (../voices.h) -- as mb_voice.h is the
 * Mockingboard's.
 *
 * Settings, on NVDA's scales, onto the TSR's own commands (TECHMAN.TXT on its disk), each sent only when it changed
 * (they stay with the TSR, a cancel included):
 *   rate 0-100        ESC > 0-9 (50 = 3: S_TALK.EXE 1.21 starts at the speed ESC >3 gives, though its manual says
 *                     5; stv_rate_step)
 *   pitch 0-100       ESC P 0-9 (50 = 5, its default)
 *   tone 0-25         ESC { A-Z (8 = I, its default)
 *   inflection        1: its regular inflection (ESC m 0, the default); 0: monotone (ESC m 1)
 *   volume            the PCM gain in percent (100 = 1; above it louder, clipped at full scale), as the other voices;
 *                     the box's own volume is set to 9, its top step (ESC < 9, with the first text after a boot), so
 *                     it is as loud as the Speak-Out, the Accents and the Sounding Board; its default, 4, was 4.2 dB
 *                     under them
 *   numbers           the shared custom number processing (../numwords.h); off, the TSR reads three digits and more
 *                     one by one and drops a number's point, dollar and percent (its punctuation is off)
 *   pitch_offset      a capital (NVDA's PitchCommand): the pitch digit for pitch + offset, for that text
 *
 * The text: the accented-letter pass and currencies, then 7-bit printable text with every control character a space
 * (../accent_text.h: so no ESC, Ctrl-X or index mark reaches the TSR from the text); dollar amounts and a percent
 * after a number as words ("$2.63" -> "2 dollars 63 cents", as numwords.h's pound: the TSR's punctuation is off and
 * it says neither "$" nor "%"); then, with numbers, the number words (nw_normalise).  It is said in parts of at most STV_PART characters cut at sentence ends, then clause marks, then between words, each
 * ended by the carriage return that makes the TSR speak.  The next part goes in while the box still has a few
 * phonemes queued (STV_LOW bytes), so speech runs on and no call waits on the TSR's 1,200-byte buffer.
 *
 * The pitch at an utterance's start (a host feature, as the Speak-Out's and the Accents' snap): the TSR writes its pitch
 * targets in the chip's slowest glide field, so each utterance slid in from where the last one ended, and a capital or
 * a new pitch setting was heard as a swoop.  The first changes of target (the PA that starts speech, then the first
 * phoneme) land at once; the glides inside the utterance stay the TSR's own.
 *
 * The TSR comes from the caller (st_host.h: STH_FILE), never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef ST_VOICE_H
#define ST_VOICE_H

#include <stddef.h>
#include "st_host.h"
#include "../marks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct st_voice st_voice;

#define STV_PART 160                       /* characters in one part at most */
#define STV_LOW 48                         /* the next part goes in below this many queued bytes (12 phonemes) */

ST_API st_voice *stv_create(const unsigned char *exe, size_t n, double out_rate, char *err, int errlen);
ST_API void stv_destroy(st_voice *v);

ST_API void stv_set(st_voice *v, int rate, int pitch, int tone, int volume, int inflection, int numbers);
/* Starts one utterance (UTF-8), after anything still playing is cancelled -- and after a fault, on a box booted again
   from the kept S_TALK.EXE (am_voice.c's restart).  1: speaking; 0: nothing to say (or no box could be booted). */
ST_API int stv_speak(st_voice *v, const char *utf8, int pitch_offset);
/* The next 30 ms block: 16-bit mono PCM at out_rate in an internal buffer valid until the next call; the silence
   before the first phoneme left out.  *done = 1 once every part has been said and the box is idle (or it failed:
   stv_fault). */
ST_API int stv_render(st_voice *v, const short **pcm, int *done);
/* Stops at once: the parts not said are dropped, the TSR's Ctrl-X empties its buffer (sth_cancel). */
ST_API void stv_cancel(st_voice *v);
/* nonzero once the host has faulted or the box stalled (speaking, silent for 4 s), until the next speak boots a new
   box */
ST_API int stv_fault(const st_voice *v);

/* Index marks (../marks.h).  The TSR has its own (its manual): a Ctrl-F in the text is counted when speech passes it,
   and the status request (INT 17h AH=2) returns the count in DH and clears it; nothing switches it on.
   stv_speak_marked: as stv_speak, a Ctrl-F directly after the word before each word one of the caller's marks stands
   before.  The text is made the voice's whole, exactly as without marks, and cut into the same parts: no mark moves a
   cut.  Marks on one word are one Ctrl-F; one before the first word needs none.
   stv_reached: the marks speech reached in the block the last stv_render returned, in order, each with the sample at
   which the TSR's count rose (st_host.h sth_replies): one before the first word with the first block, one after the
   last word and whatever is left with the last, nothing after a cancel.
   MEASURED on the emulated TSR (2026-11-19, docs/index-marks.md): the count rises in the interrupt that writes what
   follows the mark -- the next word's first phoneme at rate digits 0 to 7, a pause 4.3 ms before it at 8 and 9; a
   mark at a word break is a space to the TSR and leaves the speech as it is at digits 0 to 7; at 8 and 9 it can add
   or remove a 4.3 ms pause write unless it stands before the space ("word<06> next"), which is where it is put;
   Ctrl-X zeroes the count. */
ST_API int stv_marks_exact(const st_voice *v);
ST_API int stv_speak_marked(st_voice *v, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks);
ST_API int stv_reached(st_voice *v, const ssv_reach **out);
/* The parts stv_speak_marked would send (as stv_parts), for tests. */
ST_API int stv_parts_marked(const char *utf8, int numbers, const ssv_mark *marks, int n_marks, char *out, int cap);
ST_API extern int stv_marks_break;          /* the tests' controls (st_voice.c) */

/* The TSR's digit for NVDA's pitch (0-9; 50 = 5), for its rate (0-9; 50 = 3), and its tone letter for 0-25 (else
   'I'), for tests and front ends. */
ST_API int stv_step(int value);
ST_API int stv_rate_step(int value);
ST_API char stv_tone_letter(int tone);
/* The text the voice says for utf8 (with numbers on or off), as the parts it gives the TSR, joined by '|': the
   length, copied into out (NUL-terminated) when it fits in cap.  For tests. */
ST_API int stv_parts(const char *utf8, int numbers, char *out, int cap);
/* "parts" (the last utterance's), "sent" (how many of them have gone in), "commands" (bytes of settings sent with
   it); -1 unknown */
ST_API int stv_get_int(const st_voice *v, const char *name);

ST_API st_host *stv_host(st_voice *v);

#ifdef __cplusplus
}
#endif
#endif
