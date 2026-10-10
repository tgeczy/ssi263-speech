/* bl_host.c -- src/hosts/blazie.py's Blazie class in C, line for line (see bl_host.h).  Every comparison, every order
 * of operations and every rounding is Python's: run() takes its sample count with round() (half to even:
 * nearbyint), run_until_request() truncates, cycles are int() (truncation) of CLOCK_HZ x dt [x speed].
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_board.h"
#include "bl_host.h"
#include "bl_whine_table.h"
/* run_ahead.c's allocations, through the host: a test can make one fail (bh_set_int "fail_alloc_size"; every call site
   there has its run_ahead r, whose b.ctx is the host) */
static void *bh_ra_realloc(void *host, void *p, size_t n);
#define RA_REALLOC(p, n) bh_ra_realloc(r->b.ctx, (p), (n))
#include "run_ahead.c"             /* the run-ahead mode (bh_set_int "run_ahead"): one translation unit */

/* BH_TRACE (-DBH_TRACE, a scratch build only, never a release): an ordered trace in the working directory's
   bh_trace.txt -- the unit's serial receive and transmit, its SSI-263 writes and A/R, the host's queue, drop and ^X,
   the capture's writes and the chip's, ^F echoes and the cancel's bookkeeping, and RAM writes in up to 8 watched
   ranges (bh_set_int "watch_lo"/"watch_hi", then "trace" 1 on / 0 off / -1 closed, "trace_mark" n).  Watchpoints,
   not disassembly: the run-ahead cancel leak was found with it (run_ahead_cancel.py). */
#ifdef BH_TRACE
extern FILE *bh_trace_fp;
extern int bh_trace_on;
extern unsigned bh_watch_lo[8], bh_watch_hi[8];
extern int bh_n_watch;
#define HTR(...) do { if (bh_trace_fp && bh_trace_on) fprintf(bh_trace_fp, __VA_ARGS__); } while (0)
#else
#define HTR(...) do { } while (0)
#endif

#define CLOCK_HZ 6144000.0
#define PLAYING_MAX_S 1.0              /* longer than any phoneme the firmware loads (bh_busy) */
#define UNANSWERED_S 1.0               /* bh_busy's host timeout policy: a request given to the firmware and
                                          left unanswered this long counts as the end (not a proof) */

void ssi_onepole(double *x, int n, double b0, double b1, double a1, double *state);   /* ssi263dsp.c */

struct bl_host {
    bl_unit *unit;
    ssi263 *chip;
    double out_rate;
    int board_on;
    double b0, b1, a1, board_state[2];
    int whine, whine_key_r4, whine_key_mode, whine_has_key;
    double whine_key_rate, whine_fc, whine_ph;
    double whine_tab[BL_WHINE_TABLE];
    int whine_tab_ok;
    double cancel_cut, cancel_quiet;
    int ar;                        /* -1: none sent yet (Python's None) */
    unsigned char *tx;
    int n_tx, cap_tx;
    int sent_f, echo_f, stale_f;
    double say_time, last_speech, last_load, ar_time, turbo, prep_step;
    int preparing, turbo_between_lines;
    double *buf;
    int n_buf, cap_buf;
    int log_on;
    bh_write *wl;
    int n_wl, cap_wl;
    bl_idle *idle;                 /* the emulator's idle-channel sounds (bh_set_idle); NULL: off */
    int idle_power;                /* the channel's power (port A0 bit 1) as last told to it */
    run_ahead ra;                  /* the unit run ahead of the chip (run_ahead.h); off unless ra_on */
    int ra_on;
    int log_ar;                    /* tests: the A/R edges given to the unit (reg 8) and the run-ahead segments'
                                      openings (reg 9-11) in the write log too */
    struct held { int say, off, n; } *held;   /* input given while a run-ahead utterance plays: delivered, in order,
                                                 when it ends (bh_say) */
    int n_held, cap_held;
    unsigned char *held_bytes;
    int n_held_bytes, cap_held_bytes;
    double *pace;                  /* lane 1 (bh_pace): the schedule of the next run-ahead utterance */
    int n_pace;
    int ar_hold;                   /* bh_cancel of a run-ahead capture: A/R not requesting over the first ^X slice */
    /* Faults (Astra, Reply 112 item 2): nothing the unit did is lost silently.  BH_FAULT_*: the host cannot go on as
       if nothing happened -- bh_busy reports -1 BEFORE anything else (held input included), input is refused (-1)
       and held input is not delivered, so no new utterance starts over it, until the explicit recovery: bh_cancel
       (the utterance abandoned, held input dropped, ^X; the NVDA driver goes further and restarts the unit) */
    int fault;
    int tx_lost, wl_lost;          /* transmitted bytes (bh_tx), logged writes (bh_writes) the host could not keep:
                                      counted, read by the caller (native_blazie.py raises) -- records, not state */
    size_t fail_size;              /* tests: run_ahead.c's next allocation of exactly this many bytes fails */
    int fail_tx, fail_log;         /* tests: the n-th transmitted byte / logged write from now cannot be kept */
    /* Both bits enabled by default: the lockstep's own cancel race -- ^X met the
       firmware mid-copy of its next line, and a character of the cancelled text led the next utterance.  bit 0: the
       unit runs on, A/R not requesting, its chip writes dropped, until its CPU waits for an interrupt (at most
       RA_SETTLE_S of CPU time; ra_settle's rule for run ahead); bit 1: A/R not requesting over the first ^X slice */
    int cancel_settle;
    int settling, cancel_settled, cancel_dropped;
    /* The line-start lift (bh_set_int "line_lift"): note-taking mode's lift of a line read because the user moved
       to it, which speech-box lines never get.  The firmware opens every line's phonemes with a pitch marker -- in
       note-taking mode, on a moved-to line, a raise (the pitch byte + 1Bh), else a normal one -- and a normal
       marker a few slots on.  A marker shows in the chip writes as a second R1 write before the next R0 (its own,
       then the usual one).  Armed, the line's first marker is made the raise and the second ends it, as the
       emulated unit does in note-taking mode; with the unit's voice inflection off it drops its markers, and so
       there is no lift -- as on the unit.  0 off, 1 armed (the next line), 2 lifting. */
    int lift, lift_r1s, lift_val;
};

enum { BH_FAULT_RUN_AHEAD = 1, BH_FAULT_EVENT = 2 };

static void *bh_ra_realloc(void *host, void *p, size_t n)
{
    bl_host *h = (bl_host *)host;
    if (h && h->fail_size && n == h->fail_size) {
        h->fail_size = 0;                                  /* once */
        return NULL;
    }
    return realloc(p, n);
}

/* the faults now: the run-ahead capture's error (sticky until bh_cancel), a board event lost */
static int faults(const bl_host *h)
{
    int f = h->fault;
    if (h->ra.outcome == RA_ERROR || (h->ra.active && h->ra.end == RA_END_ALLOC))
        f |= BH_FAULT_RUN_AHEAD;
    return f;
}

/* ... as they stop the host (the RA_BRK_STICKY control: the run-ahead error as the 0.7 draft let it through) */
static int blocked(const bl_host *h)
{
    return faults(h) & ~(h->ra.brk == RA_BRK_STICKY ? BH_FAULT_RUN_AHEAD : 0);
}

/* tests: the n-th store from now fails, as a failed allocation would (a countdown; 0: none) */
static int inject(int *countdown)
{
    return *countdown > 0 && --*countdown == 0;
}

/* the tests' log of writes (bh_writes), at the chip's time now */
static void log_write(bl_host *h, int reg, int val)
{
    int fail;
    if (!h->log_on)
        return;
    fail = inject(&h->fail_log);
    if (h->n_wl == h->cap_wl && !fail) {
        int cap = h->cap_wl ? h->cap_wl * 2 : 1024;
        bh_write *w = (bh_write *)realloc(h->wl, (size_t)cap * sizeof(bh_write));
        if (w) { h->wl = w; h->cap_wl = cap; }
    }
    if (h->n_wl < h->cap_wl && !fail) {
        h->wl[h->n_wl].t = ssi263_time(h->chip);
        h->wl[h->n_wl].reg = reg;
        h->wl[h->n_wl].val = val;
        h->n_wl++;
    } else if (h->ra.brk != RA_BRK_DROP)
        h->wl_lost++;                                      /* never silently: the log is incomplete from here */
}

/* the idle model hears every chip write and the channel's power, at the sample the block has reached */
static void idle_events(bl_host *h, int reg, int val)
{
    if (reg >= 0)
        bl_idle_write(h->idle, h->n_buf, reg, val);
    else {
        int p = (bl_port_a0(h->unit) >> 1) & 1;
        if (p != h->idle_power) {
            h->idle_power = p;
            bl_idle_power(h->idle, h->n_buf, p);
        }
    }
}

static int request(const bl_host *h)
{
    return ssi263_request(h->chip) ? 1 : 0;
}

/* a write of the unit's, applied to the chip now, with the host's bookkeeping */
static void chip_write(bl_host *h, int reg, int val)
{
    if (h->lift && reg == 1) {
        if (++h->lift_r1s == 2) {                      /* a marker */
            if (h->lift == 1) {
                h->lift = 2;
                h->lift_val = (val + 0x1B) & 0xFF;     /* the raise: + 1Bh, wrapping as the firmware's does */
            } else
                h->lift = 0;                           /* the second: the firmware's own pitch again */
        }
        if (h->lift == 2)
            val = h->lift_val;
    } else if (reg == 0)
        h->lift_r1s = 0;
    HTR("PLAY t=%.6f cyc=%llu r%d=%02X ra=%d/%d ri=%d nw=%d\n", ssi263_time(h->chip), (unsigned long long)bl_cycles(h->unit),
        reg, val, h->ra.active, h->ra.capturing, h->ra.ri, h->ra.nw);
    ssi263_write(h->chip, reg, val);
    if (h->idle)
        idle_events(h, reg, val);
    log_write(h, reg, val);
    if (reg == 0 && !(ssi263_reg(h->chip, 3) & 0x80))
        h->last_load = ssi263_time(h->chip);        /* any phoneme, PA included (bh_busy) */
    if (reg == 0 && !(ssi263_reg(h->chip, 3) & 0x80) && (val & 0x3F)) {
        h->last_speech = ssi263_time(h->chip);      /* PA (code 00) is not speech */
        h->preparing = 0;
    }
}

/* Python's _cmd(): the unit's events since the last call, applied in order */
static void events(bl_host *h)
{
    const bl_event *ev;
    int n = bl_events(h->unit, &ev), i;
    for (i = 0; i < n; i++) {
        if (ev[i].type == 'W' && h->settling) {
            HTR("SETTLE-DROP r%d=%02X\n", ev[i].a, ev[i].b);
            h->cancel_dropped++;                           /* the cancelled utterance's future (cancel_settle) */
        } else if (ev[i].type == 'W') {
            /* run ahead: into the script, played later -- and after a failed allocation, lost with the capture (its
               error, RA_ERROR), never applied out of order */
            if (h->ra.active && (h->ra.capturing || h->ra.end == RA_END_ALLOC)) {
                HTR("CAP w%d seg=%d r%d=%02X\n", h->ra.nw, h->ra.nack - 1, ev[i].a, ev[i].b);
                ra_write_reg(&h->ra, ev[i].a, ev[i].b);
            } else
                chip_write(h, ev[i].a, ev[i].b);
        } else {
            unsigned char b = ev[i].a;
            int fail = inject(&h->fail_tx);
            if (h->n_tx == h->cap_tx && !fail) {
                int cap = h->cap_tx ? h->cap_tx * 2 : 256;
                unsigned char *t = (unsigned char *)realloc(h->tx, (size_t)cap);
                if (t) { h->tx = t; h->cap_tx = cap; }
            }
            if (h->n_tx < h->cap_tx && !fail)
                h->tx[h->n_tx++] = b;
            else if (h->ra.brk != RA_BRK_DROP)
                h->tx_lost++;                              /* the record only: the echo is still counted below */
            if (b == 0x06) {
                HTR("ECHO t=%.6f echo_f=%d->%d sent_f=%d stale=%d\n", ssi263_time(h->chip), h->echo_f, h->echo_f + 1,
                    h->sent_f, h->stale_f);
                h->echo_f += 1;
                h->say_time = ssi263_time(h->chip);        /* progress: patience counts from here */
                if (h->stale_f > 0)
                    h->stale_f -= 1;                       /* an earlier say()'s held flush line */
                else if (h->turbo_between_lines && bh_owed(h) > 0)
                    h->preparing = 1;                      /* a line is done; the next is being read */
            }
        }
    }
    bl_clear_events(h->unit);
    if (bl_events_lost(h->unit)) {                         /* a chip write or serial byte the board could not store */
        if (h->ra.brk != RA_BRK_DROP)
            h->fault |= BH_FAULT_EVENT;
        bl_clear_events_lost(h->unit);
    }
    if (h->idle)
        idle_events(h, -1, 0);
}

static void set_ar(bl_host *h)
{
    int r = h->ar_hold ? 0 : request(h);
    if (r != h->ar) {
        HTR("LAR t=%.6f %d\n", ssi263_time(h->chip), r);
        h->ar = r;
        if (r)
            h->ar_time = ssi263_time(h->chip);             /* when the firmware was given the request (bh_busy) */
        bl_set_ar(h->unit, r);
        if (h->log_ar)
            log_write(h, 8, r);                            /* the edge the unit is given (lockstep) */
        events(h);
    }
}

static void run_cpu(bl_host *h, double cycles_f)
{
    long long c = (long long)cycles_f;                     /* int(): truncation */
    if (c < 1)
        c = 1;
    bl_run(h->unit, (unsigned long long)c);
    events(h);
}

/* ---- the board as run_ahead.h sees it -------------------------------------------------------------------------- */
static void ra_cb_run(void *ctx, unsigned long long cycles)
{
    bl_host *h = (bl_host *)ctx;
    bl_run(h->unit, cycles);
    events(h);                                             /* its writes go to ra_write_reg while capturing */
}

static unsigned long long ra_cb_cycles(void *ctx)
{
    return bl_cycles(((bl_host *)ctx)->unit);
}

static void ra_cb_set_ar(void *ctx, int requesting)
{
    bl_host *h = (bl_host *)ctx;
    bl_set_ar(h->unit, requesting);
    if (h->log_ar)
        log_write(h, 12, requesting);                      /* the stand-in's line to the unit (capture) */
    events(h);
}

static int ra_cb_more(void *ctx)
{
    return bh_owed((bl_host *)ctx) > 0;                    /* a line's ^F echo still owed: it is not all spoken */
}

static void ra_cb_apply(void *ctx, int reg, int val)
{
    chip_write((bl_host *)ctx, reg, val);
}

static int ra_cb_idle(void *ctx)
{
    bl_probe p;
    bl_probe_get(((bl_host *)ctx)->unit, &p);
    return p.sleeping || p.halted;                         /* the Z180 waits for an interrupt (SLP or HALT) */
}

static void ra_cb_opened(void *ctx, int seg, int how)
{
    bl_host *h = (bl_host *)ctx;
    if (h->log_ar)
        log_write(h, 9 + how, seg);                        /* 9 at the request, 10 moved up, 11 without waiting */
}

BL_API bl_host *bh_create(const char *firmware, const char *state, ssi263 *chip, double out_rate, double board_hz,
                          const unsigned long long *key_at, const unsigned char *key_val, int n_keys,
                          unsigned long long boot_instr, int log_writes, char *err, int errlen)
{
    bl_host *h = (bl_host *)calloc(1, sizeof(bl_host));
    if (!h) { snprintf(err, errlen, "out of memory"); return NULL; }
    h->chip = chip;
    h->log_on = log_writes != 0;
    h->out_rate = out_rate;
    if (board_hz > 0.0) {                                  /* dsp.onepole_coeffs: butter(1, hz, fs) */
        double k = tan(M_PI * board_hz / out_rate);
        h->board_on = 1;
        h->b0 = k / (1.0 + k);
        h->b1 = k / (1.0 + k);
        h->a1 = (k - 1.0) / (k + 1.0);
    }
    h->cancel_settle = 3;                                  /* finish the interrupted copy, then hold A/R over ^X */
    h->cancel_cut = 0.1;
    h->cancel_quiet = 0.08;
    h->ar = -1;
    h->say_time = 0.0;
    h->turbo = 4.0;
    h->prep_step = 0.0;                                    /* None */
    h->last_speech = -1.0;
    h->last_load = -1.0;
    h->ar_time = -1.0;
    h->unit = bl_create(firmware, state, 5.0, key_at, key_val, n_keys, err, errlen);
    if (!h->unit) { free(h); return NULL; }
    bl_boot(h->unit, boot_instr);                          /* _cmd("B ...") */
    events(h);
    bl_live(h->unit);                                      /* _cmd("LIVE") */
    events(h);
    {
        ra_board b;
        b.ctx = h;
        b.run = ra_cb_run;
        b.cycles = ra_cb_cycles;
        b.set_ar = ra_cb_set_ar;
        b.more = ra_cb_more;
        b.apply = ra_cb_apply;
        b.opened = ra_cb_opened;
        b.idle = ra_cb_idle;
        b.clock_hz = CLOCK_HZ;
        ra_init(&h->ra, &b);
    }
    return h;
}

BL_API void bh_destroy(bl_host *h)
{
    if (!h)
        return;
    bl_destroy(h->unit);
    bl_idle_free(h->idle);
    ra_free(&h->ra);
    free(h->held);
    free(h->held_bytes);
    free(h->pace);
    free(h->tx);
    free(h->buf);
    free(h->wl);
    free(h);
}

BL_API int bh_writes(const bl_host *h, const bh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

BL_API void bh_clear_writes(bl_host *h)
{
    h->n_wl = 0;
}

static void send_now(bl_host *h, const unsigned char *data, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (data[i] == 0x06)
            h->sent_f++;
    bl_queue(h->unit, data, n);
    events(h);
}

static void say_now(bl_host *h, const unsigned char *data, int n)
{
    HTR("SAY t=%.6f cyc=%llu n=%d ra_on=%d sent=%d echo=%d req=%d\n", ssi263_time(h->chip),
        (unsigned long long)bl_cycles(h->unit), n, h->ra_on, h->sent_f, h->echo_f, request(h));
    h->say_time = ssi263_time(h->chip);
    h->preparing = 1;
    h->stale_f = h->sent_f - h->echo_f > 0 ? h->sent_f - h->echo_f : 0;   /* echoes still due from earlier sends */
    if (h->ra_on) {                                        /* run ahead: captured from here, played in bh_run */
        if (h->ra.active)
            ra_flush(&h->ra);                              /* only the 0.7 draft's control gets here (hold) */
        h->preparing = 0;
        h->ar = request(h);
        ra_pace(&h->ra, h->n_pace ? h->pace : NULL, h->n_pace);
        h->n_pace = 0;                                     /* a schedule serves one utterance */
        ra_start(&h->ra, ssi263_reg(h->chip, 3), h->ar);
    } else
        h->ra.outcome = RA_IDLE;                           /* the lockstep's own judgement from here (bh_busy) */
    send_now(h, data, n);
}

/* input while a run-ahead utterance plays (Astra, Reply 107): the unit has run ahead of the listener, so input given
   now cannot reach it where the listener is.  It is held, in order, and delivered when the utterance ends (complete,
   a limit or an error), where the capture's frontier and the playback's meet; a cancel drops it with the unit's
   unread input.  In the lockstep the unit would have it in its serial queue behind the utterance's own lines. */
static int hold(bl_host *h, int say, const unsigned char *data, int n)
{
    if (h->n_held == h->cap_held) {
        int cap = h->cap_held ? h->cap_held * 2 : 8;
        struct held *p = (struct held *)realloc(h->held, (size_t)cap * sizeof *p);
        if (!p)
            return 0;
        h->held = p;
        h->cap_held = cap;
    }
    if (h->n_held_bytes + n > h->cap_held_bytes) {
        int cap = h->cap_held_bytes ? h->cap_held_bytes : 256;
        unsigned char *b;
        while (cap < h->n_held_bytes + n)
            cap *= 2;
        b = (unsigned char *)realloc(h->held_bytes, (size_t)cap);
        if (!b)
            return 0;
        h->held_bytes = b;
        h->cap_held_bytes = cap;
    }
    memcpy(h->held_bytes + h->n_held_bytes, data, (size_t)n);
    h->held[h->n_held].say = say;
    h->held[h->n_held].off = h->n_held_bytes;
    h->held[h->n_held].n = n;
    h->n_held++;
    h->n_held_bytes += n;
    return 1;
}

/* a run-ahead utterance whose speech is over (RA_TRAILING) ends now: its idle rest applied at once, content checked
   (ra_flush) -- those pauses are cut, a deliberate retiming -- so input can reach the unit.  1 when none plays now. */
static int ra_end_for_input(bl_host *h)
{
    if (!h->ra.active)
        return 1;
    if (!ra_trailing(&h->ra, h->chip) || !ra_flush(&h->ra))
        return 0;
    h->ar = request(h);            /* the unit saw every acknowledgement in the capture: no edge for the flushed loads */
    return 1;
}

/* the held input, in order, once no run-ahead utterance plays; a say among it may start one: the rest waits */
static void deliver(bl_host *h)
{
    int i = 0, k;
    while (i < h->n_held && !blocked(h) && ra_end_for_input(h)) {   /* never over a fault: it waits for bh_cancel */
        const struct held *e = &h->held[i++];
        if (e->say)
            say_now(h, h->held_bytes + e->off, e->n);
        else
            send_now(h, h->held_bytes + e->off, e->n);
    }
    for (k = i; k < h->n_held; k++)
        h->held[k - i] = h->held[k];
    h->n_held -= i;
    if (!h->n_held)
        h->n_held_bytes = 0;
    h->ra.stop_trailing = h->n_held > 0;
}

/* input now goes to the unit (1), or behind held input / a run-ahead utterance still speaking (0) */
static int ra_takes_input(bl_host *h)
{
    if (h->ra.brk == RA_BRK_COMPLETION)
        return 1;                                          /* the 0.7 draft: straight in (bh_say flushed) */
    return !h->n_held && ra_end_for_input(h);
}

BL_API int bh_send(bl_host *h, const unsigned char *data, int n)
{
    if (n <= 0)
        return 1;
    if (blocked(h))
        return -1;                                         /* a fault: refused, not taken (bh_cancel first) */
    if (!ra_takes_input(h)) {
        h->ra.stop_trailing = 1;
        return hold(h, 0, data, n);
    }
    send_now(h, data, n);
    return 1;
}

BL_API int bh_say(bl_host *h, const unsigned char *data, int n)
{
    if (blocked(h))
        return -1;                                         /* a fault: refused, not taken (bh_cancel first) */
    if (!ra_takes_input(h)) {
        h->ra.stop_trailing = 1;
        return hold(h, 1, data, n);
    }
    say_now(h, data, n);
    return 1;
}

BL_API int bh_owed(const bl_host *h)
{
    return h->sent_f - 1 - h->echo_f;
}

BL_API int bh_busy(const bl_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    double since = h->say_time > h->last_speech ? h->say_time : h->last_speech;   /* max(say_time, last_speech) */
    if (blocked(h))
        return -1;                                         /* a fault, before anything else: held input included */
    if (h->ra.brk == RA_BRK_COMPLETION && (h->ra.active || h->ra.outcome == RA_COMPLETE))
        return ra_sounding(&h->ra, h->chip) && (h->ra.capturing || now - h->last_load < PLAYING_MAX_S);  /* 0.7 draft */
    if (h->n_held)
        return 1;                                          /* input waits for the utterance playing */
    switch (ra_state(&h->ra, h->chip)) {                   /* run ahead (run_ahead.h: its completion) */
    case RA_CAPTURING:
    case RA_REPLAYING:
    case RA_FINAL_LOAD:
        return 1;
    case RA_TRAILING:                                      /* the speech is over; its idle rest may play on */
    case RA_COMPLETE:
        return 0;
    case RA_ERROR:
        return -1;                                         /* not done: failed (its script is incomplete) */
    default:
        break;                                             /* none, cancelled, or a limit: the lockstep judges */
    }
    if ((now - h->last_speech) < quiet)
        return 1;
    /* the firmware is still inside an utterance while the chip plays a phoneme it loaded -- when speech has loaded
       since the last ^F echo (see blazie.py's busy) */
    if (h->last_speech > h->say_time) {
        if (!request(h) && (now - h->last_load) < PLAYING_MAX_S)
            return 1;
        /* ... and while the request has not reached the firmware, or reached it and is not answered yet (see
           blazie.py's busy; Astra, Reply 100) */
        if (request(h) && !(h->ar == 1 && h->last_load < h->ar_time && now - h->ar_time >= UNANSWERED_S))
            return 1;
    }
    return bh_owed(h) > 0 && (now - since) < patience;
}

static int reserve(bl_host *h, int n);
static double ra_block(bl_host *h, double seconds);

BL_API double bh_skip(bl_host *h, double seconds)
{
    double t = 0.0;
    if (h->ra.active || h->n_held) {                       /* run ahead: its script plays on, unheard */
        int keep = h->n_buf;
        h->n_buf = 0;
        t = ra_block(h, seconds);
        h->n_buf = keep;
        if (h->ra.active)
            t = seconds;
    }
    while (t < seconds - 1e-9) {
        double before, dt;
        set_ar(h);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, seconds - t);
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;                                     /* max(..., 1e-5) */
        run_cpu(h, CLOCK_HZ * dt);
        t += dt;
    }
    return t;
}

BL_API double bh_cancel(bl_host *h, double limit, double quiet, double cut)
{
    int holding;
    double t = 0.0;
    HTR("CANCEL t=%.6f cyc=%llu ra active=%d capturing=%d nw=%d ri=%d nack=%d seg=%d pending=%d end=%d req=%d "
        "sent=%d echo=%d stale=%d held=%d\n", ssi263_time(h->chip), (unsigned long long)bl_cycles(h->unit),
        h->ra.active, h->ra.capturing, h->ra.nw, h->ra.ri, h->ra.nack, h->ra.seg, h->ra.pending, h->ra.end,
        request(h), h->sent_f, h->echo_f, h->stale_f, h->n_held);
    h->n_held = h->n_held_bytes = 0;                       /* held input: dropped, as the unit's unread input is */
    h->lift = h->lift_r1s = 0;                             /* a line-start lift not reached: cut with its line */
    h->ra.stop_trailing = 0;
    h->sent_f -= bl_drop(h->unit);                         /* _cmd("D"): drop what the unit has not taken yet */
    events(h);
    if (h->ra.active && ra_flush(&h->ra)) {                /* run ahead, only idle writes left (content checked): */
        h->ar = request(h);                                /* applied at once, the chip as the unit left it */
    } else if (h->ra.active) {                             /* ... speech still to come: */
        /* The cancel meets the unit at the capture's frontier, not where the listener is.  Two things made the
           Braille Lite lead the next utterance with cancelled text there (nvda/tools/run_ahead_state.py, its sweep):
           the capture had parked the CPU mid-routine -- ra_settle lets it run on to a wait for an interrupt, as a
           lockstep unit mostly is when ^X comes -- and the chip's request, given at once, had the unit load the
           next phoneme of the dropped script and resume its text work before its ^X was handled.  So its A/R stays
           not requesting over the first ^X slice: that request belongs to the script being dropped.  Then the
           script not yet played is dropped; the unit, ahead, is cut as usual, in lockstep (it cannot take back
           what it did ahead: its ^F echoes, its reading) */
        ra_settle(&h->ra);
        HTR("SETTLE cyc=%llu settled=%d dropped=%d\n", (unsigned long long)bl_cycles(h->unit), h->ra.settled,
            h->ra.dropped);
        ra_abort(&h->ra);
        h->ar = -1;
        h->ar_hold = h->ra.brk != RA_BRK_SETTLE;           /* the control puts both back */
    } else if (h->cancel_settle) {                         /* lockstep cancellation (struct bl_host) */
        h->cancel_settled = -1;
        h->cancel_dropped = 0;
        if (h->cancel_settle & 1) {
            unsigned long long start = bl_cycles(h->unit), cap = (unsigned long long)(RA_SETTLE_S * CLOCK_HZ);
            h->settling = 1;
            bl_set_ar(h->unit, 0);
            events(h);
            while (!ra_cb_idle(h) && bl_cycles(h->unit) - start < cap) {
                bl_run(h->unit, RA_SLICE);
                events(h);
            }
            h->cancel_settled = ra_cb_idle(h) ? 1 : 0;
            h->settling = 0;
            h->ar = -1;                                    /* the unit's line was dropped: the next slice gives it */
            HTR("LOCK-SETTLE cyc=%llu settled=%d dropped=%d\n", (unsigned long long)bl_cycles(h->unit),
                h->cancel_settled, h->cancel_dropped);
        }
        h->ar_hold = (h->cancel_settle & 2) != 0;
    }
    /* the explicit recovery from a fault (Reply 112 item 2): the utterance, its held input and a failed script are
       abandoned here (ra_reset), the unit gets its ^X below */
    if (h->ra.outcome != RA_CANCELLED)
        ra_reset(&h->ra);
    h->fault = 0;
    holding = bh_owed(h) == 0;                             /* decided after the drop (see blazie.py) */
    h->preparing = 0;
    if (cut < 0.0)
        cut = h->cancel_cut;
    if (quiet < 0.0)
        quiet = h->cancel_quiet;
    while (t < limit) {
        bl_urgent(h->unit, 0x18);                          /* _cmd("U 18") */
        events(h);
        t += bh_skip(h, cut);
        h->ar_hold = 0;
        if (t >= 0.04 && ssi263_time(h->chip) - h->last_speech > quiet
                && (bh_owed(h) <= 0 || t >= 1.0))
            break;
    }
    h->ar_hold = 0;
    HTR("CANCEL END t=%.6f cyc=%llu echo %d -> %d (holding %d) sent=%d\n", ssi263_time(h->chip),
        (unsigned long long)bl_cycles(h->unit), h->echo_f, holding ? h->sent_f - 1 : h->sent_f, holding, h->sent_f);
    h->echo_f = holding ? h->sent_f - 1 : h->sent_f;
    return t;
}

static int reserve(bl_host *h, int n)
{
    if (h->n_buf + n > h->cap_buf) {
        int cap = h->cap_buf ? h->cap_buf : 4096;
        double *b;
        while (cap < h->n_buf + n)
            cap *= 2;
        b = (double *)realloc(h->buf, (size_t)cap * sizeof(double));
        if (!b)
            return 0;
        h->buf = b;
        h->cap_buf = cap;
    }
    return 1;
}

/* whine_wave(): one 64-tick period at tone register r4, band-limited under 0.45 x rate */
static int whine_wave(bl_host *h, int r4, double rate, int mode)
{
    const double xck = 1e6;
    int n_div = 256 - r4, tone, n, j, nl = 0;
    double fc, lv[65];
    int has[65];
    unsigned long long seed = BL_WHINE_SEED;
    int ln[64];
    double la[64], lp[64];
    const char *s;
    if (n_div <= 0)
        return 0;
    fc = xck / (2.0 * n_div);
    tone = 32 - n_div;
    if (tone > 26) tone = 26;
    if (tone < 0) tone = 0;
    memset(has, 0, sizeof has);
    for (s = BL_WHINE_TONES[mode - 1][tone]; *s; ) {
        char *end;
        long k = strtol(s, &end, 10);
        if (end == s || *end != ':')
            break;
        lv[k] = strtod(end + 1, &end);
        has[k] = 1;
        s = end;
        while (*s == ' ') s++;
    }
    for (n = 1; n < 65; n++) {
        seed = (seed * 1103515245ULL + 12345ULL) & 0x7FFFFFFFULL;
        if (has[n] && n * fc / 64.0 < 0.45 * rate) {
            ln[nl] = n;
            la[nl] = sqrt(2.0) * pow(10.0, lv[n] / 20.0) * BL_WHINE_VOWEL_RMS;
            lp[nl] = 2 * M_PI * (double)seed / 0x7FFFFFFF;
            nl++;
        }
    }
    for (j = 0; j < BL_WHINE_TABLE; j++) {
        double sum = 0.0;
        int i;
        for (i = 0; i < nl; i++)
            sum += la[i] * cos(2 * M_PI * ln[i] * j / BL_WHINE_TABLE + lp[i]);
        h->whine_tab[j] = sum;
    }
    h->whine_fc = fc;
    return 1;
}

static void add_whine(bl_host *h, double *y, int n)
{
    int r4 = ssi263_reg(h->chip, 4), r3 = ssi263_reg(h->chip, 3), i;
    double inc, ph;
    if (!h->whine_has_key || r4 != h->whine_key_r4 || h->out_rate != h->whine_key_rate || h->whine != h->whine_key_mode) {
        h->whine_has_key = 1;
        h->whine_key_r4 = r4;
        h->whine_key_rate = h->out_rate;
        h->whine_key_mode = h->whine;
        h->whine_tab_ok = whine_wave(h, r4, h->out_rate, h->whine);
    }
    if (!h->whine_tab_ok || !n)
        return;
    inc = h->whine_fc / 64.0 / h->out_rate;
    ph = h->whine_ph;
    if ((r3 & 0x80) || !(r3 & 0x70)) {
        h->whine_ph = fmod(ph + inc * n, 1.0);
        return;
    }
    for (i = 0; i < n; i++) {
        double pos = ph * BL_WHINE_TABLE, fr;
        int j = (int)pos;
        fr = pos - j;
        y[i] += h->whine_tab[j % BL_WHINE_TABLE] * (1.0 - fr) + h->whine_tab[(j + 1) % BL_WHINE_TABLE] * fr;
        ph += inc;
        if (ph >= 1.0)
            ph -= 1.0;
    }
    h->whine_ph = ph;
}

/* run ahead's part of a block: the script played, and held input delivered when an utterance ends (which may start
   the next); the chip time played.  The lockstep (bh_run) goes on from there. */
static double ra_block(bl_host *h, double seconds)
{
    double t = 0.0;
    for (;;) {
        double played;
        if (h->n_held)
            deliver(h);
        if (!h->ra.active || t >= seconds - 1e-12 || !reserve(h, (int)((seconds - t) * h->out_rate) + 64))
            break;
        h->n_buf += (int)ra_play(&h->ra, h->chip, h->out_rate, seconds - t, h->buf + h->n_buf, &played);
        t += played;
        if (h->ra.active && h->n_held && ra_trailing(&h->ra, h->chip))
            continue;                                      /* the speech is over: the held input goes in now */
        if (h->ra.active)
            break;                                         /* the block is full */
        /* ended: the unit's line stays as the capture left it (raised) -- the chip requests too after a complete
           final load, so no second edge; if the chip still plays (a limit), it is given the chip's line */
        h->ar = (request(h) || h->ra.brk == RA_BRK_COMPLETION) ? request(h) : -1;
        /* a spoken final load held (a limit, run_ahead.h): the unit was given that request in the capture and never
           answered it -- the lockstep's own bounded policy for exactly this (bh_busy: UNANSWERED_S, "not a proof")
           counts from when its timer ended.  Without this its request time was stale: busy for ever */
        if (h->ra.held)
            h->ar_time = h->ra.held_at;
    }
    return t;
}

BL_API int bh_run(bl_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0;
    h->n_buf = 0;
    if (h->idle)
        bl_idle_begin(h->idle);
    if (h->ra.active || h->n_held)
        t = ra_block(h, seconds);
    /* still playing the script: the block is full, whatever rounding left of it -- a lockstep slice now would give
       the unit the chip's A/R behind the stand-in's back (a 44.1 kHz session stalled that way, Reply 107 lane 2) */
    if (h->ra.active && h->ra.brk != RA_BRK_SLIVER)
        t = seconds;
    while (t < seconds) {
        double before, st, dt, speed;
        long n, got;
        set_ar(h);
        before = ssi263_time(h->chip);
        st = (h->preparing && h->prep_step > 0.0) ? h->prep_step : step;
        if (!request(h)) {
            n = (long)(st * h->out_rate);                  /* int(): truncation */
            if (!reserve(h, (int)n + 1)) break;
            got = ssi263_run_until_request(h->chip, n, h->buf + h->n_buf);
        } else {
            n = (long)nearbyint(st / 4 * h->out_rate);     /* round(): half to even */
            if (!reserve(h, (int)n + 1)) break;
            got = ssi263_run(h->chip, n, h->buf + h->n_buf);
        }
        h->n_buf += (int)got;
        dt = ssi263_time(h->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;
        speed = h->preparing ? h->turbo : 1.0;
        run_cpu(h, CLOCK_HZ * dt * speed);
        t += dt;
    }
    if (h->board_on && h->n_buf)
        ssi_onepole(h->buf, h->n_buf, h->b0, h->b1, h->a1, h->board_state);
    if (h->idle)
        bl_idle_render(h->idle, h->buf, h->n_buf);         /* in place of the whine below */
    else if (h->whine)
        add_whine(h, h->buf, h->n_buf);
    *audio = h->buf;
    return h->n_buf;
}

BL_API int bh_set_idle(bl_host *h, const bl_idle_options *o)
{
    if (!o) {
        bl_idle_free(h->idle);
        h->idle = NULL;
        return 1;
    }
    if (h->idle) {
        bl_idle_set(h->idle, o);
        return 1;
    }
    h->idle_power = (bl_port_a0(h->unit) >> 1) & 1;
    h->idle = bl_idle_new(h->out_rate, o, h->idle_power, ssi263_reg(h->chip, 3), ssi263_reg(h->chip, 4));
    return h->idle != NULL;
}

BL_API void bh_set_whine(bl_host *h, int mode)
{
    h->whine = (mode >= 0 && mode <= 2) ? mode : 0;
}

BL_API int bh_get_whine(const bl_host *h)
{
    return h->whine;
}

BL_API int bh_get_int(const bl_host *h, const char *name)
{
    if (!strcmp(name, "sent_f")) return h->sent_f;
    if (!strcmp(name, "echo_f")) return h->echo_f;
    if (!strcmp(name, "stale_f")) return h->stale_f;
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "turbo_between_lines")) return h->turbo_between_lines;
    if (!strcmp(name, "ar")) return h->ar;
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "line_lift")) return h->lift;
    if (!strcmp(name, "run_ahead")) return h->ra_on;
    if (!strcmp(name, "run_ahead_state")) return ra_state(&h->ra, h->chip);
    if (!strcmp(name, "run_ahead_end")) return h->ra.end;
    if (!strcmp(name, "run_ahead_break")) return h->ra.brk;
    if (!strcmp(name, "run_ahead_played")) return h->ra.ri;
    if (!strcmp(name, "run_ahead_captured")) return h->ra.nw;
    if (!strcmp(name, "run_ahead_settled")) return h->ra.settled;
    if (!strcmp(name, "run_ahead_dropped")) return h->ra.dropped;
    if (!strcmp(name, "run_ahead_held")) return h->ra.held;
    if (!strcmp(name, "fault")) return faults(h);
    if (!strcmp(name, "cancel_settle")) return h->cancel_settle;
    if (!strcmp(name, "cancel_settled")) return h->cancel_settled;
    if (!strcmp(name, "cancel_dropped")) return h->cancel_dropped;
    if (!strcmp(name, "tx_lost")) return h->tx_lost;
    if (!strcmp(name, "writes_lost")) return h->wl_lost;
    if (!strcmp(name, "held")) return h->n_held;
    if (!strcmp(name, "log_ar")) return h->log_ar;
    if (!strcmp(name, "port_a0")) return bl_port_a0(h->unit);
    if (!strcmp(name, "model")) return bl_model(h->unit);
    if (!strcmp(name, "flash_busy")) return bl_flash_busy(h->unit, NULL, NULL);
    if (!strcmp(name, "flash_chip_erases")) {
        unsigned long n;
        bl_flash_busy(h->unit, &n, NULL);
        return (int)n;
    }
    return 0;
}

BL_API void bh_set_int(bl_host *h, const char *name, int v)
{
    if (!strcmp(name, "sent_f")) h->sent_f = v;
    else if (!strcmp(name, "echo_f")) h->echo_f = v;
    else if (!strcmp(name, "stale_f")) h->stale_f = v;
    else if (!strcmp(name, "preparing")) h->preparing = v;
    else if (!strcmp(name, "turbo_between_lines")) h->turbo_between_lines = v;
    else if (!strcmp(name, "log_writes")) h->log_on = v != 0;
    else if (!strcmp(name, "line_lift")) {             /* 1: lift the next line's start; 0: not (bh_cancel too) */
        h->lift = v ? 1 : 0;
        h->lift_r1s = 0;
    }
    else if (!strcmp(name, "run_ahead"))                   /* off by default (run_ahead.h): from the next say; an */
        h->ra_on = v != 0;                                 /* utterance playing ends as it would (bh_cancel stops) */
    else if (!strcmp(name, "run_ahead_break")) h->ra.brk = v;   /* the tests' controls only (run_ahead.h RA_BRK_*) */
    else if (!strcmp(name, "log_ar")) h->log_ar = v != 0;
    else if (!strcmp(name, "tx_lost")) h->tx_lost = v;         /* the caller has reported them */
    else if (!strcmp(name, "writes_lost")) h->wl_lost = v;
    else if (!strcmp(name, "cancel_settle")) h->cancel_settle = v & 3;   /* tests can restore the old race */
    else if (!strcmp(name, "flash_timed")) bl_flash_timed(h->unit, v);   /* the emulator: bl_board.h */
    /* tests only (run_ahead_fault.py): one allocation or store made to fail, as memory running out would */
    else if (!strcmp(name, "fail_alloc_size")) h->fail_size = v > 0 ? (size_t)v : 0;
    else if (!strcmp(name, "fail_event")) bl_fail_event(h->unit, v);
    else if (!strcmp(name, "fail_tx")) h->fail_tx = v;
    else if (!strcmp(name, "fail_log")) h->fail_log = v;
#ifdef BH_TRACE
    else if (!strcmp(name, "trace")) {
        if (!bh_trace_fp && v > 0)
            bh_trace_fp = fopen("bh_trace.txt", "w");     /* in the working directory */
        bh_trace_on = v > 0;
        if (bh_trace_fp)
            fflush(bh_trace_fp);
        if (v < 0 && bh_trace_fp) {
            fclose(bh_trace_fp);
            bh_trace_fp = NULL;
        }
    } else if (!strcmp(name, "trace_mark"))
        HTR("MARK %d t=%.6f cyc=%llu\n", v, ssi263_time(h->chip), (unsigned long long)bl_cycles(h->unit));
    else if (!strcmp(name, "watch_lo") && bh_n_watch < 8) bh_watch_lo[bh_n_watch] = (unsigned)v;
    else if (!strcmp(name, "watch_hi") && bh_n_watch < 8) bh_watch_hi[bh_n_watch++] = (unsigned)v;
#endif
}

BL_API int bh_script(const bl_host *h, const void **writes)
{
    *writes = h->ra.w;
    return h->ra.nw;
}

BL_API int bh_pace(bl_host *h, const double *t, int n)
{
    if (n > 0) {
        double *p = (double *)realloc(h->pace, (size_t)n * sizeof *p);
        if (!p)
            return 0;
        memcpy(p, t, (size_t)n * sizeof *p);
        h->pace = p;
    }
    h->n_pace = n > 0 ? n : 0;
    return 1;
}

BL_API void bh_probe(const bl_host *h, bl_probe *p)
{
    bl_probe_get(h->unit, p);
}

BL_API int bh_memory(const bl_host *h, int which, const unsigned char **bytes)
{
    return bl_memory(h->unit, which, bytes);
}

BL_API int bh_braille(const bl_host *h, unsigned char *cells, int capacity)
{
    return bl_braille(h->unit, cells, capacity);
}

BL_API double bh_get_double(const bl_host *h, const char *name)
{
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "turbo")) return h->turbo;
    if (!strcmp(name, "prep_step")) return h->prep_step;
    if (!strcmp(name, "cancel_cut")) return h->cancel_cut;
    if (!strcmp(name, "cancel_quiet")) return h->cancel_quiet;
    return 0.0;
}

BL_API void bh_set_double(bl_host *h, const char *name, double v)
{
    if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
    else if (!strcmp(name, "turbo")) h->turbo = v;
    else if (!strcmp(name, "prep_step")) h->prep_step = v;
    else if (!strcmp(name, "cancel_cut")) h->cancel_cut = v;
    else if (!strcmp(name, "cancel_quiet")) h->cancel_quiet = v;
}

BL_API int bh_tx(const bl_host *h, const unsigned char **bytes)
{
    *bytes = h->tx;
    return h->n_tx;
}

BL_API void bh_clear_tx(bl_host *h)
{
    h->n_tx = 0;
}

BL_API int bh_serial_attach(bl_host *h, int on)
{
    return bl_serial_attach(h->unit, on);
}

BL_API int bh_serial_write(bl_host *h, const unsigned char *bytes, int n)
{
    return bl_serial_write(h->unit, bytes, n);
}

BL_API int bh_serial_space(const bl_host *h)
{
    return bl_serial_space(h->unit);
}

BL_API int bh_serial_read(bl_host *h, unsigned char *out, int cap, bl_serial_status *status)
{
    return bl_serial_read(h->unit, out, cap, status);
}

int bh_key(bl_host *h, int chord)
{
    return bl_key(h->unit, chord);
}

int bh_save_state(const bl_host *h, const char *path)
{
    return bl_save_state(h->unit, path);
}

void bh_battery(bl_host *h, int level)
{
    bl_battery(h->unit, level);
}

/* ---- the emulator's: keys held down, the clock controller (bl_board.h); the drivers never call these ---------- */
void bh_keys_down(bl_host *h, int bits)
{
    bl_keys_down(h->unit, bits);
}

void bh_braille_bars(bl_host *h, int down)
{
    bl_braille_bars(h->unit, down);
}

int bh_clock_on(bl_host *h, const blc_time *now, long long unix_now)
{
    return bl_clock_on(h->unit, now, unix_now);
}

int bh_clock_time(const bl_host *h, int alarm, blc_time *t)
{
    return bl_clock_time(h->unit, alarm, t);
}

void bh_clock_wall(bl_host *h, long long unix_now)
{
    bl_clock_wall(h->unit, unix_now);
}

int bh_starts(const bl_host *h)
{
    return bl_starts(h->unit);
}
