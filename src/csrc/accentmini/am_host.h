/* am_host.h -- the Accent-mini host in C: src/hosts/accent.py's Accent, line for line, on the bare PC of ../pc86
 * (MAME's 8086 with 1 MB): Aicom's DOS driver SPKEMS.DVC loaded as DOS would load it from CONFIG.SYS, its INIT,
 * an expanded-memory manager (INT 67h, LIM 3.2/4.0 as far as the driver asks, the page frame copied in and out), the
 * BIOS/DOS services it calls, the card (data port 3EFh, control port 3EEh, A/R on IRQ2 through a one-request PIC) and
 * an SSI-263 (../ssi263.h), with text printed to the driver's LPT through INT 17h as a screen reader did.
 *
 * Time, as accent.py: each slice the IRQ is offered, the chip runs (to its next A/R request, at most `step`; or
 * step / 4 while requesting) and the CPU runs max(100, int(cpu_ips x dt)) instructions -- cpu_ips 5,000,000 is a
 * COMPATIBILITY POLICY kept from the Unicorn host, not a clock (Astra, Reply 104).  Every rounding and order of
 * operations is Python's: run_until_request's sample count is int() (truncation), run's round() (half to even).
 * A driver call that blocks (an option command waiting for the speech buffer to drain) runs the card meanwhile and
 * keeps its audio for the next run (pending); a text may be left with the driver in the background (amh_say's
 * background = say(background=True)), run() carrying on with it step for step.
 *
 * The driver comes from the caller, as bytes or a file; never built in.  The chip: the caller's, or (chip NULL) one
 * made from the built-in defaults (ssi263_default_params), which is what the NVDA driver's SSI263C is.
 *
 * Errors: what accent.py raises (an unimplemented service or port, a call still waiting after its limit, an opcode the
 * 8086 runs differently from the 80186) makes the host FAULT: the call returns -1, amh_error says why, and every later
 * call returns -1 too.  The NVDA driver restarts the card then; so does am_voice.
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef AM_HOST_H
#define AM_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define AM_API __declspec(dllexport)
#else
#define AM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct am_host am_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} amh_write;

#define AMH_CPU_IPS 5000000.0              /* instructions per chip second: a policy (above), not a clock */

/* Loads the EXE-format driver at 0800:0000 with its relocations, and sets up the BIOS, the EMS manager and the card;
   INIT is not run yet (amh_init, amh_boot).  NULL on failure, the reason in err. */
AM_API am_host *amh_create(const unsigned char *dvc, size_t n, ssi263 *chip, double out_rate, char *err, int errlen);
AM_API am_host *amh_create_file(const char *dvc_path, ssi263 *chip, double out_rate, char *err, int errlen);
AM_API void amh_destroy(am_host *h);
AM_API ssi263 *amh_chip(am_host *h);

/* The driver's INIT request, as DOS makes it (DEVICE=SPKEMS.DVC).  Returns its status word, -1 on a fault. */
AM_API int amh_init(am_host *h);
/* Prints bytes to the Accent's LPT (INT 17h per character).  speech: 1 = text to be spoken (busy's patience waits
   for its first phoneme), 0 = commands only, -1 = decide as accent.py does on the bytes (Latin-1: a letter or digit
   outside ESC commands).  background: a call still waiting after its budget is left with the driver and amh_run
   carries it on.  0, or -1 on a fault. */
AM_API int amh_say(am_host *h, const unsigned char *bytes, int n, int speech, int background);
/* `seconds` of chip time in `step` slices (0.0005 s): the chip's audio at out_rate (what a blocked call made first),
   in an internal buffer valid until the next call.  Returns the sample count, -1 on a fault. */
AM_API int amh_run(am_host *h, double seconds, double step, const double **audio);
/* The index marks the audio amh_run just returned has passed.  With ESC Y X sent, a Ctrl-F (06h) in the text is the
   driver's index marker (its manual, 3.2.4): it counts each one when the word before it has ended.  The count is the
   LPT Status Read's DH (INT 17h AH=2, after ESC Y Z); the host does not make that call -- the driver, called while
   the chip's request waits, writes the chip itself and delays the speech (measured), and it holds the count back
   while an option command waits -- but reads the driver's own count, a byte of its data, after every slice.  The
   marks carry no name: they are the next ones in the order they were sent.  At most AMH_REPLIES a run;
   amh_get_int "marks" counts every one passed since amh_set_int "marks" 0, which also forgets those not yet
   given; "marks_known" is 0 for a driver image whose count the host does not know (no mark is then given).  A
   flush (amh_cancel) drops the marks of the text it cut: none of them is given. */
typedef struct {
    int sample;                            /* samples of the run's audio made when it was passed */
} amh_reply;
#define AMH_REPLIES 64
AM_API int amh_replies(const am_host *h, const amh_reply **out);
/* The driver runs with the chip keeping time but making no sound (step 0.002 s); returns the time run, -1 on a fault. */
AM_API double amh_skip(am_host *h, double seconds, double step);
/* 1 while the Accent is still busy with what it was given (accent.py's quiet 0.03 s, patience 1.5 s) */
AM_API int amh_busy(const am_host *h, double quiet, double patience);
/* the driver's own flag on the card: its interrupt enabled (C0h) while it speaks, 40h the moment it is done */
AM_API int amh_speaking(const am_host *h);
/* INIT, then ESC =F (a carriage return starts speech), ESC =M (Ctrl-X flushes at once) and Ctrl-X, the greeting let
   play out silently (limit 4 s).  Returns the time run, -1 on a fault. */
AM_API double amh_boot(am_host *h, double limit);
/* Ctrl-X, the Accent's flush: a text the driver is still taking stops after its current character, then what it had
   sent plays out silently (limit 0.6 s).  Returns the time run, -1 on a fault. */
AM_API double amh_cancel(am_host *h, double limit);

AM_API const char *amh_error(const am_host *h);    /* why the host faulted; "" while it has not */

/* The host's state, by name.  Doubles: "cpu_ips", "last_speech", "say_time", "time" (the chip's; read-only).  Ints:
   "preparing", "log_writes" (keep every write for amh_writes), "control", "in_service", "irq_latched", "call" (a
   background call running), "settles", "request" (read-only), "marks", "marks_known" (amh_replies).  -1: unknown. */
AM_API double amh_get_double(const am_host *h, const char *name);
AM_API void amh_set_double(am_host *h, const char *name, double v);
AM_API int amh_get_int(const am_host *h, const char *name);
AM_API void amh_set_int(am_host *h, const char *name, int v);
/* the tests' control: 1 takes a background call waiting past its limit for stalled even while the card speaks */
AM_API extern int amh_call_stall_break;
/* the tests' control: 1 takes a running call's CX slot in the driver's own stack segment, as it was */
AM_API extern int amh_drop_ss_break;
AM_API int amh_writes(const am_host *h, const amh_write **writes);   /* with "log_writes": drain it, or it grows */
AM_API void amh_clear_writes(am_host *h);

#ifdef __cplusplus
}
#endif
#endif
