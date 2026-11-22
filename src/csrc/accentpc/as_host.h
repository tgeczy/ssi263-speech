/* as_host.h -- the Accent-PC host in C: src/hosts/accent_pc.py's AccentPC, line for line (its chip-time lockstep, say,
 * busy, boot and cancel), around the board (as_board.h) and an SSI-263 (../ssi263.h).
 *
 * The lockstep, as accent_pc.py: each slice, one byte into the 8251 if it is free, the TRAP gate, then the chip runs
 * (to its next A/R request, at most `step`; or step / 4 while requesting) and the 8085 runs as many T-states as that
 * chip time holds.  Every rounding and order of operations is Python's: run_until_request's sample count is int()
 * (truncation), run's is round() (half to even: nearbyint), T-states are int() of cpu_hz x dt [x turbo], at least 20.
 *
 * Two of its numbers are not the Accent-PC's, and are labelled so:
 *   cpu_hz  3,072,000: a GUESS (a 6.144 MHz crystal, halved by the 8085); the unit's clock has not been measured.
 *   turbo   8: a HOST feature, not the unit's: while the firmware reads a sentence, before its first phoneme, the 8085
 *           runs that many times faster.  Speech itself is paced by A/R, so it is unchanged.
 *
 * The ROMs come from the caller -- a folder (ash_create_dir: u2.BIN, u3.BIN, u4.BIN) or memory (ash_create) -- and are
 * never built in.  The chip: the caller's (Python's SSI263C: ssi263.dll is linked, so both use the one copy loaded),
 * or, with chip NULL, one the host makes from the built-in defaults (ssi263_default_params) and frees.
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef AS_HOST_H
#define AS_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define AS_API __declspec(dllexport)
#else
#define AS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct as_host as_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} ash_write;

#define ASH_CPU_HZ 3072000.0               /* a GUESS (above) */
#define ASH_TURBO 8.0                      /* a host feature (above) */

AS_API as_host *ash_create(const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                           const unsigned char *u4, size_t n4, ssi263 *chip, double out_rate, char *err, int errlen);
AS_API as_host *ash_create_dir(const char *rom_dir, ssi263 *chip, double out_rate, char *err, int errlen);
AS_API void ash_destroy(as_host *h);
AS_API ssi263 *ash_chip(as_host *h);

/* Bytes down the serial line.  speech: 1 = text to be spoken (the host speeds the 8085 up until its first phoneme and
   waits for it: busy's patience), 0 = commands only, -1 = decide as accent_pc.py does (a letter or digit outside the
   ESC commands, Latin-1).  -1 decides on the bytes given: accent_pc.py decides on the text before it is encoded, so a
   caller with text outside Latin-1 (a letter that reaches the Accent as "?") decides itself, as accent_pc_c.py does.
   1, or 0 when out of memory (nothing taken). */
AS_API int ash_say(as_host *h, const unsigned char *bytes, int n, int speech);
/* `seconds` of chip time in `step` slices (0.0005 s): the chip's audio at out_rate, in an internal buffer valid until
   the next call.  Returns the sample count (-1: out of memory). */
AS_API int ash_run(as_host *h, double seconds, double step, const double **audio);
/* The bytes the unit sent on its serial line during the last ash_run (and since the run before it), each with the
   samples of that run's audio made when it came (to the host's slice): with the index marks on (as_voice.h) each
   is a 06h, the unit's prompt for a mark speech has reached.  At most ASH_REPLIES a run. */
typedef struct {
    int value;                             /* the byte */
    int sample;                            /* samples of the run's audio made when it came */
} ash_reply;
#define ASH_REPLIES 64
AS_API int ash_replies(const as_host *h, const ash_reply **out);
/* The firmware runs with the chip keeping time but making no sound (step 0.002 s); returns the time run. */
AS_API double ash_skip(as_host *h, double seconds, double step);
/* 1 while the Accent is still busy with what it was given (quiet 0.03 s, patience 1.5 s: accent_pc.py's) */
AS_API int ash_busy(const as_host *h, double quiet, double patience);
AS_API int ash_speaking(const as_host *h); /* the firmware's own flag: A/R gated onto TRAP (port 40h bit 4) */
/* Power-up (limit 4 s): the RAM test and set-up, then ESC =F (a carriage return starts speech), ESC =M (Ctrl-X
   flushes at once) and Ctrl-X, so the "Accent ready." greeting is flushed rather than sat through.  Returns the time
   run. */
AS_API double ash_boot(as_host *h, double limit);
/* Ctrl-X, the Accent's flush (limit 0.6 s); what was sent is let play out silently.  Returns the time run. */
AS_API double ash_cancel(as_host *h, double limit);

/* the host's state, by name.  Doubles: "cpu_hz", "turbo", "tick_hz" (RST 7.5's clock, 0 = none), "last_speech",
   "say_time", "time" (the chip's; read-only).  Ints: "work" (read-only: the instructions the
   firmware ran outside its waiting loops in the last ash_run -- reading text, making phonemes; as_board_take_work), "preparing", "log_writes" (keep every write for ash_writes),
   "request" (read-only), and as_board_get's names ("switches" and "python_slices" settable).  -1 / 0: unknown. */
AS_API double ash_get_double(const as_host *h, const char *name);
AS_API void ash_set_double(as_host *h, const char *name, double v);
AS_API int ash_get_int(const as_host *h, const char *name);
AS_API void ash_set_int(as_host *h, const char *name, int v);
AS_API int ash_writes(const as_host *h, const ash_write **writes);   /* with "log_writes": drain it, or it grows */
AS_API void ash_clear_writes(as_host *h);
AS_API int ash_tx(const as_host *h, const unsigned char **bytes);    /* every byte the firmware sent */
/* accent_pc.py's rule for say(speech=None) on Latin-1 bytes: 1 if a letter or digit is left outside ESC commands */
AS_API int ash_is_speech(const unsigned char *bytes, int n);

/* ---- another board under this host ----------------------------------------------------------------------------
   The lockstep, say, busy, boot and cancel above ask only this of a board; ash_create gives them the Accent-PC's
   (as_board.h).  The Accent SA's Z80 board (../accentz80/az_host.h) is the other: the same firmware design on
   another processor, behind the same serial line. */
typedef struct {
    void (*destroy)(void *board);
    void (*serial)(void *board);           /* one byte into the 8251 when it is free and RTS is up */
    void (*request_line)(void *board);     /* A/R to the CPU as the board gates it (the PC card: TRAP) */
    void (*tick)(void *board);             /* the PC card's RST 7.5 clock ("tick_hz"); NULL: the board has none */
    unsigned long long (*run)(void *board, unsigned long long t_states);
    int (*send)(void *board, const unsigned char *bytes, int n);
    void (*drop_input)(void *board);
    int (*tx)(const void *board, const unsigned char **bytes);
    int (*get)(const void *board, const char *name);   /* "rx", "rx_ready" and "speaking" at least */
    void (*set)(void *board, const char *name, int v);
    unsigned (*take_work)(void *board);    /* instructions outside the firmware's waiting loops, then 0 */
} ash_board;
/* The host with no board yet (cpu_hz: the board's clock; the chip as ash_create's), the chip as a board's callbacks
   reach it (ctx = the host), and the board given: the host owns it from then on.  Nothing else may be called
   between ash_create_bare and ash_attach but ash_destroy. */
AS_API as_host *ash_create_bare(ssi263 *chip, double out_rate, double cpu_hz, char *err, int errlen);
AS_API void ash_chip_write(void *host, int reg, int val);
AS_API int ash_chip_request(void *host);
AS_API void ash_attach(as_host *h, const ash_board *ops, void *board);
/* The board under the host (ash_create's: an as_board *), for a board's own tools. */
AS_API void *ash_board_of(as_host *h);

#ifdef __cplusplus
}
#endif
#endif
