/* as_board.h -- the Aicom Accent SA around an 8085 core (MAME's, ../cpu/i8085_mame.cpp, through ../cpu/cpu.h), as a
 * library: one instance per unit.
 *
 * The machine is src/hosts/accent_sa.py's, exactly (read from Aicom's code and confirmed by it speaking):
 *   0000-77FF  u2's lower half (program)             7800-7FFF  RAM, 2 KB (SP = 8000h); ROM writes are ignored
 *   8000-FFFF  a 32 KB window, banked by port 40h bits 0-1: 0 = u2's upper half, 1 = u3, 2 = u4, 3 = nothing (FFh)
 *   ports 03-07  the SSI-263, reversed: 03 = R4, 04 = R3, 05 = R2, 06 = R1, 07 = R0; a read of 07 has A/R in bit 7
 *   ports 20/21  the 8251 (as_usart.h); RxRDY is RST 6.5
 *   port 40      out: the bank (bits 0-1) and bit 4, which gates A/R onto TRAP; in: the option switches
 *   every other port reads 0 and ignores writes (port 80h, the parallel input on RST 5.5, is unused)
 *   TRAP         A/R AND port 40h bit 4, sampled at a slice's start (as_board_trap), as accent_sa.py's _trap()
 *   RST 7.5      a clock the firmware times at power-up; the host pulses it (as_board_rst75) or never (mode 3)
 *
 * The chip is the caller's, through as_chip: a write reaches it at once, inside the slice, and IN 07h reads its A/R
 * then (the firmware polls it).  The board keeps no time: the host (as_host.h) runs it in slices of T-states.
 *
 * Counting (as_board_run): the 8085 core takes one STEP as an interrupt's acceptance and the instruction after it,
 * together.  The Python host (src/hosts/i8085.py's run()) tests its budget between the two, so an acceptance that
 * reaches the budget ends its slice and the vector's first instruction runs in the next.  With python_slices on (the
 * default) the board stops before that instruction: its registers, memory and I/O are unchanged until a later
 * call executes it. A TRAP raised while the core is in EI's shadow is held through the instruction after the EI, as the
 * Python core holds it (CONTRACT.md 5: the chip does not).  Both are the HOST's counting, not the core's: the core's
 * T-states per instruction and per acceptance match the Python core's (cpu/trace_i8085.py).  Off, the core's own
 * semantics stand (experimental).
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef AS_BOARD_H
#define AS_BOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_U2_SIZE 0x10000                 /* 64 KB: the program and, above 8000h, bank 0 */
#define AS_U3_SIZE 0x8000                  /* 32 KB: bank 1 */
#define AS_U4_SIZE 0x8000                  /* 32 KB: bank 2, the "AICOM COPYRIGHT" exception dictionary */

typedef struct as_board as_board;

typedef struct {
    void *ctx;
    void (*write)(void *ctx, int reg, int val);   /* an SSI-263 register write, reg 0-4 (R0-R4) */
    int (*request)(void *ctx);                    /* A/R asserted */
} as_chip;

/* The three ROMs, copied (the caller may free them).  Exact sizes are required (accent_sa.py fails on shorter ones).
   NULL on failure, the reason in err. */
as_board *as_board_create(const uint8_t *u2, size_t n2, const uint8_t *u3, size_t n3, const uint8_t *u4, size_t n4,
                          const as_chip *chip, char *err, int errlen);
void as_board_destroy(as_board *b);

/* accent_sa.py's _serial(): one byte into the 8251 when it is free and RTS is up; it raises RST 6.5 */
void as_board_serial(as_board *b);
/* accent_sa.py's _trap(): the TRAP line = A/R AND port 40h bit 4, given to the core when it changes */
void as_board_trap(as_board *b);
void as_board_rst75(as_board *b);          /* one RST 7.5 edge (the power-up clock, when the host has one) */
/* i8085.py's run(budget): whole steps until at least `budget` T-states, counted as above; returns those counted */
uint64_t as_board_run(as_board *b, uint64_t budget);

int as_board_send(as_board *b, const uint8_t *bytes, int n);   /* the host's bytes; 1, or 0 when out of memory */
void as_board_drop_input(as_board *b);     /* the queued bytes, not the loaded one (accent_sa.py's rx.clear()) */
int as_board_tx(const as_board *b, const uint8_t **bytes);     /* every byte the firmware sent */

/* state, by name: "latch40", "bank", "switches" (settable), "python_slices" (settable), "usart_cmd", "usart_mode_next",
   "rx" (queued), "rx_ready", "trap_line", "pc", "sp", "af", "bc", "de", "hl", "im" (the RIM view), "halted",
   "splits" (acceptances that ended a slice before the instruction), "ei_traps" (TRAPs held through EI's shadow); -1: unknown */
int as_board_get(const as_board *b, const char *name);
void as_board_set(as_board *b, const char *name, int v);
uint64_t as_board_cycles(const as_board *b);   /* the core's T-states since power-on */
uint64_t as_board_steps(const as_board *b);
uint8_t as_board_peek(const as_board *b, uint16_t addr);   /* memory as the CPU reads it now (the bank in the window) */
/* How many instructions the 8085 has run outside the firmware's waiting loops (as_board.c's WAITING: idle, polling,
   the phoneme TRAP) since the last call -- its own work, reading text and making phonemes; then 0.  Watches only. */
unsigned as_board_take_work(as_board *b);

#ifdef __cplusplus
}
#endif
#endif
