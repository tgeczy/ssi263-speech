/* so_host.h -- the Speak-Out host in C: src/hosts/speakout.py's SpeakOutV40 on the "mame-steps" core (the add-on's
 * default since 0.7), line for line (its chip-time lockstep, say, busy, boot, cancel and skip), around the board
 * (so_board.h) and an SSI-263 (../ssi263.h).
 *
 * The lockstep, as speakout.py: each slice, the board's offer (the chip's A/R on IR4 first, else a serial byte on
 * IR1), then the chip runs (to its next A/R request, at most `step`; or step / 4 while requesting) and the V40 runs
 * max(200, int(cpu_ips x dt)) instructions as Unicorn counts them (so_run_steps_unicorn); the chip's writes are then
 * applied in order.  Every rounding and order of operations is Python's: run_until_request's sample count is int()
 * (truncation), run's is round() (half to even: nearbyint), dt is at least 1e-5.
 *
 * cpu_ips 1,500,000 is the host's established coupling of instructions to chip time, not a measured clock.
 *
 * The firmware (SPEAKOUT.HEX) comes from the caller -- a path or its Intel HEX text -- and is never built in.  The
 * chip: the caller's, or, with chip NULL, one the host makes from the built-in defaults (ssi263_default_params) and
 * frees.  Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef SO_HOST_H
#define SO_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define SO_API __declspec(dllexport)
#else
#define SO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct so_host so_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} soh_write;

#define SOH_CPU_IPS 1500000.0              /* speakout.py's cpu_ips (above) */
#define SOH_QUIET 0.06                     /* busy()'s defaults */
#define SOH_PATIENCE 1.5

SO_API so_host *soh_create(const char *hex, size_t len, ssi263 *chip, double out_rate, char *err, int errlen);
SO_API so_host *soh_create_file(const char *path, ssi263 *chip, double out_rate, char *err, int errlen);
SO_API void soh_destroy(so_host *h);
SO_API ssi263 *soh_chip(so_host *h);

/* say(): bytes down the serial line (Latin-1).  Text with a letter or digit (Python's isalnum, the bytes under 32
   left out) that does not start with ^E is speech: until its first phoneme the box counts as busy (patience). */
SO_API void soh_say(so_host *h, const unsigned char *bytes, int n);
/* run(): `seconds` of chip time in `step` slices (0.0005 s): the chip's audio at out_rate, in an internal buffer
   valid until the next call.  Returns the sample count (-1: out of memory). */
SO_API int soh_run(so_host *h, double seconds, double step, const double **audio);
/* The bytes the box sent back on its serial line during the last soh_run -- its replies to index marks (so_voice.h:
   05h 5Bh xx in the text comes back as xx when speech reaches it) -- each with the samples of that run's audio made
   when it came (to the host's slice, 0.5 ms).  At most SOH_REPLIES are kept a run; soh_get_int "tx_count" counts
   every byte ever sent back, those outside a run too. */
typedef struct {
    int value;                             /* the byte */
    int sample;                            /* samples of the run's audio made when it came */
} soh_reply;
#define SOH_REPLIES 64
SO_API int soh_replies(const so_host *h, const soh_reply **out);
/* skip(): the firmware runs with the chip keeping time but making no sound (step 0.002 s); returns the time run. */
SO_API double soh_skip(so_host *h, double seconds, double step);
/* input_pending(): text not yet taken in, or taken in and not yet spoken by the rules */
SO_API int soh_input_pending(const so_host *h);
/* busy(quiet, patience): input pending, a speech phoneme loaded within `quiet` s, or a line not yet started (up to
   `patience` s) */
SO_API int soh_busy(const so_host *h, double quiet, double patience);
/* boot(seconds): power on (run, the audio discarded), the greeting cancelled, run 0.2 s */
SO_API void soh_boot(so_host *h, double seconds);
/* cancel(limit): input dropped, Ctrl-X, what was queued played out silently (up to `limit` s) */
SO_API void soh_cancel(so_host *h, double limit);
/* nonzero once the V40 has faulted, halted or met an undefined opcode */
SO_API int soh_fault(const so_host *h);

/* the host's state, by name.  Doubles: "cpu_ips", "last_speech", "say_time", "time" (the chip's; read-only).
   Ints: "preparing", "request" (read-only), "steps" (read-only, the V40's), "log_writes" (keep every chip write for
   soh_writes, as am_host and as_host do: the tests watch the chip through it).  -1: unknown. */
SO_API double soh_get_double(const so_host *h, const char *name);
SO_API void soh_set_double(so_host *h, const char *name, double v);
SO_API int soh_get_int(const so_host *h, const char *name);
SO_API void soh_set_int(so_host *h, const char *name, int v);
SO_API int soh_writes(const so_host *h, const soh_write **writes);   /* with "log_writes": drain it, or it grows */
SO_API void soh_clear_writes(so_host *h);

#ifdef __cplusplus
}
#endif
#endif
