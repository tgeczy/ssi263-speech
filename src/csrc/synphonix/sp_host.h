/* sp_host.h -- Artic Technologies' SynPhonix 215 in C: src/hosts/synphonix.py, line for line, on MAME's 8086 (../pc86).
 * The card's text-to-speech is Artic's DOS TSR VEST.COM 1.60 ("Visions Enhanced Speech Translator", the "Special
 * Release For Artic Visions" that Business Vision 3.08 loads first), loaded as DOS would with no switches (IRQ 7) until
 * it stays resident, in a bare PC that stands in for DOS and the BIOS: the card's key at 2E0h-2E5h, the chip's
 * registers eight above (each byte VEST writes rotated left one bit: the card's wiring), its A/R on IRQ 7 with a
 * specific EOI, and text in as Business Vision gives it -- a far call, after PUSHF, to the entry VEST's signature
 * leads to, AL=7 to speak.  The card's lines are synphonix.py's (as read from VEST and confirmed by it speaking).
 *
 * Each copy of VEST holds the serial of the card it was sold with: the host gives the emulated card the key the copy
 * expects, read from the copy's own serial check (synphonix.py card_key), so any copy of VEST 1.60 finds its card and
 * no serial is written here.
 *
 * Where it differs from synphonix.py: nowhere on purpose.  SYNPHONIX_HOST_BREAK (key, rotate, eoi, cancel: the Python
 * host's must-fail controls) is read here too, at sph_create; "keycache" is the Python host's alone (a per-process
 * cache this host does not have).
 *
 * The program comes from the caller and is never built in.  cpu_ips 5,000,000 is the Python host's coupling of
 * instructions to chip time, not a clock.  Not thread-safe per instance.  MIT.
 */
#ifndef SP_HOST_H
#define SP_HOST_H

#include <stddef.h>
#include "../ssi263.h"

#if defined(_WIN32)
#define SP_API __declspec(dllexport)
#else
#define SP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sp_host sp_host;

typedef struct {
    double t;                              /* the chip time the write was applied at */
    int reg, val;                          /* the chip's value: VEST's byte rotated back */
} sph_write;

#define SPH_CPU_IPS 5000000.0              /* synphonix.py's cpu_ips */
#define SPH_BOOT_LIMIT 10.0                /* boot()'s limit (synphonix.py) */
#define SPH_SAY_CX 0x0200                  /* VEST's own announcement's offsets (9EB5): rate +2 ... */
#define SPH_SAY_DX 0x00E6                  /* ... and filter E6h: say()'s defaults */
#define SPH_FILE "artic-synphonix/VEST.COM"

/* VEST.COM's bytes; NULL with the reason in err */
SP_API sp_host *sph_create(const unsigned char *com, size_t n, ssi263 *chip, double out_rate, char *err, int errlen);
SP_API void sph_destroy(sp_host *h);
SP_API ssi263 *sph_chip(sp_host *h);

/* The key a SynPhonix 215 matching this VEST gives at 2E0h-2E5h (synphonix.py card_key): 0, or -1 with the reason */
SP_API int sph_card_key(const unsigned char *com, size_t n, unsigned char key[6], char *err, int errlen);

/* say(): text (bytes, at most 16 KB) spoken with AL=7: CH, CL, DH, DL the rate, amplitude, pitch and filter offsets
   VEST adds to its frames (SPH_SAY_CX, SPH_SAY_DX: its own).  The call returns once VEST has queued the text; while
   its queue is full the chip runs, and that audio is what the next sph_run returns first. */
SP_API void sph_say(sp_host *h, const unsigned char *bytes, int n, int cx, int dx);
/* say with VEST's option byte as well (AH with AL=7): bit 6 flat inflection, bits 0-3 the caller's exception
   dictionaries (none here), bit 7 queue nothing.  sph_say is sph_say_ah with AH 0.  (Beyond synphonix.py, whose say()
   passes AH 0: the voice's, sp_voice.h.) */
SP_API void sph_say_ah(sp_host *h, const unsigned char *bytes, int n, int ah, int cx, int dx);
/* frames VEST has queued and not yet sent: its own pointers (190h in, 192h out) in its ring (194h start, 196h end:
   its queue routines at 0AC1 and 0D5E wrap there), 5 bytes a frame.  (Beyond synphonix.py: read, never written.) */
SP_API int sph_queued_frames(const sp_host *h);
/* run(): `seconds` of chip time in `step` slices: the chip's audio at out_rate, in an internal buffer valid until the
   next call (what a blocked say made first).  Returns the sample count (-1: a fault). */
SP_API int sph_run(sp_host *h, double seconds, double step, const double **audio);
/* skip(): VEST runs with the chip keeping time but making no sound; returns the time run (-1: a fault) */
SP_API double sph_skip(sp_host *h, double seconds, double step);
/* cancel(): AL=1, cut speech: the queue emptied and a silent R0 */
SP_API void sph_cancel(sp_host *h);
/* any other call: AX (AL the function), the registers it reads; returns AX after it (-1: a fault) */
SP_API int sph_call(sp_host *h, int ax, int bx, int cx, int dx);
/* queued(): frames VEST has queued and its IRQ handler has not yet sent */
SP_API int sph_queued(const sp_host *h);
/* busy(quiet): frames queued, the handler not yet idle, or a phoneme within the last `quiet` seconds */
SP_API int sph_busy(const sp_host *h, double quiet);
/* boot(limit): VEST's own start-up announcement, played out silently */
SP_API void sph_boot(sp_host *h, double limit);
/* nonzero once VEST has faulted (an unimplemented service, a halt, an 80186-only opcode) */
SP_API int sph_fault(const sp_host *h);
SP_API const char *sph_error(const sp_host *h);

/* the host's state, by name: doubles "cpu_ips", "last_speech", "time" (read-only); ints "log_writes" (keep every
   chip write for sph_writes), "request" (read-only), "segment" and "entry" (read-only: where VEST was found).
   -1: unknown. */
SP_API double sph_get_double(const sp_host *h, const char *name);
SP_API void sph_set_double(sp_host *h, const char *name, double v);
SP_API int sph_get_int(const sp_host *h, const char *name);
SP_API void sph_set_int(sp_host *h, const char *name, int v);
SP_API int sph_writes(const sp_host *h, const sph_write **writes);
SP_API void sph_clear_writes(sp_host *h);

#ifdef __cplusplus
}
#endif
#endif
