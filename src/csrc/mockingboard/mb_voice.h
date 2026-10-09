/* mb_voice.h -- the Mockingboard as a voice: Sweet Micro Systems' text-to-speech (mb_host.h) with NVDA's settings,
 * the shared text path and the firmware's limits taken care of, for the voice table (../voices.h) -- as as_voice.h is
 * the Accent SA's.
 *
 * Settings, on NVDA's scales, onto the firmware's own four bytes:
 *   rate 0-100     the firmware's rate 0-13 (50 = 8, the toolkit demo's; 14 and 15 wrap in INFLECTION: never sent)
 *   pitch 0-100    its inflection, the starting pitch, 0-26 (50 = 8, the demo's)
 *   volume         the PCM gain in percent (100 = 1; above it louder, clipped at full scale), as the other voices;
 *                  the chip's amplitude stays the demo's 11 and its filter 230
 *   numbers        the shared custom number processing (../numwords.h); off, the firmware reads digits one by one
 *   pitch_offset   a capital (NVDA's PitchCommand): the inflection raised by offset x 18/50 for that text
 *
 * The text: currencies (numwords.h), the accented-letter pass (../translit.h, ASCII), then 7-bit printable text,
 * whitespace collapsed; lower case is the firmware's to fold.  Long text is said in parts the firmware can take: at
 * most MBV_PART characters, split at sentence ends, then at clause marks, then between words; a part the host still
 * refuses as too long (MBH_FAULT_LONG: digit strings make five frames a character) is split in two and said again.
 * The parts follow each other as soon as the firmware is idle.
 *
 * The firmware file comes from the caller (mb_host.h: MB_FILE), never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef MB_VOICE_H
#define MB_VOICE_H

#include <stddef.h>
#include "mb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mb_voice mb_voice;

#define MBV_PART 120                       /* characters in one part at most */

MB_API mb_voice *mbv_create(const unsigned char *image, size_t n, double out_rate, char *err, int errlen);
MB_API mb_voice *mbv_create_dir(const char *dir, double out_rate, char *err, int errlen);
MB_API void mbv_destroy(mb_voice *v);

MB_API void mbv_set(mb_voice *v, int rate, int pitch, int volume, int numbers);
/* Starts one utterance (UTF-8).  1: speaking; 0: nothing to say. */
MB_API int mbv_speak(mb_voice *v, const char *utf8, int pitch_offset);
/* The next 30 ms block: 16-bit mono PCM at out_rate in an internal buffer valid until the next call.
   *done = 1 once every part has been said and the firmware is idle. */
MB_API int mbv_render(mb_voice *v, const short **pcm, int *done);
/* Stops at once: the parts not said are dropped, the one playing ends at the firmware's next frame (mbh_cancel). */
MB_API void mbv_cancel(mb_voice *v);

/* The firmware's settings for NVDA's values (above), for tests and front ends that show them. */
MB_API int mbv_rate_step(int rate);
MB_API int mbv_pitch_step(int pitch);
/* The text the voice says for utf8 (with numbers on or off), as the parts it gives the firmware, joined by '|': the
   length, copied into out (NUL-terminated) when it fits in cap.  For tests. */
MB_API int mbv_parts(const char *utf8, int numbers, char *out, int cap);
/* How many parts the last utterance had, and how many were split again after a refusal. */
MB_API int mbv_get_int(const mb_voice *v, const char *name);

MB_API mb_host *mbv_host(mb_voice *v);

#ifdef __cplusplus
}
#endif
#endif
