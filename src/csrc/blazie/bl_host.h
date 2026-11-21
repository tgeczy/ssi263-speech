/* bl_host.h -- the Braille Lite host in C: src/hosts/blazie.py's lockstep, cancel and ^F bookkeeping, line for line,
 * around the library board (bl_board.h) and an SSI-263 made by ssi263.dll (the caller owns it).
 *
 * The unit's writes are applied to the chip at the chip's current time after each CPU run; the chip's A/R request
 * goes back before the next.  nvda/tools/golden/blazie_*.txt gates it through src/hosts/native_blazie.py.
 */
#ifndef BL_HOST_H
#define BL_HOST_H

#include "../ssi263.h"
#include "bl_serial.h"
#include "bl_idle.h"
#include "bl_board.h"

#if defined(_WIN32)
#define BL_API __declspec(dllexport)
#else
#define BL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bl_host bl_host;

typedef struct {
    double t;                      /* the chip time the write was applied at */
    int reg, val;
} bh_write;

/* Boots the unit (keys at those instruction counts, then live mode at boot_instr).  board_hz <= 0: no board
   low-pass.  log_writes: keep every SSI-263 write (bh_writes) for tests -- drain it, or it grows.  NULL on failure,
   the reason in err. */
BL_API bl_host *bh_create(const char *firmware, const char *state, ssi263 *chip, double out_rate, double board_hz,
                          const unsigned long long *key_at, const unsigned char *key_val, int n_keys,
                          unsigned long long boot_instr, int log_writes, char *err, int errlen);
BL_API int bh_writes(const bl_host *h, const bh_write **writes);
BL_API void bh_clear_writes(bl_host *h);
BL_API void bh_destroy(bl_host *h);

/* bytes for the unit, counting ^F; and a say: the lines and flush, as say() builds them.  While a run-ahead utterance
   plays (below) both are held and delivered in order when it ends.  1, or 0 when out of memory (nothing taken), or
   -1 refused over a fault (below; nothing taken). */
BL_API int bh_send(bl_host *h, const unsigned char *data, int n);
BL_API int bh_say(bl_host *h, const unsigned char *data, int n);
BL_API int bh_owed(const bl_host *h);
/* 1 while the unit is still speaking what it was given, 0 when done; -1 on a fault (Astra, Reply 112), before
   anything else, held input included: a run-ahead utterance failed (out of memory: its script is incomplete), or the
   board lost a chip write or serial byte (out of memory).  A fault is sticky: input is refused and held input is not
   delivered (no new utterance starts over it) until bh_cancel, the explicit recovery ("fault": which). */
BL_API int bh_busy(const bl_host *h, double quiet, double patience);
/* also clears a fault: the utterance, held input and a failed script abandoned, the unit given its ^X */
BL_API double bh_cancel(bl_host *h, double limit, double quiet, double cut);   /* quiet, cut < 0: the defaults */
BL_API double bh_skip(bl_host *h, double seconds);
/* Runs `seconds` of chip time in `step` lockstep (0.0005 s): the audio after the board pole and the whine, in an
   internal buffer valid until the next call. */
BL_API int bh_run(bl_host *h, double seconds, double step, const double **audio);

/* whine: 0 off, 1 hiss, 2 whine */
BL_API void bh_set_whine(bl_host *h, int mode);
BL_API int bh_get_whine(const bl_host *h);

/* The emulator's idle channel (bl_idle.h): the measured noise and lines at their absolute level, the pop when the
   firmware opens the channel, the click when it clicks it off, the 10 Hz tick, and when the channel is heard.  While
   set it replaces bh_set_whine's hiss/whine; NULL turns it off again (the default: the screen-reader drivers never set
   it).  0 if out of memory. */
BL_API int bh_set_idle(bl_host *h, const bl_idle_options *o);

/* the host's state, as blazie.py keeps it (tests and the Python wrapper read and some set these).
   "run_ahead" (int, 0 = off, the default; EXPERIMENTAL, opt-in): from the next bh_say, each utterance is captured
   with the unit run ahead of the chip -- a bounded streaming capture, up to RA_AHEAD segments ahead -- and played from
   the script (run_ahead.h).  Its end is inferred from bh_owed's ^F echo accounting and a quiet interval: a policy,
   not a proof.  bh_busy follows run_ahead.h's completion: busy until the capture has ended by that policy, every
   write has played and the speech has ended (a pause the final load, ended: the chip's request alone is not
   silence); a limit hands over to the lockstep's own judgement (a spoken final load held: its UNANSWERED_S counts
   from that load's request); an allocation failure is -1.  Input given meanwhile is held (bh_say).  The capture's other effects -- the unit's serial bytes,
   ^F echoes, XON/XOFF, its RAM -- happen at the capture's frontier, ahead of the listener: a cancel cannot take them
   back (nvda/tools/run_ahead_state.py compares them).  bh_cancel over a running capture first lets the unit, parked
   mid-routine at the frontier, run on to a wait for an interrupt (run_ahead.h ra_settle), and holds its A/R not
   requesting over the first ^X slice: before that, cancelled text could lead the next utterance
   (nvda/tools/run_ahead_cancel.py).  Timing is emulated Z180 time, not a chip-bus measurement.
   The pipe host has no such mode.  Read-only: "run_ahead_state" (RA_*), "run_ahead_end" (RA_END_*),
   "run_ahead_played" / "run_ahead_captured" (writes), "run_ahead_settled" / "run_ahead_dropped" (the last cancel's
   ra_settle), "run_ahead_held" (the last utterance ended at its bound, a spoken final load held), "held" (inputs
   waiting), "port_a0", "model" (bl_model: BL_MODEL_*), "fault" (BH_FAULT_*: 1 the run-ahead script, 2 a board
   event).  Read and reset by the caller: "tx_lost", "writes_lost" (bytes of bh_tx, writes of bh_writes the host could not keep: records, not a fault).
   "cancel_settle" (3 = both bits, the release default; 0 restores the old race for tests):
   the lockstep's bh_cancel lets the unit run on, A/R not requesting and its chip writes dropped, to a wait for an
   interrupt before its ^X (bit 0), and holds A/R not requesting over the first ^X slice (bit 1); read-only
   "cancel_settled" (1 idle, 0 the cap, -1 not run) and "cancel_dropped" (its writes) for the last cancel.
   "flash_timed" (write only; 0 = off, the default): the file flash's busy time (bl_board.h bl_flash_timed), which the
   emulator turns on; read-only "flash_busy" and "flash_chip_erases" (bl_flash_busy).
   "double_speed" (0 = off, the default: bl_board.h bl_set_double_speed), read-only "double_speed_now" and "ccr";
   "latch_race_ns" (the SSI-263's latch time, bl_board.h BL_LATCH_RACE_US, in ns; 0 off: tests), "latch_gap_ns" (the
   widest gap it catches, BL_LATCH_GAP_US) and read-only "latch_catches" (the writes it caught).
   Tests only: "run_ahead_break" (RA_BRK_*), "log_ar" (the A/R edges given to the unit, reg 8, and the run-ahead
   segments' openings, reg 9 + how, in the write log), and one failure each, as memory running out would cause it:
   "fail_alloc_size" (run_ahead.c's next allocation of that many bytes), "fail_event" / "fail_tx" / "fail_log" (the
   n-th board event, transmitted byte, logged write from now). */
BL_API int bh_get_int(const bl_host *h, const char *name);
BL_API void bh_set_int(bl_host *h, const char *name, int v);
/* tests: the current (or last) run-ahead script, as run_ahead.h's ra_write array; its length */
BL_API int bh_script(const bl_host *h, const void **writes);
/* tests, lane 1: the next run-ahead utterance's writes at these chip times (copied); 0 when out of memory */
BL_API int bh_pace(bl_host *h, const double *t, int n);
/* tests: the unit's state (bl_board.h's bl_probe, bl_memory) for comparing two units at a checkpoint */
BL_API void bh_probe(const bl_host *h, bl_probe *p);
BL_API int bh_memory(const bl_host *h, int which, const unsigned char **bytes);
/* the program flash (bl_board.h bl_program_flash): bank a then bank b, 512 KB; the emulator keeps an updated bank */
BL_API long bh_program_flash(bl_host *h, unsigned char **data, unsigned long *writes);
BL_API int bh_braille(const bl_host *h, unsigned char *cells, int capacity);
BL_API void bh_braille_bars(bl_host *h, int down);
BL_API double bh_get_double(const bl_host *h, const char *name);
BL_API void bh_set_double(bl_host *h, const char *name, double v);
BL_API int bh_tx(const bl_host *h, const unsigned char **bytes);   /* every byte the unit sent back */
/* The ^F echoes of the last bh_run, for a caller that uses the unit's echo as an index mark (bl_voice.h): each
   with its number ("echo_f" once it had come) and the sample of that run's audio at which it came.  At most
   BH_ECHOES are kept a run (a 30 ms run sees one or two); "echo_f" itself is never short.  Not for the run
   ahead mode: there the echoes come at the capture's frontier, ahead of the audio. */
typedef struct {
    int n;                                 /* its number: the value of "echo_f" with this echo counted */
    int sample;                            /* samples of the run's audio made when it came */
} bh_echo;
#define BH_ECHOES 64
BL_API int bh_echoes(const bl_host *h, const bh_echo **out);
BL_API void bh_clear_tx(bl_host *h);        /* forget them (a program that never reads them: the emulator app) */
/* the serial port carried to a real port (bl_board.h's bl_serial_*); the drivers never attach it */
BL_API int bh_serial_attach(bl_host *h, int on);
BL_API int bh_serial_write(bl_host *h, const unsigned char *bytes, int n);
BL_API int bh_serial_space(const bl_host *h);
BL_API int bh_serial_read(bl_host *h, unsigned char *out, int cap, bl_serial_status *status);
BL_API int bh_key(bl_host *h, int chord);   /* a braille chord pressed live (bl_key) */
BL_API void bh_battery(bl_host *h, int level);   /* the battery gauge's reading (bl_battery) */
BL_API int bh_save_state(const bl_host *h, const char *path);   /* bl_save_state: 1 on success */
/* the emulator's (bl_board.h's bl_keys_down and bl_clock_*): keys held down, the clock controller */
BL_API void bh_keys_down(bl_host *h, int bits);
BL_API int bh_clock_on(bl_host *h, const blc_time *now, long long unix_now);
BL_API extern int bh_clock_hurry_break;     /* the tests' control: the turbo's cycles counted as clock time */
BL_API extern int bh_lowpass_break;         /* the tests' control: the board low-pass kept above Nyquist (#81) */
BL_API int bh_clock_time(const bl_host *h, int alarm, blc_time *t);
BL_API void bh_clock_wall(bl_host *h, long long unix_now);
/* time the unit's clock kept while the host ran nothing (a suspend), added (#86) */
BL_API void bh_clock_advance(bl_host *h, long long seconds);
BL_API int bh_starts(const bl_host *h);     /* bl_starts: the firmware's starts so far */

#ifdef __cplusplus
}
#endif
#endif
