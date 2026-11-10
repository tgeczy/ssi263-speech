/* emu_unit.c -- see emu_unit.h.  The chip is made as the NVDA driver makes it (bl_voice.c's blv_create).  The
 * Braille Lite runs through bl_host (its lockstep, board low-pass and idle-channel noise), booted with no chords, so
 * the unit comes up in its own main menu.  The Type 'n Speak runs through the same lockstep written out here:
 * bl_host carries the Braille Lite driver's bookkeeping, which the Type 'n Speak has no use for.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../csrc/ssi263.h"
#include "../../csrc/blazie/bl_host.h"
#include "../../csrc/blazie/tns_board.h"
#include "emu_unit.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BOARD_LOWPASS_HZ 5000.0     /* as bl_voice.c */
#define CLOSURE_NOISE_LEAD_MS 10.0
#define MAKEUP 2.0
#define BOOT_INSTR 100000ULL        /* the stand-in boot before the chip takes over A/R: short, so the greeting is heard */
#define STEP_S 0.0005               /* the host's lockstep */
#define CLOCK_HZ 6144000.0
#define BATTERY_LEVEL 128           /* the gauge's raw reading: "100 percent", not charging (255: charging) */

void ssi_onepole(double *x, int n, double b0, double b1, double a1, double *state);   /* ssi263dsp.c */

/* ---- the program flash kept beside the state (emu_unit.h emu_save) ---------------------------------------------- */
/* STATE.program -- for a state written as NAME.new and then put in place (the Linux apps: whole or not at all),
   beside NAME, the name it will have */
static void program_path(char *out, size_t n, const char *state)
{
    size_t k = strlen(state);
    if (k > 4 && !strcmp(state + k - 4, ".new"))
        k -= 4;
    snprintf(out, n, "%.*s.program", (int)k, state);
}

/* Bank a's serial number (00080h, 16 bytes) and owner's name (00090h, 40), the one part of it a unit writes for itself:
   Blazie's name brander (BSNAME.BNS) programs the name there once, the firmware's p-chord th says both, and Blazie's
   updater keeps 00080h-000B7h through an update (test_brand.c).  Blank (FFh) in the image, so a bank a differing from
   this firmware's only there is still this firmware's, branded. */
#define OWNER_FROM 0x80L
#define OWNER_TO 0xB8L

/* bank a (n bytes) the same as want but for the serial number and name */
static int same_but_owner(const unsigned char *a, const unsigned char *want, long n)
{
#ifdef EMU_NO_OWNER_KEEP                    /* test_brand's must-fail control: every byte, as before */
    return memcmp(a, want, (size_t)n) == 0;
#else
    return memcmp(a, want, OWNER_FROM) == 0 && memcmp(a + OWNER_TO, want + OWNER_TO, (size_t)(n - OWNER_TO)) == 0;
#endif
}

/* the file's 512 KB if it is a whole program flash (else NULL; the caller frees it) */
static unsigned char *program_read(const char *path, long n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p = f ? (unsigned char *)malloc((size_t)n) : NULL;
    if (p && (fread(p, 1, (size_t)n, f) != (size_t)n || fgetc(f) != EOF)) {
        free(p);
        p = NULL;
    }
    if (f)
        fclose(f);
    return p;
}

/* a bank the firmware update wrote, and bank a's serial number and name, put back: the file's bank a must be the one
   this firmware loaded, but for those */
static void program_restore(bl_host *h, const char *state)
{
    char path[1024];
    unsigned char *pf, *saved;
    long n = bh_program_flash(h, &pf, NULL);
#ifdef EMU_NO_PROGRAM_KEEP                  /* test_update's must-fail control: nothing put back, nothing kept */
    return;
#endif
    if (!state || n <= 0)
        return;
    program_path(path, sizeof path, state);
    saved = program_read(path, n);
    if (saved && same_but_owner(saved, pf, n / 2)) {
        memcpy(pf + OWNER_FROM, saved + OWNER_FROM, OWNER_TO - OWNER_FROM);
        memcpy(pf + n / 2, saved + n / 2, (size_t)n / 2);
    }
    free(saved);
}

/* after a save: an updated bank b or a branded bank a written beside the state (as NAME.new, then put in place); a
   file of ours (its bank a this firmware's, but for the name) whose update and name are undone removed; a file for
   another firmware left alone.  1 on success. */
static int program_keep(bl_host *h, const char *state)
{
    char path[1024], tmp[1040];
    unsigned char *pf, *old;
    long n = bh_program_flash(h, &pf, NULL);
    int ok;
    FILE *f;
#ifdef EMU_NO_PROGRAM_KEEP
    return 1;
#endif
    if (n <= 0)
        return 1;
    program_path(path, sizeof path, state);
    if (memcmp(pf, pf + n / 2, (size_t)n / 2) != 0) {
        snprintf(tmp, sizeof tmp, "%s.new", path);
        ok = (f = fopen(tmp, "wb")) != NULL;
        if (ok) {
            ok = fwrite(pf, 1, (size_t)n, f) == (size_t)n;
            ok = (fclose(f) == 0) && ok;
        }
        if (ok) {
            remove(path);                    /* (Windows' rename will not replace a file) */
            ok = rename(tmp, path) == 0;
        }
        if (!ok)
            remove(tmp);
        return ok;
    }
    old = program_read(path, n);
    if (old && same_but_owner(old, pf, n / 2))
        remove(path);
    free(old);
    return 1;
}

static long long g_fake_now = -1;   /* emu_fake_host_time */

/* the host's local time as a calendar time, and its seconds since 1970 (for the time a unit was switched off) */
static long long host_now(blc_time *t)
{
    time_t now = g_fake_now >= 0 ? (time_t)g_fake_now : time(NULL);
    struct tm *tm = g_fake_now >= 0 ? gmtime(&now) : localtime(&now);
    memset(t, 0, sizeof *t);
    if (tm) {
        t->year = tm->tm_year + 1900;
        t->month = tm->tm_mon + 1;
        t->day = tm->tm_mday;
        t->hour = tm->tm_hour;
        t->minute = tm->tm_min;
        t->second = tm->tm_sec > 59 ? 59 : tm->tm_sec;
    }
    return (long long)now;
}

void emu_fake_host_time(long long seconds)
{
    g_fake_now = seconds;
}

struct emu_unit {
    int kind;
    ssi263 *chip;
    bl_host *host;                  /* the Braille Lite */
    tns_unit *tns;                  /* the Type 'n Speak */
    int ar;
    double b0, b1, a1, lp[2];
    double *buf;
    int cap;
    double out_rate, gain;
    const double *pend;             /* rendered but not yet handed out */
    int n_pend;
    int quick;                      /* emu_set_quick */
    int hurry;                      /* a key taken, no spoken phoneme loaded since: the CPU at EMU_QUICK_TURBO */
    double hurry_from;              /* chip time of that key */
    int tns_r3;                     /* the Type 'n Speak's R3 as written (a load with bit 7 clear is a phoneme) */
    int starts;                     /* the Braille Lite firmware's starts seen (bh_starts) */
    /* the advance bars (emu_keys_down, emu_key; the board's bl_bars) */
    int bars_held;                  /* down now through emu_keys_down */
    int bars_from_held;             /* went down through emu_keys_down since the last chord: not pressed again */
    int bars_tapped;                /* pressed by a chord (a terminal's keys), let go by bars_tick */
    double bars_down_at[2];         /* chip time each bar went down */
    int bars_up_wanted;             /* let go by the hand, still down until EMU_BAR_MIN_S has passed */
    int bar_chord;                  /* a chord typed with a bar: given once the firmware has the bar */
    double hold_until;              /* emu_hold_at_start: its keys let up at this chip time (0: none held) */
};

/* the board's bars (bl_board.h bl_braille_bars): bit 0 forward (the advance bar), bit 1 back */
#define BAR_FORWARD 1
#define BAR_BACK 2

/* EMU_ADVANCE / EMU_BACK as the board's bars */
static int bars_of(int bits)
{
    return ((bits & EMU_ADVANCE) ? BAR_FORWARD : 0) | ((bits & EMU_BACK) ? BAR_BACK : 0);
}

/* The bars to the board: down while held or tapped, and each kept down at least EMU_BAR_MIN_S of chip time -- the
   firmware polls them every 100 ms from its timer, so this is more than two polls and a tap is never missed; a chord
   typed with a bar goes to the unit once the bar has been down EMU_BAR_CHORD_S, when the firmware has marked it
   held. */
static void bars_tick(emu_unit *u)
{
    double now = ssi263_time(u->chip);
    int want = u->bars_held | u->bars_tapped | u->bars_up_wanted, b, down = 0;
    if (!u->host)
        return;
    for (b = 0; b < 2; b++) {
        int bit = 1 << b;
        if (!(want & bit))
            continue;
        if ((u->bars_held & bit) || now - u->bars_down_at[b] < EMU_BAR_MIN_S)
            down |= bit;
        else {
            u->bars_tapped &= ~bit;
            u->bars_up_wanted &= ~bit;
        }
    }
    if (u->bar_chord && down) {
        int ready = 1;
        for (b = 0; b < 2; b++)
            if ((down & (1 << b)) && now - u->bars_down_at[b] < EMU_BAR_CHORD_S)
                ready = 0;
        if (ready) {
            bh_key(u->host, u->bar_chord);
            u->bar_chord = 0;
        }
    } else if (u->bar_chord) {      /* the bar went up first (cannot happen: it is kept down for the chord) */
        bh_key(u->host, u->bar_chord);
        u->bar_chord = 0;
    }
    bh_braille_bars(u->host, down);
}

static void bars_press(emu_unit *u, int bars)
{
    double now = ssi263_time(u->chip);
    int b, down = u->bars_held | u->bars_tapped | u->bars_up_wanted;
    for (b = 0; b < 2; b++)
        if ((bars & (1 << b)) && !(down & (1 << b)))
            u->bars_down_at[b] = now;
}

/* bars held down now (board bits): pressed, and let go no sooner than EMU_BAR_MIN_S after they went down */
static void bars_hold(emu_unit *u, int bars)
{
    bars_press(u, bars & ~u->bars_held);
    u->bars_up_wanted |= u->bars_held & ~bars;
    u->bars_held = bars;
    u->bars_from_held |= bars;
    bars_tick(u);
}

emu_unit *emu_create(int kind, const char *firmware, const char *state, double out_rate, int whine, char *err,
                     int errlen)
{
    emu_unit *u = (emu_unit *)calloc(1, sizeof(emu_unit));
    ssi263_params p;
    if (!u) { snprintf(err, errlen, "out of memory"); return NULL; }
    u->kind = kind;
    ssi263_default_params(&p);
    p.closure_noise_lead_s = CLOSURE_NOISE_LEAD_MS / 1000.0;
    if (whine && kind == EMU_BRAILLE_LITE)
        p.carrier_rel_db = -300.0;
    u->chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
    if (!u->chip) { snprintf(err, errlen, "could not create the chip"); free(u); return NULL; }
    if (kind == EMU_TYPE_N_SPEAK) {
        double k = tan(M_PI * BOARD_LOWPASS_HZ / out_rate);   /* dsp.onepole_coeffs, as bl_host */
        u->tns = tns_create(firmware, state, err, errlen);
        if (!u->tns) { ssi263_free(u->chip); free(u); return NULL; }
        {
            blc_time now;
            long long secs = host_now(&now);
            if (!tns_clock_on(u->tns, &now, secs)) {
                snprintf(err, errlen, "out of memory");
                tns_destroy(u->tns); ssi263_free(u->chip); free(u); return NULL;
            }
        }
        u->b0 = u->b1 = k / (1.0 + k);
        u->a1 = (k - 1.0) / (k + 1.0);
        u->ar = -1;
    } else {
        u->host = bh_create(firmware, state, u->chip, out_rate, BOARD_LOWPASS_HZ, NULL, NULL, 0, BOOT_INSTR, 0, err,
                            errlen);
        if (!u->host) { ssi263_free(u->chip); free(u); return NULL; }
        bh_set_whine(u->host, whine);
        bh_battery(u->host, BATTERY_LEVEL);   /* the status menu's % reads the gauge (without it: frozen) */
        {   /* the clock controller: the saved one goes on, or it starts at the host's time */
            blc_time now;
            long long secs = host_now(&now);
            if (!bh_clock_on(u->host, &now, secs)) {
                snprintf(err, errlen, "out of memory");
                bh_destroy(u->host); ssi263_free(u->chip); free(u); return NULL;
            }
        }
        program_restore(u->host, state);   /* a bank an earlier firmware update wrote (the unit boots in bank a) */
    }
    emu_set_flash_timed(u, 1);      /* erases take the chip's time: the unit chirps while it initialises its flash */
    u->out_rate = out_rate;
    u->gain = MAKEUP;
    return u;
}

void emu_destroy(emu_unit *u)
{
    if (!u) return;
    if (u->host) bh_destroy(u->host);
    if (u->tns) tns_destroy(u->tns);
    ssi263_free(u->chip);
    free(u->buf);
    free(u);
}

int emu_kind(const emu_unit *u)
{
    return u->kind;
}

int emu_model(const emu_unit *u)
{
    if (u->tns)
        return EMU_MODEL_TYPE_N_SPEAK;
    switch (bh_get_int(u->host, "model")) {
    case BL_MODEL_BNS2000: return EMU_MODEL_BNS2000;
    case BL_MODEL_BNS640: return EMU_MODEL_BNS640;
    default: return EMU_MODEL_BRAILLE_LITE;
    }
}

/* ---- the Type 'n Speak's lockstep (bl_host.c's bh_run, without the driver's turbo and whine) ------------------ */
static void tns_events_to_chip(emu_unit *u)
{
    const bl_event *ev;
    int n = tns_events(u->tns, &ev), i;
    for (i = 0; i < n; i++)
        if (ev[i].type == 'W') {
            ssi263_write(u->chip, ev[i].a, ev[i].b);
            if (ev[i].a == 3)
                u->tns_r3 = ev[i].b;
            else if (ev[i].a == 0 && !(u->tns_r3 & 0x80) && (ev[i].b & 0x3F))
                u->hurry = 0;       /* a spoken phoneme (PA is code 00): quick response ends */
        }
    tns_clear_events(u->tns);
}

static int tns_render(emu_unit *u, double seconds, const double **out)
{
    double t = 0.0;
    int n_buf = 0;
    while (t < seconds) {
        double before, dt;
        unsigned long long cyc;
        long n, got;
        int r = ssi263_request(u->chip) ? 1 : 0;
        if (r != u->ar) {
            u->ar = r;
            tns_set_ar(u->tns, r);
            tns_events_to_chip(u);
        }
        before = ssi263_time(u->chip);
        n = r ? (long)nearbyint(STEP_S / 4 * u->out_rate) : (long)(STEP_S * u->out_rate);
        if (n_buf + n + 1 > u->cap) {
            int cap = (n_buf + (int)n + 1) * 2;
            double *b = (double *)realloc(u->buf, sizeof(double) * (size_t)cap);
            if (!b) break;
            u->buf = b;
            u->cap = cap;
        }
        got = r ? ssi263_run(u->chip, n, u->buf + n_buf) : ssi263_run_until_request(u->chip, n, u->buf + n_buf);
        n_buf += (int)got;
        dt = ssi263_time(u->chip) - before;
        if (dt < 1e-5)
            dt = 1e-5;
        cyc = (unsigned long long)(CLOCK_HZ * dt * (u->hurry ? EMU_QUICK_TURBO : 1.0));
        tns_run(u->tns, cyc ? cyc : 1);
        tns_events_to_chip(u);
        t += dt;
    }
    if (n_buf)
        ssi_onepole(u->buf, n_buf, u->b0, u->b1, u->a1, u->lp);
    *out = u->buf;
    return n_buf;
}

/* quick response ends at the first spoken phoneme (bl_host clears `preparing` there; the Type 'n Speak's lockstep
   clears hurry) or after EMU_QUICK_LIMIT_S: a key that brings no speech must not leave the unit fast */
int emu_restart_break;
int emu_bars_break;

static void hurry_check(emu_unit *u)
{
    if (u->host) {                  /* the firmware restarted (p-chord l): its start reads the keys held at its own
                                       pace, so quick response ends there (8 times faster, no hand could be in time) */
        int s = bh_starts(u->host);
        if (s != u->starts) {
            u->starts = s;
            if (u->hurry && !emu_restart_break) {
                u->hurry = 0;
                bh_set_int(u->host, "preparing", 0);
            }
        }
    }
    if (!u->hurry)
        return;
    if (u->host && !bh_get_int(u->host, "preparing"))
        u->hurry = 0;
    else if (ssi263_time(u->chip) - u->hurry_from > EMU_QUICK_LIMIT_S) {
        u->hurry = 0;
        if (u->host)
            bh_set_int(u->host, "preparing", 0);
    }
}

void emu_render(emu_unit *u, short *out, int n)
{
    while (n > 0) {
        int k;
        if (!u->n_pend) {
            double sec = (double)n / u->out_rate;
            hurry_check(u);
            bars_tick(u);
            if (u->hold_until > 0 && ssi263_time(u->chip) >= u->hold_until) {
                u->hold_until = 0;
                bh_keys_down(u->host, 0);
            }
            u->n_pend = u->tns ? tns_render(u, sec, &u->pend) : bh_run(u->host, sec, STEP_S, &u->pend);
            if (u->host)
                bh_clear_tx(u->host);   /* unplugged, what the unit sends goes nowhere (plugged, it never lands here) */
            if (u->n_pend <= 0) {   /* nothing came back: give silence rather than spin */
                memset(out, 0, sizeof(short) * (size_t)n);
                u->n_pend = 0;
                return;
            }
        }
        k = n < u->n_pend ? n : u->n_pend;
        ssi_pcm16(u->pend, k, u->gain, out);
        u->pend += k;
        u->n_pend -= k;
        out += k;
        n -= k;
    }
}

int emu_key(emu_unit *u, int key)
{
    if (u->quick && (u->host || (key & 0x80))) {   /* a chord, or a Type 'n Speak key going down */
        u->hurry = 1;
        u->hurry_from = ssi263_time(u->chip);
        if (u->host) {
            bh_set_double(u->host, "turbo", EMU_QUICK_TURBO);
            bh_set_int(u->host, "preparing", 1);     /* bl_host: the CPU at `turbo` until a spoken phoneme loads */
        }
    }
    if (!u->tns && emu_bars_break)  /* the tests' control: the bar as port 40h bit 7, which the firmware never reads */
        return bh_key(u->host, key & 0xFF);
    if (!u->tns) {
        int bars = bars_of(key) & ~u->bars_from_held;   /* a bar the device path holds is down already */
        u->bars_from_held = u->bars_held;
        key &= 0x7F;
        if (bars) {                 /* a terminal's chord with a bar: the bar pressed, the dots once it is read */
            bars_press(u, bars);
            u->bars_tapped |= bars;
            if (key) {
                if (u->bar_chord)
                    bh_key(u->host, u->bar_chord);
                u->bar_chord = key;
            }
            bars_tick(u);
            return 1;
        }
        if (!key)
            return 1;               /* the bars alone, held through the device path: nothing more to send */
    }
    return u->tns ? tns_key(u->tns, key) : bh_key(u->host, key);
}

void emu_set_quick(emu_unit *u, int on)
{
    u->quick = on != 0;
    if (!u->quick && u->hurry) {
        u->hurry = 0;
        if (u->host)
            bh_set_int(u->host, "preparing", 0);
    }
}

void emu_set_double_speed(emu_unit *u, int on)
{
    if (u->host)
        bh_set_int(u->host, "double_speed", on != 0);
}

int emu_double_speed(const emu_unit *u)
{
    return u->host ? bh_get_int(u->host, "double_speed") : 0;
}

int emu_double_speed_now(const emu_unit *u)
{
    return u->host ? bh_get_int(u->host, "double_speed_now") : 0;
}

double emu_time(const emu_unit *u)
{
    return ssi263_time(u->chip);
}

void emu_set_flash_timed(emu_unit *u, int on)
{
    if (u->tns)
        tns_flash_timed(u->tns, on);
    else
        bh_set_int(u->host, "flash_timed", on);
}

int emu_flash(const emu_unit *u, int *chip_erases)
{
    if (u->tns) {
        unsigned long n;
        int busy = tns_flash_busy(u->tns, &n, NULL);
        if (chip_erases) *chip_erases = (int)n;
        return busy;
    }
    if (chip_erases) *chip_erases = bh_get_int(u->host, "flash_chip_erases");
    return bh_get_int(u->host, "flash_busy");
}

int emu_save(emu_unit *u, const char *path)
{
    blc_time now;
    long long secs = host_now(&now);   /* when it was switched off: the controller keeps time from here */
    if (u->tns) {
        tns_clock_wall(u->tns, secs);
        return tns_save_state(u->tns, path);
    }
    bh_clock_wall(u->host, secs);
    return bh_save_state(u->host, path) && program_keep(u->host, path);
}

long emu_program_flash(emu_unit *u, unsigned char **data)
{
    if (!u->host)
        return 0;
    return bh_program_flash(u->host, data, NULL);
}

void emu_keys_down(emu_unit *u, int bits)
{
    if (!u->host)
        return;
    u->hold_until = 0;              /* the hand has the keys now: emu_hold_at_start's are not let up under it */
    bh_keys_down(u->host, bits & 0x7F);
    bars_hold(u, bars_of(bits));
}

int emu_hold_break = 0;

void emu_hold_at_start(emu_unit *u, int bits)
{
    if (!u->host || !(bits & 0x7F))
        return;
    bh_keys_down(u->host, emu_hold_break ? 0 : bits & 0x7F);
    u->hold_until = ssi263_time(u->chip) + EMU_HOLD_START_S;
}

int emu_memory(const emu_unit *u, const unsigned char **ram)
{
    return u->tns ? tns_memory(u->tns, 0, ram) : bh_memory(u->host, 0, ram);
}

int emu_braille(const emu_unit *u, unsigned char *cells, int capacity)
{
    return u->host ? bh_braille(u->host, cells, capacity) : 0;
}

void emu_braille_bars(emu_unit *u, int down)
{
    if (u->host)
        bars_hold(u, down & 3);     /* held as emu_keys_down's bars are: a quick press is never missed */
}

int emu_clock_time(const emu_unit *u, int alarm, blc_time *t)
{
    return u->tns ? tns_clock_time(u->tns, alarm, t) : bh_clock_time(u->host, alarm, t);
}

void emu_set_whine(emu_unit *u, int whine)
{
    if (u->host)
        bh_set_whine(u->host, whine);
}

int emu_set_idle(emu_unit *u, int sound, int keep_open, int pop_click, int tick)
{
    bl_idle_options o;
    if (!u->host)
        return 1;                   /* the Type 'n Speak: not measured */
    o.sound = sound;
    o.keep_open = keep_open;
    o.pop_click = pop_click;
    o.tick = tick;
    return bh_set_idle(u->host, &o);
}

void emu_set_volume(emu_unit *u, int volume)
{
    u->gain = MAKEUP * volume / 100.0;
}

int emu_serial_attach(emu_unit *u, int on)
{
    return u->tns ? tns_serial_attach(u->tns, on) : bh_serial_attach(u->host, on);
}

int emu_serial_space(const emu_unit *u)
{
    return u->tns ? tns_serial_space(u->tns) : bh_serial_space(u->host);
}

int emu_serial_write(emu_unit *u, const unsigned char *bytes, int n)
{
    return u->tns ? tns_serial_write(u->tns, bytes, n) : bh_serial_write(u->host, bytes, n);
}

int emu_serial_read(emu_unit *u, unsigned char *out, int cap, bl_serial_status *status)
{
    return u->tns ? tns_serial_read(u->tns, out, cap, status) : bh_serial_read(u->host, out, cap, status);
}
