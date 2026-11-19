/* test_m6809_contract.c -- CONTRACT.md 14's behaviour of the cpu.h 6809 core (m6809_mame.cpp over MAME's 6809).
 *
 * Each test is a few instructions in a flat 64K, hand-assembled here and driven through cpu.h only, and checks one
 * clause.  Cycles are Motorola's: the MC6809 data sheet DS9845-R2 (1983) -- Table 9 (pp.29-30) and Figure 19
 * (pp.31-33) for the instructions, Table 2 (p.17) for what an indexed mode adds, Figure 9 (p.10) for an IRQ's or
 * NMI's 19 cycles, Figure 10 (p.11) for FIRQ's 10, Figure 18 sheet 2 (p.23) for SYNC and CWAI.
 *   zero_budget     m6809_run(0) returns 0 and changes nothing, no bus access
 *   reset           PC from FFFEh-FFFFh at once, DP 0, I and F set, the counts zero; the other registers kept; a
 *                   latched NMI dropped and NMI disarmed; a held IRQ sampled again
 *   two_cores       two instances interleaved step by step behave as each alone
 *   boundary        the boundary callback once per step, with that step's PC, before its instruction's writes
 *   call_return     a host's call: m6809_regs_set (PC, S, its return address on the stack), steps until
 *                   m6809_next_pc() is that address; the first step is the entry point's, not the reset vector's
 *   regs_set_wait   m6809_regs_set leaves a CWAI: the next step starts at the new PC
 *   page_prefix     10h or 11h and its opcode are one step and one boundary: LDY # 4, LDS # 4, CMPU # 5; both bytes
 *                   are opcode fetches; none is counted as undefined
 *   irq_entry       an IRQ: the whole state pushed (12 bytes, E set in the stacked CC), I set and F left, PC from
 *                   FFF8h, 19 cycles; m6809_pc() during the stacking is the interrupted address
 *   firq_entry      a FIRQ: PC and CC only (E clear in the stacked CC), F and I set, PC from FFF6h, 10 cycles
 *   rti             RTI pulls what E says: 6 cycles after a FIRQ, 15 after an IRQ, every register back
 *   masks           I masks IRQ and F masks FIRQ; with I cleared the IRQ is taken, the FIRQ still held off
 *   priority        NMI before FIRQ before IRQ; the NMI stacks the whole state with E set and sets F and I
 *   irq_level       IRQ and FIRQ are levels: dropped before acceptance, not taken
 *   nmi_edge        NMI is taken through I and F, once per edge, 19 cycles; a held line is not taken again
 *   nmi_armed       an NMI edge before S is loaded is dropped (not kept); LDS, LEAS and TFR r,S arm it, EXG r,S
 *                   does not (upstream); m6809_regs_set arms or disarms as told
 *   boundary_irq    a line raised at a step's boundary is taken at the NEXT step's acceptance
 *   sync            SYNC: 3 cycles, then one-cycle slots with m6809_pc() on it, 1 more once a line shows; a
 *                   masked line only ends it; an unmasked one is then accepted (the address after SYNC pushed); with a
 *                   line already there: 4 cycles in all
 *   cwai            CWAI: CC ANDed, the whole state pushed with E set, 17 cycles, then one-cycle slots; a masked
 *                   line does not end it; an unmasked one does, with the vector alone (4 cycles, nothing pushed);
 *                   with one already there: 20 cycles
 *   cwai_firq       a FIRQ ending a CWAI: F and I set, the frame CWAI's whole one, so RTI is 15 cycles
 *   wait_nmi        an armed NMI's edge ends a SYNC (then accepted, 19 cycles) and a CWAI whatever its masks (the
 *                   vector, F and I set, the edge spent)
 *   wait_boundary   a line raised at a wait slot's boundary ends the wait in that step (SYNC's last cycle, CWAI's
 *                   vector); raised at the CWAI's own boundary, the CWAI finds it: 20 cycles
 *   undefined       opcodes Table 9 leaves unused are counted, with the instruction's address, and run as upstream
 *                   runs them: 01h as NEG; an unused byte after a prefix dropped, the next run as page 1; a second
 *                   prefix ignored
 *   free_run        14h starts upstream's free run: one fetch a step, counted once, until reset
 *   prefix_run      a run of prefixes longer than a step's limit: the step ends at 64 cycles, no interrupt is taken
 *                   inside the instruction, the next step finishes it; in a memory of nothing but 10h, m6809_run
 *                   still returns
 *   every_opcode    every first byte, alone and after 10h and 11h, with four operand patterns: each step returns
 *                   having spent 1 to 64 cycles, and none faults
 *   cycles          Table 9: inherent, immediate, direct, extended, the branches (53 instructions)
 *   cycles_indexed  Table 2: what every indexed mode adds, direct and indirect, on LDA, LEA, LDD, JMP, CLR, LDS (30)
 *   cycles_stack    PSH/PUL 5 + 1 a byte, the calls and returns, the software interrupts, MUL, EXG, TFR (20)
 *   bus_cycles      every cycle is one bus access; DEC $A000 as the data sheet's Example 2 (p.20); CLR reads its
 *                   byte before writing it, STA does not; only opcode bytes come through bus.fetch
 * The bus's in, out and irq_ack are NULL in every test: memory-mapped I/O only.
 *
 *   build: gcc -c test_m6809_contract.c; g++ ... m6809_mame.cpp   (../blazie/build_board.py does it)
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1.
 * m6809_controls.py undoes each rule in a copy of the core: exactly its tests must then fail.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

/* ---- a machine: 64K of RAM full of NOPs, a recorder at each boundary and on the bus ------------------------------ */
typedef struct { int step, line, level; } action;
typedef struct { uint16_t addr; uint8_t data; char kind; } access;   /* kind: 'f'etch, 'r'ead, 'w'rite */

typedef struct {
    uint8_t mem[65536];
    int pos;                                   /* the assembler's cursor */
    uint32_t pcs[512];
    int n_pcs;
    long fetches, reads, writes;
    long writes_seen_at_boundary;
    uint32_t pc_at_write;                      /* m6809_pc() as the last write saw it */
    access log[64];                            /* the bus, while logging */
    int n_log, logging;
    action acts[4];                            /* set a line at a step's boundary */
    int n_acts;
    m6809 *cpu;
} machine;

enum { V_IRQ = 0x0300, V_FIRQ = 0x0380, V_NMI = 0x0400, V_SWI = 0x0480, V_SWI2 = 0x0500, V_SWI3 = 0x0580 };

static int failures;

static void check(const char *name, int ok, const char *fmt, ...)
{
    va_list ap;
    printf("%s %s ", ok ? "ok" : "FAIL", name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!ok)
        failures++;
}

static void note(machine *m, uint32_t a, uint8_t v, char kind)
{
    if (m->logging && m->n_log < 64) {
        m->log[m->n_log].addr = (uint16_t)a;
        m->log[m->n_log].data = v;
        m->log[m->n_log].kind = kind;
        m->n_log++;
    }
}

static uint8_t rd(void *ctx, uint32_t a)
{
    machine *m = (machine *)ctx;
    m->reads++;
    note(m, a, m->mem[a & 0xFFFF], 'r');
    return m->mem[a & 0xFFFF];
}

static uint8_t fe(void *ctx, uint32_t a)
{
    machine *m = (machine *)ctx;
    m->fetches++;
    note(m, a, m->mem[a & 0xFFFF], 'f');
    return m->mem[a & 0xFFFF];
}

static void wr(void *ctx, uint32_t a, uint8_t v)
{
    machine *m = (machine *)ctx;
    m->mem[a & 0xFFFF] = v;
    m->writes++;
    m->pc_at_write = m6809_pc(m->cpu);
    note(m, a, v, 'w');
}

static void bnd(void *ctx, uint32_t pc)
{
    machine *m = (machine *)ctx;
    int k;
    for (k = 0; k < m->n_acts; k++)
        if (m->acts[k].step == m->n_pcs)
            m6809_set_irq(m->cpu, m->acts[k].line, m->acts[k].level);
    if (m->n_pcs < 512)
        m->pcs[m->n_pcs] = pc;
    m->n_pcs++;
    m->writes_seen_at_boundary = m->writes;
}

static void vector(machine *m, uint16_t at, uint16_t to)
{
    m->mem[at] = (uint8_t)(to >> 8);
    m->mem[at + 1] = (uint8_t)to;
}

static machine *make(uint16_t start)
{
    cpu_bus bus;
    machine *m = (machine *)calloc(1, sizeof *m);
    memset(m->mem, 0x12, sizeof m->mem);       /* NOP everywhere: a handler left empty is harmless */
    memset(&bus, 0, sizeof bus);
    bus.ctx = m;
    bus.read = rd;
    bus.fetch = fe;
    bus.write = wr;
    bus.boundary = bnd;                        /* in, out, irq_ack: NULL (never called) */
    vector(m, 0xFFFE, start);
    vector(m, 0xFFF8, V_IRQ);
    vector(m, 0xFFF6, V_FIRQ);
    vector(m, 0xFFFC, V_NMI);
    vector(m, 0xFFFA, V_SWI);
    vector(m, 0xFFF4, V_SWI2);
    vector(m, 0xFFF2, V_SWI3);
    m->pos = start;
    m->cpu = m6809_create(&bus, 1000000.0);
    return m;
}

static void op(machine *m, int n, ...)
{
    va_list ap;
    int k;
    va_start(ap, n);
    for (k = 0; k < n; k++)
        m->mem[m->pos++] = (uint8_t)va_arg(ap, int);
    va_end(ap);
}

static void at(machine *m, int where) { m->pos = where; }

static void done(machine *m)
{
    m6809_destroy(m->cpu);
    free(m);
}

static void regs(machine *m, m6809_regs *r) { m6809_regs_get(m->cpu, r); }

static int steps(machine *m, int n)
{
    int t = 0;
    while (n-- > 0)
        t += m6809_step(m->cpu);
    return t;
}

static int word(machine *m, int a) { return m->mem[a] << 8 | m->mem[a + 1]; }

static void lds(machine *m) { op(m, 4, 0x10, 0xCE, 0x80, 0x00); }   /* LDS #$8000: the stack, and NMI armed */

/* ---- the tests ------------------------------------------------------------------------------------------------ */

static void t_zero_budget(void)
{
    machine *m = make(0x0200);
    m6809_regs a, b;
    long bus;
    uint64_t n;
    regs(m, &a);
    bus = m->fetches + m->reads + m->writes;
    n = m6809_run(m->cpu, 0);
    regs(m, &b);
    check("zero_budget", n == 0 && a.pc == b.pc && m6809_steps(m->cpu) == 0 && m6809_cycles(m->cpu) == 0
          && m->fetches + m->reads + m->writes == bus, "run(0) = %llu, steps %llu, bus accesses %ld",
          (unsigned long long)n, (unsigned long long)m6809_steps(m->cpu), m->fetches + m->reads + m->writes - bus);
    done(m);
}

static void t_reset(void)
{
    machine *m = make(0x1234);
    m6809_regs r0, r1;
    int ok;
    regs(m, &r0);
    ok = r0.pc == 0x1234 && r0.dp == 0 && (r0.cc & 0x50) == 0x50 && m6809_cycles(m->cpu) == 0
        && m6809_steps(m->cpu) == 0 && !r0.nmi_armed;
    lds(m);                                    /* 1234 */
    op(m, 2, 0x86, 0x55);                      /* 1238 LDA #$55 */
    op(m, 2, 0x1F, 0x8B);                      /* 123A TFR A,DP */
    op(m, 2, 0x1C, 0xAF);                      /* 123C ANDCC #$AF: I and F clear */
    steps(m, 3);
    m6809_set_irq(m->cpu, M6809_NMI, 1);       /* armed: latched; then held through the reset */
    m6809_set_irq(m->cpu, M6809_IRQ, 1);       /* held through the reset */
    m6809_reset(m->cpu);
    regs(m, &r1);
    ok = ok && r1.pc == 0x1234 && r1.dp == 0 && r1.a == 0x55 && r1.s == 0x8000 && (r1.cc & 0x50) == 0x50
        && !r1.nmi_armed && m6809_cycles(m->cpu) == 0 && m6809_steps(m->cpu) == 0;
    m->n_pcs = 0;
    steps(m, 5);                               /* LDS (arms: the dropped NMI must not come back), LDA, TFR, ANDCC,
                                                  then the held IRQ */
    check("reset", ok && m->pcs[0] == 0x1234 && m->pcs[1] == 0x1238 && m->pcs[3] == 0x123C && m->pcs[4] == V_IRQ,
          "pc %04X dp %02X cc %02X; again: pc %04X a %02X dp %02X armed %d; then %04X %04X .. %04X %04X", r0.pc, r0.dp,
          r0.cc, r1.pc, r1.a, r1.dp, r1.nmi_armed, (unsigned)m->pcs[0], (unsigned)m->pcs[1], (unsigned)m->pcs[3],
          (unsigned)m->pcs[4]);
    done(m);
}

static void prog_count(machine *m)
{
    op(m, 3, 0x8E, 0x00, 0x00);                /* 0200 LDX #0 */
    op(m, 2, 0x30, 0x01);                      /* 0203 loop: LEAX 1,X */
    op(m, 2, 0x9F, 0x10);                      /* 0205 STX <$10 */
    op(m, 2, 0x1F, 0x10);                      /* 0207 TFR X,D */
    op(m, 2, 0xCB, 0x03);                      /* 0209 ADDB #3 */
    op(m, 2, 0x20, 0xF6);                      /* 020B BRA loop */
}

static void t_two_cores(void)
{
    machine *a = make(0x0200), *b = make(0x0200), *c = make(0x0200);
    m6809_regs ra, rb, rc;
    int k;
    prog_count(a);
    prog_count(b);
    prog_count(c);
    for (k = 0; k < 100; k++) {                /* a and b interleaved; c alone */
        m6809_step(a->cpu);
        m6809_step(b->cpu);
        m6809_step(b->cpu);
    }
    steps(c, 200);
    regs(a, &ra);
    regs(b, &rb);
    regs(c, &rc);
    check("two_cores", rb.x == rc.x && rb.b == rc.b && rb.pc == rc.pc && word(b, 0x10) == word(c, 0x10)
          && ra.x != rb.x && m6809_cycles(b->cpu) == m6809_cycles(c->cpu),
          "b: x %04X b %02X; alone: x %04X b %02X; a: x %04X", rb.x, rb.b, rc.x, rc.b, ra.x);
    done(a);
    done(b);
    done(c);
}

static void t_boundary(void)
{
    machine *m = make(0x0200);
    op(m, 2, 0x86, 0x55);                      /* LDA #$55 */
    op(m, 3, 0xB7, 0x40, 0x00);                /* STA $4000 */
    steps(m, 2);
    check("boundary", m->n_pcs == 2 && m->pcs[0] == 0x0200 && m->pcs[1] == 0x0202 && m->writes_seen_at_boundary == 0
          && m->writes == 1 && m6809_steps(m->cpu) == 2, "pcs %d (%04X %04X), writes before the STA's boundary %ld",
          m->n_pcs, (unsigned)m->pcs[0], (unsigned)m->pcs[1], m->writes_seen_at_boundary);
    done(m);
}

static void t_call_return(void)
{
    /* the vector points elsewhere: the entry point is the host's */
    machine *m = make(0x0200);
    m6809_regs r;
    int n = 0, t = 0;
    at(m, 0x5800);
    op(m, 2, 0x86, 0x42);                      /* 5800 LDA #$42 */
    op(m, 3, 0xBD, 0x58, 0x10);                /* 5802 JSR $5810 */
    op(m, 1, 0x39);                            /* 5805 RTS */
    at(m, 0x5810);
    op(m, 2, 0xC6, 0x07);                      /* 5810 LDB #7 */
    op(m, 1, 0x39);                            /* 5812 RTS */
    memset(&r, 0, sizeof r);
    r.pc = 0x5800;
    r.s = 0x7FFE;                              /* the host's return address on the stack: 0100h */
    m->mem[0x7FFE] = 0x01;
    m->mem[0x7FFF] = 0x00;
    r.cc = 0x50;
    m6809_regs_set(m->cpu, &r);
    while (m6809_next_pc(m->cpu) != 0x0100 && n < 100) {
        t += m6809_step(m->cpu);
        n++;
    }
    regs(m, &r);
    check("call_return", n == 5 && t == 2 + 8 + 2 + 5 + 5 && r.a == 0x42 && r.b == 7 && r.s == 0x8000 && r.pc == 0x0100
          && m->pcs[0] == 0x5800 && m->pcs[2] == 0x5810,
          "%d steps, %d cycles (LDA 2, JSR 8, LDB 2, RTS 5, RTS 5), a %02X b %02X s %04X, first step at %04X", n, t,
          r.a, r.b, r.s, (unsigned)m->pcs[0]);
    done(m);
}

static void t_regs_set_wait(void)
{
    machine *m = make(0x0200);
    m6809_regs r, w;
    lds(m);
    op(m, 2, 0x3C, 0xFF);                      /* 0204 CWAI #$FF: I and F stay set, nothing ends it */
    at(m, V_IRQ);
    op(m, 2, 0x86, 0x77);                      /* 0300 LDA #$77 */
    steps(m, 3);                               /* LDS, CWAI, a slot */
    regs(m, &w);
    r = w;
    r.pc = V_IRQ;
    m6809_regs_set(m->cpu, &r);
    steps(m, 1);
    regs(m, &r);
    check("regs_set_wait", w.waiting == M6809_IN_CWAI && r.waiting == M6809_RUNNING && r.a == 0x77 && r.pc == 0x0302
          && m->pcs[3] == V_IRQ, "waiting %d, then %d; a %02X, the step at %04X", w.waiting, r.waiting, r.a,
          (unsigned)m->pcs[3]);
    done(m);
}

static void t_page_prefix(void)
{
    machine *m = make(0x0200);
    m6809_regs r;
    int t1, t2, t3;
    op(m, 4, 0x10, 0x8E, 0x12, 0x34);          /* 0200 LDY #$1234 */
    op(m, 4, 0x10, 0xCE, 0x40, 0x00);          /* 0204 LDS #$4000 */
    op(m, 4, 0x11, 0x83, 0x00, 0x00);          /* 0208 CMPU #$0000 */
    t1 = m6809_step(m->cpu);
    t2 = m6809_step(m->cpu);
    t3 = m6809_step(m->cpu);
    regs(m, &r);
    check("page_prefix", t1 == 4 && t2 == 4 && t3 == 5 && r.y == 0x1234 && r.s == 0x4000 && r.pc == 0x020C
          && m->n_pcs == 3 && m->pcs[1] == 0x0204 && m->pcs[2] == 0x0208 && m->fetches == 6 && r.undefined == 0,
          "LDY # %d, LDS # %d, CMPU # %d cycles; %d steps; %ld opcode fetches; %u undefined", t1, t2, t3, m->n_pcs,
          m->fetches, (unsigned)r.undefined);
    done(m);
}

static void t_irq_entry(void)
{
    static const uint8_t frame[12] = {0x80, 0x11, 0x22, 0x00, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x02, 0x15};
    machine *m = make(0x0200);
    m6809_regs r;
    int t;
    lds(m);                                    /* 0200 */
    op(m, 2, 0x86, 0x11);                      /* 0204 LDA #$11 */
    op(m, 2, 0xC6, 0x22);                      /* 0206 LDB #$22 */
    op(m, 3, 0x8E, 0x33, 0x44);                /* 0208 LDX #$3344 */
    op(m, 4, 0x10, 0x8E, 0x55, 0x66);          /* 020B LDY #$5566 */
    op(m, 3, 0xCE, 0x77, 0x88);                /* 020F LDU #$7788 */
    op(m, 2, 0x1C, 0xAF);                      /* 0212 ANDCC #$AF: I and F clear, CC = 00 */
    op(m, 2, 0x12, 0x12);                      /* 0214, 0215 NOP */
    steps(m, 8);                               /* ... through the NOP at 0214: 0215 is next */
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    t = m6809_step(m->cpu);                    /* the acceptance, then the handler's NOP */
    regs(m, &r);
    /* from S up: CC (E set), A, B, DP, X, Y, U, PC -- the address of the instruction not yet run */
    check("irq_entry", r.s == 0x7FF4 && memcmp(m->mem + 0x7FF4, frame, 12) == 0 && r.cc == 0x90 && m->pcs[8] == V_IRQ
          && t == 19 + 2 && m->pc_at_write == 0x0215 && m->writes == 12,
          "s %04X, stacked cc %02X pc %04X, cc %02X, the step at %04X, %d cycles (19 + NOP), pc at the pushes %04X",
          r.s, m->mem[0x7FF4], word(m, 0x7FFE), r.cc, (unsigned)m->pcs[8], t, (unsigned)m->pc_at_write);
    done(m);
}

static void t_firq_entry(void)
{
    machine *m = make(0x0200);
    m6809_regs r;
    int t;
    lds(m);                                    /* 0200 */
    op(m, 2, 0x1A, 0x80);                      /* 0204 ORCC #$80: E set, so the acceptance must clear it */
    op(m, 2, 0x1C, 0xA0);                      /* 0206 ANDCC #$A0: I, F and LDS's N clear, CC = 80 */
    steps(m, 3);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    t = m6809_step(m->cpu);
    regs(m, &r);
    check("firq_entry", r.s == 0x7FFD && m->mem[0x7FFD] == 0x00 && word(m, 0x7FFE) == 0x0208 && m->mem[0x7FFC] == 0x12
          && r.cc == 0x50 && m->pcs[3] == V_FIRQ && t == 10 + 2 && m->writes == 3,
          "s %04X, stacked cc %02X pc %04X, cc %02X, the step at %04X, %d cycles (10 + NOP), %ld bytes pushed", r.s,
          m->mem[0x7FFD], word(m, 0x7FFE), r.cc, (unsigned)m->pcs[3], t, m->writes);
    done(m);
}

static void t_rti(void)
{
    machine *m = make(0x0200);
    m6809_regs rf, ri;
    int tf, ti;
    lds(m);                                    /* 0200 */
    op(m, 3, 0x8E, 0x33, 0x44);                /* 0204 LDX #$3344 */
    op(m, 2, 0x1C, 0xAF);                      /* 0207 ANDCC #$AF */
    at(m, V_FIRQ);
    op(m, 2, 0x12, 0x3B);                      /* 0380 NOP; 0381 RTI */
    at(m, V_IRQ);
    op(m, 4, 0x8E, 0xFF, 0xFF, 0x3B);          /* 0300 LDX #$FFFF; 0303 RTI */
    steps(m, 3);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    steps(m, 1);                               /* FIRQ, the handler's NOP */
    m6809_set_irq(m->cpu, M6809_FIRQ, 0);
    tf = m6809_step(m->cpu);                   /* RTI: CC and PC */
    regs(m, &rf);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    steps(m, 1);                               /* IRQ, the handler's LDX */
    m6809_set_irq(m->cpu, M6809_IRQ, 0);
    ti = m6809_step(m->cpu);                   /* RTI: everything */
    regs(m, &ri);
    check("rti", tf == 6 && rf.pc == 0x0209 && rf.s == 0x8000 && rf.cc == 0x00 && ti == 15 && ri.pc == 0x0209
          && ri.s == 0x8000 && ri.x == 0x3344 && ri.cc == 0x80,
          "after FIRQ %d cycles, pc %04X s %04X; after IRQ %d cycles, pc %04X s %04X x %04X cc %02X", tf, rf.pc, rf.s,
          ti, ri.pc, ri.s, ri.x, ri.cc);
    done(m);
}

static void t_masks(void)
{
    machine *m = make(0x0200);
    m6809_regs r;
    lds(m);                                    /* 0200 */
    op(m, 1, 0x12);                            /* 0204 NOP */
    op(m, 2, 0x1C, 0xEF);                      /* 0205 ANDCC #$EF: I clear, F still set */
    steps(m, 1);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    steps(m, 3);                               /* NOP, ANDCC (both masked), then the IRQ */
    regs(m, &r);
    check("masks", m->pcs[1] == 0x0204 && m->pcs[2] == 0x0205 && m->pcs[3] == V_IRQ && (r.cc & 0x50) == 0x50,
          "masked: %04X %04X; I clear: %04X", (unsigned)m->pcs[1], (unsigned)m->pcs[2], (unsigned)m->pcs[3]);
    done(m);
}

static uint32_t first_taken(int nmi, m6809_regs *after, int *stacked_cc)
{
    machine *m = make(0x0200);
    uint32_t pc;
    lds(m);
    op(m, 2, 0x1C, 0xA0);                      /* ANDCC #$A0: I and F clear, CC = 00 */
    steps(m, 2);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    if (nmi)
        m6809_set_irq(m->cpu, M6809_NMI, 1);
    steps(m, 1);                               /* the acceptance, the handler's NOP */
    pc = m->pcs[2];
    regs(m, after);
    *stacked_cc = m->mem[after->s];
    done(m);
    return pc;
}

static void t_priority(void)
{
    m6809_regs rn, rf;
    int sn, sf;
    uint32_t all = first_taken(1, &rn, &sn), two = first_taken(0, &rf, &sf);
    /* and what the NMI did: the whole state stacked with E set, F and I set */
    check("priority", all == V_NMI && two == V_FIRQ && rn.s == 0x7FF4 && sn == 0x80 && (rn.cc & 0x50) == 0x50,
          "NMI, FIRQ and IRQ: %04X (s %04X, stacked cc %02X, cc %02X); FIRQ and IRQ: %04X", (unsigned)all, rn.s, sn,
          rn.cc, (unsigned)two);
}

static void t_irq_level(void)
{
    machine *m = make(0x0200);
    lds(m);
    op(m, 2, 0x1C, 0xAF);                      /* 0204 ANDCC #$AF */
    steps(m, 2);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    m6809_set_irq(m->cpu, M6809_IRQ, 0);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    m6809_set_irq(m->cpu, M6809_FIRQ, 0);
    steps(m, 1);
    check("irq_level", m->pcs[2] == 0x0206, "the step at %04X", (unsigned)m->pcs[2]);
    done(m);
}

static void t_nmi_edge(void)
{
    machine *m = make(0x0200);
    m6809_regs r;
    int t;
    lds(m);                                    /* 0200; I and F stay set */
    at(m, V_NMI);
    op(m, 1, 0x3B);                            /* 0400 RTI */
    steps(m, 1);
    m6809_set_irq(m->cpu, M6809_NMI, 1);
    t = m6809_step(m->cpu);                    /* NMI through I and F; the handler's RTI */
    m6809_set_irq(m->cpu, M6809_NMI, 1);       /* a board refreshing its line: still held, no new edge */
    steps(m, 2);
    m6809_set_irq(m->cpu, M6809_NMI, 0);
    m6809_set_irq(m->cpu, M6809_NMI, 1);       /* a new edge */
    steps(m, 1);
    regs(m, &r);
    check("nmi_edge", m->pcs[1] == V_NMI && t == 19 + 15 && m->pcs[2] == 0x0204 && m->pcs[3] == 0x0205
          && m->pcs[4] == V_NMI, "steps at %04X (%d cycles: 19 + RTI's 15) %04X %04X (the line held), a new edge %04X",
          (unsigned)m->pcs[1], t, (unsigned)m->pcs[2], (unsigned)m->pcs[3], (unsigned)m->pcs[4]);
    done(m);
}

/* is an NMI edge taken after the two instructions at 0200h?  (X = 8000h first, for the forms that load S from X) */
static int nmi_after(int n, ...)
{
    machine *m = make(0x0200);
    va_list ap;
    int k, taken;
    op(m, 3, 0x8E, 0x80, 0x00);                /* 0200 LDX #$8000 */
    va_start(ap, n);
    for (k = 0; k < n; k++)
        m->mem[m->pos++] = (uint8_t)va_arg(ap, int);
    va_end(ap);
    steps(m, 2);
    m6809_set_irq(m->cpu, M6809_NMI, 1);
    steps(m, 1);
    taken = m->pcs[2] == V_NMI;
    done(m);
    return taken;
}

static void t_nmi_armed(void)
{
    machine *m = make(0x0200);
    m6809_regs r;
    int early, kept, armed, edge, leas, tfr, exg, set_on, set_off;
    op(m, 2, 0x12, 0x12);                      /* 0200, 0201 NOP */
    lds(m);                                    /* 0202 */
    m6809_set_irq(m->cpu, M6809_NMI, 1);       /* an edge before S is loaded */
    steps(m, 2);
    early = m->pcs[1] == V_NMI;
    steps(m, 2);                               /* LDS; then 0206: the early edge must not have been kept */
    kept = m->pcs[3] == V_NMI;
    regs(m, &r);
    armed = r.nmi_armed;
    m6809_set_irq(m->cpu, M6809_NMI, 0);
    m6809_set_irq(m->cpu, M6809_NMI, 1);
    steps(m, 1);
    edge = m->pcs[4] == V_NMI;
    done(m);
    leas = nmi_after(2, 0x32, 0x84);           /* LEAS ,X */
    tfr = nmi_after(2, 0x1F, 0x14);            /* TFR X,S */
    exg = nmi_after(2, 0x1E, 0x14);            /* EXG X,S: S is loaded, upstream does not arm */
    {
        int on;
        for (on = 0; on < 2; on++) {
            machine *h = make(0x0200);
            m6809_regs s;
            regs(h, &s);
            s.s = 0x8000;
            s.nmi_armed = (uint8_t)on;
            m6809_regs_set(h->cpu, &s);
            m6809_set_irq(h->cpu, M6809_NMI, 1);
            steps(h, 1);
            if (on)
                set_on = h->pcs[0] == V_NMI;
            else
                set_off = h->pcs[0] == V_NMI;
            done(h);
        }
    }
    check("nmi_armed", !early && !kept && armed && edge && leas && tfr && !exg && set_on && !set_off,
          "before LDS %d, kept %d, armed %d, a new edge %d; LEAS %d, TFR %d, EXG %d; regs_set 1: %d, 0: %d", early,
          kept, armed, edge, leas, tfr, exg, set_on, set_off);
}

static void t_boundary_irq(void)
{
    machine *m = make(0x0200);
    int t;
    lds(m);
    op(m, 2, 0x1C, 0xAF);                      /* 0204 ANDCC #$AF */
    op(m, 2, 0x12, 0x12);                      /* 0206, 0207 NOP */
    m->acts[0].step = 2;                       /* at the third step's boundary (the NOP at 0206) */
    m->acts[0].line = M6809_IRQ;
    m->acts[0].level = 1;
    m->n_acts = 1;
    steps(m, 2);
    t = m6809_step(m->cpu);                    /* the NOP runs: the IRQ its boundary raised waits */
    steps(m, 1);
    check("boundary_irq", t == 2 && m->pcs[2] == 0x0206 && m->pcs[3] == V_IRQ && word(m, 0x7FFE) == 0x0207,
          "the boundary's step %d cycles at %04X, then %04X with %04X pushed", t, (unsigned)m->pcs[2],
          (unsigned)m->pcs[3], word(m, 0x7FFE));
    done(m);
}

static void t_sync(void)
{
    machine *m = make(0x0200), *p, *u;
    m6809_regs r1, r2, r3;
    int t1, t2, t3, t4, tp1, tp2, tu, ok;
    uint32_t slot_pc;
    /* a masked line: it ends the SYNC, nothing is stacked */
    lds(m);                                    /* 0200 */
    op(m, 1, 0x13);                            /* 0204 SYNC */
    op(m, 2, 0x86, 0x5A);                      /* 0205 LDA #$5A */
    steps(m, 1);
    t1 = m6809_step(m->cpu);
    regs(m, &r1);
    t2 = m6809_step(m->cpu);
    slot_pc = m6809_pc(m->cpu);
    t3 = m6809_step(m->cpu);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);       /* I is set */
    t4 = m6809_step(m->cpu);
    regs(m, &r2);
    steps(m, 1);
    regs(m, &r3);
    ok = t1 == 3 && r1.waiting == M6809_IN_SYNC && t2 == 1 && t3 == 1 && slot_pc == 0x0204 && t4 == 1
        && r2.waiting == M6809_RUNNING && r3.a == 0x5A && r3.s == 0x8000 && m->pcs[5] == 0x0205;
    done(m);
    /* the line there before the SYNC: 4 cycles in all (Table 9: >= 4) */
    p = make(0x0200);
    lds(p);
    op(p, 1, 0x13);
    steps(p, 1);
    m6809_set_irq(p->cpu, M6809_FIRQ, 1);      /* F is set */
    tp1 = m6809_step(p->cpu);
    tp2 = m6809_step(p->cpu);
    regs(p, &r2);
    ok = ok && tp1 + tp2 == 4 && r2.waiting == M6809_RUNNING && r2.pc == 0x0205;
    done(p);
    /* an unmasked line: after the SYNC's last cycle it is accepted, the address after the SYNC pushed */
    u = make(0x0200);
    lds(u);                                    /* 0200 */
    op(u, 2, 0x1C, 0xAF);                      /* 0204 ANDCC #$AF */
    op(u, 1, 0x13);                            /* 0206 SYNC */
    steps(u, 4);                               /* LDS, ANDCC, SYNC, a slot */
    m6809_set_irq(u->cpu, M6809_IRQ, 1);
    tu = m6809_step(u->cpu);                   /* the SYNC's last cycle */
    steps(u, 1);                               /* the acceptance */
    ok = ok && tu == 1 && u->pcs[5] == V_IRQ && word(u, 0x7FFE) == 0x0207;
    check("sync", ok, "SYNC %d, slots %d %d at %04X, the line: %d; already there: %d + %d; unmasked: %d, then %04X "
          "with %04X pushed", t1, t2, t3, (unsigned)slot_pc, t4, tp1, tp2, tu, (unsigned)u->pcs[5], word(u, 0x7FFE));
    done(u);
}

static void t_cwai(void)
{
    machine *m = make(0x0200), *p;
    m6809_regs r1, r2;
    int t1, t2, t3, t4, tp, ok, stacked;
    long pushed;
    lds(m);                                    /* 0200 */
    op(m, 2, 0x86, 0x11);                      /* 0204 LDA #$11 */
    op(m, 2, 0x3C, 0xEF);                      /* 0206 CWAI #$EF: I clear, F still set: CC = 40 */
    steps(m, 2);
    t1 = m6809_step(m->cpu);
    regs(m, &r1);
    pushed = m->writes;
    t2 = m6809_step(m->cpu);                   /* a slot */
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);      /* masked: does not end it */
    t3 = m6809_step(m->cpu);
    m6809_set_irq(m->cpu, M6809_FIRQ, 0);
    m6809_set_irq(m->cpu, M6809_IRQ, 1);
    t4 = m6809_step(m->cpu);                   /* the vector, nothing else */
    regs(m, &r2);
    steps(m, 1);
    ok = t1 == 17 && r1.waiting == M6809_IN_CWAI && r1.s == 0x7FF4 && m->mem[0x7FF4] == 0xC0 && m->mem[0x7FF5] == 0x11
        && word(m, 0x7FFE) == 0x0208 && pushed == 12 && t2 == 1 && t3 == 1 && m->pcs[4] == 0x0206 && t4 == 4
        && r2.pc == V_IRQ && r2.cc == 0xD0 && r2.s == 0x7FF4 && m->writes == 12 && r2.waiting == M6809_RUNNING
        && m->pcs[6] == V_IRQ;
    stacked = m->mem[0x7FF4];
    done(m);
    /* the interrupt already there, unmasked by the CWAI itself: 20 cycles in its own step (Table 9: >= 20) */
    p = make(0x0200);
    lds(p);
    op(p, 2, 0x3C, 0xEF);
    steps(p, 1);
    m6809_set_irq(p->cpu, M6809_IRQ, 1);       /* I is set until the CWAI */
    tp = m6809_step(p->cpu);
    regs(p, &r1);
    ok = ok && tp == 20 && r1.pc == V_IRQ && r1.waiting == M6809_RUNNING;
    check("cwai", ok, "CWAI %d cycles, stacked cc %02X, slots %d %d, the IRQ: %d cycles to %04X, cc %02X, %ld bytes "
          "pushed in all; already there: %d", t1, stacked, t2, t3, t4, r2.pc, r2.cc, pushed, tp);
    done(p);
}

static void t_cwai_firq(void)
{
    machine *m = make(0x0200);
    m6809_regs r1, r2;
    int t;
    lds(m);                                    /* 0200 */
    op(m, 3, 0x8E, 0x33, 0x44);                /* 0204 LDX #$3344 */
    op(m, 2, 0x3C, 0xBF);                      /* 0207 CWAI #$BF: F clear, I still set: CC = 10 */
    at(m, V_FIRQ);
    op(m, 4, 0x8E, 0xFF, 0xFF, 0x3B);          /* 0380 LDX #$FFFF; 0383 RTI */
    steps(m, 3);
    m6809_set_irq(m->cpu, M6809_FIRQ, 1);
    steps(m, 1);                               /* the vector */
    regs(m, &r1);
    m6809_set_irq(m->cpu, M6809_FIRQ, 0);
    steps(m, 1);                               /* LDX */
    t = m6809_step(m->cpu);                    /* RTI: the stacked E says everything */
    regs(m, &r2);
    check("cwai_firq", r1.pc == V_FIRQ && r1.cc == 0xD0 && m->mem[0x7FF4] == 0x90 && m->writes == 12 && t == 15
          && r2.pc == 0x0209 && r2.x == 0x3344 && r2.s == 0x8000,
          "to %04X, cc %02X, stacked cc %02X, %ld bytes; RTI %d cycles to %04X, x %04X", r1.pc, r1.cc, m->mem[0x7FF4],
          m->writes, t, r2.pc, r2.x);
    done(m);
}

static void t_wait_nmi(void)
{
    /* an armed NMI's edge ends a SYNC, and a CWAI whatever its masks */
    machine *m = make(0x0200), *w;
    m6809_regs r;
    int ts, ta, tc, th, ok;
    lds(m);                                    /* 0200 */
    op(m, 1, 0x13);                            /* 0204 SYNC */
    steps(m, 2);
    m6809_set_irq(m->cpu, M6809_NMI, 1);
    ts = m6809_step(m->cpu);                   /* the SYNC's last cycle */
    ta = m6809_step(m->cpu);                   /* the acceptance, the handler's NOP */
    ok = ts == 1 && ta == 19 + 2 && m->pcs[3] == V_NMI && word(m, 0x7FFE) == 0x0205;
    done(m);
    w = make(0x0200);
    lds(w);                                    /* 0200 */
    op(w, 2, 0x3C, 0xAF);                      /* 0204 CWAI #$AF: I and F clear */
    steps(w, 2);
    m6809_set_irq(w->cpu, M6809_NMI, 1);
    tc = m6809_step(w->cpu);                   /* the vector; F and I set */
    regs(w, &r);
    th = steps(w, 2);                          /* the handler's two NOPs: the edge is spent, no second NMI */
    check("wait_nmi", ok && tc == 4 && r.pc == V_NMI && (r.cc & 0x50) == 0x50 && th == 4 && w->writes == 12
          && w->pcs[3] == V_NMI, "SYNC: %d, then %d cycles (19 + NOP); CWAI: %d cycles to %04X, cc %02X, then %d "
          "cycles, %ld bytes pushed in all", ts, ta, tc, r.pc, r.cc, th, w->writes);
    done(w);
}

static void raise_at(machine *m, int step, int line)
{
    m->acts[0].step = step;
    m->acts[0].line = line;
    m->acts[0].level = 1;
    m->n_acts = 1;
}

static void t_wait_boundary(void)
{
    /* a wait's test for a line is the instruction's own, at E: it comes after the step's boundary, so a line the
       board raises at a slot's boundary ends the wait in that step */
    machine *s = make(0x0200), *c, *o;
    m6809_regs rs, rc, ro;
    int ts, tc, to, ok;
    lds(s);                                    /* 0200 */
    op(s, 2, 0x1C, 0xAF);                      /* 0204 ANDCC #$AF */
    op(s, 1, 0x13);                            /* 0206 SYNC */
    raise_at(s, 4, M6809_IRQ);                 /* steps: LDS, ANDCC, SYNC, a slot, then this one's boundary */
    steps(s, 4);
    ts = m6809_step(s->cpu);                   /* the SYNC's last cycle */
    regs(s, &rs);
    steps(s, 1);                               /* the acceptance is still the next step's A */
    ok = ts == 1 && rs.waiting == M6809_RUNNING && rs.s == 0x8000 && s->pcs[4] == 0x0206 && s->pcs[5] == V_IRQ
        && word(s, 0x7FFE) == 0x0207;
    done(s);
    c = make(0x0200);
    lds(c);                                    /* 0200 */
    op(c, 2, 0x3C, 0xEF);                      /* 0204 CWAI #$EF: I clear */
    raise_at(c, 3, M6809_IRQ);                 /* steps: LDS, CWAI, a slot, then this one's boundary */
    steps(c, 3);
    tc = m6809_step(c->cpu);                   /* the vector, in the step whose boundary raised the line */
    regs(c, &rc);
    ok = ok && tc == 4 && rc.pc == V_IRQ && c->pcs[3] == 0x0204;
    done(c);
    o = make(0x0200);
    lds(o);
    op(o, 2, 0x3C, 0xEF);
    raise_at(o, 1, M6809_IRQ);                 /* at the CWAI's own boundary: its test, 16 cycles on, finds the line */
    steps(o, 1);
    to = m6809_step(o->cpu);
    regs(o, &ro);
    check("wait_boundary", ok && to == 20 && ro.pc == V_IRQ, "raised at a slot's boundary: SYNC ends in %d cycle, "
          "CWAI vectors in %d; at the CWAI's own boundary: %d cycles to %04X", ts, tc, to, ro.pc);
    done(o);
}

static void t_undefined(void)
{
    machine *m = make(0x0200);
    m6809_regs r0, r1, r2, r3, r4;
    int t1, t2, t3, t4;
    op(m, 1, 0x12);                            /* 0200 NOP */
    op(m, 2, 0x01, 0x20);                      /* 0201 01h: upstream runs NEG <$20 */
    op(m, 4, 0x10, 0x4F, 0x86, 0x77);          /* 0203 10h 4Fh (unused on page 2), then LDA #$77 */
    op(m, 5, 0x10, 0x10, 0x8E, 0x12, 0x34);    /* 0207 10h 10h, then LDY #$1234 */
    op(m, 4, 0x11, 0x4F, 0xC6, 0x33);          /* 020C 11h 4Fh (unused on page 3), then LDB #$33 */
    m->mem[0x0020] = 0x05;
    steps(m, 1);
    regs(m, &r0);
    t1 = m6809_step(m->cpu);
    regs(m, &r1);
    t2 = m6809_step(m->cpu);
    regs(m, &r2);
    t3 = m6809_step(m->cpu);
    regs(m, &r3);
    t4 = m6809_step(m->cpu);
    regs(m, &r4);
    check("undefined", r0.undefined == 0 && r1.undefined == 1 && r1.undefined_at == 0x0201 && t1 == 6
          && m->mem[0x0020] == 0xFB && r2.undefined == 2 && r2.undefined_at == 0x0203 && r2.a == 0x77 && r2.pc == 0x0207
          && t2 == 4 && r3.undefined == 3 && r3.undefined_at == 0x0207 && r3.y == 0x1234 && r3.pc == 0x020C && t3 == 5
          && r4.undefined == 4 && r4.undefined_at == 0x020C && r4.b == 0x33 && r4.pc == 0x0210 && t4 == 4,
          "01h: %u at %04X, %d cycles, [20] %02X; 10h 4Fh: %u at %04X, a %02X pc %04X, %d cycles; 10h 10h: %u, y %04X, "
          "%d cycles; 11h 4Fh: %u, b %02X, %d cycles", (unsigned)r1.undefined, r1.undefined_at, t1, m->mem[0x0020],
          (unsigned)r2.undefined, r2.undefined_at, r2.a, r2.pc, t2, (unsigned)r3.undefined, r3.y, t3,
          (unsigned)r4.undefined, r4.b, t4);
    done(m);
}

static void t_free_run(void)
{
    machine *m = make(0x0200);
    m6809_regs r1, r2, r3;
    int t1, t2;
    long f;
    op(m, 4, 0x14, 0x01, 0x01, 0x01);          /* 0200 14h, then bytes that are unused opcodes themselves */
    t1 = m6809_step(m->cpu);
    regs(m, &r1);
    f = m->fetches;
    t2 = steps(m, 3);
    regs(m, &r2);
    f = m->fetches - f;
    m6809_reset(m->cpu);
    regs(m, &r3);
    check("free_run", t1 == 1 && r1.free_run && r1.undefined == 1 && r1.undefined_at == 0x0200 && t2 == 3 && f == 3
          && r2.pc == 0x0204 && r2.undefined == 1 && r2.free_run && !r3.free_run && r3.undefined == 0,
          "14h: %d cycle, free_run %d, %u undefined; 3 steps: %d cycles, %ld fetches, pc %04X, %u undefined; reset: %d",
          t1, r1.free_run, (unsigned)r1.undefined, t2, f, r2.pc, (unsigned)r2.undefined, r3.free_run);
    done(m);
}

static void t_prefix_run(void)
{
    machine *m = make(0x0200), *e;
    m6809_regs r1, r2;
    int t1, t2, k;
    uint32_t pc1;
    uint64_t endless;
    for (k = 0; k < 100; k++)
        op(m, 1, 0x10);                        /* 0200-0263: a hundred prefixes */
    op(m, 3, 0x8E, 0x12, 0x34);                /* 0264 ... LDY #$1234 */
    m->acts[0].step = 0;                       /* its stack and an IRQ, from the start: I is clear (below) */
    m->acts[0].line = M6809_IRQ;
    m->acts[0].level = 1;
    m->n_acts = 1;
    regs(m, &r1);
    r1.s = 0x8000;
    r1.cc = 0x00;
    m6809_regs_set(m->cpu, &r1);
    t1 = m6809_step(m->cpu);                   /* cut at the limit */
    regs(m, &r1);
    pc1 = m6809_pc(m->cpu);
    t2 = m6809_step(m->cpu);                   /* the rest: no acceptance inside the instruction */
    regs(m, &r2);
    steps(m, 1);                               /* now the IRQ */
    /* a memory of nothing but prefixes: m6809_run still returns */
    e = make(0x0200);
    memset(e->mem, 0x10, sizeof e->mem);
    endless = m6809_run(e->cpu, 1);
    done(e);
    check("prefix_run", t1 == 64 && r1.y != 0x1234 && pc1 == 0x0200 && t2 == 100 + 3 - 64 && r2.y == 0x1234
          && r2.pc == 0x0267 && r2.s == 0x8000 && m->pcs[1] == 0x0200 && m->pcs[2] == V_IRQ
          && word(m, 0x7FFE) == 0x0267 && r2.undefined == 99 && endless == 64,
          "%d cycles, then %d (103 in all), y %04X pc %04X; steps at %04X %04X %04X, %04X pushed; %u undefined; "
          "nothing but 10h: run(1) = %llu", t1, t2, r2.y, r2.pc, (unsigned)m->pcs[0], (unsigned)m->pcs[1],
          (unsigned)m->pcs[2], word(m, 0x7FFE), (unsigned)r2.undefined, (unsigned long long)endless);
    done(m);
}

static void t_every_opcode(void)
{
    /* every first byte, alone and after 10h and 11h, each with four operand patterns (00h; 84h, the ,X postbyte;
       9Fh, extended indirect; FFh): three steps, each returning with at least a cycle spent, and no fault */
    static const int prefix[3] = {-1, 0x10, 0x11};
    static const uint8_t fill[4] = {0x00, 0x84, 0x9F, 0xFF};
    int p, o, f, k, bad = 0, first = -1, first_t = 0, first_fault = 0;
    long n = 0;
    for (p = 0; p < 3; p++)
        for (o = 0; o < 256; o++)
            for (f = 0; f < 4; f++) {
                machine *m = make(0x1000);
                m6809_regs r;
                int low = 99, high = 0, t;
                if (prefix[p] >= 0)
                    op(m, 1, prefix[p]);
                op(m, 5, o, fill[f], fill[f], fill[f], fill[f]);
                regs(m, &r);
                r.s = 0x8000;
                r.u = 0x7000;
                r.x = 0x4000;
                r.y = 0x4100;
                m6809_regs_set(m->cpu, &r);
                for (k = 0; k < 3; k++) {
                    t = m6809_step(m->cpu);
                    if (t < low)
                        low = t;
                    if (t > high)
                        high = t;
                }
                regs(m, &r);
                n++;
                if ((low < 1 || high > 64 || r.fault) && !bad++) {
                    first = (prefix[p] >= 0 ? prefix[p] << 8 : 0) | o;
                    first_t = low < 1 ? low : high;
                    first_fault = r.fault;
                }
                done(m);
            }
    if (bad)
        check("every_opcode", 0, "%d of %ld wrong; the first: %04X with a step of %d cycles, fault %d", bad, n, first,
              first_t, first_fault);
    else
        check("every_opcode", 1, "%ld programs, three steps each: every step 1 to 64 cycles, no fault", n);
}

/* ---- cycle counts: one instruction at 1000h, the registers pointing at RAM, its cycles and its bus accesses ------ */
typedef struct {
    const char *name;
    int cycles;                                /* Motorola's */
    int n;
    uint8_t bytes[5];
} timing;

static int count_of(const timing *c, long *bus)
{
    machine *m = make(0x1000);
    m6809_regs r;
    int t, k;
    for (k = 0; k < c->n; k++)
        m->mem[0x1000 + k] = c->bytes[k];
    regs(m, &r);
    r.s = 0x8000;
    r.u = 0x7000;
    r.x = 0x4000;
    r.y = 0x4100;
    r.a = 0x01;
    r.b = 0x02;
    r.cc = 0x50;                               /* Z and C clear: BNE and BCC are taken, BEQ is not */
    m6809_regs_set(m->cpu, &r);
    *bus = m->fetches + m->reads + m->writes;
    t = m6809_step(m->cpu);
    *bus = m->fetches + m->reads + m->writes - *bus;
    done(m);
    return t;
}

static void cycle_table(const char *name, const timing *cases, int n)
{
    int k, bad = 0, t = 0;
    long bus = 0;
    const char *first = "";
    int first_t = 0, first_want = 0;
    long first_bus = 0;
    for (k = 0; k < n; k++) {
        t = count_of(&cases[k], &bus);
        if ((t != cases[k].cycles || bus != t) && !bad++) {
            first = cases[k].name;
            first_t = t;
            first_want = cases[k].cycles;
            first_bus = bus;
        }
    }
    if (bad)
        check(name, 0, "%d of %d differ; the first: %s %d cycles, %ld bus accesses (Motorola: %d)", bad, n, first,
              first_t, first_bus, first_want);
    else
        check(name, 1, "%d instructions, each Motorola's cycles and as many bus accesses", n);
}

static void t_cycles(void)
{
    /* Table 9 (pp.29-30); the long branches 5(6): Figure 19's note 5 (p.32) */
    static const timing cases[] = {
        {"NOP", 2, 1, {0x12}}, {"CLRA", 2, 1, {0x4F}}, {"INCB", 2, 1, {0x5C}}, {"SEX", 2, 1, {0x1D}},
        {"DAA", 2, 1, {0x19}}, {"ABX", 3, 1, {0x3A}}, {"ORCC #", 3, 2, {0x1A, 0x00}}, {"ANDCC #", 3, 2, {0x1C, 0xFF}},
        {"LDA #", 2, 2, {0x86, 0x01}}, {"ADDA #", 2, 2, {0x8B, 0x01}}, {"LDD #", 3, 3, {0xCC, 0x12, 0x34}},
        {"LDX #", 3, 3, {0x8E, 0x12, 0x34}}, {"LDU #", 3, 3, {0xCE, 0x12, 0x34}},
        {"LDY #", 4, 4, {0x10, 0x8E, 0x12, 0x34}}, {"LDS #", 4, 4, {0x10, 0xCE, 0x12, 0x34}},
        {"ADDD #", 4, 3, {0xC3, 0x00, 0x01}}, {"SUBD #", 4, 3, {0x83, 0x00, 0x01}}, {"CMPX #", 4, 3, {0x8C, 0x00, 0x01}},
        {"CMPD #", 5, 4, {0x10, 0x83, 0x00, 0x01}}, {"CMPY #", 5, 4, {0x10, 0x8C, 0x00, 0x01}},
        {"CMPS #", 5, 4, {0x11, 0x8C, 0x00, 0x01}},
        {"LDA <", 4, 2, {0x96, 0x20}}, {"STA <", 4, 2, {0x97, 0x20}}, {"LDD <", 5, 2, {0xDC, 0x20}},
        {"LDX <", 5, 2, {0x9E, 0x20}}, {"STX <", 5, 2, {0x9F, 0x20}}, {"LDY <", 6, 3, {0x10, 0x9E, 0x20}},
        {"ADDD <", 6, 2, {0xD3, 0x20}}, {"CMPX <", 6, 2, {0x9C, 0x20}}, {"CMPU <", 7, 3, {0x11, 0x93, 0x20}},
        {"INC <", 6, 2, {0x0C, 0x20}}, {"TST <", 6, 2, {0x0D, 0x20}}, {"CLR <", 6, 2, {0x0F, 0x20}},
        {"JMP <", 3, 2, {0x0E, 0x20}},
        {"LDA >", 5, 3, {0xB6, 0x40, 0x00}}, {"STA >", 5, 3, {0xB7, 0x40, 0x00}}, {"LDD >", 6, 3, {0xFC, 0x40, 0x00}},
        {"STD >", 6, 3, {0xFD, 0x40, 0x00}}, {"STY >", 7, 4, {0x10, 0xBF, 0x40, 0x00}},
        {"SUBD >", 7, 3, {0xB3, 0x40, 0x00}}, {"CMPS >", 8, 4, {0x11, 0xBC, 0x40, 0x00}},
        {"DEC >", 7, 3, {0x7A, 0x40, 0x00}}, {"TST >", 7, 3, {0x7D, 0x40, 0x00}}, {"CLR >", 7, 3, {0x7F, 0x40, 0x00}},
        {"JMP >", 4, 3, {0x7E, 0x40, 0x00}},
        {"BRA", 3, 2, {0x20, 0x10}}, {"BRN", 3, 2, {0x21, 0x10}}, {"BNE taken", 3, 2, {0x26, 0x10}},
        {"BEQ not taken", 3, 2, {0x27, 0x10}}, {"LBRA", 5, 3, {0x16, 0x01, 0x00}},
        {"LBRN", 5, 4, {0x10, 0x21, 0x01, 0x00}}, {"LBNE taken", 6, 4, {0x10, 0x26, 0x01, 0x00}},
        {"LBEQ not taken", 5, 4, {0x10, 0x27, 0x01, 0x00}},
    };
    cycle_table("cycles", cases, (int)(sizeof cases / sizeof cases[0]));
}

static void t_cycles_indexed(void)
{
    /* Table 2 (p.17): the mode's cycles added to the instruction's base -- LDA 4+, LEA 4+, LDD 5+, JMP 3+, CLR 6+ */
    static const timing cases[] = {
        {"LDA ,X", 4 + 0, 2, {0xA6, 0x84}}, {"LDA 5,X", 4 + 1, 2, {0xA6, 0x05}},
        {"LDA $10,X", 4 + 1, 3, {0xA6, 0x88, 0x10}}, {"LDA $0100,X", 4 + 4, 4, {0xA6, 0x89, 0x01, 0x00}},
        {"LDA A,X", 4 + 1, 2, {0xA6, 0x86}}, {"LDA B,X", 4 + 1, 2, {0xA6, 0x85}}, {"LDA D,X", 4 + 4, 2, {0xA6, 0x8B}},
        {"LDA ,X+", 4 + 2, 2, {0xA6, 0x80}}, {"LDA ,X++", 4 + 3, 2, {0xA6, 0x81}},
        {"LDA ,-X", 4 + 2, 2, {0xA6, 0x82}}, {"LDA ,--X", 4 + 3, 2, {0xA6, 0x83}},
        {"LDA $10,PCR", 4 + 1, 3, {0xA6, 0x8C, 0x10}}, {"LDA $0100,PCR", 4 + 5, 4, {0xA6, 0x8D, 0x01, 0x00}},
        {"LDA [,X]", 4 + 3, 2, {0xA6, 0x94}}, {"LDA [$10,X]", 4 + 4, 3, {0xA6, 0x98, 0x10}},
        {"LDA [$0100,X]", 4 + 7, 4, {0xA6, 0x99, 0x01, 0x00}}, {"LDA [A,X]", 4 + 4, 2, {0xA6, 0x96}},
        {"LDA [B,X]", 4 + 4, 2, {0xA6, 0x95}}, {"LDA [D,X]", 4 + 7, 2, {0xA6, 0x9B}},
        {"LDA [,X++]", 4 + 6, 2, {0xA6, 0x91}}, {"LDA [,--X]", 4 + 6, 2, {0xA6, 0x93}},
        {"LDA [$10,PCR]", 4 + 4, 3, {0xA6, 0x9C, 0x10}}, {"LDA [$0100,PCR]", 4 + 8, 4, {0xA6, 0x9D, 0x01, 0x00}},
        {"LDA [$4000]", 4 + 5, 4, {0xA6, 0x9F, 0x40, 0x00}},
        {"LEAX 1,X", 4 + 1, 2, {0x30, 0x01}}, {"LEAS ,X", 4 + 0, 2, {0x32, 0x84}},
        {"LDD ,Y", 5 + 0, 2, {0xEC, 0xA4}}, {"JMP ,X", 3 + 0, 2, {0x6E, 0x84}}, {"CLR ,U", 6 + 0, 2, {0x6F, 0xC4}},
        {"LDS ,X", 6 + 0, 3, {0x10, 0xEE, 0x84}},
    };
    cycle_table("cycles_indexed", cases, (int)(sizeof cases / sizeof cases[0]));
}

static void t_cycles_stack(void)
{
    /* Table 9 and Figure 19's note 4 (p.32): "5 cycles plus 1 cycle for each byte pushed or pulled" */
    static const timing cases[] = {
        {"PSHS (none)", 5, 2, {0x34, 0x00}}, {"PSHS A,B", 5 + 2, 2, {0x34, 0x06}}, {"PSHS X", 5 + 2, 2, {0x34, 0x10}},
        {"PSHS all", 5 + 12, 2, {0x34, 0xFF}}, {"PULS A,B", 5 + 2, 2, {0x35, 0x06}},
        {"PULS all", 5 + 12, 2, {0x35, 0xFF}}, {"PSHU Y", 5 + 2, 2, {0x36, 0x20}}, {"PULU CC", 5 + 1, 2, {0x37, 0x01}},
        {"JSR <", 7, 2, {0x9D, 0x20}}, {"JSR >", 8, 3, {0xBD, 0x40, 0x00}}, {"JSR ,X", 7 + 0, 2, {0xAD, 0x84}},
        {"BSR", 7, 2, {0x8D, 0x10}}, {"LBSR", 9, 3, {0x17, 0x01, 0x00}}, {"RTS", 5, 1, {0x39}},
        {"SWI", 19, 1, {0x3F}}, {"SWI2", 20, 2, {0x10, 0x3F}}, {"SWI3", 20, 2, {0x11, 0x3F}},
        {"MUL", 11, 1, {0x3D}}, {"EXG X,Y", 8, 2, {0x1E, 0x12}}, {"TFR X,Y", 6, 2, {0x1F, 0x12}},
    };
    cycle_table("cycles_stack", cases, (int)(sizeof cases / sizeof cases[0]));
}

static long reads_of(machine *m, int addr)
{
    long n = 0;
    int k;
    for (k = 0; k < m->n_log; k++)
        if (m->log[k].addr == addr && m->log[k].kind == 'r')
            n++;
    return n;
}

static void t_bus_cycles(void)
{
    /* the data sheet's Example 2 (p.20): DEC $A000 at 8000h, [A000] = 80h.  Cycles 1 2 3 5 7 are its accesses;
       cycles 4 and 6 are don't-care cycles, reads at an address upstream chooses */
    machine *m = make(0x8000), *s, *c;
    access *l;
    int t, dec, sta_reads, clr_reads, clr_wrote;
    op(m, 3, 0x7A, 0xA0, 0x00);
    m->mem[0xA000] = 0x80;
    m->logging = 1;
    t = m6809_step(m->cpu);
    l = m->log;
    dec = t == 7 && m->n_log == 7 && l[0].kind == 'f' && l[0].addr == 0x8000 && l[0].data == 0x7A
        && l[1].kind == 'r' && l[1].addr == 0x8001 && l[1].data == 0xA0 && l[2].kind == 'r' && l[2].addr == 0x8002
        && l[3].kind == 'r' && l[4].kind == 'r' && l[4].addr == 0xA000 && l[4].data == 0x80 && l[5].kind == 'r'
        && l[6].kind == 'w' && l[6].addr == 0xA000 && l[6].data == 0x7F && m->fetches == 1;
    done(m);
    s = make(0x0200);
    op(s, 3, 0xB7, 0x40, 0x00);                /* STA $4000 */
    s->logging = 1;
    steps(s, 1);
    sta_reads = (int)reads_of(s, 0x4000);
    done(s);
    c = make(0x0200);
    op(c, 3, 0x7F, 0x40, 0x00);                /* CLR $4000: the chip reads the byte first (Figure 18 sheet 5) */
    c->logging = 1;
    steps(c, 1);
    clr_reads = (int)reads_of(c, 0x4000);
    clr_wrote = c->n_log == 7 && c->log[6].kind == 'w' && c->log[6].addr == 0x4000 && c->log[6].data == 0;
    check("bus_cycles", dec && sta_reads == 0 && clr_reads == 1 && clr_wrote,
          "DEC $A000: %d cycles, %s; STA reads its address %d times, CLR %d", t, dec ? "as Example 2" : "NOT Example 2",
          sta_reads, clr_reads);
    done(c);
}

int main(void)
{
    t_zero_budget();
    t_reset();
    t_two_cores();
    t_boundary();
    t_call_return();
    t_regs_set_wait();
    t_page_prefix();
    t_irq_entry();
    t_firq_entry();
    t_rti();
    t_masks();
    t_priority();
    t_irq_level();
    t_nmi_edge();
    t_nmi_armed();
    t_boundary_irq();
    t_sync();
    t_cwai();
    t_cwai_firq();
    t_wait_nmi();
    t_wait_boundary();
    t_undefined();
    t_free_run();
    t_prefix_run();
    t_every_opcode();
    t_cycles();
    t_cycles_indexed();
    t_cycles_stack();
    t_bus_cycles();
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
