/* sb_host.h -- GW Micro's Sounding Board in C: src/hosts/soundingboard.py, line for line, on MAME's 8086 (../pc86).
 * The card's own DOS drivers, version 2.7 -- SBLOAD.COM ("Text-To-Speech in DOS": the rules) and SB.COM (the card
 * driver), both packed .COM files that unpack themselves -- loaded as DOS would, each until it stays resident, in a bare
 * PC that stands in for DOS and the BIOS: the card's SSI-263 at 300h-304h with A/R on IRQ2 (vector 0Ah), a BIOS timer
 * (SB.COM waits for 18 ticks at start-up), and text in as a screen reader sent it, INT 17h to LPT3.  The drivers'
 * front end is the Speak-Out's, byte for byte (tools/check_soundingboard.py), so the Speak-Out's voice (so_voice.h)
 * drives this host as it drives so_host.h: the same calls, sbh_ for soh_.
 *
 * Where it differs from soundingboard.py, on purpose, so the voice sees one behaviour: say() counts text starting with
 * ^E (a setting) as no speech, as so_host.h does; and boot() waits until the card's own greeting ("Sounding board
 * version 2.7") has played out, silently, whatever `seconds` asks (up to SBH_GREETING_LIMIT).
 *
 * Only version 2.7 is known to work (input_pending reads SB.COM's frame ring at its offsets 14Ch/14Eh): the app imports
 * only the two files by sha256.  The drivers come from the caller and are never built in.  cpu_ips 5,000,000 is the
 * Python host's coupling of instructions to chip time, not a clock.  Not thread-safe per instance.  MIT.
 */
#ifndef SB_HOST_H
#define SB_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define SB_API __declspec(dllexport)
#else
#define SB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sb_host sb_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} sbh_write;

#define SBH_CPU_IPS 5000000.0              /* soundingboard.py's cpu_ips */
#define SBH_GREETING_LIMIT 6.0             /* boot()'s limit (soundingboard.py) */
#define SBH_SBLOAD_SHA256 "f821c9c28180cfcecc0e5b422bb30b1b8a97d37bac7188405f066baa56cd7b72"
#define SBH_SB_SHA256 "0946a2f2caa5acad54ef2113d1ebb5f40f686219eea0b3697e478dce19c9158e"

/* SBLOAD.COM's and SB.COM's bytes, loaded in that order; NULL with the reason in err */
SB_API sb_host *sbh_create(const unsigned char *sbload, size_t sbload_len, const unsigned char *sb, size_t sb_len,
                           ssi263 *chip, double out_rate, char *err, int errlen);
SB_API void sbh_destroy(sb_host *h);
SB_API ssi263 *sbh_chip(sb_host *h);

/* say(): bytes to LPT3 (INT 17h, AH=0 per character), as fast as the driver takes them: queued, and printed a piece
   at a time while SB.COM's frame ring is under half full (here and at each run and skip), so no printer call waits
   on a full ring however long the text.  Text with a letter or digit (bytes under 32 left out) that does not start
   with ^E is speech: until its first phoneme the card counts as busy (patience). */
SB_API void sbh_say(sb_host *h, const unsigned char *bytes, int n);
/* run(): `seconds` of chip time in `step` slices: the chip's audio at out_rate, in an internal buffer valid until the
   next call (what a blocked say made first).  Returns the sample count (-1: a fault). */
SB_API int sbh_run(sb_host *h, double seconds, double step, const double **audio);
/* The index marks (05h 5Bh xx in the text, so_voice.h) the driver crossed in the audio the last sbh_run returned,
   each with the samples of that audio made when it was crossed (to the host's slice).  SB.COM keeps what its
   status call (INT 17h AH=02h on its port: DH, DL) reports in two bytes of its own, a count of marks crossed and
   the value of the last; the host reads them after every slice instead of calling, which would reset them and
   run the driver's code.  So `value` is the mark's byte for the newest of a slice and -1 for any crossed with
   it in the same slice (the driver keeps only the last value).  At most SBH_REPLIES a run; sbh_get_int
   "marks" counts every mark ever crossed.  After sbh_cancel the count starts again from where the driver is. */
typedef struct {
    int value;                             /* the mark's byte, or -1 (above) */
    int sample;                            /* samples of the run's audio made when it was crossed */
} sbh_reply;
#define SBH_REPLIES 64
SB_API int sbh_replies(const sb_host *h, const sbh_reply **out);
/* skip(): the drivers run with the chip keeping time but making no sound; returns the time run */
SB_API double sbh_skip(sb_host *h, double seconds, double step);
/* input_pending(): text not yet printed, or frames the rules have made and the IRQ handler has not yet sent (SB.COM's
   ring) */
SB_API int sbh_input_pending(const sb_host *h);
SB_API int sbh_busy(const sb_host *h, double quiet, double patience);
SB_API void sbh_boot(sb_host *h, double seconds);
/* cancel(limit): the text not yet printed dropped, then Ctrl-X, the driver's momentary silence; what was queued played
   out silently (up to `limit` s) */
SB_API void sbh_cancel(sb_host *h, double limit);
/* nonzero once the drivers have faulted (an unimplemented service, a halt, an 80186-only opcode) */
SB_API int sbh_fault(const sb_host *h);
SB_API const char *sbh_error(const sb_host *h);

/* the host's state, by name, as so_host.h's: doubles "cpu_ips", "last_speech", "say_time", "time" (read-only); ints
   "preparing", "request" (read-only), "log_writes" (keep every chip write for sbh_writes).  -1: unknown. */
SB_API double sbh_get_double(const sb_host *h, const char *name);
SB_API void sbh_set_double(sb_host *h, const char *name, double v);
SB_API int sbh_get_int(const sb_host *h, const char *name);
SB_API void sbh_set_int(sb_host *h, const char *name, int v);
SB_API int sbh_writes(const sb_host *h, const sbh_write **writes);
SB_API void sbh_clear_writes(sb_host *h);

#ifdef __cplusplus
}
#endif
#endif
