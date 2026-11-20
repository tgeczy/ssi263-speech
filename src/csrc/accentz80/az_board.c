/* az_board.c -- the Accent SA's board around a Z80 core (az_board.h).  MIT. */
#include "az_board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "as_usart.h"

#define RAM_SIZE 0x2000                    /* the 6264 */
#define MODE(b) ((b)->latch & 3)
#define NMI_ENABLE 0x10
#define UNDRIVEN 0xFF

struct az_board {
    uint8_t u5[AZ_U5_SIZE];
    uint8_t u4[AZ_U4_SIZE];
    uint8_t ram[RAM_SIZE];
    uint8_t latch, switches, foot;
    az_chip chip;
    as_usart usart;
    z80 *cpu;
    int nmi_line;                          /* the /NMI line as last given to the core */
    unsigned nmi_edges, unmapped_in, unmapped_out;
    unsigned work;                         /* instructions outside the firmware's waiting loops (az_board_take_work) */
    const uint16_t (*waiting)[2];          /* this firmware's table (az_waiting.inc), NULL: not measured */
    size_t n_waiting;
    uint8_t *seen;                         /* az_board_take_pcs: one bit an address, NULL until asked for */
};

/* ---- memory: the PLD's RAM, BANK, ROM_U4 and ROM_U5 --------------------------------------------------------------- */

/* the 6264 cell an address reaches, or NULL: 7000h-7FFFh is its upper half (BANK high), the window in MODE 3 its
   lower half (BANK low), A0-A11 the CPU's */
static uint8_t *ram_cell(az_board *b, uint32_t a)
{
    if ((a & 0xF000) == 0x7000)
        return &b->ram[0x1000 | (a & 0x0FFF)];
    if ((a & 0x8000) && MODE(b) == 3)
        return &b->ram[a & 0x0FFF];
    return NULL;
}

static uint8_t rd(void *ctx, uint32_t a)
{
    az_board *b = (az_board *)ctx;
    const uint8_t *cell;
    a &= 0xFFFF;
    cell = ram_cell(b, a);
    if (cell)
        return *cell;
    if (!(a & 0x8000) || MODE(b) == 0)
        return b->u5[a];
    if (MODE(b) == 1)
        return b->u4[a];
    return b->u4[a & 0x7FFF];              /* MODE 2 */
}

static void wr(void *ctx, uint32_t a, uint8_t v)
{
    az_board *b = (az_board *)ctx;
    uint8_t *cell = ram_cell(b, a & 0xFFFF);
    if (cell)                              /* RAM only: ROM ignores writes */
        *cell = v;
}

/* ---- /NMI: A/R AND the latch's bit 4, the Z80 taking the edge ---------------------------------------------------- */

static void nmi_update(az_board *b)
{
    int line = b->chip.request(b->chip.ctx) && (b->latch & NMI_ENABLE);
    if (line == b->nmi_line)
        return;
    b->nmi_line = line;
    if (line)
        b->nmi_edges++;
    z80_set_irq(b->cpu, Z80_NMI, line);
}

/* ---- I/O: the device by A12-A15, the port address's high byte ----------------------------------------------------- */

static int device(uint16_t port)
{
    return port >> 12;
}

static uint8_t in(void *ctx, uint16_t port)
{
    az_board *b = (az_board *)ctx;
    switch (device(port)) {
    case 0x0:
        return b->chip.request(b->chip.ctx) ? 0x80 : 0x00;
    case 0x1:
        return b->foot;
    case 0x2: {
        int drop;
        uint8_t v = as_usart_in(&b->usart, 0x20 | (port & 1), &drop);
        if (drop)
            z80_set_irq(b->cpu, Z80_INT, 0);
        return v;
    }
    case 0x4:
        return b->switches;
    }
    b->unmapped_in++;
    return UNDRIVEN;
}

static void out(void *ctx, uint16_t port, uint8_t v)
{
    az_board *b = (az_board *)ctx;
    switch (device(port)) {
    case 0x0:
        b->chip.write(b->chip.ctx, port & 7, v);
        nmi_update(b);                     /* a write to the chip may take A/R down */
        return;
    case 0x2:
        as_usart_out(&b->usart, 0x20 | (port & 1), v);
        return;
    case 0x4:
        b->latch = v;
        nmi_update(b);                     /* bit 4 raised with A/R up is an edge too */
        return;
    }
    b->unmapped_out++;                     /* 1xxx is read only; 8xxx goes nowhere */
}

static int irq_ack(void *ctx, int line, int n)
{
    (void)ctx; (void)line; (void)n;
    return -1;                             /* nothing drives the bus (the firmware runs mode 1, which ignores it) */
}

/* Where the firmware waits: its loops while it has nothing to read or say, and the few instructions that close an
   utterance.  MEASURED per firmware by az_waiting.c (which says how) and kept in az_waiting.inc, keyed by U5's FNV-1a;
   anything else the Z80 runs is the firmware's own work -- reading text and making phonemes -- which between two
   clauses it does with its speaking flag down and no phoneme written, where busy() cannot see it.  A range at 8000h
   or above is U5's upper half only (MODE 0).  A U5 that was not measured has no table: its work is not counted
   ("waiting_known" 0), and a voice then calls a text done at its first quiet block. */
#include "az_waiting.inc"

static const struct {
    unsigned long u5;
    const uint16_t (*ranges)[2];
    size_t n;
} WAITING[] = {
    {AZ_WAITING_0_U5, AZ_WAITING_0, sizeof AZ_WAITING_0 / sizeof AZ_WAITING_0[0]},
#if AZ_WAITING_FIRMWARES > 1
    {AZ_WAITING_1_U5, AZ_WAITING_1, sizeof AZ_WAITING_1 / sizeof AZ_WAITING_1[0]},
#endif
};

static unsigned long fnv1a(const uint8_t *p, size_t n)
{
    unsigned long h = 2166136261UL;
    size_t i;
    for (i = 0; i < n; i++)
        h = ((h ^ p[i]) * 16777619UL) & 0xFFFFFFFFUL;
    return h;
}

static int waiting(const az_board *b, uint32_t pc)
{
    size_t i;
    if (pc >= 0x8000 && MODE(b) != 0)
        return 0;
    for (i = 0; i < b->n_waiting; i++)
        if (pc >= b->waiting[i][0] && pc <= b->waiting[i][1])
            return 1;
    return 0;
}

/* Phase D: after any acceptance, before the instruction */
static void boundary(void *ctx, uint32_t pc)
{
    az_board *b = (az_board *)ctx;
    pc &= 0xFFFF;
    if (b->waiting && !waiting(b, pc))
        b->work++;
    if (b->seen)
        b->seen[pc >> 3] |= (uint8_t)(1 << (pc & 7));
}

unsigned az_board_take_work(az_board *b)
{
    unsigned w = b->work;
    b->work = 0;
    return w;
}

int az_board_take_pcs(az_board *b, uint16_t *out_pcs, int cap)
{
    int n = 0;
    uint32_t pc;
    if (!b->seen) {
        b->seen = (uint8_t *)calloc(1, 0x2000);
        return 0;
    }
    for (pc = 0; pc < 0x10000; pc++)
        if (b->seen[pc >> 3] >> (pc & 7) & 1) {
            if (n < cap)
                out_pcs[n] = (uint16_t)pc;
            n++;
        }
    memset(b->seen, 0, 0x2000);
    return n;
}

/* ---- the board -------------------------------------------------------------------------------------------------- */

static az_board *fail(char *err, int errlen, const char *what, size_t got, size_t want)
{
    if (err && errlen > 0) {
        if (want)
            snprintf(err, (size_t)errlen, "%s: %lu bytes, expected %lu", what, (unsigned long)got, (unsigned long)want);
        else
            snprintf(err, (size_t)errlen, "%s", what);
    }
    return NULL;
}

az_board *az_board_create(const uint8_t *u5, size_t n5, const uint8_t *u4, size_t n4, const az_chip *chip, char *err,
                          int errlen)
{
    az_board *b;
    cpu_bus bus;
    unsigned long id;
    size_t i;
    if (!u5 || n5 != AZ_U5_SIZE)
        return fail(err, errlen, "U5", u5 ? n5 : 0, AZ_U5_SIZE);
    if (!u4 || n4 != AZ_U4_SIZE)
        return fail(err, errlen, "U4", u4 ? n4 : 0, AZ_U4_SIZE);
    if (!chip || !chip->write || !chip->request)
        return fail(err, errlen, "no chip", 0, 0);
    b = (az_board *)calloc(1, sizeof *b);
    if (!b)
        return fail(err, errlen, "out of memory", 0, 0);
    memcpy(b->u5, u5, AZ_U5_SIZE);
    memcpy(b->u4, u4, AZ_U4_SIZE);
    b->chip = *chip;
    b->switches = 0x10;                    /* the fixed bits: D4 = 1, D5 = 0; the four dips reading 0 */
    id = fnv1a(u5, AZ_U5_SIZE);
    for (i = 0; i < sizeof WAITING / sizeof WAITING[0]; i++)
        if (WAITING[i].u5 == id) {
            b->waiting = WAITING[i].ranges;
            b->n_waiting = WAITING[i].n;
        }
    b->foot = 0x0F;
    as_usart_init(&b->usart);
    memset(&bus, 0, sizeof bus);
    bus.ctx = b;
    bus.read = rd;
    bus.write = wr;
    bus.in = in;
    bus.out = out;
    bus.irq_ack = irq_ack;
    bus.boundary = boundary;
    b->cpu = z80_create(&bus, AZ_CPU_HZ);  /* reset: PC 0, interrupts off */
    if (!b->cpu) {
        as_usart_free(&b->usart);
        free(b);
        return fail(err, errlen, "out of memory", 0, 0);
    }
    return b;
}

void az_board_destroy(az_board *b)
{
    if (!b)
        return;
    z80_destroy(b->cpu);
    as_usart_free(&b->usart);
    free(b->seen);
    free(b);
}

void az_board_serial(az_board *b)
{
    if (as_usart_load(&b->usart))
        z80_set_irq(b->cpu, Z80_INT, 1);
}

void az_board_nmi(az_board *b)
{
    nmi_update(b);
}

uint64_t az_board_run(az_board *b, uint64_t budget)
{
    return z80_run(b->cpu, budget);
}

int az_board_send(az_board *b, const uint8_t *bytes, int n)
{
    return as_usart_queue(&b->usart, bytes, n);
}

void az_board_drop_input(az_board *b)
{
    as_usart_drop(&b->usart);
}

int az_board_tx(const az_board *b, const uint8_t **bytes)
{
    *bytes = b->usart.tx;
    return b->usart.n_tx;
}

int az_board_get(const az_board *b, const char *name)
{
    z80_regs r;
    z80_regs_get(b->cpu, &r);
    if (!strcmp(name, "latch")) return b->latch;
    if (!strcmp(name, "mode")) return MODE(b);
    if (!strcmp(name, "switches")) return b->switches;
    if (!strcmp(name, "foot")) return b->foot;
    if (!strcmp(name, "usart_cmd")) return b->usart.cmd;
    if (!strcmp(name, "usart_mode_next")) return b->usart.mode_next;
    if (!strcmp(name, "rx")) return b->usart.n;
    if (!strcmp(name, "rx_ready")) return b->usart.rx_ready;
    if (!strcmp(name, "nmi_line")) return b->nmi_line;
    if (!strcmp(name, "nmi_edges")) return (int)b->nmi_edges;
    if (!strcmp(name, "unmapped_in")) return (int)b->unmapped_in;
    if (!strcmp(name, "unmapped_out")) return (int)b->unmapped_out;
    if (!strcmp(name, "waiting_known")) return b->waiting != NULL;
    if (!strcmp(name, "pc")) return r.pc;
    if (!strcmp(name, "sp")) return r.sp;
    if (!strcmp(name, "af")) return r.af;
    if (!strcmp(name, "bc")) return r.bc;
    if (!strcmp(name, "de")) return r.de;
    if (!strcmp(name, "hl")) return r.hl;
    if (!strcmp(name, "im")) return r.im;
    if (!strcmp(name, "iff1")) return r.iff1;
    if (!strcmp(name, "halted")) return r.halted;
    return -1;
}

void az_board_set(az_board *b, const char *name, int v)
{
    if (!strcmp(name, "switches"))
        b->switches = (uint8_t)v;
    else if (!strcmp(name, "foot"))
        b->foot = (uint8_t)v;
}

uint64_t az_board_cycles(const az_board *b)
{
    return z80_cycles(b->cpu);
}

uint64_t az_board_steps(const az_board *b)
{
    return z80_steps(b->cpu);
}

uint8_t az_board_peek(const az_board *b, uint16_t addr)
{
    return rd((void *)b, addr);
}
