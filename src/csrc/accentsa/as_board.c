/* as_board.c -- the Accent SA board around an 8085 core (as_board.h).  MIT. */
#include "as_board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "as_usart.h"

#define RAM_BASE 0x7800
#define WINDOW 0x8000
#define OP_EI 0xFB

struct as_board {
    uint8_t u2[AS_U2_SIZE];
    uint8_t banks[4][0x8000];              /* u2's upper half, u3, u4, nothing (FFh) */
    uint8_t ram[0x800];
    const uint8_t *bank;                   /* the window's bank now */
    uint8_t latch40, switches;
    as_chip chip;
    as_usart usart;
    i8085 *cpu;
    int trap_line;                         /* the TRAP line as last given to the core (accent_sa.py's trap_line) */
    /* counting (as_board_run) */
    int python_slices;
    uint64_t d_cycles;                     /* the core's T-states at the step's D (after any acceptance) */
    uint8_t d_op;                          /* the opcode at the step's D */
    uint8_t last_op;                       /* the last step's instruction (EI's shadow follows an EI) */
    int trap_held;                         /* a TRAP edge held through EI's shadow: given after the next step */
    int splits, ei_traps;
    unsigned work;                         /* instructions outside the firmware's waiting loops (as_board_take_work) */
};

/* ---- the bus ---------------------------------------------------------------------------------------------------- */

static uint8_t rd(void *ctx, uint32_t a)
{
    as_board *b = (as_board *)ctx;
    a &= 0xFFFF;
    if (a < RAM_BASE)
        return b->u2[a];
    if (a < WINDOW)
        return b->ram[a - RAM_BASE];
    return b->bank[a - WINDOW];
}

static void wr(void *ctx, uint32_t a, uint8_t v)
{
    as_board *b = (as_board *)ctx;
    a &= 0xFFFF;
    if (a >= RAM_BASE && a < WINDOW)       /* RAM only: ROM and the window ignore writes */
        b->ram[a - RAM_BASE] = v;
}

static uint8_t in(void *ctx, uint16_t p)
{
    as_board *b = (as_board *)ctx;
    if (p == 0x07)
        return b->chip.request(b->chip.ctx) ? 0x80 : 0x00;
    if (p == 0x20 || p == 0x21) {
        int drop;
        uint8_t v = as_usart_in(&b->usart, p, &drop);
        if (drop)
            i8085_set_irq(b->cpu, I8085_RST65, 0);
        return v;
    }
    if (p == 0x40)
        return b->switches;
    return 0x00;
}

static void out(void *ctx, uint16_t p, uint8_t v)
{
    as_board *b = (as_board *)ctx;
    if (p >= 0x03 && p <= 0x07)
        b->chip.write(b->chip.ctx, 7 - p, v);          /* reversed: 03 = R4 ... 07 = R0 */
    else if (p == 0x40) {
        b->latch40 = v;
        b->bank = b->banks[v & 3];
    } else if (p == 0x20 || p == 0x21)
        as_usart_out(&b->usart, p, v);
}

/* Where the firmware waits (issue #8): its loops while it has nothing to read or say -- after power-up or a Ctrl-X
   (02C9h, 16DAh, 17EEh) and after speech (035Bh, 0393h), polling the serial line and the queue (0F65h, 186Ah, 191Fh,
   198Eh, 61D6h) -- and the phoneme TRAP (its vector 24h, the handler 4E68h, queue refill 4DD3h, 0663h, 0EC4h, 0F6Ah,
   the speaking flag's 4FD8h-502Bh).  Watched on u2 (Accent SA, 1986-1989): every instruction the 8085 ran in steady
   idle, after a power-up, a cancel, settings commands and speech, and while the TRAP fed the last phonemes -- and the
   about 60 instructions, once, that close an utterance as its last phoneme goes (1699h, 4DEBh, 4F6Eh, 9495h in the
   window's bank 0).  Anything else is the firmware's own work: reading text and making phonemes, about 12,000
   instructions a 30 ms block, which between two clauses it does with its speaking flag down and no phoneme written,
   where busy() cannot see it.  A range at 8000h or above is u2's upper half only (the window's bank 0). */
static const uint16_t WAITING[][2] = {
    {0x0024, 0x0024}, {0x02C9, 0x02E4}, {0x035B, 0x0373}, {0x0393, 0x03A9}, {0x0663, 0x066D}, {0x0EC4, 0x0EC8},
    {0x0F65, 0x0F9D}, {0x1699, 0x16BA}, {0x16DA, 0x16DE}, {0x17EE, 0x17F3}, {0x186A, 0x1874}, {0x191F, 0x1953},
    {0x198E, 0x1992}, {0x4DD3, 0x4DDD}, {0x4DEB, 0x4E27}, {0x4E68, 0x4F15}, {0x4F6E, 0x502B}, {0x61D6, 0x61DB},
    {0x9495, 0x95D3}};

static int waiting(const as_board *b, uint32_t pc)
{
    size_t i;
    if (pc >= WINDOW && (b->latch40 & 3) != 0)
        return 0;
    for (i = 0; i < sizeof WAITING / sizeof WAITING[0]; i++)
        if (pc >= WAITING[i][0] && pc <= WAITING[i][1])
            return 1;
    return 0;
}

/* Phase D: after any acceptance, before the instruction */
static void boundary(void *ctx, uint32_t pc)
{
    as_board *b = (as_board *)ctx;
    b->d_cycles = i8085_cycles(b->cpu);
    b->d_op = rd(b, pc);
    if (!waiting(b, pc & 0xFFFF))
        b->work++;
}

unsigned as_board_take_work(as_board *b)
{
    unsigned w = b->work;
    b->work = 0;
    return w;
}

/* ---- the board -------------------------------------------------------------------------------------------------- */

static void fail(char *err, int errlen, const char *what, size_t got, size_t want)
{
    if (err && errlen > 0)
        snprintf(err, (size_t)errlen, "%s: %lu bytes, expected %lu", what, (unsigned long)got, (unsigned long)want);
}

as_board *as_board_create(const uint8_t *u2, size_t n2, const uint8_t *u3, size_t n3, const uint8_t *u4, size_t n4,
                          const as_chip *chip, char *err, int errlen)
{
    as_board *b;
    cpu_bus bus;
    if (!u2 || n2 != AS_U2_SIZE) {
        fail(err, errlen, "u2", u2 ? n2 : 0, AS_U2_SIZE);
        return NULL;
    }
    if (!u3 || n3 != AS_U3_SIZE) {
        fail(err, errlen, "u3", u3 ? n3 : 0, AS_U3_SIZE);
        return NULL;
    }
    if (!u4 || n4 != AS_U4_SIZE) {
        fail(err, errlen, "u4", u4 ? n4 : 0, AS_U4_SIZE);
        return NULL;
    }
    if (!chip || !chip->write || !chip->request) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "no chip");
        return NULL;
    }
    b = (as_board *)calloc(1, sizeof *b);
    if (!b) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(b->u2, u2, AS_U2_SIZE);
    memcpy(b->banks[0], u2 + WINDOW, 0x8000);
    memcpy(b->banks[1], u3, AS_U3_SIZE);
    memcpy(b->banks[2], u4, AS_U4_SIZE);
    memset(b->banks[3], 0xFF, 0x8000);
    b->bank = b->banks[0];
    b->chip = *chip;
    b->python_slices = 1;
    as_usart_init(&b->usart);
    memset(&bus, 0, sizeof bus);
    bus.ctx = b;
    bus.read = rd;
    bus.write = wr;
    bus.in = in;
    bus.out = out;
    bus.boundary = boundary;
    b->cpu = i8085_create(&bus, 0.0);      /* reset: PC 0, IE off, all masks set */
    if (!b->cpu) {
        as_usart_free(&b->usart);
        free(b);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    return b;
}

void as_board_destroy(as_board *b)
{
    if (!b)
        return;
    i8085_destroy(b->cpu);
    as_usart_free(&b->usart);
    free(b);
}

void as_board_serial(as_board *b)
{
    if (as_usart_load(&b->usart))
        i8085_set_irq(b->cpu, I8085_RST65, 1);
}

void as_board_trap(as_board *b)
{
    int line = b->chip.request(b->chip.ctx) && (b->latch40 & 0x10);
    if (line == b->trap_line)
        return;
    b->trap_line = line;
    if (line && b->python_slices && b->last_op == OP_EI) {
        b->trap_held = 1;                  /* i8085.py: nothing is sampled in EI's shadow, TRAP included */
        b->ei_traps++;
        return;
    }
    b->trap_held = 0;
    i8085_set_irq(b->cpu, I8085_TRAP, line);
}

void as_board_rst75(as_board *b)
{
    i8085_set_irq(b->cpu, I8085_RST75, 1);  /* an edge: the core latches it */
    i8085_set_irq(b->cpu, I8085_RST75, 0);
}

/* Python may end its budget after acceptance. The core's slice entry point leaves the vector instruction entirely
   unexecuted; only an executed instruction advances last_op or releases an EI-held TRAP. */
uint64_t as_board_run(as_board *b, uint64_t budget)
{
    uint64_t done = 0;
    while (done < budget) {
        int accepted_only = 0;
        int t = b->python_slices ? i8085_step_slice(b->cpu, budget - done, &accepted_only)
                                : i8085_step(b->cpu);
        done += (uint64_t)t;
        if (accepted_only) {
            b->splits++;
            return done;
        }
        b->last_op = b->d_op;
        if (b->trap_held) {                /* EI's shadow is over: the held edge now */
            b->trap_held = 0;
            i8085_set_irq(b->cpu, I8085_TRAP, b->trap_line);
        }
    }
    return done;
}

int as_board_send(as_board *b, const uint8_t *bytes, int n)
{
    return as_usart_queue(&b->usart, bytes, n);
}

void as_board_drop_input(as_board *b)
{
    as_usart_drop(&b->usart);
}

int as_board_tx(const as_board *b, const uint8_t **bytes)
{
    *bytes = b->usart.tx;
    return b->usart.n_tx;
}

int as_board_get(const as_board *b, const char *name)
{
    i8085_regs r;
    i8085_regs_get(b->cpu, &r);
    if (!strcmp(name, "latch40")) return b->latch40;
    if (!strcmp(name, "bank")) return b->latch40 & 3;
    if (!strcmp(name, "switches")) return b->switches;
    if (!strcmp(name, "python_slices")) return b->python_slices;
    if (!strcmp(name, "usart_cmd")) return b->usart.cmd;
    if (!strcmp(name, "usart_mode_next")) return b->usart.mode_next;
    if (!strcmp(name, "rx")) return b->usart.n;
    if (!strcmp(name, "rx_ready")) return b->usart.rx_ready;
    if (!strcmp(name, "trap_line")) return b->trap_line;
    if (!strcmp(name, "pc")) return r.pc;
    if (!strcmp(name, "sp")) return r.sp;
    if (!strcmp(name, "af")) return r.af;
    if (!strcmp(name, "bc")) return r.bc;
    if (!strcmp(name, "de")) return r.de;
    if (!strcmp(name, "hl")) return r.hl;
    if (!strcmp(name, "im")) return r.im;
    if (!strcmp(name, "halted")) return r.halted;
    if (!strcmp(name, "splits")) return b->splits;
    if (!strcmp(name, "ei_traps")) return b->ei_traps;
    return -1;
}

void as_board_set(as_board *b, const char *name, int v)
{
    if (!strcmp(name, "switches"))
        b->switches = (uint8_t)v;
    else if (!strcmp(name, "python_slices"))
        b->python_slices = v != 0;
}

uint64_t as_board_cycles(const as_board *b)
{
    return i8085_cycles(b->cpu);
}

uint64_t as_board_steps(const as_board *b)
{
    return i8085_steps(b->cpu);
}

uint8_t as_board_peek(const as_board *b, uint16_t addr)
{
    return rd((void *)b, addr);
}
