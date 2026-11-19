/* sv_voice.h -- Speech Systems' Super Voice (1984) as a voice: its own text-to-speech (sv_host.h) with NVDA's
 * settings, the shared text path and the program's limits taken care of, for the voice table (../voices.h) -- as
 * mb_voice.h is the Mockingboard's.
 *
 * Settings, on NVDA's scales, onto the program's own control codes (the manual, pp.21-22), written at the head of
 * each part of a text -- the host starts every text from the program as loaded, so nothing carries over:
 *   rate 0-100     ^Sn, the program's rate 1-16 (50 = 11, its default: nothing is written then)
 *   pitch 0-100    ^ITn,3, its inflection level 1-32 at its default speed of change 3 (50 = 14, its default:
 *                  nothing is written then)
 *   volume         the PCM gain in percent (100 = 1; above it louder, clipped at full scale), as the other voices;
 *                  the chip's amplitude stays the program's
 *   numbers        the shared custom number processing (../numwords.h).  Off (the default here), the program reads
 *                  numbers itself: it knows money ("$12.50": twelve dollars fifty cents) and numbers up to the
 *                  trillions
 *   pitch_offset   a capital (NVDA's PitchCommand): the inflection level raised by offset x 18/50 for that text
 * At 50, 50 a text reaches the program with nothing added: the voice is the cartridge's as it was sold.
 *
 * The text: currencies when numbers is on (numwords.h), the accented-letter pass (../translit.h, ASCII), then 7-bit
 * printable text, whitespace collapsed; either case is the program's to read.  Then what this program needs, each
 * a rule of the voice's and not the cartridge's:
 *   ^              (5Eh, the Color Computer's up arrow) is its control character: a text's own is taken out, or a
 *                  stray "^V0" in a document would silence it and "^P" would switch it to phoneme names
 *   a period after a digit, not before another digit, gets a space in front of it: the program says "point" for
 *                  any period that follows a digit, a sentence's last one too ("PAGE 1." = "page one point")
 *   "              taken out: the program says "quote" for each one
 *   a hyphen       not in front of a digit becomes a space: the program says "minus" for every hyphen ("A-B" = "a
 *                  minus b"); in front of a digit it is left to it
 * Its other symbols are left to it (it says "percent", "and", "equals", "plus", "number").
 *
 * Long text is said in parts the program can take: at most SVV_PART characters (the manual's limit is 240 with the
 * control codes), split at sentence ends, then at clause marks, then between words.  The parts follow each other as
 * soon as the program has returned.
 *
 * The program file comes from the caller (sv_host.h: SV_FILE), never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef SV_VOICE_H
#define SV_VOICE_H

#include <stddef.h>
#include "sv_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sv_voice sv_voice;

#define SVV_PART 200                       /* characters in one part at most, before its control codes */

SV_API sv_voice *svv_create(const unsigned char *image, size_t n, double out_rate, char *err, int errlen);
SV_API sv_voice *svv_create_dir(const char *dir, double out_rate, char *err, int errlen);
SV_API void svv_destroy(sv_voice *v);

SV_API void svv_set(sv_voice *v, int rate, int pitch, int volume, int numbers);
/* Starts one utterance (UTF-8).  1: speaking; 0: nothing to say. */
SV_API int svv_speak(sv_voice *v, const char *utf8, int pitch_offset);
/* The next 30 ms block: 16-bit mono PCM at out_rate in an internal buffer valid until the next call.
   *done = 1 once every part has been said and the program has returned. */
SV_API int svv_render(sv_voice *v, const short **pcm, int *done);
/* Stops at once: the parts not said are dropped, the one being said is cut (svh_cancel). */
SV_API void svv_cancel(sv_voice *v);

/* The program's settings for NVDA's values (above), for tests and front ends that show them. */
SV_API int svv_rate_step(int rate);        /* ^S 1-16 */
SV_API int svv_pitch_step(int pitch);      /* ^IT 1-32 */
/* The text the voice says for utf8 (with numbers on or off), as the parts it gives the program without their
   control codes, joined by '|': the length, copied into out (NUL-terminated) when it fits in cap.  For tests. */
SV_API int svv_parts(const char *utf8, int numbers, char *out, int cap);
/* "parts": how many parts the last utterance had.  -1: unknown. */
SV_API int svv_get_int(const sv_voice *v, const char *name);

SV_API sv_host *svv_host(sv_voice *v);

#ifdef __cplusplus
}
#endif
#endif
