/* tns_board.h -- the Blazie Type 'n Speak board around a Z180, per instance: the QWERTY unit beside the Braille Lite
 * (bl_board.h).  The same firmware design and speech routines, on a different board, mapped by running the firmware
 * (the port map and the key codes below were measured, not taken from any source):
 *
 *   SSI-263 at ports 90h-94h (registers 0-4), its A/R request on /INT1.
 *   Serial: ASCI0 (9600 bit/s 8N1 from the firmware; 19200 for the storage commands), port B0h bit 0 = its line
 *   drivers on -- the Braille Lite's design (bl_serial.h).
 *   Keyboard: one byte per key event on port D0h, with /INT2: bit 7 = 1 key down, 0 key up; the low 7 bits are the
 *   key's position (tns_keys.h).  E0h (read): status -- bit 0 = 0 a key is waiting, bit 1 = 0 battery low,
 *   bit 2 = 0 power switch off; FFh when idle.  80h (read): watchdog.  B0h (write): power/control latch.
 *   F0h (write): the flash bank -- bit 5 opens a 128 KB window at E0000h onto the file flash, the low 5 bits pick
 *   the page (a 29F016: the firmware uses 2 MB; the board keeps the 4 MB the five bits reach).
 *   C3h (write): the 8255's control word; port C bit 4 calls the clock controller, which the firmware talks to over
 *   the Z180's CSI/O (bl_clock.h; on with tns_clock_on).
 *   Memory: the firmware image from physical 00000h; RAM everywhere else, including 0-3FFFFh past the image's end
 *   (a new file's text starts right after the program).
 *
 * The unit runs live from the start: the host drives A/R from its chip (tns_set_ar).  With no saved state the unit
 * starts cold -- Ctrl+Alt+Del held at power-on, its own cold reset -- and asks to set up its file system, its flash
 * and its folders (seven questions, each answered y; the Spanish unit's yes is s: ../../apps/blazie/tns_setup.h).
 */
#ifndef TNS_BOARD_H
#define TNS_BOARD_H

#include "bl_board.h"          /* bl_event */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tns_unit tns_unit;

/* firmware: a .TNS update file (the ROM image is found in it by content); state: a tns_save_state file, or NULL for
   a cold start (the unit's cold reset).  NULL on failure, with a reason in err. */
tns_unit *tns_create(const char *firmware, const char *state, char *err, int errlen);
void tns_destroy(tns_unit *u);

void tns_run(tns_unit *u, unsigned long long cycles);   /* run that many CPU cycles */
void tns_set_ar(tns_unit *u, int requesting);           /* the chip's A/R request */
/* a key event (bit 7 = down): queued, and delivered with /INT2 once the firmware has read the last one; 0 if 64 are
   already waiting */
int  tns_key(tns_unit *u, int code);
unsigned long long tns_cycles(const tns_unit *u);

/* the SSI-263 writes ('W') since the last tns_clear_events, in order */
int  tns_events(const tns_unit *u, const bl_event **events);
void tns_clear_events(tns_unit *u);

/* the serial port carried to a real port, as bl_board.h's bl_serial_*: unplugged (the default) the unit's bytes go
   nowhere and nothing arrives */
int  tns_serial_attach(tns_unit *u, int on);
int  tns_serial_write(tns_unit *u, const unsigned char *bytes, int n);
int  tns_serial_space(const tns_unit *u);
int  tns_serial_read(tns_unit *u, unsigned char *out, int cap, bl_serial_status *status);

/* the RAM (1 MB) and the file flash (4 MB), as a switched-off unit keeps them (and the clock controller when it is
   on); 1 on success */
int  tns_save_state(const tns_unit *u, const char *path);
/* the file flash's busy time, as bl_board.h's bl_flash_timed / bl_flash_busy (off by default; the emulator turns it
   on: initialising the flash then takes the chip erase's ~46 s, the unit chirping while it waits) */
void tns_flash_timed(tns_unit *u, int on);
int  tns_flash_busy(const tns_unit *u, unsigned long *chip_erases, unsigned long *sector_erases);

/* The clock controller on the CSI/O (bl_clock.h), called through the 8255 at C3h: as bl_board.h's bl_clock_on,
   bl_clock_time and bl_clock_wall. */
int  tns_clock_on(tns_unit *u, const blc_time *now, long long unix_now);
int  tns_clock_time(const tns_unit *u, int alarm, blc_time *t);
void tns_clock_wall(tns_unit *u, long long unix_now);
/* how fast the emulator runs the CPU against real time from now on (quick key response; 1 at its own speed): the
   clock keeps real time (#78) */
void tns_clock_speed(tns_unit *u, double speed);
/* tests: which 0, the 1 MB address space's RAM (the program at its start); which 1, the 4 MB file flash; the size */
int  tns_memory(const tns_unit *u, int which, const unsigned char **bytes);

/* the program's end in the address space (its image's length: RAM starts there), or -1 if the file holds no Type
   'n Speak image.  A unit whose file system was never set up put its first file's first byte below it, where the
   program is: that byte was never stored (../../apps/blazie/tns_rescue.h). */
long tns_program_end(const char *firmware);

/* Tests' must-fail control (an app never sets it): nonzero, a cold start sends the keys the emulator sent before
   this fix (the 0.6 and 0.7 previews), which the firmware takes as its warm reset (no file system, no folders). */
extern int tns_cold_break;

#ifdef __cplusplus
}
#endif
#endif
