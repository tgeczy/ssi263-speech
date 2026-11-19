/* sv_host.h -- the host of Speech Systems' Super Voice (1984): the speech cartridge for the Tandy Color Computer with
 * an SSI-263 at FF80h-FF84h, driven by its own text-to-speech, SUPERTOK (Del Software, "V5.0A"; the disk's file
 * SUPERTOK.BIN), on MAME's 6809 (../cpu/m6809_mame.cpp), in lockstep with our chip (../ssi263.h).
 *
 * Nothing of the Color Computer is used or needed: no BASIC ROM, no boot.  The firmware is ONE file, SV_FILE, exactly
 * as it is on the disk (a LOADM file: 00, length, load address 5800h, 10,225 bytes of program and dictionary, then
 * FF 00 00 and the entry).  Only the known file is accepted (SV_SHA256): the host calls into it at a fixed address.
 *
 * How the host drives it, as the manual's own example does (p.19: DEFUSR1=&H5805 : X$=USR1(PHRASE$)):
 *   a text    the program's "multi-line input" entry, 5805h, as Extended BASIC's USR calls it with a string: X = the
 *             string's descriptor (its length, a byte, its address, a byte), A nonzero.  The program copies the text
 *             to 02DDh on (BASIC's line buffer), speaks it -- converting as it goes, waiting for the chip's A/R by
 *             reading FF80h (D7 high: a request) before each phoneme -- and returns when the line is spoken.
 *             MEASURED on the program itself (2026-11-19, every instruction outside it trapped): on this path it
 *             calls no ROM routine and reads no variable of BASIC's; its other writes are three to the PIAs (FF01h,
 *             FF03h, FF23h: the computer's sound switch), which the host ignores.  (Its keyboard entries, 5800h and
 *             5807h, do call BASIC's line input and character output: not used here.)
 *   settings  the program's own control codes in the text, an up arrow (5Eh) and a letter (the manual, pp.21-22): ^Vn
 *             volume 0-15, ^Sn rate 1-16, ^ITn,m transitioned inflection (n 1-32, m 1-8), ^An articulation 1-8, ^Fn
 *             filter 1-255, ^C the defaults.  The host adds none: its caller writes them.
 *
 * The clocks.  SVH_E_HZ, 894,886.25 Hz, is the Color Computer's E clock (14.31818 MHz / 16), and the chip's clock is
 * taken to be the same.  INFERRED, not measured on a cartridge: the program's note table (72 pitch values at 5D09h
 * for its ^N command) is in tune, A = 55 Hz within 1.04 cents over its lowest octave, at that clock, and almost two
 * semitones sharp at 1 MHz.  Whether the cartridge feeds the chip E, or twice E with DIV2 high, the table cannot say;
 * for the chip model both are this number.
 *
 * The lockstep: the 6809 runs instruction by instruction, each E cycle a cycle of the chip's time; the chip is
 * brought up to the 6809's time at every access to it and at the end of each svh_run.  So the program finds A/R
 * when a real one would, to within one output sample.
 *
 * Numbers that are not the Super Voice's, labelled so:
 *   a text's start   from the call until the program first has to wait for the chip, the chip's time stands still:
 *                    a HOST feature, so speech starts at once.  On a Color Computer that stretch is 40 to 150 ms
 *                    (its set-up, a delay loop, the first word's conversion; in a freshly loaded program also the
 *                    dictionary's index).  From the first wait on nothing is skipped.  "skip_start" 0: nothing is.
 *   every text       finds the program as it was loaded.  The program keeps the last phoneme it wrote and, at its
 *                    next call, writes it while the chip's CTL bit is 1 -- so the chip's mode would come from the
 *                    duration bits of whatever a cut text was saying, and after a cut in a long pause (00) the chip
 *                    never requests again and the call never returns (MEASURED).  A text spoken to its end leaves
 *                    C0h there, and no trace but the control codes it set.  So control codes do not carry over
 *                    from one text to the next: a caller repeats its settings in each.
 *   cancel           the program has none.  The host leaves the call where it is and writes what the program's own
 *                    end writes: R3 with the amplitude 0, then R4 = FFh.  The cut phoneme is still in the chip, and
 *                    the next text's opening would give it its amplitude back (MEASURED: that text up to 82 ms late,
 *                    the old phoneme sounding first).  So before the text after a cut the chip is rested: CTL 1 and
 *                    0 again, a pause loaded, the chip's time run to that pause's end unheard (sv_host.c rest).
 *                    That text's first phoneme is then written at once, and it starts from silence whatever was
 *                    cut.
 *   a text's pitch   lands at once until its first phoneme: the opening's own pitch, and a pitch code at the text's
 *                    head.  The chip would glide there from where its counter was, the bottom of its range on a
 *                    new chip, and the first word climbed (heard).  The Mockingboard host's remedy, chosen by ear
 *                    there.  Inside a text the program's pitch codes glide as the chip glides them.
 *   where things are the string at 3000h, its descriptor at 3100h, the stack below 4000h, the host's return address
 *                    0002h: the host's choices, as BASIC would have made its own.
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef SV_HOST_H
#define SV_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define SV_API __declspec(dllexport)
#else
#define SV_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sv_host sv_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;
} svh_write;

#define SV_FILE "SUPERTOK.BIN"
#define SV_SHA256 "4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c"
#define SVH_E_HZ 894886.25                 /* inferred (above) */
/* "Each phrase is limited by BASIC to 240 characters, including spaces and control codes" (the manual, p.20).  The
   program itself took 255 here; the host keeps to what its makers wrote. */
#define SVH_MAX_TEXT 240
#define SVH_FAULT_STUCK 2                  /* "fault": the program waited for the chip longer than any phoneme lasts,
                                              or never reached its first wait */
#define SVH_FAULT_LEFT 3                   /* "fault": the program left its own code (5800h-7FF0h) */

SV_API sv_host *svh_create(const unsigned char *image, size_t n, ssi263 *chip, double out_rate, char *err, int errlen);
SV_API int svh_is_known(const unsigned char *image, size_t n);   /* 1 when these bytes are SV_FILE (SV_SHA256) */
SV_API sv_host *svh_create_dir(const char *dir, ssi263 *chip, double out_rate, char *err, int errlen);
SV_API void svh_destroy(sv_host *h);
SV_API ssi263 *svh_chip(sv_host *h);

/* One text (1-SVH_MAX_TEXT bytes, the program's own character set: ASCII, either case, its control codes): the call
   made and run up to its first wait for the chip.  1: speaking (or already done: a text of control codes alone);
   0: refused -- empty, too long, still busy, or a fault. */
SV_API int svh_say(sv_host *h, const unsigned char *text, int n);
/* `seconds` of time: the 6809 and the chip in step, the chip's audio at out_rate in an internal buffer valid until
   the next call.  Returns the sample count (-1: out of memory). */
SV_API int svh_run(sv_host *h, double seconds, const double **audio);
SV_API int svh_busy(const sv_host *h);       /* the call has not returned */
SV_API void svh_cancel(sv_host *h);          /* (above) */

/* State, by name.  Ints: "log_writes" (keep every chip write for svh_writes), "skip_start" (1; 0: the chip's time
   runs from the call on, as on the machine), "fault" (0, SVH_FAULT_STUCK, SVH_FAULT_LEFT: the last text's),
   "start_cycles" (the E cycles the last text ran before its first wait: what the host skipped, or would have),
   "undefined" (opcodes Motorola leaves unused, run since the host was made: none expected), "pitch_gap" (the chip's
   pitch counter from its target; the tests), "mem:HHHH" (that byte as the 6809 sees it; the tests).  -1: unknown. */
SV_API int svh_get_int(const sv_host *h, const char *name);
SV_API void svh_set_int(sv_host *h, const char *name, int v);
SV_API int svh_writes(const sv_host *h, const svh_write **writes);
SV_API void svh_clear_writes(sv_host *h);

#ifdef __cplusplus
}
#endif
#endif
