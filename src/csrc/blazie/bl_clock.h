/* bl_clock.h -- the Blazie units' clock controller (Braille Lite 2000 and Type 'n Speak), as their firmware uses it.
 *
 * Found by running the firmware (port logs and watchpoints on the English and Spanish Braille Lite and the Type 'n
 * Speak), not from a document.  The Z180 keeps no time of day itself: it counts seconds from its own 10 Hz timer,
 * and asks a separate controller for the minute, hour, day, month and year.  That controller sits on the Z180's
 * CSI/O (clocked serial I/O) and drives its clock: the firmware arms the CSI/O with an external clock (CNTR SS =
 * 111) and the controller clocks every byte in both directions.
 *
 *   Z180 to controller: the firmware loads TRDR, arms TE, raises the 8255's port C bit 4 (Braille Lite: control
 *   word 09h to port 83h; Type 'n Speak: 09h to C3h) to call the controller, waits for TE to clear, and drops it
 *   (08h).  Controller to Z180: the firmware keeps RE and EIE armed (CNTR = 67h); each byte the controller clocks in
 *   is a CSI/O interrupt, and the handler re-arms.  A byte is a command or a tagged field:
 *
 *     02h        what follows is the clock          03h  ... the alarm
 *     04h        read: the controller answers with the selected set's fields, the year last (the firmware waits for it)
 *     20h + m    minute, its low 5 bits             05h  minute + 32 (after 20h + m; minutes 32-59)
 *     06h        minute "any" (after 20h + 1Fh)
 *     A0h + h    hour, 0-23 (the alarm's 31 = any hour)
 *     40h + m    month, 1-12 (the alarm's 0 = any)  60h + d  day, 1-31 (the alarm's 0 = any)
 *     80h + y    year, y = year - 1989 in 5 bits: 1989-2020.  The firmware sends a two-digit year yy as yy + 11
 *                (yy < 89) or yy - 89 (yy >= 89) with nothing masked: from 2021 on the sum reaches the hour's tag,
 *                and setting the year to 2021 or later sets the hour instead (as on a real unit: its protocol has
 *                no room for those years).  Read back, a year field of 0 (1989) means "no alarm set".
 *     0Ah        (controller to Z180) the alarm went off
 *
 *   Writing the clock's minute starts the minute over (the firmware zeroes its own seconds as it sends it).  When no
 *   answer comes, the firmware says "reset clock, first date, then time" and shows 1999: the emulator's unit did
 *   that before this model existed.
 *
 * What is inferred rather than seen: the controller's timing (a byte per BLC_BYTE_S), that its year counter wraps
 * after 2020, and when the alarm goes off: at the start of a minute whose fields match the alarm's, with 31 any hour,
 * 63 any minute, 0 any day or month, and a year of 2000 (what the firmware sends for a year typed "xx") any year.
 *
 * Time passes in the unit's own (emulated) CPU cycles, so a saved state goes on from where it was; a host may add
 * the time the unit was switched off (blc_advance_off: the controller keeps time on its battery; no alarm while off).
 * Portable C, no globals but the tests' control.
 */
#ifndef BL_CLOCK_H
#define BL_CLOCK_H

#include <stdio.h>
#include "../cpu/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLC_YEAR0 1989              /* the year field's 0 */
#define BLC_YEARS 32                /* 5 bits: 1989-2020 */
#define BLC_BYTE_S 0.001            /* the controller's pace: a byte per millisecond (model) */
#define BLC_SAVE_SIZE 64            /* blc_save's bytes */

enum { BLC_CLOCK, BLC_ALARM };
enum { BLC_YEAR, BLC_MONTH, BLC_DAY, BLC_HOUR, BLC_MINUTE, BLC_FIELDS };

typedef struct {
    int year, month, day, hour, minute, second;
} blc_time;

typedef struct {
    unsigned char f[2][BLC_FIELDS];  /* the clock and the alarm, as the controller holds them (year: 0 = 1989) */
    int sel;                         /* BLC_CLOCK or BLC_ALARM: where fields and reads go */
    unsigned long long sub;          /* CPU cycles into the clock's current minute */
    unsigned long long last;         /* the CPU cycle count it was last brought up to */
    unsigned long long next_tx;      /* the earliest cycle for its next byte to the Z180 */
    unsigned char out[32];           /* bytes waiting to go to the Z180 */
    int n_out;
    double hz;                       /* the CPU clock */
} blc_clock;

/* The tests' controls (an app never sets it): 1 the clock never advances, 2 year fields from the Z180 are dropped,
   3 a saved state leaves the controller out, 4 the year mapped as 0.7.0-0.7.5 did (the nearest same calendar) and
   saved units left on it, 5 a migrated unit's dated alarm left on the old year. */
extern int blc_break;

/* A controller with every field 0; `now`: the CPU's cycle count. */
void blc_init(blc_clock *c, double hz, unsigned long long now);
/* Sets the clock from a calendar time (the host's): the year as the controller can hold it (blc_year5). */
void blc_set(blc_clock *c, const blc_time *t);
/* The clock's or the alarm's fields as a calendar time (year 1989 + the field; second from the minute's progress). */
void blc_get(const blc_clock *c, int which, blc_time *t);
/* The year field for a calendar year: the year itself in 1989-2020; otherwise a multiple of 28 years away (2026 ->
   1998), the same calendar (the weekday of 1 January and the leap day), so the weekdays the firmware works out are
   right, and stay right as the controller counts on through its New Years.  Two limits remain: the controller's
   own year wraps from 2020 back to 1989 (a unit started in 2048 is in 2020, whose New Year gives 1989, not 2049's
   1993), and 2100 is not a leap year. */
int blc_year5(int year);
/* A saved unit whose clock is on 0.7.0-0.7.5's mapping (2026 -> 2015, which its New Year then made 2016) is moved
   onto blc_year5's, only when its date is the host's, give or take a day, so a date the user set is kept.  Its date
   is counted from its own 1 January, so a unit that counted 2016's 29 February comes back as 1 March.  A dated
   alarm moves with it (no alarm, any year, any month or day kept as they are).  1 when it was moved. */
int blc_migrate(blc_clock *c, const blc_time *host);

/* Called at every CPU step boundary: time passes up to the CPU's cycle count, a byte the firmware is sending
   (`selected`: the 8255's port C bit 4) is taken, and the next byte the controller has is clocked in when the
   firmware has the CSI/O armed to receive. */
void blc_step(blc_clock *c, z180 *cpu, int selected);
/* The time the unit was switched off: the clock goes on (no alarm goes off); at most a year is counted. */
void blc_advance_off(blc_clock *c, double seconds);
/* The byte level, without a CPU (blc_step uses these; tests too): a byte from the Z180; the controller's next byte
   to it (-1: none); time passing while the unit is on (an alarm can go off). */
void blc_put(blc_clock *c, int byte);
int blc_take(blc_clock *c);
void blc_advance(blc_clock *c, double seconds);

/* An 8255 control word: bit 7 set is a mode word (the outputs go low), clear a port C bit set/reset. */
void blc_ppi_control(unsigned char *port_c, int value);

/* The controller in BLC_SAVE_SIZE bytes, with the host's time it was saved at (seconds; -1 unknown); and back.
   blc_load returns 0 for bytes that are not a saved controller. */
void blc_save(const blc_clock *c, long long saved_at, unsigned char *buf);
int blc_load(blc_clock *c, const unsigned char *buf, long long *saved_at);
/* A saved state's tail: the controller after the memory (a state without it is a unit whose controller was never
   saved).  write: 1 on success; read: 1 when there was one. */
int blc_write_tail(FILE *f, const blc_clock *c, long long saved_at);
int blc_read_tail(FILE *f, unsigned char *buf);

#ifdef __cplusplus
}
#endif
#endif
