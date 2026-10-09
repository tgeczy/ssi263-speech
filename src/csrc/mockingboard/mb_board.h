/* mb_board.h -- the machine Sweet Micro Systems' Mockingboard text-to-speech runs on: an Apple II's 6502 and memory
 * (48 KB of RAM and a 16 KB language card), and a Mockingboard in slot 4 with its SSI-263.  Only what the firmware
 * uses is modelled:
 *
 *   0000-BFFF   RAM
 *   C080-C08F   the language card's switches (read RAM or "ROM", bank 1 or 2, write enable by two reads)
 *   C440-C447   the SSI-263's registers (writes; A0-A2 select them).  The card's first VIA sees these writes too; the
 *               firmware never reads it, and its sound chips are not modelled
 *   C480-C48F   the second VIA: its PCR, IFR and IER, and CA1 -- the SSI-263's A/R -- as its interrupt
 *   D000-FFFF   the language card's RAM, or our own 16 bytes of "ROM" (below)
 *
 * No Apple software at all: no monitor ROM, no DOS, no Applesoft.  The "ROM" is ours: at FFF0h STA $45 / JMP ($03FE)
 * -- the part of the monitor's IRQ handler the driver relies on (its handler ends LDA $45 / RTI) -- at FFF5h our idle
 * loop (JMP $FFF5), and the vectors (NMI and RESET to the idle loop, IRQ to FFF0h).  Every other ROM byte reads 00h.
 *
 * The firmware's files come from the caller and are placed as DOS's BLOAD placed them (mb_board_load).  MIT.
 */
#ifndef MB_BOARD_H
#define MB_BOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mb_board mb_board;

#define MB_IDLE 0xFFF5u               /* our idle loop: the host's return address for a call */
#define MB_CPU_HZ 1022727.0           /* the Apple II's 6502: 14.31818 MHz / 14 */

typedef struct {
    void *ctx;
    void (*write)(void *ctx, int reg, int val);   /* the chip: reg = A0-A2 (0-7) */
} mb_chip;

mb_board *mb_board_create(const mb_chip *chip);
void mb_board_destroy(mb_board *b);

/* A file into memory as BLOAD puts it: below D000h into RAM; from D000h into the language card's RAM (bank 1 for
   D000-DFFF: the demo write-enables bank 1, $C089 twice, before its BLOADs).  0 if it does not fit in 64 KB. */
int mb_board_load(mb_board *b, uint16_t addr, const unsigned char *data, size_t n);

/* Memory as the 6502 sees it now (the language card's switches applied); a write is a CPU write (the chip and the
   VIA see it).  For the host's set-up: the text, the settings, the stack. */
uint8_t mb_board_peek(const mb_board *b, uint16_t addr);
void mb_board_poke(mb_board *b, uint16_t addr, uint8_t v);

/* The SSI-263's A/R pin into CA1: asserted = the pin LOW (the chip requests).  The VIA flags the edge its PCR
   selects (bit 0: 0 = the falling edge, A/R becoming asserted), and the 6502's IRQ follows IFR & IER. */
void mb_board_ar(mb_board *b, int asserted);

/* Calls a firmware routine: pushes our idle loop's return address and sets PC; the caller then runs the board until
   mb_board_idle.  Registers A, X, Y as given; the stack is reset to FFh. */
void mb_board_call(mb_board *b, uint16_t addr, uint8_t a, uint8_t x, uint8_t y);
int mb_board_idle(const mb_board *b);          /* 1 while the 6502 is in our idle loop */
uint16_t mb_board_pc(const mb_board *b);       /* where the next instruction is */
/* The host's guard: CPU writes into lo..hi are counted ("guard_writes"); the count restarts. */
void mb_board_guard(mb_board *b, uint16_t lo, uint16_t hi);
/* Abandons a call: the 6502 back in our idle loop (stack reset, I set), the language card reading "ROM". */
void mb_board_abort(mb_board *b);

uint64_t mb_board_run(mb_board *b, uint64_t cycles);    /* whole instructions; returns the cycles run */
uint64_t mb_board_cycles(const mb_board *b);

/* State, by name: "ifr", "ier", "pcr", "irq" (the 6502's line), "lc_read_ram", "lc_bank", "lc_write",
   "vector_reads" (reads of FFFEh: IRQ acceptances, two each), "vector_reads_lc" (those while the language card's
   RAM was read: the vectors would then be the card's, not ours -- the firmware never does this), "guard_writes",
   "pc", "decimal", "undocumented" (the core's counts).  -1: unknown. */
long mb_board_get(const mb_board *b, const char *name);

#ifdef __cplusplus
}
#endif
#endif
