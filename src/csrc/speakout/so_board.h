/* so_board.h -- the GW Micro Speak-Out (1995) around a V40 core (MAME's NEC core, ../cpu/v40_mame.cpp), as a
 * library: one instance per unit.
 *
 * The board is src/hosts/speakout.py's machine with the CPU replaced and nothing else changed (Astra and Tomi, the
 * scope of this step): 1 MB of RAM holding the firmware's Intel HEX; the SSI-263 memory-mapped at F000:FE00-FE04
 * (R0-R4; the firmware never reads it, and the RAM keeps what was written, as under Unicorn); the V40's ICU and SCU
 * reduced as that host reduces them (so_icu.h, so_scu.h); every other port reads 0 and ignores writes (the TCU, the
 * SCU's set-up, the V40's system registers FFF0h-FFFEh).  The chip's writes come back as a list, in order, per call;
 * the host (its SSI-263 model and its chip-time loop) applies them, as bl_board.h does for the Braille Lite.
 *
 * Power-on: the V40 starts at FFFF:0000; the board puts a far JMP to 0000:0100 there, runs it as the first step and
 * restores those five bytes, so the firmware starts where the Unicorn host starts it, with the same memory.
 *
 * Not thread-safe per instance; different instances may be used from different threads.  MIT.
 */
#ifndef SO_BOARD_H
#define SO_BOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct so_board so_board;

typedef struct {
    uint8_t reg, val;                  /* an SSI-263 write: register 0-4, its value */
} so_write;

#define SO_IRQ_SERIAL 1                /* the SCU's receiver: vector 9 */
#define SO_IRQ_CHIP 4                  /* the SSI-263's A/R: vector 0Ch */

so_board *so_create(void);
void so_destroy(so_board *b);
/* the firmware (Intel HEX text); the data bytes stored, -1 if malformed */
long so_load_hex(so_board *b, const char *text, size_t len);
void so_power_on(so_board *b);         /* reset, then the reset vector's JMP to 0000:0100 (above) */

/* speakout.py's slice start: with the chip requesting, offer IR4; otherwise, with serial input, load a byte if none
   is loaded and offer IR1.  An offer is taken or dropped at once (so_icu.h); a taken one is accepted at the next
   step.  Returns the IR taken, or -1. */
int so_offer(so_board *b, int chip_request);
uint64_t so_run_steps(so_board *b, uint64_t n);      /* exactly n steps; returns their clocks */
uint64_t so_run_cycles(so_board *b, uint64_t n);     /* whole steps until >= n clocks; returns the clocks run */
/* n instructions as Unicorn counts them (a REP ended by its count is one more; so_board.c), for comparing the two
   cores step for step; returns the clocks run */
uint64_t so_run_steps_unicorn(so_board *b, uint64_t n);

void so_send(so_board *b, const uint8_t *bytes, int n);   /* bytes for the serial input */
void so_drop_input(so_board *b);       /* the queued and the loaded byte */
int so_input_queued(const so_board *b);
int so_input_count(const so_board *b);  /* the bytes queued and loaded, not yet read by the firmware */

int so_writes(const so_board *b, const so_write **w);     /* the chip writes since so_clear_writes, in order */
void so_clear_writes(so_board *b);
/* The bytes the box sent on its serial line since so_clear_tx (the SCU's data register, OUT port 0), in order: its
   replies to index marks (so_voice.h).  At most SO_TX are kept between two clears; the box sends one a chip
   interrupt at the most, and the host clears after every slice. */
#define SO_TX 64
int so_tx(const so_board *b, const uint8_t **bytes);
void so_clear_tx(so_board *b);

void so_read(const so_board *b, uint32_t addr, uint8_t *out, int n);
void so_poke(so_board *b, uint32_t addr, const uint8_t *data, int n);
uint64_t so_cycles(const so_board *b);
uint64_t so_steps(const so_board *b);
/* the CPU's state for a caller without cpu.h: ps, ip, psw, halted, fault, undefined, undefined_at */
void so_cpu_state(const so_board *b, uint32_t out[7]);
void so_icu_state(const so_board *b, int out[2]);     /* the mask, the in-service set */

#ifdef __cplusplus
}
#endif
#endif
