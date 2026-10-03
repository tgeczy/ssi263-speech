/* as_voice.c -- see as_voice.h.  Each part names the driver function it ports (nvda/accent/synthDrivers/accentmini.py,
 * the "sa" voice); change them together.  test_android_native.py compares the two byte for byte, and
 * nvda/tools/native_driver_equiv.py the NVDA driver on this library against 0.7.0's Python one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as_voice.h"
#include "../accent_text.h"

/* the driver's constants */
#define BLOCK_S 0.03
#define STEP_S 0.0005
#define LEAD_THRESHOLD 0.003
#define LEAD_PREROLL 220
#define BOOT_LIMIT 4.0
#define CANCEL_LIMIT 0.6
#define QUIET 0.03
#define PATIENCE 1.5
#define STALL_S 4.0                     /* the driver's watchdog: busy, speaking, and silent this long */
/* Issue #8: between two clauses of one text (after "Rate:", a comma, a full stop) the firmware can empty its phoneme
   queue, drop its speaking flag (port 40h bit 4) and read the next clause, at its own pace, before it speaks again:
   nothing on the serial line and no phoneme, ash_busy() false.  The text was called done there, and the rest stayed
   in the unit and came out with the next speech (measured: 0.1 s for NVDA's dialog labels, past 2 s for long
   clauses).  What tells the two apart is the 8085 itself: at a true end it is back in its waiting loops, between
   clauses it is still running the rules, about 12,000 instructions a block outside its waiting loops (the host's
   "work", as_board.c's WAITING, which holds the few that close an utterance too).  So a quiet block in which it ran
   ANY of its own code is not done -- a block it entered its next clause at the very end of counts as one it worked
   all through: the blocks are held until it either speaks again (they go out first, the audio unbroken) or spends a
   whole block waiting (they are dropped: the audio ends where it always did).  SETTLE_S caps that wait, should the
   firmware ever work on without a word (the 4 s watchdog only runs while the speaking flag is up): then the held
   audio goes out, the rest is flushed so the next text starts clean, and asv_limit() says the text was stopped at
   the limit, not finished -- never a plain done. */
#define SETTLE_S 6.0
#define WORK_MIN 1                      /* instructions outside the waiting loops in one block: it is at work */
#define OLD_WORK_MIN 2000               /* asv_set_test_break(3): the first revision's whole-block threshold */
/* DEFAULTS = ("5", 5, 5, 0): rate, pitch, voice, intonation after power-up */
#define DEF_RATE AT_DEF_RATE
#define DEF_PITCH AT_DEF_PITCH
#define DEF_VOICE AT_DEF_VOICE
#define DEF_INFL AT_DEF_INFL

struct as_voice {
    as_host *h;
    ssi263 *chip;
    unsigned char *rom[3];                             /* u2, u3, u4, kept to restart the unit after a failure */
    size_t n_rom[3];
    double out_rate;
    int rate, pitch, inflection, volume, numbers;      /* NVDA's scales */
    int voice;                                         /* the NVDA variant: the voice characteristic, ESC V0-9 */
    char sent_rate;                                    /* _sent */
    int sent_pitch, sent_voice, sent_infl;
    int base_pitch, cur_pitch;                         /* settings[1], _cur_pitch */
    int pitch_dirty, snap_until_speech;
    int lead, active;
    int job, fault;                                    /* inside asv_begin ... asv_end; the job failed */
    double t_start, gain;
    short *pcm;
    int pcm_cap;
    double settle, quiet_since;                        /* SETTLE_S; the chip time busy() went false, -1 while busy */
    short *held;                                       /* the blocks since then, not given out yet */
    int n_held, held_cap;
    int limit;                                         /* the last text was called done at SETTLE_S (asv_limit) */
    int test_break;                                    /* asv_set_test_break: the tests' must-fail controls only */
};

/* ---- _boot ---------------------------------------------------------------------------------------------------- */
static as_host *boot(const as_voice *v, char *err, int errlen)
{
    as_host *h = ash_create(v->rom[0], v->n_rom[0], v->rom[1], v->n_rom[1], v->rom[2], v->n_rom[2], NULL,
                            v->out_rate, err, errlen);
    if (h)
        ash_boot(h, BOOT_LIMIT);
    return h;
}

static void defaults_sent(as_voice *v)                 /* self._sent = DEFAULTS */
{
    v->sent_rate = DEF_RATE; v->sent_pitch = DEF_PITCH; v->sent_voice = DEF_VOICE; v->sent_infl = DEF_INFL;
}

static void free_roms(as_voice *v)
{
    int i;
    for (i = 0; i < 3; i++)
        free(v->rom[i]);
}

AS_API as_voice *asv_create(const unsigned char *u2, size_t n2, const unsigned char *u3, size_t n3,
                            const unsigned char *u4, size_t n4, double out_rate, char *err, int errlen)
{
    const unsigned char *src[3] = {u2, u3, u4};
    size_t n[3] = {n2, n3, n4};
    int i;
    as_voice *v = (as_voice *)calloc(1, sizeof(as_voice));
    if (!v) { snprintf(err, (size_t)errlen, "out of memory"); return NULL; }
    for (i = 0; i < 3; i++) {
        v->rom[i] = (unsigned char *)malloc(n[i] ? n[i] : 1);
        if (!v->rom[i]) {
            free_roms(v);
            free(v);
            snprintf(err, (size_t)errlen, "out of memory");
            return NULL;
        }
        if (n[i])
            memcpy(v->rom[i], src[i], n[i]);
        v->n_rom[i] = n[i];
    }
    v->out_rate = out_rate;
    v->h = boot(v, err, errlen);
    if (!v->h) { free_roms(v); free(v); return NULL; }
    v->chip = ash_chip(v->h);
    defaults_sent(v);
    v->rate = 50; v->pitch = 50; v->inflection = 100; v->volume = 100; v->numbers = 1; v->voice = DEF_VOICE;
    v->base_pitch = v->cur_pitch = DEF_PITCH;
    v->settle = SETTLE_S;
    v->quiet_since = -1.0;
    return v;
}

AS_API void asv_destroy(as_voice *v)
{
    if (!v) return;
    ash_destroy(v->h);
    free_roms(v);
    free(v->pcm);
    free(v->held);
    free(v);
}

AS_API void asv_set_settle(as_voice *v, double seconds)
{
    v->settle = seconds > 0.0 ? seconds : 0.0;
}

static void unsettle(as_voice *v)                      /* no wait in progress: nothing held */
{
    v->quiet_since = -1.0;
    v->n_held = 0;
}

/* a new text, a cancel, a job's end: a wait in progress belongs to the text before, and goes with it -- its held
   audio and its pending done (asv_set_test_break(1) keeps them: the control for exactly that) */
static void forget(as_voice *v)
{
    if (v->test_break != 1)
        unsettle(v);
}

AS_API int asv_limit(const as_voice *v) { return v->limit; }

AS_API void asv_set_test_break(as_voice *v, int what) { v->test_break = what; }

static int hold(as_voice *v, const short *s, int n)    /* a block of the wait, kept; 0 when out of memory */
{
    if (v->n_held + n > v->held_cap) {
        int cap = v->held_cap ? v->held_cap : 4096;
        short *q;
        while (cap < v->n_held + n)
            cap *= 2;
        q = (short *)realloc(v->held, (size_t)cap * sizeof(short));
        if (!q)
            return 0;
        v->held = q;
        v->held_cap = cap;
    }
    if (n > 0)
        memcpy(v->held + v->n_held, s, (size_t)n * sizeof(short));
    v->n_held += n;
    return 1;
}

AS_API as_host *asv_host(as_voice *v) { return v->h; }
AS_API int asv_fault(const as_voice *v) { return v->fault; }

AS_API void asv_set(as_voice *v, int rate, int pitch, int inflection, int volume, int numbers)
{
    v->rate = rate; v->pitch = pitch; v->inflection = inflection; v->volume = volume; v->numbers = numbers != 0;
}

AS_API void asv_set_voice(as_voice *v, int voice)
{
    if (voice >= 0 && voice <= 9)                      /* _set_variant: "0" to "9" only */
        v->voice = voice;
}

/* ---- _accent_pitch, _accent_settings, and the text: ../accent_text.c (shared with the Accent-mini's am_voice) */
AS_API int asv_pitch_step(int pitch) { return at_pitch_step(pitch); }

AS_API int asv_say_bytes(const char *utf8, int with_numbers, char *out, int cap)
{
    char *s = at_say_text(utf8, with_numbers);
    int n;
    if (!s) return -1;
    n = (int)strlen(s);
    if (out && n < cap) memcpy(out, s, (size_t)n + 1);
    free(s);
    return n;
}

/* ---- faults: _run's "Accent speech failed; restarting the emulated card" (a job only) ---------------------------- */
static void unsettle(as_voice *v);
static void resend_pitch(as_voice *v);

static void failed(as_voice *v)
{
    v->fault = 1;
    v->active = 0;
    unsettle(v);
    /* _speakJob's finally would have restored the pitch on the old unit: what is left of it is the flag */
    if (v->cur_pitch != v->base_pitch) {
        v->cur_pitch = v->base_pitch;
        v->pitch_dirty = 1;
    }
}

static int restart(as_voice *v)                        /* self._box = self._boot(); self._sent = DEFAULTS */
{
    as_host *h = boot(v, NULL, 0);
    if (!h)
        return 0;
    ash_destroy(v->h);
    v->h = h;
    v->chip = ash_chip(h);
    defaults_sent(v);
    v->fault = 0;
    return 1;
}

/* ---- _speakJob / _speakItems ----------------------------------------------------------------------------------- */
static void say_str(as_voice *v, const char *s, int speech)
{
    ash_say(v->h, (const unsigned char *)s, (int)strlen(s), speech);
}

static void say_pitch(as_voice *v, int p)                     /* box.chip.snap_pitch = True; box.say("\x1bP%d") */
{
    char cmd[16];
    ssi263_set_snap_pitch(v->chip, 1);
    snprintf(cmd, sizeof cmd, "\x1bP%d", p);
    say_str(v, cmd, 0);
}

/* _speakJob's finally: the user's pitch again, only after the capital's audio exists */
static void restore_pitch(as_voice *v)
{
    if (v->cur_pitch != v->base_pitch) {
        say_pitch(v, v->base_pitch);
        v->cur_pitch = v->base_pitch;
        v->pitch_dirty = 1;
    }
}

/* _speakJob's start: only the settings that changed are sent (repeated option commands emit preparation records of
   their own), the lead trim armed */
static void begin(as_voice *v)
{
    char r = at_rate_char(v->rate), cmd[64];
    int p = at_pitch_step(v->pitch), infl = at_inflection(v->inflection), k = 0;
    v->active = 0;
    forget(v);
    v->limit = 0;
    if (r != v->sent_rate) k += snprintf(cmd + k, sizeof cmd - (size_t)k, "\x1bR%c", r);
    if (p != v->sent_pitch) k += snprintf(cmd + k, sizeof cmd - (size_t)k, "\x1bP%d", p);
    if (v->voice != v->sent_voice) k += snprintf(cmd + k, sizeof cmd - (size_t)k, "\x1bV%d", v->voice);
    if (infl != v->sent_infl) k += snprintf(cmd + k, sizeof cmd - (size_t)k, "\x1bM%d", infl);
    if (k) {
        say_str(v, cmd, 0);
        v->sent_rate = r; v->sent_pitch = p; v->sent_voice = v->voice; v->sent_infl = infl;
    }
    v->base_pitch = v->cur_pitch = p;
    v->gain = v->volume / 100.0;
    v->lead = 1;
}

/* the items: PitchCommand(offset) */
static void pitch_item(as_voice *v, int pitch_offset)
{
    int want = at_pitch_step(v->pitch + pitch_offset);
    if (want != v->cur_pitch) {
        say_pitch(v, want);
        v->cur_pitch = want;
        v->pitch_dirty = 1;
    }
}

/* a text item: t_start = box.chip.time; box.say(text + "\r") -- ESC =F: the carriage return starts speech */
static void text_item(as_voice *v, const unsigned char *line, int n)
{
    v->t_start = ash_get_double(v->h, "time");
    ash_say(v->h, line, n, -1);
    v->active = 1;
    forget(v);
    v->limit = 0;
}

AS_API int asv_speak(as_voice *v, const char *utf8, int pitch_offset)
{
    char *text, *line;
    size_t n;
    v->job = 0;
    begin(v);
    if (pitch_offset)
        pitch_item(v, pitch_offset);
    text = at_say_text(utf8, v->numbers);
    if (!text || !*text) {
        free(text);
        restore_pitch(v);
        return 0;
    }
    n = strlen(text);
    line = (char *)malloc(n + 2);
    if (!line) { free(text); restore_pitch(v); return 0; }
    memcpy(line, text, n);
    line[n] = '\r';
    free(text);
    text_item(v, (const unsigned char *)line, (int)n + 1);
    free(line);
    return 1;
}

/* a text's end: in a job the pitch stays as the sequence left it until asv_end (the driver's finally) */
static void finish(as_voice *v)
{
    v->active = 0;
    forget(v);
    if (!v->job)
        restore_pitch(v);
}

/* the utterance cannot go on (out of memory, or the watchdog): a job fails (the driver restarts the unit); a single
   utterance just ends, as before */
static void stop(as_voice *v)
{
    if (v->job)
        failed(v);
    else
        finish(v);
}

AS_API int asv_render(as_voice *v, const short **pcm, int *done)
{
    const double *y;
    double now;
    int n, start = 0, count, working;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active) { *done = 1; return 0; }
    n = ash_run(v->h, BLOCK_S, STEP_S, &y);
    if (n < 0) { stop(v); *done = 1; return 0; }
    if (v->lead) {                                            /* _trim_lead */
        int k;
        start = n;
        for (k = 0; k < n; k++)
            if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) {
                start = k > LEAD_PREROLL ? k - LEAD_PREROLL : 0;
                v->lead = 0;
                break;
            }
    }
    count = n - start;
    if (count > v->pcm_cap) {
        short *q = (short *)realloc(v->pcm, (size_t)count * sizeof(short));
        if (!q) { stop(v); *done = 1; return 0; }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    if (v->snap_until_speech && ash_get_double(v->h, "last_speech") >= v->t_start) {
        ssi263_set_snap_pitch(v->chip, 0);
        v->snap_until_speech = 0;
    }
    if (count < 0)
        count = 0;
    now = ash_get_double(v->h, "time");
    working = ash_get_int(v->h, "work") >= (v->test_break == 3 ? OLD_WORK_MIN : WORK_MIN);   /* not waiting */
    if (v->quiet_since >= 0.0) {                              /* a silence while it works: this block held too */
        if (!hold(v, v->pcm, count)) { stop(v); *done = 1; return 0; }
        if (ash_busy(v->h, QUIET, PATIENCE)) {                /* it was a pause: what was held goes out, unbroken */
            if (v->n_held > v->pcm_cap) {
                short *q = (short *)realloc(v->pcm, (size_t)v->n_held * sizeof(short));
                if (!q) { stop(v); *done = 1; return 0; }
                v->pcm = q;
                v->pcm_cap = v->n_held;
            }
            memcpy(v->pcm, v->held, (size_t)v->n_held * sizeof(short));
            *pcm = v->pcm;
            count = v->n_held;
            unsettle(v);
            return count;
        }
        if (!working) {                                       /* a whole block waiting: nothing more to say */
            v->pitch_dirty = 0;                               /* the driver has taken everything sent so far */
            finish(v);                                        /* the held silence is dropped with the wait */
            *done = 1;
        } else if (now - v->quiet_since >= v->settle) {       /* the safety limit, still working: said so */
            count = 0;
            if (v->test_break != 2) {                         /* (2: the control -- a plain done, the audio lost) */
                if (v->n_held > v->pcm_cap) {
                    short *q = (short *)realloc(v->pcm, (size_t)v->n_held * sizeof(short));
                    if (!q) { stop(v); *done = 1; return 0; }
                    v->pcm = q;
                    v->pcm_cap = v->n_held;
                }
                memcpy(v->pcm, v->held, (size_t)v->n_held * sizeof(short));
                *pcm = v->pcm;
                count = v->n_held;
                v->limit = 1;
                /* what the firmware was still making is dropped, as a cancel drops it: the next text starts clean
                   instead of behind this one's rest (the issue's symptom) */
                ash_cancel(v->h, CANCEL_LIMIT);
                if (v->pitch_dirty)
                    resend_pitch(v);
            }
            finish(v);
            *done = 1;
            return count;
        }
        return 0;
    }
    if (!ash_busy(v->h, QUIET, PATIENCE)) {
        if (working && v->settle > 0.0) {
            v->quiet_since = now;                             /* this block goes out as it did; then wait */
            return count;
        }
        v->pitch_dirty = 0;
        finish(v);
        *done = 1;
    } else {
        double last = ash_get_double(v->h, "last_speech");
        double quiet = ash_get_double(v->h, "time") - (last > v->t_start ? last : v->t_start);
        if (ash_speaking(v->h) && quiet > STALL_S) {          /* "card stalled": the driver restarts the card */
            stop(v);
            *done = 1;
        }
    }
    return count;
}

static void resend_pitch(as_voice *v)                         /* _resend_pitch */
{
    say_pitch(v, v->sent_pitch);
    v->snap_until_speech = 1;
    v->cur_pitch = v->sent_pitch;
}

/* cancel(), then _run's flush: box.cancel(), and _resend_pitch when a pitch command may have been dropped */
AS_API void asv_cancel(as_voice *v)
{
    if (v->active)
        finish(v);
    forget(v);
    ash_cancel(v->h, CANCEL_LIMIT);
    if (v->pitch_dirty)
        resend_pitch(v);
}

/* ---- a job: the NVDA driver's whole speech sequence (as_voice.h) ------------------------------------------------ */
AS_API int asv_begin(as_voice *v)
{
    v->job = 0;
    v->active = 0;
    if (v->fault && !restart(v))
        return -1;
    begin(v);
    v->job = 1;
    return 0;
}

AS_API int asv_pitch(as_voice *v, int pitch_offset)
{
    if (v->fault)
        return -1;
    pitch_item(v, pitch_offset);
    return 0;
}

AS_API int asv_text(as_voice *v, const unsigned char *bytes, int n)
{
    if (v->fault)
        return -1;
    if (n <= 0)
        return 0;
    text_item(v, bytes, n);
    return 1;
}

AS_API int asv_end(as_voice *v)
{
    v->active = 0;
    v->job = 0;
    forget(v);
    if (v->fault)                                             /* failed() has done what the finally could */
        return -1;
    restore_pitch(v);
    return 0;
}

AS_API int asv_flush(as_voice *v)
{
    if (v->fault && !restart(v))
        return -1;
    forget(v);
    ash_cancel(v->h, CANCEL_LIMIT);
    if (v->pitch_dirty)
        resend_pitch(v);
    return 0;
}
