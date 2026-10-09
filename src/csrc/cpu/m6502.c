/* m6502.c -- the 6502 of cpu.h: Fake6502's instructions (m6502_fake_machine.c, generated from the pinned file by
 * extract_6502_machine.py) behind OUR step driver, the contract's phases for the 6502 (CONTRACT.md 13):
 *
 *   A  acceptance: an NMI edge, else an IRQ level unless I (as polled, below) masks it: PC and P (B clear) pushed,
 *      I set, PC from FFFAh / FFFEh; 7 cycles
 *   B  its charge
 *   D  the boundary: steps + 1, bus->boundary(ctx, pc)
 *   E  one instruction, Fake6502's
 *   F  its charge: the table's cycles, a page crossed (upstream's penalty rule), a branch taken, decimal mode
 *
 * The poll (chip): the NMOS 6502 polls its interrupt lines before an instruction's last cycle, so CLI, SEI and PLP
 * change I only after the next poll -- the instruction after them runs before an IRQ they unmask (or one SEI masks
 * is still taken).  RTI's I counts at once.  Not modelled: a taken branch that crosses no page delaying the poll by
 * an instruction, and an interrupt hijacking BRK.  Neither occurs in the Mockingboard's firmware (no BRK; IRQs only
 * while it idles).
 *
 * Fake6502 v1.1 is (c) 2011 Mike Chambers, public domain ("if you use it please do give credit"); this file and
 * cpu.h's 6502 section are MIT.
 */
#include <stdint.h>
#include <stdlib.h>

#include "cpu.h"

struct m6502 {
    cpu_bus bus;
    double clock_hz;
    uint64_t cycles, steps;
    uint32_t saved_pc;
    /* Fake6502's state (m6502_fake_machine.c's names) */
    uint16_t pc;
    uint8_t sp, a, x, y, status;
    uint16_t oldpc, ea, reladdr, value, result;
    uint8_t opcode, penaltyop, penaltyaddr;
    int extra;                         /* cycles an instruction adds to its table's */
    /* ours */
    int irq, nmi_line, nmi_pending;
    int i_polled;                      /* I as the next poll sees it */
    uint32_t decimal, undocumented;
    uint16_t undocumented_at;
};

#if defined(_MSC_VER)
#define M6502_TLS __declspec(thread)
#else
#define M6502_TLS __thread
#endif
/* the core whose instruction runs: set by m6502_step for Fake6502's code (one per thread) */
static M6502_TLS struct m6502 *m6502_current;
#define M6502_CUR m6502_current

static uint8_t m6502_bus_read(struct m6502 *c, uint16_t addr)
{
    return c->bus.read(c->bus.ctx, addr);
}

static void m6502_bus_write(struct m6502 *c, uint16_t addr, uint8_t v)
{
    c->bus.write(c->bus.ctx, addr, v);
}

static uint8_t m6502_bus_fetch(struct m6502 *c, uint16_t addr)
{
    return c->bus.fetch ? c->bus.fetch(c->bus.ctx, addr) : c->bus.read(c->bus.ctx, addr);
}

#include "m6502_fake_machine.c"

/* the 151 documented NMOS opcodes, a row of 16 per line (1 = documented) */
static const char DOCUMENTED[256] = {
    1,1,0,0,0,1,1,0,1,1,1,0,0,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
    1,1,0,0,1,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
    1,1,0,0,0,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
    1,1,0,0,0,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
    0,1,0,0,1,1,1,0,1,0,1,0,1,1,1,0,  1,1,0,0,1,1,1,0,1,1,1,0,0,1,0,0,
    1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,1,1,1,0,1,1,1,0,1,1,1,0,
    1,1,0,0,1,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
    1,1,0,0,1,1,1,0,1,1,1,0,1,1,1,0,  1,1,0,0,0,1,1,0,1,1,0,0,0,1,1,0,
};

#define I_FLAG 0x04

m6502 *m6502_create(const cpu_bus *bus, double clock_hz)
{
    m6502 *c;
    if (!bus || !bus->read || !bus->write)
        return NULL;
    c = (m6502 *)calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->bus = *bus;
    c->clock_hz = clock_hz;
    m6502_reset(c);
    return c;
}

void m6502_destroy(m6502 *c)
{
    free(c);
}

void m6502_reset(m6502 *c)
{
    c->a = c->x = c->y = 0;
    c->sp = 0xFD;
    c->status = 0x20 | I_FLAG;         /* bit 5 always 1; I set */
    c->i_polled = 1;
    c->pc = (uint16_t)(m6502_bus_read(c, 0xFFFC) | m6502_bus_read(c, 0xFFFD) << 8);
    c->saved_pc = c->pc;
    c->cycles = c->steps = 0;
    c->nmi_pending = 0;
    c->decimal = c->undocumented = 0;
    c->undocumented_at = 0;
}

static void accept(m6502 *c, uint16_t vector)
{
    m6502_current = c;
    push16(c->pc);
    push8((uint8_t)((c->status | 0x20) & ~0x10));   /* B clear: a hardware interrupt */
    c->status |= I_FLAG;
    c->pc = (uint16_t)(m6502_bus_read(c, vector) | m6502_bus_read(c, (uint16_t)(vector + 1)) << 8);
}

int m6502_step(m6502 *c)
{
    int t = 0, op;
    /* A, B: acceptance and its charge */
    if (c->nmi_pending) {
        c->nmi_pending = 0;
        accept(c, 0xFFFA);
        t += 7;
    } else if (c->irq && !c->i_polled) {
        accept(c, 0xFFFE);
        t += 7;
    }
    c->cycles += (uint64_t)t;
    /* D: the boundary */
    c->saved_pc = c->pc;
    c->steps++;
    if (c->bus.boundary)
        c->bus.boundary(c->bus.ctx, c->pc);
    /* E: the instruction, Fake6502's (its exec loop's body) */
    m6502_current = c;
    op = m6502_bus_fetch(c, c->pc);
    c->pc++;
    c->opcode = (uint8_t)op;
    c->status |= 0x20;
    c->penaltyop = c->penaltyaddr = 0;
    c->extra = 0;
    if (!DOCUMENTED[op]) {
        c->undocumented++;
        c->undocumented_at = (uint16_t)c->saved_pc;
    }
    {
        int i_before = (c->status & I_FLAG) != 0;
        (*addrtable[op])();
        (*optable[op])();
        /* the poll: CLI, SEI and PLP change I after it, so the next acceptance sees I as it was before them;
           every other instruction (RTI too) has changed I before it */
        c->i_polled = (op == 0x58 || op == 0x78 || op == 0x28) ? i_before : (c->status & I_FLAG) != 0;
    }
    /* F: its charge */
    {
        int n = (int)ticktable[op] + c->extra + ((c->penaltyop && c->penaltyaddr) ? 1 : 0);
        c->cycles += (uint64_t)n;
        t += n;
    }
    return t;
}

uint64_t m6502_run(m6502 *c, uint64_t budget)
{
    uint64_t done = 0;
    while (done < budget)
        done += (uint64_t)m6502_step(c);
    return done;
}

void m6502_set_irq(m6502 *c, int line, int asserted)
{
    if (line == M6502_IRQ)
        c->irq = asserted != 0;
    else if (line == M6502_NMI) {
        if (asserted && !c->nmi_line)
            c->nmi_pending = 1;              /* the falling edge of /NMI */
        c->nmi_line = asserted != 0;
    }
}

uint64_t m6502_cycles(const m6502 *c) { return c->cycles; }
uint64_t m6502_steps(const m6502 *c) { return c->steps; }
uint32_t m6502_pc(const m6502 *c) { return c->saved_pc; }

void m6502_regs_get(const m6502 *c, m6502_regs *out)
{
    out->pc = c->pc;
    out->a = c->a;
    out->x = c->x;
    out->y = c->y;
    out->sp = c->sp;
    out->p = (uint8_t)((c->status | 0x20) & ~0x10);
    out->decimal = c->decimal;
    out->undocumented = c->undocumented;
    out->undocumented_at = c->undocumented_at;
}

void m6502_regs_set(m6502 *c, const m6502_regs *in)
{
    c->pc = in->pc;
    c->saved_pc = in->pc;
    c->a = in->a;
    c->x = in->x;
    c->y = in->y;
    c->sp = in->sp;
    c->status = (uint8_t)(in->p | 0x20);
    c->i_polled = (in->p & I_FLAG) != 0;
}
