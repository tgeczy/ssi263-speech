/* bl_board.h -- the Blazie Braille Lite 2000 board around a Z180 core, as a library: one instance per unit.  The
 * Braille 'n Speak 2000 runs on it too (bl_model).
 *
 * The live path of z180emu/bns.c (the investigation tool, which keeps its tracing), per instance, so a program can
 * run an English and a Spanish unit side by side.  The unit's SSI-263 writes and serial bytes come back as events,
 * in order, per call; the host (the SSI-263 model and the lockstep) applies them.  Behaviour is bns.c's --live mode
 * line for line: nvda/tools/golden/blazie_*.txt gates it.
 *
 * Not thread-safe per instance; different instances may be used from different threads.
 */
#ifndef BL_BOARD_H
#define BL_BOARD_H

#include "bl_serial.h"
#include "bl_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bl_unit bl_unit;

typedef struct {
    unsigned char type;          /* 'W': an SSI-263 write (a = register 0-4, b = value); 'T': a serial byte (a) */
    unsigned char a, b;
} bl_event;

/* firmware: a .BNS update file (the ROM image from file offset 3000h); state: battery-backed RAM (256 KB) + the file
   flash (512 KB or 2 MB: bl_save_state), as make_states.sh saves them; phon_ms: the stand-in A/R timing before live mode (bns --phon-ms);
   keys: braille chords pressed at those instruction counts during the boot (bns --key INSTR=CHORD).
   The firmware is a Braille Lite 2000's or a Braille 'n Speak 2000's (bl_model).  NULL on failure, with a reason in
   err. */
bl_unit *bl_create(const char *firmware, const char *state, double phon_ms,
                   const unsigned long long *key_at, const unsigned char *key_val, int n_keys,
                   char *err, int errlen);
void bl_destroy(bl_unit *u);

/* The unit the firmware was built for, told by its idle loop (bl_create's sites).  The Braille 'n Speak 2000 is the
   Braille Lite 2000's board without the braille display, measured by running both on this board (BS03ENG.BNS and
   BS2SLL.BNS against BL2ENG.BNS, the same keys at the same instruction counts): the same ports and port values --
   keyboard at 40h, SSI-263 at C0h-C4h, E0h's memory and flash banks, A0h's power bits, the battery gauge on B0h/81h,
   the clock controller on the CSI/O and the 8255's port C bit 4, the 2 MB file flash -- the same memory test and the
   same prompts at the same points; only the display's traffic is missing (the Braille Lite clocks it through the
   8255's port C bits 0-2, about 6000 control words in its first 30 million instructions; the Braille 'n Speak writes
   six, its clock controller's bit alone).  Port 40h bit 7 (the advance bar on the Braille Lite) is never used. */
#define BL_MODEL_BRAILLE_LITE 0      /* Braille Lite 2000 */
#define BL_MODEL_BNS2000 1           /* Braille 'n Speak 2000 */
int  bl_model(const bl_unit *u);
/* Last complete display latch in standard dot order (dot 1 = bit 0, dot 8 = bit 7), left to right.
   Returns 18 or 40 cells copied, or 0 before the first valid latch, without a display, or if capacity is too small.
   The padding shift registers are not cells. This is hardware output, including the firmware cursor dots. */
int bl_braille(const bl_unit *u, unsigned char *cells, int capacity);
/* Physical display bars: bit 0 forward, bit 1 back. Active-low contacts at port 81h bits 6/7.
   These contacts are polled separately from the chord keyboard. */
void bl_braille_bars(bl_unit *u, int down);

void bl_boot(bl_unit *u, unsigned long long target_instr);   /* bns 'B': run until that instruction count */
void bl_live(bl_unit *u);                                    /* bns 'LIVE': the host drives A/R from now on */
void bl_run(bl_unit *u, unsigned long long cycles);          /* bns 'R': run that many CPU cycles */
void bl_set_ar(bl_unit *u, int requesting);                  /* bns 'A' */
void bl_queue(bl_unit *u, const unsigned char *bytes, int n);/* bns 'S': bytes for the unit's serial input */
void bl_urgent(bl_unit *u, int byte);                        /* bns 'U': one byte delivered even under XOFF (^X) */
int  bl_drop(bl_unit *u);                                    /* bns 'D': drop queued input; the ^F markers dropped */
unsigned long long bl_cycles(const bl_unit *u);              /* CPU cycles so far */
/* the last value the firmware wrote to port A0 (0 before any).  Bit 1 is the speech channel's power, watched on the
   running firmware (English and Spanish): set ~0.26 s before a line's first phoneme when the channel was off, cleared
   with R3 = 00 when the firmware clicks the channel off (~10 s after the last phoneme).  The emulator's idle sounds
   (bl_idle.h) follow it. */
int  bl_port_a0(const bl_unit *u);
/* a braille key chord pressed live (port 40h: dot 1 = bit 0 .. dot 6 = bit 5, space = bit 6, bit 7 = an advance
   key): latched with /INT2 at the next boundary after the last chord was read; 0 if 16 are already waiting */
int  bl_key(bl_unit *u, int chord);
/* the battery gauge's reading (0-255, the converter's raw value); -1 (the default) leaves ports B0h/81h as they
   were: the status menu's % then waits forever */
void bl_battery(bl_unit *u, int level);
/* a braille chord held down at power-on (bns --hold): port 40h reads it (chord & 7Fh) until the firmware reaches the
   point where it waits for the keys' release, or a key is pressed.  Call before the first bl_boot. */
void bl_hold(bl_unit *u, int chord);
/* The braille keys physically down now (bl_key's bits), for a host whose keyboard reports each key (the emulator):
   port 40h reads them while no chord waits to be read.  The firmware reads that port only for a chord it was
   interrupted for, and while it starts (power-on, or its own restart such as p-chord l's): there it looks for a
   chord held down (i-chord: the cold reset; all seven keys: the warm reset; ...).  A chord the starting firmware
   read that way is not delivered again when its keys come up: bl_key takes it and does nothing.  Never called (the
   drivers' path), nothing changes. */
void bl_keys_down(bl_unit *u, int bits);
extern int bl_keys_break;   /* the tests' control (an app never sets it): nonzero delivers that chord again */
/* how many times the firmware has run from its reset vector (power-on, and each restart: p-chord l) */
int  bl_starts(const bl_unit *u);
/* the battery-backed RAM + file flash, in bl_create's state format (what a real unit keeps while switched off);
   1 on success.  The file flash is 2 MB; while all but its first 512 KB is erased the state keeps the 786432-byte
   format (256 KB + 512 KB) every earlier state has, otherwise it is 256 KB + 2 MB; with the clock controller on
   (bl_clock_on) its 64-byte tail follows (bl_clock.h).  bl_create reads all four sizes. */
int  bl_save_state(const bl_unit *u, const char *path);
/* The file flash's busy time (flash29.h): off (the default) every erase and program is done at once, as the
   screen-reader drivers and their state recipes need; on (the emulator), an erase takes the chip's typical time and the
   firmware chirps through the speech chip while it waits (an initialisation's chip erase: ~46 s).  bl_flash_busy: 1
   while an erase runs, 2 while a byte programs, 0 idle; the counts of chip and sector erases so far (NULL: not
   wanted). */
void bl_flash_timed(bl_unit *u, int on);
int  bl_flash_busy(const bl_unit *u, unsigned long *chip_erases, unsigned long *sector_erases);

/* The clock controller on the Z180's CSI/O (bl_clock.h): the time and date the firmware reads and sets.  Off by
   default (the screen-reader drivers' path: the firmware then finds no clock, as it always did).  bl_clock_on
   switches it on: with the controller saved in the state, it goes on from there, plus the time between unix_now and
   when it was saved (bl_clock_wall; both in seconds, -1 unknown); without one, it starts at `now` (NULL: every field
   0).  0 if out of memory.  bl_clock_time: the clock's (alarm 0) or the alarm's (1) fields; 0 when off. */
int  bl_clock_on(bl_unit *u, const blc_time *now, long long unix_now);
int  bl_clock_time(const bl_unit *u, int alarm, blc_time *t);
void bl_clock_wall(bl_unit *u, long long unix_now);   /* the host's time, saved with the controller by bl_save_state */

/* The serial port carried to a real port (the emulator's COM port; bl_serial.h): from bl_serial_attach(u, 1) the
   unit's serial bytes no longer come back as 'T' events, nor does bl_queue feed it -- bl_serial_write gives it what
   arrived (returns how many it took: at most bl_serial_space), bl_serial_read hands out what it sent, each run of
   bytes with the line status it left under (call until 0).  Off (the default) is the screen-reader drivers' path,
   unchanged.  bl_serial_attach returns 0 when out of memory. */
int  bl_serial_attach(bl_unit *u, int on);
int  bl_serial_write(bl_unit *u, const unsigned char *bytes, int n);
int  bl_serial_space(const bl_unit *u);
int  bl_serial_read(bl_unit *u, unsigned char *out, int cap, bl_serial_status *status);

/* The board's state as the firmware sees it, for comparing two units at a checkpoint (nvda/tools/run_ahead_state.py):
   read without side effects.  Not a save state: the Z180's internal timers and interrupt flags are not exposed by
   ../cpu/cpu.h, so they are not here either. */
typedef struct {
    unsigned long long cycles;                   /* CPU T-states since reset */
    int pc, sp, iff1, iff2, im, halted, sleeping;
    int ssi[5];                                  /* the SSI-263 registers as the firmware last wrote them */
    int ssi_ar, ssi_mode, int1;                  /* A/R as the unit sees it; its mode; /INT1 asserted */
    int key_latched, int2, n_live_keys;          /* a braille chord waiting to be read; /INT2 */
    int queued, urgent, host_xoff;               /* serial input not yet taken; a ^X in flight; the unit's XOFF */
    int port_a0, port_e0;
    int asci_cntla, asci_cntlb, asci_stat, asci_asext, asci_astc;
} bl_probe;
void bl_probe_get(const bl_unit *u, bl_probe *p);
/* which 0: the 1 MB address space's RAM (00000-3FFFF unused: the ROM), which 1: the 2 MB file flash; the size */
int bl_memory(const bl_unit *u, int which, const unsigned char **bytes);

/* the events since the last bl_clear_events, in order */
int  bl_events(const bl_unit *u, const bl_event **events);
void bl_clear_events(bl_unit *u);
/* events the board could not store (its buffer could not grow): lost from the list above, and counted here -- a chip
   write or a transmitted byte gone, which a host must treat as a fault, never pass over (Astra, Reply 112) */
int  bl_events_lost(const bl_unit *u);
void bl_clear_events_lost(bl_unit *u);
/* tests: the n-th event from now cannot be stored, as when an allocation fails (0: none) */
void bl_fail_event(bl_unit *u, int n);
/* Tests' must-fail control (an app never sets it): nonzero, a read of 00h-3Fh with the high byte not 0 answers FFh
   as before the bus fix (both boards' io_read): SIMON.BNS's IN A,(34h) wait never ends (test_games.c) */
extern int bl_bus_break;

#ifdef __cplusplus
}
#endif
#endif
