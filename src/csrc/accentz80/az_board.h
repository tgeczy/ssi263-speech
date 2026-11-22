/* az_board.h -- the Aicom Accent SA, the stand-alone box, around a Z80 core (MAME's, ../cpu/z80_mame.cpp, through
 * ../cpu/cpu.h), as a library: one instance per unit.
 *
 * The machine is the one Kevin Horton traced from his own Accent SA and wrote down: his drawing of the board, the
 * equations of its PLD (read back from the part) and his memory and I/O maps.  What the firmware itself was then
 * seen to do on this board (4.1 of 2 October 1996, run here) is marked "seen".
 *
 *   memory, by the PLD:
 *   0000-6FFF  ROM U5 (program)
 *   7000-7FFF  RAM: the 6264's upper half (1000h-1FFFh; its A12 is the PLD's BANK, high here).  SP starts at 8000h
 *   8000-FFFF  by MODE, the control latch's bits 0-1:
 *                0  U5's upper half            1  U4's upper half (U4's A15 is BANK too)
 *                2  U4's lower half            3  the RAM's LOWER half (0000h-0FFFh), eight times over
 *              ROM ignores writes.  Seen in mode 3: address 8000h only (a read and a write test at power-up), so the
 *              other 4095 bytes and the seven mirrors are the equations', not exercised.
 *
 *   I/O, by the PLD: the device is chosen by A12-A15 -- the HIGH byte of the port address, which is B for IN r,(C) /
 *   OUT (C),r and A for IN A,(n) / OUT (n),A.  Seen: the firmware puts the same number on both halves (0000h, 0101h
 *   ... 2121h, 4040h), except its first write after reset, LD B,40h / OUT (C),A, which loads only the half that
 *   matters.
 *   0xxx  the SC-02 (SSI-263): RS0-RS2 are A0-A2, straight (R0 at 0 ... R4 at 4); any read has A/R in bit 7
 *   1xxx  the foot switch inputs on D0-D3, read only.  Not seen read; "foot" (0Fh: a GUESS, none pressed)
 *   2xxx  the 8251 (../accentpc/as_usart.h: the same mode and commands as the Accent-PC's firmware, seen: 4Eh, then
 *         15h and 37h as its buffer fills and empties): A0 = 0 data, 1 control.  RxRDY is /INT (mode 1: RST 38h).
 *         Its select is not in the PLD's equations; "20-2F" is Kevin's map, taken as the others are
 *   4xxx  read: the option switches: dips 5, 6, 7, 8 on D1, D2, D3, D7 and two fixed bits, D4 = 1 and D5 = 0
 *         ("switches", 10h: all four reading 0; which position that is, and what each does, is not known -- none
 *         changed what the unit said here).  write: the control latch: bits 0-1 MODE, bit 3 the LED (0 = on), bit 4
 *         NMI enable, bit 5 "RI" to the serial connector.  Power-up value 00h (ASSUMED: the firmware's first act is
 *         to write it)
 *   8xxx  decoded by the PLD and wired to nothing; the firmware reads and writes it once at power-up (code it shares
 *         with the Accent-PC).  This and every other undecoded port read FFh (a GUESS: nothing drives the bus) and
 *         are counted ("unmapped_in", "unmapped_out")
 *
 *   /NMI       the chip's A/R AND the latch's bit 4.  The Z80 takes it on the EDGE: one acceptance each time the
 *              line goes active, whether A/R rose or the firmware raised bit 4 with A/R already up.  A write to the
 *              chip or the latch moves the line at once, inside the slice; A/R rising is the host's to report
 *              (az_board_nmi), since the chip runs between slices
 *   clock      8 MHz (the drawing: the 8.000 MHz oscillator to the Z80; its 2 MHz tap, halved inside the chip by the
 *              strapped DIV2 pin, is the SC-02's 1 MHz).  The board keeps no time: the host runs it in T-states
 *
 * The Z80's registers at power-up are the core's (../cpu/CONTRACT.md 15); the firmware loads what it uses.
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef AZ_BOARD_H
#define AZ_BOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AZ_U5_SIZE 0x10000                 /* 64 KB: the program and, above 8000h, MODE 0 */
#define AZ_U4_SIZE 0x10000                 /* 64 KB: MODE 2 (lower half) and MODE 1 (upper half): the dictionary */
#define AZ_CPU_HZ 8000000.0

typedef struct az_board az_board;

typedef struct {
    void *ctx;
    void (*write)(void *ctx, int reg, int val);   /* an SSI-263 register write, reg 0-7 as A0-A2 give it */
    int (*request)(void *ctx);                    /* A/R asserted */
} az_chip;

/* The two ROMs, copied (the caller may free them).  Exact sizes are required.  NULL on failure, the reason in err. */
az_board *az_board_create(const uint8_t *u5, size_t n5, const uint8_t *u4, size_t n4, const az_chip *chip, char *err,
                          int errlen);
void az_board_destroy(az_board *b);

/* one byte into the 8251 when it is free and RTS is up; it raises /INT */
void az_board_serial(az_board *b);
/* the /NMI line from A/R and the latch, given to the core when it changed (the host calls it after the chip ran) */
void az_board_nmi(az_board *b);
/* whole instructions until at least `budget` T-states; returns those run */
uint64_t az_board_run(az_board *b, uint64_t budget);

int az_board_send(az_board *b, const uint8_t *bytes, int n);   /* the host's bytes; 1, or 0 when out of memory */
void az_board_drop_input(az_board *b);     /* the queued bytes, not the loaded one */
int az_board_tx(const az_board *b, const uint8_t **bytes);     /* every byte the firmware sent */

/* state, by name: "latch", "mode", "switches" (settable), "foot" (settable), "usart_cmd", "usart_mode_next", "rx"
   (queued), "rx_ready", "nmi_line", "nmi_edges" (since power-up), "unmapped_in", "unmapped_out", "waiting_known"
   (this U5's waiting loops were measured: az_board_take_work counts), "pc", "sp", "af", "bc", "de", "hl", "im",
   "iff1", "halted"; -1: unknown */
int az_board_get(const az_board *b, const char *name);
void az_board_set(az_board *b, const char *name, int v);
uint64_t az_board_cycles(const az_board *b);   /* the core's T-states since power-on */
uint64_t az_board_steps(const az_board *b);
uint8_t az_board_peek(const az_board *b, uint16_t addr);   /* memory as the CPU reads it now */
/* How many instructions the Z80 has run outside the firmware's waiting loops (az_waiting.inc, by firmware) since
   the last call -- its own work, reading text and making phonemes; then 0.  Always 0 for a U5 that was not
   measured.  Watches only. */
unsigned az_board_take_work(az_board *b);
/* For measuring those loops (az_waiting.c): the addresses of the instructions run since the last call, each
   once, in `out` (up to cap); returns how many there were.  Off until the first call. */
int az_board_take_pcs(az_board *b, uint16_t *out, int cap);

#ifdef __cplusplus
}
#endif
#endif
