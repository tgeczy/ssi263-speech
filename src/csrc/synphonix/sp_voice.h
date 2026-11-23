/* sp_voice.h -- the Artic SynPhonix as a voice: Artic's VEST.COM (sp_host.h) with NVDA's settings, the shared text
 * path and VEST's queue taken care of, for the voice table (../voices.h) -- as st_voice.h is the SmarTalk's.
 *
 * Settings, on NVDA's scales, onto the offsets VEST adds to its frames when it is asked to speak (AL=7; measured on
 * the host, 2026-11-23): VEST has no in-text settings commands.
 *   rate 0-100        CH, the rate offset: 0-50 onto 0-2 and 50-100 onto 2-5.  50 is 2, VEST's own (its start-up
 *                     announcement's), the speed first heard.  A sentence took 5.4, 4.5, 3.5, 2.8, 2.2 s at 0-4; at 5,
 *                     1.4 s, and VEST makes the pitch flat there by itself; above 5 it is no faster
 *   pitch 0-100       DH, the pitch offset: 6 steps of 8 on R1 each way, 50 VEST's own (offset 0).  VEST adds DH x 8
 *                     to R1, from 20h on as a signed step (3Fh -8, 3Ah -48), saturating at 0 and F8h
 *   volume            the PCM gain in percent (100 = 1; above it louder, clipped at full scale), as the other voices;
 *                     VEST's amplitude offset is SPV_AMP, near its top (CL 6 and above are one level), so it is as
 *                     loud as the other voices
 *   inflection        1: VEST's own; 0: flat (AH bit 6, Business Vision's "Inflection Flat")
 *   numbers           the shared custom number processing (../numwords.h); off, VEST reads every digit one by one
 *   pitch_offset      a capital (NVDA's PitchCommand): the pitch for pitch + offset, for that text
 *   The filter offset is VEST's own (DL E6h).
 *
 * The text: the accented-letter pass and currencies, then 7-bit printable text with every control character a space
 * (../accent_text.h); dollar amounts and a percent after a number as words, as the SmarTalk's; then, with numbers, the
 * number words (nw_normalise).  VEST's own reading is kept wherever it is right, and its contractions are ("don't",
 * "I'm", "we'll", "can't", "o'clock" all right, and wrong without the apostrophe); two of its readings are changed:
 *   - a possessive after a hard C ("Artic's", "Eric's", "music's"): VEST treats the C as soft, as in "Grace's", and
 *     adds a syllable ("Articus"); the apostrophe goes ("Artics" is right)
 *   - an apostrophe that is not between two letters (a quote mark: "rock 'n' roll", "the dogs'"): VEST says the word
 *     "apostrophe"; it goes
 *   and VEST's only in-text commands, a word that is exactly ".+" or ".-" (a pitch step), cannot come from the text: the
 *   point goes.
 * It is said in parts of at most SPV_PART characters cut at sentence ends, then clause marks, then between words, one
 * AL=7 call each.  The next part goes in while VEST still has SPV_LOW frames queued (sp_host.h sph_queued_frames), so
 * speech runs on, and no call waits on VEST's queue (it holds 1,000 frames: about 20 s of speech).
 *
 * VEST comes from the caller (sp_host.h: SPH_FILE), never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef SP_VOICE_H
#define SP_VOICE_H

#include <stddef.h>
#include "sp_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sp_voice sp_voice;

#define SPV_PART 240                       /* characters in one part at most */
#define SPV_LOW 40                         /* the next part goes in below this many queued frames */

SP_API sp_voice *spv_create(const unsigned char *com, size_t n, double out_rate, char *err, int errlen);
SP_API void spv_destroy(sp_voice *v);

SP_API void spv_set(sp_voice *v, int rate, int pitch, int volume, int inflection, int numbers);
/* Starts one utterance (UTF-8), after anything still playing is cancelled -- and after a fault, on a card booted again
   from the kept VEST.COM.  1: speaking; 0: nothing to say (or no card could be booted). */
SP_API int spv_speak(sp_voice *v, const char *utf8, int pitch_offset);
/* The next 30 ms block: 16-bit mono PCM at out_rate in an internal buffer valid until the next call; the silence
   before the first phoneme left out.  *done = 1 once every part has been said and the card is idle (or it failed:
   spv_fault). */
SP_API int spv_render(sp_voice *v, const short **pcm, int *done);
/* Stops at once: the parts not said are dropped, VEST's cut (AL=1) empties its queue. */
SP_API void spv_cancel(sp_voice *v);
/* nonzero once the host has faulted or the card stalled (busy, silent for 4 s), until the next speak boots a new one */
SP_API int spv_fault(const sp_voice *v);

/* VEST's offsets for NVDA's rate (CH, 0-5; 50 = 2) and pitch (DH; 50 = 0), for tests and front ends */
SP_API int spv_rate_offset(int value);
SP_API int spv_pitch_offset(int value);
/* The text the voice says for utf8 (with numbers on or off), as the parts it gives VEST, joined by '|': the length,
   copied into out (NUL-terminated) when it fits in cap.  For tests. */
SP_API int spv_parts(const char *utf8, int numbers, char *out, int cap);
/* "parts" (the last utterance's), "sent" (how many of them have gone in); -1 unknown */
SP_API int spv_get_int(const sp_voice *v, const char *name);

SP_API sp_host *spv_host(sp_voice *v);

#ifdef __cplusplus
}
#endif
#endif
