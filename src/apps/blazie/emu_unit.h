/* emu_unit.h -- one emulated Blazie unit running in real time: the firmware, its board, the SSI-263, 16-bit PCM out.
 *
 * Two kinds: the Braille Lite 2000 (bl_board, braille chords; the Braille 'n Speak 2000 too, emu_model) and the Type
 * 'n Speak (tns_board, QWERTY key events).
 * Portable (no platform calls): the shell pulls audio with emu_render at the pace of its sound card, and pushes keys
 * with emu_key from its keyboard.  The two may come from different threads: the shell serialises them with its own
 * lock (see main_win.c), and the unit takes each key at an instruction boundary.
 *
 * Unlike the screen-reader drivers, the unit is NOT put into speech-box mode: it boots to its own main menu, as it
 * does when switched on, and everything it does -- speech, key echo, the channel left open until the firmware clicks
 * it off -- comes from the firmware.
 */
#ifndef BLAZIE_EMU_UNIT_H
#define BLAZIE_EMU_UNIT_H

#include "../../csrc/blazie/bl_serial.h"
#include "../../csrc/blazie/bl_clock.h"

typedef struct emu_unit emu_unit;

enum { EMU_BRAILLE_LITE, EMU_TYPE_N_SPEAK };

/* firmware: a .BNS (Braille Lite) or .TNS (Type 'n Speak) update file; state: its saved memory (for the Braille
   Lite required -- bl2_2003_warm.state; for the Type 'n Speak NULL = a cold start: the unit's cold reset,
   which asks to set up its file system, flash and folders, tns_setup.h).  whine: 0 off, 1 hiss, 2 whine (the Braille Lite's idle channel noise).  NULL on failure, the reason in
   err. */
emu_unit *emu_create(int kind, const char *firmware, const char *state, double out_rate, int whine, char *err,
                     int errlen);
void emu_destroy(emu_unit *u);
int emu_kind(const emu_unit *u);
/* The unit the firmware is for.  EMU_BRAILLE_LITE also runs the Braille 'n Speak 2000's firmware (BS03ENG.BNS,
   BS2SLL.BNS: its factory states from make_state.c): the Braille Lite 2000's board without the braille display
   (../../csrc/blazie/bl_board.h bl_model), the same six keys and space bar as chords; it has no advance bar (the
   firmware never reads that bit).  Its idle channel is the Braille Lite's (measured on Tomi's Braille Lite only).
   EMU_MODEL_BNS640 is the Braille 'n Speak 640 (BSPENG.BNS): the same board, with an OKI M6242B clock in place of
   the clock controller (../../csrc/blazie/bl_rtc6242.h). */
enum { EMU_MODEL_BRAILLE_LITE, EMU_MODEL_BNS2000, EMU_MODEL_TYPE_N_SPEAK, EMU_MODEL_BNS640 };
int emu_model(const emu_unit *u);
/* Raw display cells (up to 40), left to right in standard eight-dot order; 0 when unavailable.
   Copy of the last complete hardware latch, including any cursor dots set by the firmware. */
int emu_braille(const emu_unit *u, unsigned char *cells, int capacity);
/* Physical display bars, independent of keyboard chords: bit 0 forward, bit 1 back -- down while set, as
   emu_keys_down's EMU_ADVANCE and EMU_BACK (each kept down at least EMU_BAR_MIN_S, so a quick press is seen). */
void emu_braille_bars(emu_unit *u, int down);

/* renders `n` samples of the unit running in real time into out (16-bit mono PCM at out_rate) */
void emu_render(emu_unit *u, short *out, int n);
/* a key: for the Braille Lite a chord (chords.h bits), for the Type 'n Speak a key event (tns_board.h: bit 7 =
   down); 0 if the unit's key queue is full */
int emu_key(emu_unit *u, int key);
/* The Braille Lite's keys physically down now (chords.h bits; 0 when none), as they go down and come up, beside
   emu_key's chord when the last one comes up: the unit reads keys held while it starts (power-on, or its own restart
   after p-chord l): i-chord held is the cold reset, and so on (bl_board.h bl_keys_down).  No effect on the Type 'n
   Speak, whose keys go down and up through emu_key.
   The advance bars: EMU_ADVANCE and EMU_BACK, the Braille Lite 2000's two bars (bl_board.h bl_bars; the firmware
   never reads them through the chord port, so they are pressed on its port 81h instead).  Through emu_keys_down a
   bar is down while held; in an emu_key chord (a terminal's keys, which never come up) it is tapped, and the chord's
   dots go to the unit once the firmware has the bar down (its chord with the bar: advance bar + chord).  Either way
   a bar is kept down at least EMU_BAR_MIN_S of the unit's time (the firmware polls the bars every 100 ms).  A bar held
   through emu_keys_down is not pressed again by the chord that ends when it comes up.  The Braille 'n Speak has no
   bars: its firmware never reads them. */
#define EMU_ADVANCE 0x80
#define EMU_BACK 0x100
#define EMU_BAR_MIN_S 0.25
#define EMU_BAR_CHORD_S 0.15
void emu_keys_down(emu_unit *u, int bits);
/* the tests' control (an app never sets it): nonzero, emu_key sends a chord's bar as the old shells did -- port 40h
   bit 7, which the firmware never reads, so the bars do nothing */
extern int emu_bars_break;
/* Keys held as the unit is switched on: down from now until EMU_HOLD_START_S of the unit's time has passed, then up
   with no chord sent -- a person holding them while switching it on.  Call it right after emu_create (a restart is
   the unit saved, destroyed and created again from what it saved).  One-handed mode, the Braille Lite 2000's and the
   Braille 'n Speaks' (their manuals: "press a dot 6 as you turn on the unit", "a dot 3 when next turning on the
   unit" to leave it): EMU_ONE_HAND_ON and EMU_ONE_HAND_OFF.  The firmware keeps the mode in its memory, so the saved
   state carries it; the unit says "one hand mode on" at every start while it is on.  No effect on the Type 'n Speak
   (its manual has no one-handed mode). */
#define EMU_HOLD_START_S 2.0
#define EMU_ONE_HAND_ON 0x20
#define EMU_ONE_HAND_OFF 0x04
void emu_hold_at_start(emu_unit *u, int bits);
/* the tests' control (an app never sets it): nonzero, emu_hold_at_start lets the keys up at once */
extern int emu_hold_break;
/* the most cells emu_braille gives (bl_display.h BLD_MAX_CELLS) */
#define EMU_CELLS 40
/* saves what the unit keeps while switched off (its files and settings, its clock), in the state format emu_create
   reads; 1 on success.  The Braille Lite's program flash too, once a firmware update has written its other bank
   (bl_board.h bl_program_flash): beside the state as PATH.program (both banks, 512 KB), which emu_create puts back
   when its bank a is the firmware it loads -- an update into bank b, Blazie's way, the unit still booting in bank a
   and p-chord l reaching the update.  (A bank-a update is not kept: the board finds its code sites in bank a.)  Nor
   is bank a's serial number and owner's name lost (00080h-000B7h: Blazie's name brander, test_brand.c), the one part
   of bank a a unit writes for itself: kept the same way, and put back over this firmware's blank ones.  A
   PATH.program of this firmware whose bank b matches bank a again, with no name, is removed; one of another firmware
   is left.  A PATH ending in ".new" (a state written first and then put in place, as the Linux apps save) keeps its program
   beside the name without ".new". */
int emu_save(emu_unit *u, const char *path);
/* the Braille Lite's program flash, bank a then bank b (0 for the Type 'n Speak): tests */
long emu_program_flash(emu_unit *u, unsigned char **data);
/* The unit's clock controller (../../csrc/blazie/bl_clock.h): emu_create switches it on, going on from the clock
   saved in the state (plus the time the unit was switched off: the controller keeps time on its battery) or, with
   none saved, starting at the host's local time -- the year as the unit can hold it (1989-2020: a later year goes
   back 28 years at a time, the same calendar, which stays this year's through the unit's own New Years; a unit
   saved on 0.7.0-0.7.5's year is moved onto it).  The time then passes in the unit's own time.  emu_clock_time: the
   clock's (alarm 0) or the alarm's (1) fields. */
int emu_clock_time(const emu_unit *u, int alarm, blc_time *t);
/* tests: the unit's RAM as its 1 MB address space (the Braille Lite's 00000-3FFFF unused: the ROM); its size */
int emu_memory(const emu_unit *u, const unsigned char **ram);
/* tests: the host's time emu_create and emu_save use, in seconds since 1970 read as local time (-1: the real one) */
void emu_fake_host_time(long long seconds);
void emu_set_whine(emu_unit *u, int whine);
/* The Braille Lite's idle channel as the unit sounds (../../csrc/blazie/bl_idle.h), in place of emu_set_whine's:
   sound 0 none, 1 hiss, 2 whine, 3 as the unit (the hiss at even volumes, the whine at odd), at the measured level,
   the same at every volume; keep_open 0 only under speech, 1 until the unit clicks off, 2 always; pop_click: the pop
   when the channel opens and the click when it is clicked off; tick: the 10 Hz tick while the channel is heard.
   0 if out of memory.  No effect on the Type 'n Speak. */
int emu_set_idle(emu_unit *u, int sound, int keep_open, int pop_click, int tick);
/* 0-100: the output gain (the unit's own volume keys still work on top of it) */
void emu_set_volume(emu_unit *u, int volume);

/* The unit's serial port (its RS-232 port: WinDisk, PCDISK, a terminal or a screen reader on the far end;
   ../../csrc/blazie/bl_serial.h says what was measured).  Unplugged by default.  emu_serial_attach(u, 1) plugs it
   into the shell's port: emu_serial_write gives the unit the bytes that arrived there (returns how many it took --
   at most emu_serial_space; keep the rest for later), emu_serial_read hands out what the unit sent, one run of bytes
   per status (call until it returns 0; set the port to *status before sending the bytes).  The bytes move as the
   unit runs (emu_render), at the rate the firmware programmed, in the unit's time.  0 from attach: out of memory. */
int emu_serial_attach(emu_unit *u, int on);
int emu_serial_space(const emu_unit *u);
int emu_serial_write(emu_unit *u, const unsigned char *bytes, int n);
int emu_serial_read(emu_unit *u, unsigned char *out, int cap, bl_serial_status *status);
/* Quick key response (off by default; not the real unit's pace).  After a key the firmware works for a while before
   it speaks: after any chord in the Braille Lite's main menu, ~240 ms of CPU time with the chip's request left
   unanswered, speech at 280 ms; after a Type 'n Speak key, speech at 240 ms (measured in emulated time: 6.144 MHz, the
   clock the unit's own serial divisor and 10 Hz timer give).  On, the CPU runs EMU_QUICK_TURBO times faster from a
   key until the firmware loads its first spoken phoneme (or EMU_QUICK_LIMIT_S of chip time passes): the words come
   sooner, the phonemes themselves play as before. */
#define EMU_QUICK_TURBO 8.0
#define EMU_QUICK_LIMIT_S 1.0
void emu_set_quick(emu_unit *u, int on);
/* Quick response also ends when the Braille Lite firmware restarts (p-chord l): its start reads the keys held at the
   unit's own pace.  emu_restart_break: the tests' control (an app never sets it), nonzero keeps the old rule. */
extern int emu_restart_break;
/* The double-speed upgrade (off by default: it was a $99 option, not in every unit; ../../csrc/blazie/bl_board.h
   bl_set_double_speed).  On, the Braille Lite firmware's own switch (the status menu, then + (dots 3-4-6), answered
   y) runs its CPU twice as fast; off, that switch changes nothing, as on a unit without the upgrade.  It can change
   while the unit runs.  emu_double_speed_now: 1 while the CPU runs doubled.  No effect on the Type 'n Speak. */
void emu_set_double_speed(emu_unit *u, int on);
int emu_double_speed(const emu_unit *u);
int emu_double_speed_now(const emu_unit *u);
/* chip time in seconds (tests) */
double emu_time(const emu_unit *u);
/* The file flash (../../csrc/blazie/flash29.h): its erases and writes take a 29F016's typical time, on by default --
   initialising the flash (the Type 'n Speak's first start, the Braille Lite's reset) is a ~46 s chip erase, with the
   firmware's chirps while it waits.  emu_set_flash_timed(u, 0): done at once (tests' controls).  emu_flash: 1 while
   an erase runs, 2 while a byte programs, 0 idle; the chip erases so far. */
void emu_set_flash_timed(emu_unit *u, int on);
int emu_flash(const emu_unit *u, int *chip_erases);

#endif
