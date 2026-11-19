/* st_host.h -- Automated Functions' SmarTalk in C: src/hosts/smartalk.py, line for line, on MAME's 8086 (../pc86).
 * The box's own DOS text-to-speech, S_TALK.EXE version 1.21 (1995-11-15: the rules and dictionary in conventional
 * memory), loaded as DOS would with the installer's command line (L1 I7 T0 F3 S1) until it stays resident, in a bare
 * PC that stands in for DOS and the BIOS: the box on LPT1 (378h-37Ah) with the chip's A/R on ACK and IRQ7 (vector
 * 0Fh), a BIOS timer (the power-up waits for 40 ticks), and text in as a screen reader sent it, INT 17h to LPT1.  The
 * box's lines are smartalk.py's (as read from the TSR and confirmed by it speaking): the data latched into the
 * register control bits 1-3 select when /STROBE is released, ACK = A/R, ERROR = INIT, SELECT high, BUSY and PAPER-OUT
 * at a powered box's levels.
 *
 * Where it differs from smartalk.py: nowhere on purpose.  boot() is smartalk.py's: ST_RUN.EXE's two lines (the "{"
 * that lets the rules speak), played out silently.
 *
 * Only version 1.21 is known to work (the TSR's data segment at CS:0538, its queue at 01E2 and its speaking flag at
 * 01EC): sth_create refuses an image without its banner.  The program comes from the caller and is never built in.
 * cpu_ips 5,000,000 is the Python host's coupling of instructions to chip time, not a clock.  Not thread-safe per
 * instance.  MIT.
 */
#ifndef ST_HOST_H
#define ST_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define ST_API __declspec(dllexport)
#else
#define ST_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct st_host st_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} sth_write;

#define STH_CPU_IPS 5000000.0              /* smartalk.py's cpu_ips */
#define STH_BOOT_LIMIT 8.0                 /* boot()'s limit (smartalk.py) */
#define STH_FILE "automated-functions-smartalk/S_TALK.EXE"
#define STH_SHA256 "6cef7bc467959960505bfd771d07f387b7bdfad25bad2b51801ae56bb8d78e6c"

/* S_TALK.EXE's bytes; NULL with the reason in err */
ST_API st_host *sth_create(const unsigned char *exe, size_t n, ssi263 *chip, double out_rate, char *err, int errlen);
ST_API void sth_destroy(st_host *h);
ST_API ssi263 *sth_chip(st_host *h);

/* say(): bytes to LPT1 (INT 17h, AH=0 per character).  Text with a letter or digit (bytes under 32 left out) is
   speech: until its first phoneme the box counts as busy (patience).  It speaks at a carriage return. */
ST_API void sth_say(st_host *h, const unsigned char *bytes, int n);
/* run(): `seconds` of chip time in `step` slices: the chip's audio at out_rate, in an internal buffer valid until the
   next call (what a blocked say made first).  Returns the sample count (-1: a fault). */
ST_API int sth_run(st_host *h, double seconds, double step, const double **audio);
/* The index marks (Ctrl-F in the text, st_voice.h) the TSR passed in the audio the last sth_run returned, each with
   the samples of that audio made when it was passed (to the host's slice).  The TSR keeps what its status call
   (INT 17h AH=2: DH, and the call clears it) reports in a word of its own; the host reads that word after every
   slice instead of calling, which would clear it and run the TSR's code.  The marks carry no name: they are the
   next ones in the order they were sent.  At most STH_REPLIES a run; sth_get_int "marks" counts every one ever
   passed.  Ctrl-X (sth_cancel) zeroes the TSR's count: none of a cancelled text is given. */
typedef struct {
    int sample;                            /* samples of the run's audio made when it was passed */
} sth_reply;
#define STH_REPLIES 64
ST_API int sth_replies(const st_host *h, const sth_reply **out);
/* skip(): the TSR runs with the chip keeping time but making no sound; returns the time run */
ST_API double sth_skip(st_host *h, double seconds, double step);
/* input_pending(): bytes of phonemes the rules have queued and the IRQ handler has not yet sent (4 a phoneme) */
ST_API int sth_queued(const st_host *h);
ST_API int sth_input_pending(const st_host *h);
/* speaking: the TSR's own flag, 1 from its first phoneme until its queue has run dry */
ST_API int sth_speaking(const st_host *h);
ST_API int sth_busy(const st_host *h, double quiet, double patience);
/* boot(limit): ST_RUN.EXE's "{ smartalk ready" and "Using conventional memory.", played out silently */
ST_API void sth_boot(st_host *h, double limit);
/* cancel(limit): Ctrl-X, the shut-up command; what was queued played out silently (up to `limit` s) */
ST_API void sth_cancel(st_host *h, double limit);
/* nonzero once the TSR has faulted (an unimplemented service, a halt, an 80186-only opcode) */
ST_API int sth_fault(const st_host *h);
ST_API const char *sth_error(const st_host *h);

/* the host's state, by name: doubles "cpu_ips", "last_speech", "say_time", "time" (read-only); ints "preparing",
   "request" (read-only), "control" (read-only: the port's control latch), "log_writes" (keep every chip write for
   sth_writes).  -1: unknown. */
ST_API double sth_get_double(const st_host *h, const char *name);
ST_API void sth_set_double(st_host *h, const char *name, double v);
ST_API int sth_get_int(const st_host *h, const char *name);
ST_API void sth_set_int(st_host *h, const char *name, int v);
ST_API int sth_writes(const st_host *h, const sth_write **writes);
ST_API void sth_clear_writes(st_host *h);

#ifdef __cplusplus
}
#endif
#endif
