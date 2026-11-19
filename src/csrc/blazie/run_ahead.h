/* run_ahead.h -- an experimental, opt-in mode: a unit run ahead of its chip, for any board whose CPU drives an
 * SSI-263 and answers its A/R request (the Braille Lite's bl_board, the Type 'n Speak's tns_board, or another unit on
 * a Z180 or anything else).  It knows the chip's side only: which writes load a phoneme, when the chip requests the
 * next one.  Nothing of any firmware.  (Astra, Reply 107: what it is, what it is not, and its contract.)
 *
 * What it is: a BOUNDED STREAMING capture, not a whole-utterance drain.  The CPU runs flat out against a stand-in chip
 * that acknowledges every phoneme load (raises A/R) RA_ACK_S of CPU time later, and it is run only until the capture
 * holds RA_AHEAD segments (acknowledgements) beyond the one being played.  Each write is kept with its CPU cycle and
 * the number of acknowledgements before it (its segment).
 *
 * Replay: the real chip plays the script in real time.  Segment k starts at the chip's own k-th request, and each of
 * its writes follows at its CPU distance from the k-th acknowledgement: the unit's answer to a request, at the speed
 * this emulator gives it (emulated Z180 time: NOT a measurement of a real unit's chip bus).  Lane 2's deliberate
 * retiming (nvda/tools/run_ahead_lanes.py): a segment that opens after a pause load (code 00), or the utterance's
 * first, and whose first write came more than RA_READ_S after its acknowledgement (the unit was reading its next line,
 * as far as the English and Spanish corpora show) is moved up to RA_LAT (the unit's last in-speech answer), and after
 * a pause it does not wait for that pause to end either.  An answer slower than RA_READ_S after a spoken phoneme is
 * kept as it is: it is not "reading" (test_run_ahead.c's slow-service and threshold fixtures).
 * Lane 1 (ra_pace): the script at a given schedule instead -- the lockstep's own -- for timestamp and PCM identity.
 *
 * The end of the capture is INFERRED, not proven: it ends when the board says nothing more is owed (the Braille Lite
 * host: its ^F echo accounting, bh_owed) and the unit has written nothing for RA_QUIET_S of CPU time -- a policy.  If
 * the unit still owes speech after RA_LIMIT_S without a write, the capture ends as a LIMIT, never as a success.
 *
 * Completion (ra_state), four facts kept apart:
 *   capture exhausted  the capture ended by the quiet policy above (RA_END_QUIET)
 *   capture limit      it ended at RA_LIMIT_S with speech still owed (RA_END_LIMIT), or on an allocation failure
 *                      (RA_END_ALLOC: an explicit error; the script is never silently shortened)
 *   playback exhausted every captured write has been applied to the chip
 *   speech ended       ... and the chip is silent by what it was given: the final load was a pause (code 00), and it
 *                      has ended (the chip requests).  Only this, after a capture exhausted by the policy, is
 *                      RA_COMPLETE.  (Nothing loaded at all counts too: nothing of the utterance sounds.)
 * The chip's request is NOT the end of the sound (Astra, Reply 112): it is permission for the next load.  A spoken
 * phoneme sounds on after its timer ends, until another load replaces it -- so a final load that is spoken, its timer
 * ended, the chip requesting, is still sounding: RA_FINAL_LOAD, "held".  The Braille Lite never ends so (both corpora:
 * every utterance's last spoken phoneme is answered at its request with a pause, measured 82-117 ms after its load),
 * but another unit, or a failure, may.  If the final load has not ended, or is held with nothing to silence it, within
 * RA_FINAL_S of the playback's end, the utterance ends as RA_LIMIT -- unresolved, never complete -- with run_ahead.held
 * set for the held case (and held_at: the chip time its timer ended).
 * Before that, RA_TRAILING: the capture exhausted, every spoken load applied, a pause loaded after the last of them
 * and ended (the chip requests), and the rest of the script idle -- pauses and register writes, nothing spoken.  The
 * speech is over there (the host's "done"); the rest plays on in real time unless the host has input for the unit,
 * which may then apply it at once (ra_flush checks its content first) -- a deliberate retiming: those pauses are cut.
 * A limit or an error ends the run-ahead utterance (ra.active = 0) as that state; the host decides what follows (the
 * Braille Lite host goes on in lockstep, whose own policies then judge "done", or reports the error).  An error is
 * sticky: no new utterance starts over it (ra_start refuses) until the host abandons it explicitly (ra_reset).
 */
#ifndef RUN_AHEAD_H
#define RUN_AHEAD_H

#include "../ssi263.h"

#define RA_ACK_S 0.001             /* the stand-in chip's acknowledgement, after a load (CPU time) */
#define RA_READ_S 0.005            /* an answer later than this at a pause join: the unit was reading, not answering */
#define RA_LAT_S 58e-6             /* the unit's answer to a request until one is measured (the corpora's median) */
#define RA_QUIET_S 0.2             /* capture exhausted: nothing more owed and no write for this long (CPU time) */
#define RA_LIMIT_S 3.0             /* capture limit: no write for this long while speech is still owed (CPU time) */
#define RA_FINAL_S 1.0             /* the final load must end within this much chip time, or the end is a limit */
#define RA_AHEAD 16                /* segments captured ahead of the replay: the bound of the streaming capture */
#ifndef RA_SETTLE_S                /* ra_settle: at most this much CPU time for the unit to wait for an interrupt.  The
                                      Braille Lite's text copies met mid-routine take ~35 ms of CPU for a 90-character
                                      line (its trace: ~2300 cycles a character staged, ~1260 moved); past them it may
                                      read a line for longer, awake, where a cancel is safe (the lockstep's too).  Every
                                      cap from 0.02 to 0.5 s cleared the sweeps; 0.5 s cost 11 ms a cancel, 0.1 s 2.5 */
#define RA_SETTLE_S 0.1
#endif

/* why the capture ended (run_ahead.end) */
enum { RA_END_NONE, RA_END_QUIET, RA_END_LIMIT, RA_END_ALLOC, RA_END_ABORT };
/* the utterance (ra_state) */
enum {
    RA_IDLE,                       /* none started (or the host's run ahead is off) */
    RA_CAPTURING,                  /* capture running (replay under way too) */
    RA_REPLAYING,                  /* capture ended; captured writes still to apply */
    RA_TRAILING,                   /* the speech is over: only idle writes left, the pause after it ended (above) */
    RA_FINAL_LOAD,                 /* playback exhausted; the final load still sounds: its timer runs (the chip does not
                                      request), or it was spoken and is held (its timer ended, nothing replaced it) */
    RA_COMPLETE,                   /* capture exhausted by the quiet policy, playback exhausted, the speech ended: a
                                      pause the final load, and ended */
    RA_LIMIT,                      /* ended at a bound: RA_LIMIT_S with speech owed, or RA_FINAL_S (its final load not
                                      ended, or spoken and held: run_ahead.held): NOT complete */
    RA_ERROR,                      /* an allocation failed: the capture is incomplete, replay stopped there.  Sticky
                                      (ra_start refuses) until ra_reset */
    RA_CANCELLED                   /* ra_abort */
};
/* The contract test's controls (test_run_ahead.c, nvda/tools/run_ahead_equiv.py): run_ahead.brk puts ONE earlier
   behaviour back, so a test can show it sees that bug.  0 (always, outside those controls): none. */
enum {
    RA_BRK_NONE,
    RA_BRK_COMPLETION,             /* 0.7 draft: playback exhausted = finished; done at the last spoken load's end */
    RA_BRK_LIMIT,                  /* 0.7 draft: the capture limit ended as a success */
    RA_BRK_ALLOC,                  /* 0.7 draft: a failed allocation dropped the acknowledgement or the write */
    RA_BRK_READING,                /* 0.7 draft: any answer over RA_READ_S moved up, also after a spoken phoneme */
    RA_BRK_SLIVER,                 /* 0.7 draft (the Braille Lite host): a lockstep slice in a block's rounding
                                      sliver while the script still played -- the unit stalled at 44.1 kHz */
    RA_BRK_SETTLE,                 /* 0.7 draft: a cancel reached the unit where the capture had stopped it, mid-
                                      routine (ra_settle skipped; the Braille Lite host: the chip's request given at
                                      once too) -- cancelled text leaked into the next utterance */
    RA_BRK_REQUEST,                /* Reply 112 item 1: the final load's request taken as the end of speech -- complete
                                      while a spoken final load still sounded */
    RA_BRK_STICKY,                 /* Reply 112 item 2 (the Braille Lite host): held input masked an error (busy 1, not
                                      -1), and was delivered over it -- a held say started a new capture, the error
                                      lost; input taken over an error; ra_start over an error */
    RA_BRK_DROP,                   /* Reply 112 item 2 (the Braille Lite host): a chip write or serial byte the board
                                      could not store, a transmitted byte or a logged write the host could not keep,
                                      lost silently */
    RA_BRK_HELD                    /* 0.9.2 (the Braille Lite host): a cancel left the unit keeping a line of the
                                      cancelled text, spoken in front of the next (bl_host.c cut_kept_line skipped) */
};

typedef struct {
    void *ctx;
    void (*run)(void *ctx, unsigned long long cycles);   /* runs the CPU; the host hands its writes to ra_write */
    unsigned long long (*cycles)(void *ctx);
    void (*set_ar)(void *ctx, int requesting);           /* the A/R line into the unit */
    int (*more)(void *ctx);                              /* the unit still owes speech for input it was given */
    void (*apply)(void *ctx, int reg, int val);          /* a write to the chip, now (the host's bookkeeping too) */
    /* optional (NULL: none), for tests: a segment opened at the chip's time now -- how: 0 at the chip's request,
       1 moved up (reading), 2 moved up without waiting for a pause to end */
    void (*opened)(void *ctx, int seg, int how);
    double clock_hz;
    /* optional (NULL: none): 1 while the unit's CPU waits for an interrupt -- halted or asleep.  A CPU state, not
       any routine's: ra_settle */
    int (*idle)(void *ctx);
} ra_board;

typedef struct {
    unsigned long long cyc;
    int seg;
    unsigned char reg, val;
} ra_write;

typedef struct {
    ra_board b;
    int active;                    /* an utterance is being captured or replayed, or its final load still sounds */
    int capturing;
    int end;                       /* RA_END_*: why the capture ended */
    int outcome;                   /* RA_COMPLETE, RA_LIMIT, RA_ERROR, RA_CANCELLED once the utterance has ended; else 0 */
    int brk;                       /* RA_BRK_*: the tests' controls only */
    ra_write *w;
    int nw, capw;
    unsigned long long *ack;       /* ack[k]: the CPU cycle of the k-th acknowledgement (ack[0]: the start) */
    unsigned char *ack_pa;         /* ack_pa[k]: the k-th acknowledged load was a pause (code 00); ack_pa[0] = 1 */
    int nack, capack;
    int r3, r3_play, pending, pend_pa;
    unsigned long long pend_at, last_write;
    int ri, seg, seg_ready, last_load;
    int last_spoken;               /* index of the last spoken load (code not 00) captured, -1: none */
    int loaded;                    /* a load has been applied by the replay */
    double anchor, final_at;       /* the segment's start; when the playback was exhausted (chip time) */
    double held_at;                /* a spoken final load's timer ended here (chip time), still sounding; -1: none */
    int held;                      /* the utterance ended as RA_LIMIT with its spoken final load held (above) */
    unsigned long long shift, lat;
    const double *pace;            /* lane 1: write i at chip time pace[i] (ra_pace); NULL: the retimed rule */
    int npace;
    int over;                      /* the speech is over (RA_TRAILING), from the moment it was seen until the end */
    int stop_trailing;             /* ra_play returns as soon as RA_TRAILING is reached (the host has input waiting) */
    int settling;                  /* ra_settle runs: the unit's writes are dropped */
    int settled;                   /* the last ra_settle: 1 the CPU waited for an interrupt, 0 not (the cap, or no
                                      idle callback), -1 none needed (no capture running); dropped: its writes */
    int dropped;
} run_ahead;

void ra_init(run_ahead *r, const ra_board *b);
void ra_free(run_ahead *r);
/* a new utterance from the current state: r3 = the chip's R3 now, requesting = its A/R request now.  Refused (0)
   while one is active: the host completes, cancels or aborts it first -- nothing is flushed here. */
int ra_start(run_ahead *r, int r3, int requesting);
/* the unit wrote a chip register during capture */
void ra_write_reg(run_ahead *r, int reg, int val);
/* plays up to `seconds` of the script on the chip into out (room for seconds x out_rate + 64 samples); returns the
   samples, and the chip time played in *played (less than `seconds` when the utterance ended: ra.active is then 0 and
   ra_state says how) */
long ra_play(run_ahead *r, ssi263 *chip, double out_rate, double seconds, double *out, double *played);
/* RA_*: where the utterance is (see above) */
int ra_state(const run_ahead *r, const ssi263 *chip);
/* 1 in RA_TRAILING (above) */
int ra_trailing(const run_ahead *r, const ssi263 *chip);
/* the 0.7 draft's rule, kept for RA_BRK_COMPLETION only: a spoken phoneme still to be applied, or the last applied
   load spoken and its timer running -- it ignored the phoneme sounding on after its timer, until the next load */
int ra_sounding(const run_ahead *r, const ssi263 *chip);
/* 1 when every write not yet applied is idle: no phoneme load other than a pause (code 00) -- content checked, with
   the R3 state each write would meet -- and the capture has ended.  Only then may the rest be applied at once
   (ra_flush): never speech. */
int ra_rest_is_idle(const run_ahead *r);
/* the rest of the script applied to the chip at once, in order.  Refused (0, nothing applied) unless
   ra_rest_is_idle; the utterance then ends as its capture did (RA_COMPLETE after the quiet policy, RA_LIMIT,
   RA_ERROR), though its last pauses were cut. */
int ra_flush(run_ahead *r);
/* stops: the script not yet played is dropped (the unit stays where the capture left it); RA_CANCELLED */
void ra_abort(run_ahead *r);
/* the host abandons an ended utterance's outcome (an error above all: its explicit recovery) -- RA_IDLE; nothing while
   one is active */
void ra_reset(run_ahead *r);
/* Before a cancel reaches the unit (a host's ^X ...) while the capture runs.  The capture stops the CPU wherever its
   bound falls: just after an acknowledgement, which wakes the unit, so it is parked MID-ROUTINE (the Braille Lite:
   awake at 97% of stops, against 15% of the time in the lockstep, whose CPU mostly waits on the chip).  A cancel
   there meets the firmware halfway through whatever it was doing -- on the Braille Lite, copying the next line's text
   into its line buffer, where a character of the cancelled text survived the ^X and led the next utterance.  So the
   unit runs on, A/R not requesting (nothing more acknowledged), until its CPU waits for an interrupt (b.idle): where
   a lockstep unit almost always is when a cancel comes.  At most RA_SETTLE_S of CPU time.  Its writes meanwhile
   belong to the script being dropped: dropped too, never applied (r->dropped counts them).  Then ra_abort.
   r->settled: 1 idle, 0 the cap (or no idle callback: nothing run), -1 no capture running (nothing to do). */
void ra_settle(run_ahead *r);
/* lane 1: apply write i of the next utterance's script at chip time t[i] (the lockstep's own schedule) instead of the
   retimed rule; a write past n waits.  The caller keeps t alive; NULL turns it off.  Tests only. */
void ra_pace(run_ahead *r, const double *t, int n);

#endif
