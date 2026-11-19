/* so_board.c -- the Speak-Out board around a V40 core (so_board.h).  MIT. */
#include "so_board.h"

#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "so_hex.h"
#include "so_icu.h"
#include "so_scu.h"

#define MEM_SIZE 0x100000
#define CHIP_BASE (0xF0000 + 0xFE00)        /* F000:FE00, R0-R4 (speakout.py hooks FE00-FE07, uses 0-4) */
#define RESET_VECTOR 0xFFFF0

struct so_board {
    uint8_t *mem;
    v40 *cpu;
    so_icu icu;
    so_scu scu;
    so_write *w;
    int n_w, cap_w;
    uint8_t tx[SO_TX];                      /* so_tx */
    int n_tx;
    /* so_run_steps_unicorn: the step's start, as its boundary saw it */
    int watch;                              /* record at the boundary */
    uint32_t step_pc;
    uint16_t step_cw;
    uint8_t rep, op;                        /* the REP prefix (F2/F3, 0: none) and the string opcode */
    uint64_t debt;                          /* counts the last call ran past its n */
};

/* ---- the bus ---------------------------------------------------------------------------------------------------- */

static uint8_t rd(void *ctx, uint32_t a)
{
    return ((so_board *)ctx)->mem[a & 0xFFFFF];
}

static void note_write(so_board *b, uint8_t reg, uint8_t val)
{
    if (b->n_w == b->cap_w) {
        int cap = b->cap_w ? b->cap_w * 2 : 256;
        so_write *w = (so_write *)realloc(b->w, (size_t)cap * sizeof *w);
        if (!w)
            return;                         /* out of memory: the write is lost, the run goes on */
        b->w = w;
        b->cap_w = cap;
    }
    b->w[b->n_w].reg = reg;
    b->w[b->n_w++].val = val;
}

static void wr(void *ctx, uint32_t a, uint8_t v)
{
    so_board *b = (so_board *)ctx;
    a &= 0xFFFFF;
    b->mem[a] = v;                          /* RAM keeps it, the chip's window included (as under Unicorn) */
    if (a >= CHIP_BASE && a <= CHIP_BASE + 4)
        note_write(b, (uint8_t)(a - CHIP_BASE), v);
}

static uint8_t in(void *ctx, uint16_t p)
{
    so_board *b = (so_board *)ctx;
    if (p == 0 || p == 1)
        return so_scu_in(&b->scu, p);
    if (p == 9)
        return so_icu_in(&b->icu, p);
    return 0;                               /* speakout.py: every other port reads 0 */
}

static void out(void *ctx, uint16_t p, uint8_t v)
{
    so_board *b = (so_board *)ctx;
    if (p == 8 || p == 9)
        so_icu_out(&b->icu, p, v);
    else if (p == 0 && b->n_tx < SO_TX)     /* the serial unit's data register: a byte for the PC (so_tx) */
        b->tx[b->n_tx++] = v;               /* everything else: ignored, as speakout.py ignores it */
}

static int ack(void *ctx, int line, int n)
{
    (void)n;
    return line == V40_INT ? so_icu_ack(&((so_board *)ctx)->icu) : -1;
}

static int string_op(uint8_t op)
{
    return (op >= 0x6C && op <= 0x6F) || (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF);
}

/* Phase D: the step's start, for so_run_steps_unicorn -- after any acceptance, so it is the instruction's own. */
static void boundary(void *ctx, uint32_t pc)
{
    so_board *b = (so_board *)ctx;
    v40_regs r;
    int i;
    uint8_t rep = 0, op = 0;
    if (!b->watch)
        return;
    for (i = 0; i < 15; i++) {
        op = b->mem[(pc + (uint32_t)i) & 0xFFFFF];
        if (op == 0xF2 || op == 0xF3)
            rep = op;
        else if (op != 0x26 && op != 0x2E && op != 0x36 && op != 0x3E && op != 0xF0)
            break;
    }
    v40_regs_get(b->cpu, &r);
    b->step_pc = pc;
    b->step_cw = r.cw;
    b->rep = string_op(op) ? rep : 0;
    b->op = op;
}

/* ---- the board -------------------------------------------------------------------------------------------------- */

so_board *so_create(void)
{
    so_board *b = (so_board *)calloc(1, sizeof *b);
    cpu_bus bus;
    if (!b)
        return NULL;
    b->mem = (uint8_t *)calloc(1, MEM_SIZE);
    if (!b->mem) {
        free(b);
        return NULL;
    }
    memset(&bus, 0, sizeof bus);
    bus.ctx = b;
    bus.read = rd;
    bus.write = wr;
    bus.in = in;
    bus.out = out;
    bus.irq_ack = ack;
    bus.boundary = boundary;
    b->cpu = v40_create(&bus, 0.0);
    if (!b->cpu) {
        free(b->mem);
        free(b);
        return NULL;
    }
    so_icu_reset(&b->icu);
    so_scu_init(&b->scu);
    return b;
}

void so_destroy(so_board *b)
{
    if (!b)
        return;
    v40_destroy(b->cpu);
    so_scu_free(&b->scu);
    free(b->w);
    free(b->mem);
    free(b);
}

long so_load_hex(so_board *b, const char *text, size_t len)
{
    return so_hex_parse(text, len, b->mem);
}

void so_power_on(so_board *b)
{
    static const uint8_t jmp[5] = {0xEA, 0x00, 0x01, 0x00, 0x00};   /* JMP FAR 0000:0100 */
    uint8_t keep[5];
    memcpy(keep, b->mem + RESET_VECTOR, 5);
    memcpy(b->mem + RESET_VECTOR, jmp, 5);
    so_icu_reset(&b->icu);
    v40_reset(b->cpu);
    v40_step(b->cpu);
    memcpy(b->mem + RESET_VECTOR, keep, 5);
    b->n_w = 0;
}

int so_offer(so_board *b, int chip_request)
{
    v40_regs r;
    int ir;
    if (chip_request)
        ir = SO_IRQ_CHIP;
    else if (so_scu_pending(&b->scu)) {
        so_scu_load(&b->scu);
        ir = SO_IRQ_SERIAL;
    } else
        return -1;
    v40_regs_get(b->cpu, &r);
    if (!so_icu_offer(&b->icu, ir, (r.psw >> 9) & 1))
        return -1;
    v40_set_irq(b->cpu, V40_INT, 1);        /* the acknowledge consumes it (CONTRACT.md 12) */
    return ir;
}

uint64_t so_run_steps(so_board *b, uint64_t n)
{
    uint64_t t = 0;
    while (n--)
        t += (uint64_t)v40_step(b->cpu);
    return t;
}

/* Unicorn (QEMU) runs a REP string instruction as one instruction per iteration, then, when the count ran out
   without the Z condition ending it, one more for the exit test (measured: REP STOSB of 4 counts 5; REPNE SCASB
   finding its byte at 4 of 8 counts 4, at 4 of 4 counts 4, not finding it in 3 counts 4; with CW = 0, 1).  This core
   takes one step per iteration and none for the exit.  So each such REP counts one step more here, and a count run
   past n is carried into the next call, as Unicorn spends it there.  A comparison aid only. */
uint64_t so_run_steps_unicorn(so_board *b, uint64_t n)
{
    uint64_t done = b->debt, t = 0;
    b->watch = 1;
    while (done < n) {
        v40_regs r;
        uint32_t after;
        t += (uint64_t)v40_step(b->cpu);
        done++;
        v40_regs_get(b->cpu, &r);
        after = (((uint32_t)r.ps << 4) + r.ip) & 0xFFFFF;
        if (b->rep && b->step_cw == 1 && r.cw == 0 && after != b->step_pc) {
            int zf = (r.psw >> 6) & 1, compares = b->op == 0xA6 || b->op == 0xA7 || b->op == 0xAE || b->op == 0xAF;
            int z_ended = compares && (b->rep == 0xF3 ? !zf : zf);
            if (!z_ended)
                done++;                     /* the exit test Unicorn counts */
        }
    }
    b->watch = 0;
    b->debt = done - n;
    return t;
}

uint64_t so_run_cycles(so_board *b, uint64_t n)
{
    return v40_run(b->cpu, n);
}

void so_send(so_board *b, const uint8_t *bytes, int n)
{
    so_scu_queue(&b->scu, bytes, n);
}

void so_drop_input(so_board *b)
{
    so_scu_drop(&b->scu);
}

int so_input_queued(const so_board *b)
{
    return so_scu_pending(&b->scu);
}

int so_input_count(const so_board *b)
{
    return so_scu_count(&b->scu);
}

int so_writes(const so_board *b, const so_write **w)
{
    *w = b->w;
    return b->n_w;
}

void so_clear_writes(so_board *b)
{
    b->n_w = 0;
}

int so_tx(const so_board *b, const uint8_t **bytes)
{
    *bytes = b->tx;
    return b->n_tx;
}

void so_clear_tx(so_board *b)
{
    b->n_tx = 0;
}

void so_read(const so_board *b, uint32_t addr, uint8_t *out, int n)
{
    int i;
    for (i = 0; i < n; i++)
        out[i] = b->mem[(addr + (uint32_t)i) & 0xFFFFF];
}

void so_poke(so_board *b, uint32_t addr, const uint8_t *data, int n)
{
    int i;
    for (i = 0; i < n; i++)
        b->mem[(addr + (uint32_t)i) & 0xFFFFF] = data[i];
}

uint64_t so_cycles(const so_board *b)
{
    return v40_cycles(b->cpu);
}

uint64_t so_steps(const so_board *b)
{
    return v40_steps(b->cpu);
}

void so_cpu_state(const so_board *b, uint32_t out[7])
{
    v40_regs r;
    v40_regs_get(b->cpu, &r);
    out[0] = r.ps;
    out[1] = r.ip;
    out[2] = r.psw;
    out[3] = r.halted;
    out[4] = r.fault;
    out[5] = r.undefined;
    out[6] = r.undefined_at;
}

void so_icu_state(const so_board *b, int out[2])
{
    out[0] = b->icu.imr;
    out[1] = b->icu.isr;
}
