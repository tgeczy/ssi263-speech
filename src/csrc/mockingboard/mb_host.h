/* mb_host.h -- the Mockingboard host: Sweet Micro Systems' text-to-speech (Mike LePage's version 1.1, 11 March 1985,
 * from the Mockingboard Developers Toolkit disk) on the board (mb_board.h), in lockstep with an SSI-263 (../ssi263.h).
 *
 * The firmware is ONE file, MB_FILE: the toolkit's six DOS 3.3 binary files back to back, each exactly as DOS stores
 * it -- its load address and length (two little-endian words), then its bytes -- in this order: TEXT TO SPEECH (8C00h),
 * INFLECTION (9000h), IIE TTS DRIVER (9300h), MKB:RULE.INDEX (D000h), MKB:RULE.LENGTH (D100h), MKB:RULE.TABLE
 * (D200h).  tools/mockingboard_firmware.py makes it from the disk image.  It is placed as the toolkit's demo BLOADs
 * it, and only the set below (MB_SHA256) is accepted: the host calls into it at fixed addresses.
 *
 * How the host drives it, as the toolkit's own paths do:
 *   settings  the four bytes the demo POKEs (92DCh-92DFh): inflection 0-26 (the starting pitch; 8 in the demo), rate
 *             0-15 (8), amplitude 0-15 (11) and the filter frequency 0-255 (230), read by INFLECTION at each text
 *   a text    as the MB$ path (MB$ GETTEXT) leaves it: at 8500h with a space before and after, 8C03h = its last index,
 *             (06h) = 8500h; then JSR 8C11h -- the rules, INFLECTION's five register streams, and the IIE TTS
 *             DRIVER's start.  Every frame then plays from the IRQ the SSI-263's A/R raises on the second VIA's CA1,
 *             while the 6502 idles; 1Eh is the firmware's busy flag (FFh while it plays).
 *
 * The lockstep is accent_sa.py's: each slice the chip runs (to its next A/R request, at most `step`; or step / 4
 * while requesting), then the 6502 runs as many cycles as that chip time holds; A/R goes to the board after every
 * chip slice and every chip write.
 *
 * The chip's clock, MBH_XCK_HZ 1,020,484: the Apple's bus clock as it averages (14.31818 MHz / 14, one cycle in 65
 * stretched), MEASURED: a recording of a real Mockingboard C playing Sweet Micro's demo (its MESS file through the
 * COMPOSITE DRIVER, on YouTube: Gciw3PYCVok, Spacedog's find), the same frames through our chip -- every sung note
 * of "Mary Had a Little Lamb" and "Three Blind Mice" at this clock within 0.1 Hz of the card's (seven notes, 83-166
 * Hz; at 1 MHz all 2.04% low).  Open: the card's durations come out 0.8% longer than ours at this clock, and its
 * output is much darker (above 1 kHz: -3 dB, rising to -20 dB above 4 kHz: its output stage or the recording's).
 *
 * Numbers that are not the Mockingboard's, labelled so:
 *   a text's conversion (the rules and INFLECTION, before the first frame) runs with the chip's time standing still:
 *               a HOST feature, so speech starts at once; on an Apple it takes a moment.  Speech itself is paced by A/R
 *               and does not change.
 *   cancel      the firmware has none: the host moves its end pointer (1Ah) to the frame playing (ECh), so the
 *               driver's own end-of-text path stops it at the next A/R.
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef MB_HOST_H
#define MB_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define MB_API __declspec(dllexport)
#else
#define MB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mb_host mb_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} mbh_write;

#define MB_FILE "mockingboard-tts-1.1.bin"
#define MB_SHA256 "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae"
/* The earlier text-to-speech (0.8; Mockingboard disks 1 and 2, the same files on both): ONE program at 6600h with
   its own inflection and composite driver, and its rules -- four DOS binary files back to back in the same form:
   TEXT TO SPEECH (6600h), MKB:RULE.INDEX (6E00h), MKB:RULE.LENGTH (6F00h), MKB:RULE.TABLE (7000h).  The same paths
   at other addresses: the text at 6000h, its last index at 6600h, the call at 660Eh, the rules done at 662Ch, the
   settings at 6A00h-6A03h (the disk's SPEECH POKEs 27136-27139), busy at FFh, the R0 frames from 6500h.  Its own
   limits: MBH_MAX_TEXT and MBH_MAX_FRAMES hold for it too (a 257th R0 frame would reach its variables at 6600h;
   that byte is its guard). */
#define MB_FILE_EARLY "mockingboard-tts-early.bin"
#define MB_SHA256_EARLY "c7c049b1b61792719e21e461a2a8c25fc32c12882c81305814a3dc67af6e5835"
#define MBH_V11 1                          /* the toolkit's 1.1 */
#define MBH_VEARLY 2                       /* the earlier one */
#define MBH_XCK_HZ 1020484.0               /* measured (above) */
/* 8C03h, the last index, is one byte and the firmware's loops run while their index is at most it: at 255 the index
   wraps and never passes it.  So 254 at most -- a space, 253 characters, a space. */
#define MBH_MAX_TEXT 253
/* The frames one text may make: its five register streams are 256-byte pages indexed by one byte, and past them the
   R0 frames run into 8B00h and then TEXT TO SPEECH itself (on an Apple too).  The host stops the conversion when the
   rules are done (8C32h) and refuses a text whose R0 frames plus 4 per punctuation mark (the most INFLECTION adds
   for one) exceed this; a write into 8B00h-8BFFh before that stops it at once.  Either way nothing was played and
   the firmware is untouched: split the text and say the parts. */
#define MBH_MAX_FRAMES 250
#define MBH_FAULT_LONG 1                   /* "fault": too many frames for one text (refused before playing) */
#define MBH_FAULT_STUCK 2                  /* "fault": the 6502 did not come back within the limit */

/* Either known file (the version by its sha256); mbh_create_dir takes MB_FILE from dir, else MB_FILE_EARLY. */
MB_API mb_host *mbh_create(const unsigned char *image, size_t n, ssi263 *chip, double out_rate, char *err, int errlen);
/* 1 when these bytes are the 1.1 firmware file (MB_SHA256) */
MB_API int mbh_is_known(const unsigned char *image, size_t n);
/* MBH_V11 or MBH_VEARLY when these bytes are one of the known firmware files, else 0: what an importer checks, and
   the file's name for it (mbh_variant_file: MB_FILE, MB_FILE_EARLY; NULL for an unknown one) */
MB_API int mbh_variant(const unsigned char *image, size_t n);
MB_API const char *mbh_variant_file(int variant);
MB_API mb_host *mbh_create_dir(const char *dir, ssi263 *chip, double out_rate, char *err, int errlen);
MB_API void mbh_destroy(mb_host *h);
MB_API ssi263 *mbh_chip(mb_host *h);

/* The four settings bytes (above); used from the next text on. */
MB_API void mbh_set(mb_host *h, int inflection, int rate, int amplitude, int filter);
/* One text (ASCII, 1-MBH_MAX_TEXT bytes; lower case is the firmware's to fold): converted at once and started.
   1: speaking; 0: refused -- empty, more than MBH_MAX_TEXT, still busy, or a fault ("fault": MBH_FAULT_LONG,
   MBH_FAULT_STUCK; 0 for the others). */
MB_API int mbh_say(mb_host *h, const unsigned char *text, int n);
/* `seconds` of chip time in `step` slices: the chip's audio at out_rate, in an internal buffer valid until the next
   call.  Returns the sample count (-1: out of memory). */
MB_API int mbh_run(mb_host *h, double seconds, double step, const double **audio);
MB_API int mbh_busy(const mb_host *h);       /* the firmware's own flag (1Eh), or its interrupt still pending */
MB_API void mbh_cancel(mb_host *h);          /* the driver's own stop, at the next A/R (above) */

/* State, by name.  Ints: "variant" (MBH_V11, MBH_VEARLY), "mem:HHHH" (that byte as the 6502 sees it; the tests), "pitch_gap" (the chip's pitch counter from its target; the tests),
   "log_writes" (keep every write for mbh_writes), "frames" (the last text's, 0-255),
   "fault" (1: the last call into the firmware did not return), and mb_board_get's names.  -1: unknown. */
MB_API int mbh_get_int(const mb_host *h, const char *name);
MB_API void mbh_set_int(mb_host *h, const char *name, int v);
MB_API int mbh_writes(const mb_host *h, const mbh_write **writes);
MB_API void mbh_clear_writes(mb_host *h);

#ifdef __cplusplus
}
#endif
#endif
