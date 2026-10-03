/* test_clock.c -- the units' clock controller (../../csrc/blazie/bl_clock.h) and the Braille Lite's keys held while
 * it starts (bl_board.h bl_keys_down): Jayson's reports, 2026-09-30.
 *
 *   test_clock unit                      the controller alone: its fields, its bytes, its calendar, saving, the alarm
 *   test_clock bl FIRMWARE STATE [PART]  the English Braille Lite through its own commands (o-chord s d, s t, t, d):
 *                                        the date and time set, the clock going on, the year, switched off and on;
 *                                        i-chord held through p-chord l's restart; and 1 March 2027's weekday
 *                                        (issue #3) (PART: clock, restart or year; start: only the clock read at
 *                                        the start, for the Spanish unit)
 *   test_clock tns FIRMWARE [PART]       the English Type 'n Speak from cold, its setup questions answered y (F4 F5,
 *                                        F9 s t, F9 s d; PART: clock)
 *
 * What the firmware says is read from its RAM: it writes the words before it speaks them ("12:35:02 pm",
 * "Wednesday September 30, 2015", "initialize file system").  The controls put one bug back and must fail:
 * TEST_CLOCK_BREAK=1 a clock that never advances, 2 year fields dropped, 3 the clock left out of a saved state,
 * 4 the year mapped as 0.7.0-0.7.5 did and saved units left on it (Jayson's issue #3) (bl_clock.h blc_break); TEST_CLOCK_HOLD_BREAK=1 keys never reported held (the app before), 2 a chord read held at
 * the start delivered again when it comes up (bl_board.h bl_keys_break), 3 quick key response left on through the
 * restart (emu_unit.h emu_restart_break).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#endif
#include "emu_unit.h"
#include "tns_setup.h"
#include "../../csrc/blazie/bl_board.h"   /* bl_keys_break */

#define RATE 11025
#define T2012 1330864440LL          /* 2012-03-04 12:34:00: the PC's clock, a year the tests never set */
#define T2015 1443616440LL          /* 2015-09-30 12:34:00 */
#define T2026_SEP 1790771640LL      /* 2026-09-30 12:34:00 */
#define T2026_EVE 1798761540LL      /* 2026-12-31 23:59:00 */
#define T2027_MAR 1803902400LL      /* 2027-03-01 12:00:00 */

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-34s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

/* ---- the controller alone --------------------------------------------------------------------------------------- */
static int weekday(int y, int m, int d)               /* 0 = Sunday (Sakamoto) */
{
    static const int k[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + k[m - 1] + d) % 7;
}

static void put(blc_clock *c, const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        blc_put(c, b[i]);
}

static void unit_tests(void)
{
    blc_clock c;
    blc_time t;
    unsigned char saved[BLC_SAVE_SIZE];
    char d[700];
    long long at = 0;
    static const unsigned char set_clock[] = {0x02, 0x20 | (34 & 0x1F), 0x05, 0xA0 | 12, 0x40 | 9, 0x60 | 30,
                                              0x80 | (15 + 11)};     /* 12:34, 9/30/15, as the firmware sends them */
    static const unsigned char want[] = {0x22, 0x05, 0xAC, 0x49, 0x7E, 0x9A};
    unsigned char got[8];
    int n = 0, b;

    snprintf(d, sizeof d, "1989 %d, 2015 %d, 2020 %d, 2026 %d (1998's calendar), 2024 %d (1996's), 2049 %d (1993's)",
             blc_year5(1989), blc_year5(2015), blc_year5(2020), blc_year5(2026), blc_year5(2024), blc_year5(2049));
    check("the year field", blc_year5(1989) == 0 && blc_year5(2015) == 26 && blc_year5(2020) == 31
                            && blc_year5(2026) == 9 && blc_year5(2024) == 7 && blc_year5(2049) == 4, d);

    {   /* Jayson's issue #3: the controller counts its own New Years, so the year it is set to must stay the host's
           calendar through them -- 28 years back.  The nearest same calendar (2026 -> 2015) became 2016, a leap
           year, and 1 March 2027 a Tuesday. */
        static const char *const wd[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                          "Saturday"};
        int w;
        blc_init(&c, 6144000.0, 0);
        t.year = 2026; t.month = 12; t.day = 31; t.hour = 23; t.minute = 59; t.second = 59;
        blc_set(&c, &t);
        blc_advance_off(&c, 1.0 + 59.0 * 86400.0);   /* switched off over New Year, on again on 1 March 2027 */
        blc_get(&c, BLC_CLOCK, &t);
        w = weekday(t.year, t.month, t.day);
        snprintf(d, sizeof d, "%04d-%02d-%02d, a %s (1 March 2027 is a %s)", t.year, t.month, t.day, wd[w],
                 wd[weekday(2027, 3, 1)]);
        check("the weekday after New Year", t.month == 3 && t.day == 1 && w == weekday(2027, 3, 1), d);
    }

    {   /* a unit saved by 0.7.0-0.7.5 (its year on the nearest calendar) is moved on loading; a date the user set,
           or one saved on the new year, is kept */
        static const struct {
            int year, month, day, host_year, host_month, host_day, want, want_month, want_day;
            const char *what;
        } m[] = {
            {2015, 9, 30, 2026, 9, 30, 1998, 9, 30, "2015 in 2026"},
            {2016, 2, 29, 2027, 3, 1, 1999, 3, 1, "2016 run on into 2027 (its leap day counted)"},
            {2016, 1, 15, 2027, 1, 15, 1999, 1, 15, "2016 run on into January 2027"},
            {2010, 3, 1, 2027, 3, 1, 1999, 3, 1, "2010 in 2027"},
            {2015, 12, 31, 2027, 1, 1, 1998, 12, 31, "2015 on New Year's Eve, the PC past midnight"},
            {2015, 6, 5, 2026, 9, 30, 2015, 6, 5, "a date the user set"},
            {1998, 9, 30, 2026, 9, 30, 1998, 9, 30, "saved on the new year"},
        };
        int i, bad = 0;
        char *p = d;
        d[0] = 0;
        for (i = 0; i < (int)(sizeof m / sizeof m[0]); i++) {
            blc_time host;
            memset(&host, 0, sizeof host);
            host.year = m[i].host_year; host.month = m[i].host_month; host.day = m[i].host_day;
            blc_init(&c, 6144000.0, 0);
            c.f[BLC_CLOCK][BLC_YEAR] = (unsigned char)(m[i].year - BLC_YEAR0);
            c.f[BLC_CLOCK][BLC_MONTH] = (unsigned char)m[i].month;
            c.f[BLC_CLOCK][BLC_DAY] = (unsigned char)m[i].day;
            blc_migrate(&c, &host);
            blc_get(&c, BLC_CLOCK, &t);
            if ((t.year != m[i].want || t.month != m[i].want_month || t.day != m[i].want_day) && ++bad
                && p - d < (int)sizeof d - 120) {
                p += snprintf(p, sizeof d - (p - d), "%s%s: %04d-%02d-%02d", p == d ? "" : "; ", m[i].what, t.year,
                              t.month, t.day);
            }
        }
        if (!bad)
            snprintf(d, sizeof d, "%d saved clocks: the old years moved, the user's and the new kept", i);
        check("a unit saved on the old year", !bad, d);
    }

    blc_init(&c, 6144000.0, 0);
    put(&c, set_clock, sizeof set_clock);
    blc_get(&c, BLC_CLOCK, &t);
    blc_put(&c, 0x04);
    while ((b = blc_take(&c)) >= 0 && n < 8)
        got[n++] = (unsigned char)b;
    snprintf(d, sizeof d, "%04d-%02d-%02d %02d:%02d; read: %d bytes %02X %02X %02X %02X %02X %02X", t.year, t.month,
             t.day, t.hour, t.minute, n, got[0], got[1], got[2], got[3], got[4], got[5]);
    check("set and read over its bytes", t.year == 2015 && t.month == 9 && t.day == 30 && t.hour == 12
                                          && t.minute == 34 && n == 6 && !memcmp(got, want, 6), d);

    blc_advance(&c, 30.0);
    blc_put(&c, 0x20 | 7);                              /* a minute written: the minute starts over */
    blc_get(&c, BLC_CLOCK, &t);
    snprintf(d, sizeof d, "%02d:%02d:%02d after 30 s and minute 7 written", t.hour, t.minute, t.second);
    check("a minute written starts it over", t.minute == 7 && t.second == 0, d);

    t.year = 2016; t.month = 2; t.day = 28; t.hour = 23; t.minute = 59; t.second = 30;
    blc_set(&c, &t);
    blc_advance(&c, 31.0);
    blc_get(&c, BLC_CLOCK, &t);
    snprintf(d, sizeof d, "2016-02-28 23:59:30 + 31 s = %04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day,
             t.hour, t.minute, t.second);
    check("time passes (a leap day)", t.year == 2016 && t.month == 2 && t.day == 29 && t.hour == 0 && t.minute == 0
                                      && t.second == 1, d);

    t.year = 2019; t.month = 12; t.day = 31; t.hour = 23; t.minute = 59; t.second = 59;
    blc_set(&c, &t);
    blc_advance(&c, 1.0);
    blc_get(&c, BLC_CLOCK, &t);
    snprintf(d, sizeof d, "2019-12-31 23:59:59 + 1 s = %04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour,
             t.minute);
    check("a new year", t.year == 2020 && t.month == 1 && t.day == 1 && t.hour == 0 && t.minute == 0, d);

    t.year = 2015; t.month = 9; t.day = 30; t.hour = 12; t.minute = 34; t.second = 20;
    blc_set(&c, &t);
    blc_save(&c, 12345, saved);
    blc_init(&c, 6144000.0, 0);
    n = blc_load(&c, saved, &at);
    blc_get(&c, BLC_CLOCK, &t);
    snprintf(d, sizeof d, "loaded %d: %04d-%02d-%02d %02d:%02d:%02d, saved at %lld", n, t.year, t.month, t.day,
             t.hour, t.minute, t.second, at);
    check("saved and loaded", n == 1 && t.year == 2015 && t.month == 9 && t.day == 30 && t.hour == 12
                              && t.minute == 34 && t.second == 20 && at == 12345, d);

    {   /* the alarm at 35 past any hour, any day: as the firmware sends "xx35" with the date "xxxxxx" */
        static const unsigned char alarm[] = {0x03, 0x20 | 35, 0x05, 0xA0 | 31, 0x40, 0x60, 0x80 | 11, 0x02};
        int fired, off;
        put(&c, alarm, sizeof alarm);
        blc_advance(&c, 39.0);                          /* 12:34:59 */
        fired = blc_take(&c);
        blc_advance(&c, 1.0);                           /* 12:35:00 */
        b = blc_take(&c);
        fired = fired < 0 && b == 0x0A;
        blc_advance_off(&c, 3600.0);                    /* switched off past 13:35: no alarm */
        off = blc_take(&c);
        blc_get(&c, BLC_CLOCK, &t);
        snprintf(d, sizeof d, "at 12:35 %s; switched off an hour: %s, %02d:%02d", fired ? "0Ah sent" : "nothing",
                 off < 0 ? "nothing" : "a byte", t.hour, t.minute);
        check("the alarm", fired && off < 0 && t.hour == 13 && t.minute == 35, d);
    }
}

/* ---- the firmware ----------------------------------------------------------------------------------------------- */
static emu_unit *g_u;
static double g_t;                  /* seconds rendered */

static void run_to(double t)
{
    static short buf[RATE / 100];
    while (g_t + 0.01 <= t + 1e-9) {
        emu_render(g_u, buf, RATE / 100);
        g_t += 0.01;
    }
}

static void key(double t, int k)    /* the Braille Lite: a chord; the Type 'n Speak: a key down and up */
{
    run_to(t);
    if (emu_kind(g_u) == EMU_TYPE_N_SPEAK) {
        emu_key(g_u, k | 0x80);
        emu_key(g_u, k & 0x7F);
    } else
        emu_key(g_u, k);
}

static void keys(double t, const int *k, int n)   /* 0.4 s apart */
{
    int i;
    for (i = 0; i < n; i++)
        key(t + 0.4 * i, k[i]);
}

static int ram_has(const char *text)
{
    const unsigned char *m;
    int n = emu_memory(g_u, &m), len = (int)strlen(text), i;
    for (i = 0; i + len <= n; i++)
        if (m[i] == (unsigned char)text[0] && !memcmp(m + i, text, (size_t)len))
            return 1;
    return 0;
}

static void start(int kind, const char *fw, const char *st, long long host)
{
    char err[256];
    emu_fake_host_time(host);
    g_u = emu_create(kind, fw, st, RATE, 0, err, sizeof err);
    if (!g_u) {
        printf("FAIL create: %s\n", err);
        exit(1);
    }
    emu_set_flash_timed(g_u, 0);            /* the cold start's flash erase done at once: its ~46 s are test_flash's */
    g_t = 0.0;
}

static void clock_checks(int kind, const char *fw, const char *st, int only_start)
{
    int tns = kind == EMU_TYPE_N_SPEAK;
    /* the Braille Lite: o-chord, s, d / t and computer-braille digits (dot 2 = 1 ...), p; the Type 'n Speak: F9, s,
       d / t, the digit keys, a */
    static const int bl_date[] = {0x55, 0x0E, 0x19}, bl_time[] = {0x55, 0x0E, 0x1E};
    static const int bl_mmddyy[] = {0x34, 0x14, 0x12, 0x34, 0x02, 0x22};       /* 0 9 3 0 1 5 */
    static const int bl_hhmm[] = {0x02, 0x06, 0x12, 0x32};                       /* 1 2 3 4 */
    static const int tns_date[] = {0xC6, 0xAC, 0xB4}, tns_time[] = {0xC6, 0xAC, 0xB5};
    static const int tns_mmddyy[] = {0x8B, 0xCF, 0xCF, 0x8B, 0x8B, 0xAD};      /* 1 0 0 1 1 4 */
    static const int tns_hhmm[] = {0xCF, 0xC7, 0xAD, 0x97};                     /* 0 9 4 5 */
    const char *set_date = tns ? "October 1, 2014" : "September 30, 2015";
    const char *minute_on = tns ? "9:46" : "12:35", *later = tns ? "9:48" : "12:37";
    blc_time t;
    char d[300], path[64];
    int ok;
    /* the Type 'n Speak from cold: its cold reset's questions answered y (tns_setup.h); every key after them o
       seconds later than the Braille Lite's, and the PC's clock o seconds earlier, so the times said are the same */
    double o = tns ? tns_setup_ready_at(0) - 12.0 : 0.0;

    start(kind, fw, st, T2012 - (long long)o);
    if (tns) {
        int k;
        for (k = 0; k < TNS_SETUP_ANSWERS; k++)
            key(tns_setup_answer_at(k, 0), tns_setup_yes_code('y'));
    }
    key(o + 12.0, tns ? 0x0E : 0x55);   /* F4, or o-chord ... */
    if (!tns)
        key(13.5, 0x1E);            /* ... t */
    run_to(o + 16.0);
    emu_clock_time(g_u, 0, &t);
    snprintf(d, sizeof d, "the PC's 2012-03-04 12:34: the unit said 12:34: %s", ram_has("12:34") ? "yes" : "no");
    check("started from the PC's clock", ram_has("12:34") && t.year == 2012 && t.hour == 12, d);
    if (only_start) {               /* the Spanish unit: its words are Spanish, its time text the same */
        emu_destroy(g_u);
        return;
    }

    /* the date, then the time, through the unit's own commands */
    keys(o + 18.0, tns ? tns_date : bl_date, 3);
    keys(o + 21.0, tns ? tns_mmddyy : bl_mmddyy, 6);
    keys(o + 28.0, tns ? tns_time : bl_time, 3);
    keys(o + 31.0, tns ? tns_hhmm : bl_hhmm, 4);
    key(o + 34.0, tns ? 0x14 : 0x0F);   /* a (am) / p (pm) */
    run_to(o + 38.0);
    emu_clock_time(g_u, 0, &t);
    ok = tns ? (t.year == 2014 && t.month == 10 && t.day == 1 && t.hour == 9 && t.minute == 45)
             : (t.year == 2015 && t.month == 9 && t.day == 30 && t.hour == 12 && t.minute == 34);
    snprintf(d, sizeof d, "the clock holds %04d-%02d-%02d %02d:%02d:%02d; the unit said \"%s\": %s", t.year, t.month,
             t.day, t.hour, t.minute, t.second, set_date, ram_has(set_date) ? "yes" : "no");
    check("set through the unit's commands", ok && ram_has(set_date), d);

    /* a minute on: the clock goes on, in the unit's time, and the unit reads it */
    key(o + 97.0, tns ? 0x0E : 0x55);
    if (!tns)
        key(98.5, 0x1E);
    run_to(o + 101.0);
    emu_clock_time(g_u, 0, &t);
    snprintf(d, sizeof d, "a minute on: %02d:%02d:%02d; the unit said %s: %s", t.hour, t.minute, t.second, minute_on,
             ram_has(minute_on) ? "yes" : "no");
    check("the clock goes on", ram_has(minute_on) && t.minute == (tns ? 46 : 35), d);

    key(o + 102.0, tns ? 0x16 : 0x55);  /* F5, or o-chord d */
    if (!tns)
        key(103.5, 0x19);
    run_to(o + 107.0);
    snprintf(d, sizeof d, "the unit read the date back: \"%s\": %s", set_date, ram_has(set_date) ? "yes" : "no");
    check("the year read back", ram_has(set_date), d);

    /* switched off for two minutes and on again: the clock kept time */
    snprintf(path, sizeof path, "test_clock.%d.state", (int)_getpid());
    emu_fake_host_time(T2012 + 1000);
    check("saved", emu_save(g_u, path), path);
    emu_destroy(g_u);
    start(kind, fw, path, T2012 + 1000 + 120);
    key(8.0, tns ? 0x0E : 0x55);
    if (!tns)
        key(9.5, 0x1E);
    run_to(12.0);
    emu_clock_time(g_u, 0, &t);
    snprintf(d, sizeof d, "off 120 s: %02d:%02d:%02d; the unit said %s: %s", t.hour, t.minute, t.second, later,
             ram_has(later) ? "yes" : "no");
    check("switched off and on", ram_has(later), d);
    emu_destroy(g_u);
    remove(path);
}

/* p-chord, l: the unit restarts (the other language bank; ours holds the same firmware in both), and i-chord held
   while it starts is its cold reset: "initialize file system?" -- and not a chord again when it comes up */
static void restart_checks(const char *fw, const char *st)
{
    const char *hb = getenv("TEST_CLOCK_HOLD_BREAK");
    int brk = hb ? atoi(hb) : 0;
    double heard;
    char d[200];
    short buf[RATE / 100];
    int i;
    double s = 0;
    bl_keys_break = brk == 2;
    start(EMU_BRAILLE_LITE, fw, st, T2015);
    key(8.0, 0x4F);                 /* p-chord */
    key(10.0, 0x07);                /* l: the restart ~0.1 s later; its start reads the keys ~0.35 s after that */
    run_to(10.25);
    if (brk != 1)
        emu_keys_down(g_u, 0x4A);   /* i-chord (dots 2 4 and space) held */
    run_to(12.0);
    emu_keys_down(g_u, 0);
    emu_key(g_u, 0x4A);             /* the chord as the app sends it when its keys come up */
    run_to(12.1);
    for (i = 0; i < 35; i++) {      /* the question goes on (a chord read again cuts it and starts it over) */
        emu_render(g_u, buf, RATE / 100);
        g_t += 0.01;
        {
            int j;
            for (j = 0; j < RATE / 100; j++)
                s += (double)buf[j] * buf[j];
        }
    }
    heard = s / (35.0 * (RATE / 100)) / (32768.0 * 32768.0);
    run_to(14.0);
    snprintf(d, sizeof d, "the unit asked \"initialize file system\": %s", ram_has("initialize file system") ? "yes" : "no");
    check("i-chord held through the restart", ram_has("initialize file system"), d);
    snprintf(d, sizeof d, "power %.2e over 12.1-12.45 s, as its keys came up", heard);
    check("the held chord is not sent again", heard > 1e-5, d);
    emu_destroy(g_u);
    bl_keys_break = 0;

    /* the same with quick key response on: it ends at the restart, so the start reads the keys at its own pace */
    emu_restart_break = brk == 3;
    start(EMU_BRAILLE_LITE, fw, st, T2015);
    emu_set_quick(g_u, 1);
    key(8.0, 0x4F);
    key(10.0, 0x07);
    run_to(10.25);
    emu_keys_down(g_u, 0x4A);
    run_to(12.0);
    emu_keys_down(g_u, 0);
    emu_key(g_u, 0x4A);
    run_to(14.0);
    snprintf(d, sizeof d, "with quick key response: the unit asked \"initialize file system\": %s",
             ram_has("initialize file system") ? "yes" : "no");
    check("held through the restart, quick", ram_has("initialize file system"), d);
    emu_destroy(g_u);
    emu_restart_break = 0;
}

/* Jayson's issue #3 through the English Braille Lite's own date (o-chord d): a unit switched off on New Year's Eve
   2026 and on again on 1 March 2027 says Monday, as 2027 does (in 1999, its year 28 back); and a unit saved by
   0.7.0-0.7.5 (2015 for 2026) is moved onto that year when it is loaded.  On the old year it said "Monday February
   29, 2016". */
static void year_checks(const char *fw, const char *st)
{
    static const char *const want = "Monday March 1, 1999";
    static const struct { long long saved; int old; const char *what; } c[] = {
        {T2026_EVE, 0, "switched off on New Year's Eve 2026"},
        {T2026_SEP, 1, "saved by 0.7.5 in September 2026"},
    };
    char d[200], path[64];
    int i, brk = blc_break;
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "test_clock_year.%d.state", (int)_getpid());
        if (c[i].old && !brk)
            blc_break = 4;          /* the old year mapping, to make the old unit's state */
        start(EMU_BRAILLE_LITE, fw, st, c[i].saved);
        run_to(2.0);
        emu_save(g_u, path);
        emu_destroy(g_u);
        blc_break = brk;
        start(EMU_BRAILLE_LITE, fw, path, T2027_MAR);
        key(8.0, 0x55);             /* o-chord d */
        key(9.5, 0x19);
        run_to(14.0);
        snprintf(d, sizeof d, "on 1 March 2027 the unit said \"%s\": %s", want, ram_has(want) ? "yes" : "no");
        check(c[i].what, ram_has(want), d);
        emu_destroy(g_u);
        remove(path);
    }
}

int main(int argc, char **argv)
{
    const char *brk = getenv("TEST_CLOCK_BREAK"), *part;
    blc_break = brk ? atoi(brk) : 0;
    if (argc >= 2 && !strcmp(argv[1], "unit"))
        unit_tests();
    else if (argc >= 4 && !strcmp(argv[1], "bl")) {
        part = argc > 4 ? argv[4] : "";
        if (!*part || !strcmp(part, "clock"))
            clock_checks(EMU_BRAILLE_LITE, argv[2], argv[3], 0);
        if (!strcmp(part, "start"))
            clock_checks(EMU_BRAILLE_LITE, argv[2], argv[3], 1);
        if (!*part || !strcmp(part, "restart"))
            restart_checks(argv[2], argv[3]);
        if (!*part || !strcmp(part, "year"))
            year_checks(argv[2], argv[3]);
    } else if (argc >= 3 && !strcmp(argv[1], "tns"))
        clock_checks(EMU_TYPE_N_SPEAK, argv[2], NULL, 0);
    else {
        printf("usage: test_clock unit | bl FIRMWARE STATE [clock|restart|start|year] | tns FIRMWARE\n");
        return 2;
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
