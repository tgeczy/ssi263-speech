/* test_z80_contract.c -- CONTRACT.md 15's behaviour of the cpu.h Z80 core (z80_mame.cpp over MAME's Z80).
 *
 * Each test is a few instructions in a flat 64K, hand-assembled here and driven through cpu.h only, and checks one
 * clause.  T-states are Zilog's: the Z80 CPU User Manual UM008011-0816 -- each instruction's own description ("M
 * Cycles, T States") in its Z80 Instruction Description chapter; "Interrupt Response" (CPU Response: modes 0, 1 and
 * 2), Figure 9 (the acknowledge cycle's two wait states), Figure 10 (the nonmaskable interrupt), Figure 11 and "HALT
 * Exit", Table 1 (the two enable flip-flops) for the interrupts.
 *   zero_budget     z80_run(0) returns 0 and changes nothing, no bus access
 *   reset           PC 0, I and R 0, IFF1 and IFF2 0, mode 0, out of HALT, the counts zero; the other registers
 *                   kept; a latched NMI edge dropped and a held NMI line no new edge; a held INT sampled again; the
 *                   power-on values upstream's (IX = IY = FFFFh, F = 40h)
 *   two_cores       two instances interleaved step by step behave as each alone
 *   boundary        the boundary callback once per step, with that step's PC, before its instruction's writes
 *   call_return     a host's call: z80_regs_set (PC, SP, its return address on the stack), steps until
 *                   z80_next_pc() is that address; the first step is the entry point's
 *   regs_set        z80_regs_set and z80_regs_get agree on every register (F and R's bit 7 too); it leaves HALT, the
 *                   EI delay and an instruction the step limit cut
 *   prefix          a prefixed instruction is one step and one boundary (LD IX,nn 14, LD (IX+d),n 19, RLC (IX+d)
 *                   23, NEG 8, SLA B 8); every M1 byte comes through bus.fetch, the DD CB form's last byte does
 *                   not; R counts the M1 cycles
 *   int_im1         mode 1: PC pushed, PC = 0038h, IFF1 = IFF2 = 0, 13 T, R counted, one acknowledge (byte 0);
 *                   z80_pc() during the stacking is the interrupted address; z80_cycles() in a callback is the
 *                   step's start during A and start + acceptance during E
 *   int_im2         mode 2: the routine's address read at I * 256 + the acknowledge byte, 19 T; an odd byte is
 *                   used as it is (upstream)
 *   int_im0         mode 0 (upstream's five): RST 13 T, CALL 19 T (acknowledge bytes 0, 1, 2), JP 12 T, EI 6 T,
 *                   NOP 2 T (upstream; the manual's rule gives 6); another byte does nothing; no answer is FFh,
 *                   RST 38h; z80_run returns with EI on the bus
 *   int_masked      INT is not taken while IFF1 is 0
 *   int_level       INT is a level: dropped before its acceptance, not taken
 *   ei_delay        the instruction after EI runs before a waiting INT is taken; EI then DI takes none
 *   ei_nmi          an NMI at EI's boundary is taken before the next instruction (upstream: only INT waits)
 *   nmi_edge        NMI: taken with IFF1 0, PC pushed, PC = 0066h, IFF1 = 0 and IFF2 kept, 11 T; a held line gives
 *                   ONE; released and asserted again, another; RETN (14 T) puts IFF2 back into IFF1
 *   nmi_first       an edge before the first step is latched (changed from upstream)
 *   priority        NMI before INT; the INT is taken once RETN has restored IFF1
 *   halt            HALT 4 T, then 4-T slots with z80_pc() on the HALT, each a fetch at the address after it, R
 *                   counted; an INT with IFF1 set ends it, the address after the HALT pushed; z80_pc() at that
 *                   acceptance is that address
 *   halt_masked     with IFF1 0 an INT leaves the core halted; an NMI ends it
 *   boundary_irq    a line raised at a step's boundary is taken at the NEXT step's acceptance, from a HALT slot too
 *   block           LDIR is one repetition a step (21, 21, 16 T), z80_pc() on it; an INT between repetitions pushes
 *                   the LDIR's own address and the copy is finished after the handler
 *   prefix_run      a run of DDh longer than a step's limit: the step ends at 64 T, no interrupt is taken inside
 *                   the instruction, the next step finishes it with the saved PC unchanged; in a memory of nothing
 *                   but DDh, z80_run still returns
 *   io              the port's 16 bits: A above n for OUT (n),A and IN A,(n) (11 T), BC for OUT (C),r and IN r,(C)
 *                   (12 T)
 *   r_register      R counts M1 cycles in 7 bits; bit 7 is what LD R,A put there
 *   undocumented    upstream's, pinned as they are: SLL, LD IXH,n, a DDh before NOP (8 T, one step), an undefined
 *                   EDh opcode (8 T, nothing), OUT (C),0 writing 00h
 *   every_opcode    every first byte, alone and after CBh, DDh, EDh, FDh, DD CB d and FD CB d, with four operand
 *                   patterns: each step returns having spent 4 to 64 T-states, and none faults
 *   cycles          Zilog's T states for 116 instruction forms, conditions met and not met
 * The bus's serial callbacks and intercept are NULL in every test.
 *
 *   build: gcc -c test_z80_contract.c; g++ ... z80_mame.cpp   (../blazie/build_board.py does it)
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1.
 * z80_controls.py undoes each rule in a copy of the core: exactly its tests must then fail.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

/* ---- a machine: 64K of RAM full of NOPs, a recorder at each boundary and on the bus ------------------------------ */
typedef struct { int step, line, level; } action;
typedef struct { uint16_t addr; uint8_t data; char kind; } access;   /* 'f'etch, 'r'ead, 'w'rite, 'i'n, 'o'ut */

typedef struct {
    uint8_t mem[65536];
    int pos;                                   /* the assembler's cursor */
    uint32_t pcs[512];
    int n_pcs;
    long fetches, reads, writes;
    long writes_seen_at_boundary;
    uint32_t pc_at_write[8];                   /* z80_pc() and z80_cycles() as each of the first writes saw them */
    uint64_t cycles_at_write[8];
    access log[64];                            /* the bus, while logging */
    int n_log, logging;
    action acts[4];                            /* set a line at a step's boundary */
    int n_acts;
    int ack[3];                                /* the acknowledge cycle's bytes, -1: no answer */
    int ack_idx[8], n_ack;                     /* the indices asked for, in order */
    uint32_t pc_at_ack;                        /* z80_pc() as the acknowledge saw it */
    z80 *cpu;
} machine;

enum { V_IM1 = 0x0038, V_NMI = 0x0066 };

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
    if (m->writes < 8) {
        m->pc_at_write[m->writes] = z80_pc(m->cpu);
        m->cycles_at_write[m->writes] = z80_cycles(m->cpu);
    }
    m->writes++;
    note(m, a, v, 'w');
}

static uint8_t pin(void *ctx, uint16_t p)
{
    note((machine *)ctx, p, 0x5A, 'i');
    return 0x5A;
}

static void pout(void *ctx, uint16_t p, uint8_t v) { note((machine *)ctx, p, v, 'o'); }

static int ack(void *ctx, int line, int n)
{
    machine *m = (machine *)ctx;
    (void)line;
    if (m->n_ack < 8)
        m->ack_idx[m->n_ack] = n;
    m->n_ack++;
    m->pc_at_ack = z80_pc(m->cpu);
    return n < 3 ? m->ack[n] : -1;
}

static void bnd(void *ctx, uint32_t pc)
{
    machine *m = (machine *)ctx;
    int k;
    for (k = 0; k < m->n_acts; k++)
        if (m->acts[k].step == m->n_pcs)
            z80_set_irq(m->cpu, m->acts[k].line, m->acts[k].level);
    if (m->n_pcs < 512)
        m->pcs[m->n_pcs] = pc;
    m->n_pcs++;
    m->writes_seen_at_boundary = m->writes;
}

/* a new core starts at 0000h: the program is assembled there */
static machine *make(void)
{
    cpu_bus bus;
    machine *m = (machine *)calloc(1, sizeof *m);
    memset(&bus, 0, sizeof bus);
    bus.ctx = m;
    bus.read = rd;
    bus.fetch = fe;
    bus.write = wr;
    bus.in = pin;
    bus.out = pout;
    bus.irq_ack = ack;
    bus.boundary = bnd;
    m->ack[0] = m->ack[1] = m->ack[2] = -1;
    m->cpu = z80_create(&bus, 4000000.0);
    return m;
}

static void op(machine *m, int n, ...)
{
    va_list ap;
    int k;
    va_start(ap, n);
    for (k = 0; k < n; k++)
        m->mem[m->pos++ & 0xFFFF] = (uint8_t)va_arg(ap, int);
    va_end(ap);
}

static void at(machine *m, int where) { m->pos = where; }

static void done(machine *m)
{
    z80_destroy(m->cpu);
    free(m);
}

static void regs(machine *m, z80_regs *r) { z80_regs_get(m->cpu, r); }

static int steps(machine *m, int n)
{
    int t = 0;
    while (n-- > 0)
        t += z80_step(m->cpu);
    return t;
}

static int word(machine *m, int a) { return m->mem[a & 0xFFFF] | m->mem[(a + 1) & 0xFFFF] << 8; }

static void act(machine *m, int step, int line, int level)
{
    m->acts[m->n_acts].step = step;
    m->acts[m->n_acts].line = line;
    m->acts[m->n_acts].level = level;
    m->n_acts++;
}

static void ld_sp(machine *m) { op(m, 3, 0x31, 0x00, 0x80); }   /* LD SP,8000h */

/* ---- the tests ------------------------------------------------------------------------------------------------ */

static void t_zero_budget(void)
{
    machine *m = make();
    z80_regs a, b;
    long bus;
    uint64_t n;
    regs(m, &a);
    bus = m->fetches + m->reads + m->writes;
    n = z80_run(m->cpu, 0);
    regs(m, &b);
    check("zero_budget", n == 0 && a.pc == b.pc && z80_steps(m->cpu) == 0 && z80_cycles(m->cpu) == 0
          && m->fetches + m->reads + m->writes == bus, "run(0) = %llu, steps %llu, bus accesses %ld",
          (unsigned long long)n, (unsigned long long)z80_steps(m->cpu), m->fetches + m->reads + m->writes - bus);
    done(m);
}

static void t_reset(void)
{
    machine *m = make();
    z80_regs r0, rh, r1;
    int ok;
    regs(m, &r0);
    ok = r0.pc == 0 && r0.i == 0 && r0.r == 0 && !r0.iff1 && !r0.iff2 && r0.im == 0 && !r0.halted
        && r0.ix == 0xFFFF && r0.iy == 0xFFFF && (r0.af & 0xFF) == 0x40 && z80_cycles(m->cpu) == 0
        && z80_steps(m->cpu) == 0;
    ld_sp(m);                                  /* 0000 */
    op(m, 2, 0x3E, 0x55);                      /* 0003 LD A,55h */
    op(m, 2, 0xED, 0x47);                      /* 0005 LD I,A */
    op(m, 2, 0xED, 0x56);                      /* 0007 IM 1 */
    op(m, 1, 0xFB);                            /* 0009 EI */
    op(m, 3, 0x01, 0x34, 0x12);                /* 000A LD BC,1234h */
    op(m, 1, 0x76);                            /* 000D HALT */
    steps(m, 7);
    regs(m, &rh);
    z80_set_irq(m->cpu, Z80_NMI, 1);           /* latched; then held through the reset */
    z80_set_irq(m->cpu, Z80_INT, 1);           /* held through the reset */
    z80_reset(m->cpu);
    regs(m, &r1);
    ok = ok && rh.halted && rh.im == 1 && rh.iff1 && rh.i == 0x55 && r1.pc == 0 && r1.i == 0 && r1.r == 0 && !r1.iff1
        && !r1.iff2 && r1.im == 0 && !r1.halted && (r1.af >> 8) == 0x55 && r1.bc == 0x1234 && r1.sp == 0x8000
        && z80_cycles(m->cpu) == 0 && z80_steps(m->cpu) == 0;
    m->n_pcs = 0;
    steps(m, 7);                               /* no NMI at the first step; LD SP ... EI, LD BC, then the held INT */
    check("reset", ok && m->pcs[0] == 0x0000 && m->pcs[1] == 0x0003 && m->pcs[5] == 0x000A && m->pcs[6] == V_IM1,
          "pc %04X i %02X r %02X im %d ix %04X f %02X; again: pc %04X i %02X im %d iff1 %d halted %d a %02X bc %04X; "
          "then %04X %04X .. %04X %04X", r0.pc, r0.i, r0.r, r0.im, r0.ix, r0.af & 0xFF, r1.pc, r1.i, r1.im, r1.iff1,
          r1.halted, r1.af >> 8, r1.bc, (unsigned)m->pcs[0], (unsigned)m->pcs[1], (unsigned)m->pcs[5],
          (unsigned)m->pcs[6]);
    done(m);
}

static void prog_count(machine *m, int down)
{
    op(m, 3, 0x21, 0x00, 0x10);                /* 0000 LD HL,1000h */
    op(m, 1, down ? 0x2B : 0x23);              /* 0003 loop: INC HL (or DEC HL) */
    op(m, 3, 0x22, 0x00, 0x40);                /* 0004 LD (4000h),HL */
    op(m, 1, 0x7D);                            /* 0007 LD A,L */
    op(m, 2, 0xC6, 0x03);                      /* 0008 ADD A,3 */
    op(m, 2, 0x18, 0xF7);                      /* 000A JR loop */
}

static void t_two_cores(void)
{
    machine *a = make(), *b = make(), *c = make();
    z80_regs ra, rb, rc;
    int k;
    prog_count(a, 1);
    prog_count(b, 0);
    prog_count(c, 0);
    for (k = 0; k < 100; k++) {                /* a and b interleaved; c alone */
        z80_step(a->cpu);
        z80_step(b->cpu);
        z80_step(b->cpu);
    }
    steps(c, 200);
    regs(a, &ra);
    regs(b, &rb);
    regs(c, &rc);
    check("two_cores", rb.hl == rc.hl && rb.af == rc.af && rb.pc == rc.pc && word(b, 0x4000) == word(c, 0x4000)
          && ra.hl != rb.hl && z80_cycles(b->cpu) == z80_cycles(c->cpu),
          "b: hl %04X af %04X; alone: hl %04X af %04X; a: hl %04X", rb.hl, rb.af, rc.hl, rc.af, ra.hl);
    done(a);
    done(b);
    done(c);
}

static void t_boundary(void)
{
    machine *m = make();
    op(m, 2, 0x3E, 0x55);                      /* 0000 LD A,55h */
    op(m, 3, 0x32, 0x00, 0x40);                /* 0002 LD (4000h),A */
    steps(m, 2);
    check("boundary", m->n_pcs == 2 && m->pcs[0] == 0x0000 && m->pcs[1] == 0x0002 && m->writes_seen_at_boundary == 0
          && m->writes == 1 && m->pc_at_write[0] == 0x0002 && z80_steps(m->cpu) == 2,
          "pcs %d (%04X %04X), writes before the store's boundary %ld, z80_pc() at the write %04X", m->n_pcs,
          (unsigned)m->pcs[0], (unsigned)m->pcs[1], m->writes_seen_at_boundary, (unsigned)m->pc_at_write[0]);
    done(m);
}

static void t_call_return(void)
{
    machine *m = make();
    z80_regs r;
    int n = 0, t = 0;
    at(m, 0x5800);
    op(m, 2, 0x3E, 0x42);                      /* 5800 LD A,42h */
    op(m, 3, 0xCD, 0x10, 0x58);                /* 5802 CALL 5810h */
    op(m, 1, 0xC9);                            /* 5805 RET */
    at(m, 0x5810);
    op(m, 2, 0x06, 0x07);                      /* 5810 LD B,7 */
    op(m, 1, 0xC9);                            /* 5812 RET */
    regs(m, &r);
    r.pc = 0x5800;
    r.sp = 0x7FFE;                             /* the host's return address on the stack: 0100h */
    m->mem[0x7FFE] = 0x00;
    m->mem[0x7FFF] = 0x01;
    z80_regs_set(m->cpu, &r);
    while (z80_next_pc(m->cpu) != 0x0100 && n < 100) {
        t += z80_step(m->cpu);
        n++;
    }
    regs(m, &r);
    check("call_return", n == 5 && t == 7 + 17 + 7 + 10 + 10 && (r.af >> 8) == 0x42 && (r.bc >> 8) == 7
          && r.sp == 0x8000 && r.pc == 0x0100 && m->pcs[0] == 0x5800 && m->pcs[2] == 0x5810,
          "%d steps, %d T (LD 7, CALL 17, LD 7, RET 10, RET 10), a %02X b %02X sp %04X, first step at %04X", n, t,
          r.af >> 8, r.bc >> 8, r.sp, (unsigned)m->pcs[0]);
    done(m);
}

static void t_regs_set(void)
{
    static const z80_regs want = {0x12D7, 0x3456, 0x789A, 0xBCDE, 0x0F28, 0x1357, 0x2468, 0x9BDF, 0x4321, 0x8765,
                                  0x7000, 0x2000, 0x3C, 0x85, 2, 1, 0, 0, 0};
    machine *m = make(), *e = make(), *p = make();
    z80_regs r, h, g;
    int same, k, t;
    op(m, 1, 0x76);                            /* 0000 HALT */
    steps(m, 2);                               /* the HALT, a slot */
    regs(m, &h);
    z80_regs_set(m->cpu, &want);
    regs(m, &g);
    same = g.af == want.af && g.bc == want.bc && g.de == want.de && g.hl == want.hl && g.af2 == want.af2
        && g.bc2 == want.bc2 && g.de2 == want.de2 && g.hl2 == want.hl2 && g.ix == want.ix && g.iy == want.iy
        && g.sp == want.sp && g.pc == want.pc && g.i == want.i && g.r == want.r && g.im == want.im
        && g.iff1 == want.iff1 && g.iff2 == want.iff2;
    m->mem[0x2000] = 0xF5;                     /* 2000 PUSH AF: the flags as the host set them */
    steps(m, 1);
    /* the EI delay: an INT held, EI, then the host's registers: taken at the very next step (mode 0, FFh: RST 38h) */
    op(e, 1, 0xFB);                            /* 0000 EI */
    z80_set_irq(e->cpu, Z80_INT, 1);
    steps(e, 1);
    regs(e, &r);
    r.pc = 0x2000;
    r.sp = 0x8000;
    z80_regs_set(e->cpu, &r);
    steps(e, 1);
    /* an instruction the step limit cut: the next step is the host's, from its first byte */
    for (k = 0; k < 40; k++)
        op(p, 1, 0xDD);
    at(p, 0x2000);
    op(p, 2, 0x3E, 0x77);                      /* 2000 LD A,77h */
    t = steps(p, 1);
    regs(p, &r);
    r.pc = 0x2000;
    z80_regs_set(p->cpu, &r);
    t = t * 100 + steps(p, 1);
    regs(p, &r);
    check("regs_set", h.halted && !g.halted && same && m->pcs[2] == 0x2000 && word(m, 0x6FFE) == 0x12D7
          && e->pcs[1] == V_IM1 && word(e, 0x7FFE) == 0x2000 && t == 6407 && p->pcs[1] == 0x2000
          && (r.af >> 8) == 0x77 && r.pc == 0x2002,
          "halted %d then %d; read back %s; PUSH AF wrote %04X; after EI the INT at %04X, %04X pushed; a cut "
          "instruction left: %d then %d T, the step at %04X, a %02X", h.halted, g.halted,
          same ? "the same" : "DIFFERENT", word(m, 0x6FFE), (unsigned)e->pcs[1], word(e, 0x7FFE), t / 100, t % 100,
          (unsigned)p->pcs[1], r.af >> 8);
    done(m);
    done(e);
    done(p);
}

static void t_prefix(void)
{
    machine *m = make();
    z80_regs r;
    int t[5], k, m1 = 1;
    static const char kinds[] = "ffrrrw";      /* RLC (IX+5): DD and CB by M1; d, the opcode, the byte; the store */
    static const uint16_t addrs[] = {0x0008, 0x0009, 0x000A, 0x000B, 0x1239, 0x1239};
    op(m, 4, 0xDD, 0x21, 0x34, 0x12);          /* 0000 LD IX,1234h */
    op(m, 4, 0xDD, 0x36, 0x05, 0x40);          /* 0004 LD (IX+5),40h */
    op(m, 4, 0xDD, 0xCB, 0x05, 0x06);          /* 0008 RLC (IX+5) */
    op(m, 2, 0xED, 0x44);                      /* 000C NEG */
    op(m, 2, 0xCB, 0x20);                      /* 000E SLA B */
    t[0] = steps(m, 1);
    t[1] = steps(m, 1);
    m->logging = 1;
    t[2] = steps(m, 1);
    m->logging = 0;
    t[3] = steps(m, 1);
    t[4] = steps(m, 1);
    regs(m, &r);
    for (k = 0; k < 6; k++)
        m1 = m1 && m->n_log == 6 && m->log[k].kind == kinds[k] && m->log[k].addr == addrs[k];
    check("prefix", t[0] == 14 && t[1] == 19 && t[2] == 23 && t[3] == 8 && t[4] == 8 && r.ix == 0x1234
          && m->mem[0x1239] == 0x80 && m->n_pcs == 5 && m->pcs[1] == 0x0004 && m->pcs[2] == 0x0008
          && m->pcs[3] == 0x000C && m->pcs[4] == 0x000E && m->fetches == 10 && m1 && r.r == 10,
          "LD IX,nn %d, LD (IX+d),n %d, RLC (IX+d) %d, NEG %d, SLA B %d T; %d steps; %ld M1 fetches, R %d; RLC's bus "
          "%s", t[0], t[1], t[2], t[3], t[4], m->n_pcs, m->fetches, r.r, m1 ? "f f r r r w" : "NOT f f r r r w");
    done(m);
}

/* LD SP; a mode; EI; three NOPs: an INT held from the start is taken after the first NOP, 0007h pushed */
static void prog_int(machine *m, int im_opcode)
{
    ld_sp(m);                                  /* 0000 */
    op(m, 2, 0xED, im_opcode);                 /* 0003 IM n */
    op(m, 1, 0xFB);                            /* 0005 EI */
    op(m, 3, 0x00, 0x00, 0x00);                /* 0006 NOP, NOP, NOP */
    z80_set_irq(m->cpu, Z80_INT, 1);
}

static void t_int_im1(void)
{
    machine *m = make();
    z80_regs r0, r;
    uint64_t start;
    int t;
    prog_int(m, 0x56);                         /* IM 1 */
    at(m, V_IM1);
    op(m, 3, 0x32, 0x00, 0x40);                /* 0038 LD (4000h),A */
    steps(m, 4);                               /* LD SP, IM 1, EI, the NOP after it */
    regs(m, &r0);
    start = z80_cycles(m->cpu);
    t = steps(m, 1);
    regs(m, &r);
    check("int_im1", t == 13 + 13 && m->pcs[4] == V_IM1 && word(m, 0x7FFE) == 0x0007 && r.sp == 0x7FFE && !r.iff1
          && !r.iff2 && m->n_ack == 1 && m->ack_idx[0] == 0 && m->pc_at_ack == 0x0007 && m->pc_at_write[0] == 0x0007
          && m->cycles_at_write[0] == start && m->cycles_at_write[2] == start + 13 && m->pc_at_write[2] == V_IM1
          && ((r.r - r0.r) & 0x7F) == 2,
          "%d T (13 and the handler's 13), at %04X, %04X pushed, iff %d %d, %d acknowledge byte(s); z80_pc() at the "
          "stacking %04X; z80_cycles() there +%d, at the handler's write +%d; R +%d", t, (unsigned)m->pcs[4],
          word(m, 0x7FFE), r.iff1, r.iff2, m->n_ack, (unsigned)m->pc_at_write[0],
          (int)(m->cycles_at_write[0] - start), (int)(m->cycles_at_write[2] - start), (r.r - r0.r) & 0x7F);
    done(m);
}

static void t_int_im2(void)
{
    int odd, ok = 1, t[2];
    uint32_t to[2];
    for (odd = 0; odd < 2; odd++) {
        machine *m = make();
        ld_sp(m);                              /* 0000 */
        op(m, 2, 0x3E, 0x20);                  /* 0003 LD A,20h */
        op(m, 2, 0xED, 0x47);                  /* 0005 LD I,A */
        op(m, 2, 0xED, 0x5E);                  /* 0007 IM 2 */
        op(m, 1, 0xFB);                        /* 0009 EI */
        op(m, 2, 0x00, 0x00);                  /* 000A NOP, NOP */
        m->ack[0] = 0x40 + odd;                /* the table's entry at 2040h (2041h) */
        m->mem[0x2040] = 0x00;
        m->mem[0x2041] = 0x30;                 /* 3000h; from 2041h: 3130h */
        m->mem[0x2042] = 0x31;
        z80_set_irq(m->cpu, Z80_INT, 1);
        steps(m, 6);
        t[odd] = steps(m, 1);
        to[odd] = m->pcs[6];
        ok = ok && word(m, 0x7FFE) == 0x000B && m->n_ack == 1 && m->ack_idx[0] == 0;
        done(m);
    }
    check("int_im2", ok && t[0] == 19 + 4 && to[0] == 0x3000 && t[1] == 19 + 4 && to[1] == 0x3130,
          "byte 40h: %d T (19 and a NOP), to %04X; byte 41h: %d T, to %04X", t[0], (unsigned)to[0], t[1],
          (unsigned)to[1]);
}

/* mode 0 with these bytes on the bus at the acknowledge: the step's T-states less the 4 of the NOP it runs after */
static int im0(int b0, int b1, int b2, z80_regs *r, uint32_t *to, int *pushed, int *asked)
{
    machine *m = make();
    int t;
    ld_sp(m);                                  /* 0000 */
    op(m, 1, 0xFB);                            /* 0003 EI */
    op(m, 3, 0x00, 0x00, 0x00);                /* 0004 NOP, NOP, NOP: the INT is taken with 0005h next */
    m->ack[0] = b0;
    m->ack[1] = b1;
    m->ack[2] = b2;
    z80_set_irq(m->cpu, Z80_INT, 1);
    steps(m, 3);
    t = steps(m, 1);
    regs(m, r);
    *to = m->pcs[3];
    *pushed = r->sp == 0x7FFE ? word(m, 0x7FFE) : -1;
    *asked = m->n_ack;
    done(m);
    return t - 4;
}

static void t_int_im0(void)
{
    z80_regs r;
    uint32_t to;
    int pushed, asked, ok, t_rst, t_ff, t_call, t_jp, t_ei, t_nop, t_other;
    machine *m;
    uint64_t ran;
    t_rst = im0(0xEF, -1, -1, &r, &to, &pushed, &asked);                 /* RST 28h */
    ok = t_rst == 13 && to == 0x0028 && pushed == 0x0005 && asked == 1 && !r.iff1 && !r.iff2;
    t_ff = im0(-1, -1, -1, &r, &to, &pushed, &asked);                    /* nothing drives the bus: FFh, RST 38h */
    ok = ok && t_ff == 13 && to == 0x0038 && pushed == 0x0005;
    t_call = im0(0xCD, 0x00, 0x30, &r, &to, &pushed, &asked);            /* CALL 3000h */
    ok = ok && t_call == 19 && to == 0x3000 && pushed == 0x0005 && asked == 3;
    t_jp = im0(0xC3, 0x00, 0x30, &r, &to, &pushed, &asked);              /* JP 3000h */
    ok = ok && t_jp == 12 && to == 0x3000 && pushed == -1 && asked == 3;
    t_ei = im0(0xFB, -1, -1, &r, &to, &pushed, &asked);                  /* EI */
    ok = ok && t_ei == 6 && to == 0x0005 && pushed == -1 && r.iff1 && r.iff2;
    t_nop = im0(0x00, -1, -1, &r, &to, &pushed, &asked);                 /* NOP */
    ok = ok && t_nop == 2 && to == 0x0005 && pushed == -1 && !r.iff1;
    t_other = im0(0x3C, -1, -1, &r, &to, &pushed, &asked);               /* INC A: not one of upstream's five */
    ok = ok && t_other == 2 && to == 0x0005 && pushed == -1 && (r.af >> 8) == 0 && !r.iff1 && asked == 1;
    /* EI on the bus at every acknowledge: each step an acceptance and one instruction, and z80_run returns */
    m = make();
    op(m, 1, 0xFB);                            /* 0000 EI */
    m->ack[0] = 0xFB;
    z80_set_irq(m->cpu, Z80_INT, 1);
    ran = z80_run(m->cpu, 1000);
    regs(m, &r);
    check("int_im0", ok && ran >= 1000 && ran < 1000 + 10 && r.pc > 0x0040,
          "RST %d T, FFh %d, CALL %d, JP %d, EI %d, NOP %d, another byte %d (nothing done); EI at every acknowledge: "
          "run(1000) = %llu, pc %04X", t_rst, t_ff, t_call, t_jp, t_ei, t_nop, t_other, (unsigned long long)ran, r.pc);
    done(m);
}

static void t_int_masked(void)
{
    machine *m = make();
    z80_regs r;
    ld_sp(m);                                  /* 0000; then NOPs: IFF1 is 0 from reset */
    z80_set_irq(m->cpu, Z80_INT, 1);
    steps(m, 6);
    regs(m, &r);
    check("int_masked", m->pcs[5] == 0x0007 && r.sp == 0x8000 && m->n_ack == 0, "six steps, the last at %04X, sp %04X, "
          "%d acknowledge(s)", (unsigned)m->pcs[5], r.sp, m->n_ack);
    done(m);
}

static void t_int_level(void)
{
    machine *m = make();
    z80_regs r;
    prog_int(m, 0x56);                         /* IM 1; the INT held from the start */
    act(m, 3, Z80_INT, 0);                     /* dropped at the boundary of the NOP after EI */
    steps(m, 7);
    regs(m, &r);
    check("int_level", m->pcs[4] == 0x0007 && m->pcs[6] == 0x0009 && r.sp == 0x8000 && m->n_ack == 0 && r.iff1,
          "dropped before its acceptance: the next steps at %04X .. %04X, %d acknowledge(s)", (unsigned)m->pcs[4],
          (unsigned)m->pcs[6], m->n_ack);
    done(m);
}

static void t_ei_delay(void)
{
    machine *m = make(), *d = make();
    z80_regs r;
    prog_int(m, 0x56);
    steps(m, 5);
    ld_sp(d);                                  /* 0000 */
    op(d, 1, 0xFB);                            /* 0003 EI */
    op(d, 1, 0xF3);                            /* 0004 DI: runs before the INT can be taken, and masks it */
    z80_set_irq(d->cpu, Z80_INT, 1);
    steps(d, 6);
    regs(d, &r);
    check("ei_delay", m->pcs[3] == 0x0006 && m->pcs[4] == V_IM1 && word(m, 0x7FFE) == 0x0007 && d->pcs[2] == 0x0004
          && d->pcs[5] == 0x0007 && d->n_ack == 0 && r.sp == 0x8000,
          "after EI: %04X, then the INT (%04X, %04X pushed); EI, DI: %04X .. %04X, %d acknowledge(s)",
          (unsigned)m->pcs[3], (unsigned)m->pcs[4], word(m, 0x7FFE), (unsigned)d->pcs[2], (unsigned)d->pcs[5],
          d->n_ack);
    done(m);
    done(d);
}

static void t_ei_nmi(void)
{
    machine *m = make();
    ld_sp(m);                                  /* 0000 */
    op(m, 1, 0xFB);                            /* 0003 EI */
    op(m, 2, 0x00, 0x00);                      /* 0004 NOP, NOP */
    act(m, 1, Z80_NMI, 1);                     /* at EI's boundary */
    steps(m, 3);
    check("ei_nmi", m->pcs[1] == 0x0003 && m->pcs[2] == V_NMI && word(m, 0x7FFE) == 0x0004,
          "EI at %04X, then %04X with %04X pushed", (unsigned)m->pcs[1], (unsigned)m->pcs[2], word(m, 0x7FFE));
    done(m);
}

static void t_nmi_edge(void)
{
    machine *m = make();
    z80_regs r1, r2, r3;
    int t1, t2, t3;
    ld_sp(m);                                  /* 0000 */
    op(m, 1, 0xFB);                            /* 0003 EI */
    at(m, V_NMI);
    op(m, 1, 0x00);                            /* 0066 NOP */
    op(m, 2, 0xED, 0x45);                      /* 0067 RETN */
    steps(m, 1);
    z80_set_irq(m->cpu, Z80_NMI, 1);
    t1 = steps(m, 1);                          /* taken with IFF1 0: the acceptance and the handler's NOP */
    regs(m, &r1);
    t2 = steps(m, 1);                          /* RETN */
    steps(m, 2);                               /* EI, a NOP: the line is still held, no second NMI */
    z80_set_irq(m->cpu, Z80_NMI, 0);
    z80_set_irq(m->cpu, Z80_NMI, 1);
    t3 = steps(m, 1);                          /* a new edge: taken, IFF1 was 1 */
    regs(m, &r2);
    steps(m, 1);                               /* RETN: IFF1 back from IFF2 */
    regs(m, &r3);
    steps(m, 1);
    check("nmi_edge", t1 == 11 + 4 && m->pcs[1] == V_NMI && r1.sp == 0x7FFE && word(m, 0x7FFE) == 0x0005
          && !r1.iff1 && !r1.iff2 && t2 == 14 && m->pcs[3] == 0x0003 && m->pcs[4] == 0x0004 && t3 == 11 + 4
          && m->pcs[5] == V_NMI && !r2.iff1 && r2.iff2 && r3.iff1 && r3.iff2 && m->pcs[7] == 0x0005 && m->n_ack == 0,
          "%d T (11 and a NOP) to %04X, iff %d %d; RETN %d T; held: %04X %04X; a new edge: %d T to %04X, iff %d %d; "
          "after RETN iff %d %d, on at %04X", t1, (unsigned)m->pcs[1], r1.iff1, r1.iff2, t2, (unsigned)m->pcs[3],
          (unsigned)m->pcs[4], t3, (unsigned)m->pcs[5], r2.iff1, r2.iff2, r3.iff1, r3.iff2, (unsigned)m->pcs[7]);
    done(m);
}

static void t_nmi_first(void)
{
    machine *m = make();
    int t;
    z80_set_irq(m->cpu, Z80_NMI, 1);
    t = steps(m, 1);
    check("nmi_first", t == 11 + 4 && m->pcs[0] == V_NMI && word(m, 0xFFFE) == 0x0000,
          "an edge before the first step: %d T, at %04X", t, (unsigned)m->pcs[0]);
    done(m);
}

static void t_priority(void)
{
    machine *m = make();
    prog_int(m, 0x56);                         /* IM 1; the INT held from the start */
    at(m, V_NMI);
    op(m, 2, 0xED, 0x45);                      /* 0066 RETN */
    steps(m, 4);
    z80_set_irq(m->cpu, Z80_NMI, 1);           /* both wait now, IFF1 set */
    steps(m, 2);
    check("priority", m->pcs[4] == V_NMI && m->pcs[5] == V_IM1 && word(m, 0x7FFE) == 0x0007 && m->n_ack == 1,
          "both waiting: %04X first, then %04X (%04X pushed)", (unsigned)m->pcs[4], (unsigned)m->pcs[5],
          word(m, 0x7FFE));
    done(m);
}

static void t_halt(void)
{
    machine *m = make();
    z80_regs r0, r1, r2, r3;
    int t_halt, t_slot, t_wake, at7;
    ld_sp(m);                                  /* 0000 */
    op(m, 2, 0xED, 0x56);                      /* 0003 IM 1 */
    op(m, 1, 0xFB);                            /* 0005 EI */
    op(m, 1, 0x76);                            /* 0006 HALT */
    steps(m, 3);
    regs(m, &r0);
    t_halt = steps(m, 1);
    regs(m, &r1);
    m->logging = 1;
    t_slot = steps(m, 1);
    m->logging = 0;
    at7 = m->n_log == 1 && m->log[0].kind == 'f' && m->log[0].addr == 0x0007;
    steps(m, 1);
    regs(m, &r2);
    z80_set_irq(m->cpu, Z80_INT, 1);
    t_wake = steps(m, 1);
    regs(m, &r3);
    check("halt", t_halt == 4 && r1.halted && r1.pc == 0x0007 && t_slot == 4 && m->pcs[4] == 0x0006
          && m->pcs[5] == 0x0006 && at7 && ((r2.r - r1.r) & 0x7F) == 2 && r2.halted && t_wake == 13 + 4
          && m->pcs[6] == V_IM1 && !r3.halted && word(m, 0x7FFE) == 0x0007 && m->pc_at_ack == 0x0007,
          "HALT %d T, pc %04X; a slot %d T at %04X, its fetch %s, R +%d a slot; the INT: %d T to %04X, %04X pushed, "
          "z80_pc() at the acknowledge %04X", t_halt, r1.pc, t_slot, (unsigned)m->pcs[4],
          at7 ? "at 0007" : "NOT at 0007", ((r2.r - r1.r) & 0x7F) / 2, t_wake, (unsigned)m->pcs[6], word(m, 0x7FFE),
          (unsigned)m->pc_at_ack);
    done(m);
}

static void t_halt_masked(void)
{
    machine *m = make();
    z80_regs r1, r2;
    ld_sp(m);                                  /* 0000 */
    op(m, 1, 0x76);                            /* 0003 HALT, IFF1 0 */
    z80_set_irq(m->cpu, Z80_INT, 1);
    steps(m, 6);
    regs(m, &r1);
    z80_set_irq(m->cpu, Z80_NMI, 1);
    steps(m, 1);
    regs(m, &r2);
    check("halt_masked", r1.halted && m->pcs[5] == 0x0003 && m->n_ack == 0 && !r2.halted && m->pcs[6] == V_NMI
          && word(m, 0x7FFE) == 0x0004,
          "an INT with IFF1 0: halted %d, still at %04X; an NMI: halted %d, at %04X, %04X pushed", r1.halted,
          (unsigned)m->pcs[5], r2.halted, (unsigned)m->pcs[6], word(m, 0x7FFE));
    done(m);
}

static void t_boundary_irq(void)
{
    machine *m = make(), *h = make();
    prog_int(m, 0x56);
    z80_set_irq(m->cpu, Z80_INT, 0);           /* not held: raised at the second NOP's boundary */
    act(m, 4, Z80_INT, 1);
    steps(m, 6);
    ld_sp(h);                                  /* 0000 */
    op(h, 1, 0xFB);                            /* 0003 EI */
    op(h, 1, 0x76);                            /* 0004 HALT */
    act(h, 4, Z80_INT, 1);                     /* at the second slot's boundary */
    steps(h, 6);
    check("boundary_irq", m->pcs[4] == 0x0007 && m->pcs[5] == V_IM1 && word(m, 0x7FFE) == 0x0008
          && h->pcs[4] == 0x0004 && h->pcs[5] == V_IM1 && word(h, 0x7FFE) == 0x0005,
          "raised at %04X's boundary: taken at the next step (%04X, %04X pushed); at a HALT slot's (%04X): the next "
          "(%04X, %04X pushed)", (unsigned)m->pcs[4], (unsigned)m->pcs[5], word(m, 0x7FFE), (unsigned)h->pcs[4],
          (unsigned)h->pcs[5], word(h, 0x7FFE));
    done(m);
    done(h);
}

static void prog_ldir(machine *m)
{
    ld_sp(m);                                  /* 0000 */
    op(m, 3, 0x21, 0x00, 0x40);                /* 0003 LD HL,4000h */
    op(m, 3, 0x11, 0x00, 0x50);                /* 0006 LD DE,5000h */
    op(m, 3, 0x01, 0x03, 0x00);                /* 0009 LD BC,3 */
    op(m, 1, 0xFB);                            /* 000C EI */
    op(m, 2, 0xED, 0xB0);                      /* 000D LDIR */
    at(m, V_IM1);
    op(m, 1, 0xC9);                            /* 0038 RET */
    m->mem[0x4000] = 0x11;
    m->mem[0x4001] = 0x22;
    m->mem[0x4002] = 0x33;
}

static void t_block(void)
{
    machine *m = make(), *i = make();
    z80_regs r;
    int t1, t2, t3, copied;
    prog_ldir(m);
    steps(m, 5);
    t1 = steps(m, 1);
    t2 = steps(m, 1);
    t3 = steps(m, 1);
    steps(m, 1);
    regs(m, &r);
    copied = m->mem[0x5000] == 0x11 && m->mem[0x5001] == 0x22 && m->mem[0x5002] == 0x33;
    prog_ldir(i);
    act(i, 5, Z80_INT, 1);                     /* at the first repetition's boundary (mode 0, FFh: RST 38h) */
    steps(i, 10);                              /* ... the acceptance and RET, two repetitions, a NOP */
    check("block", t1 == 21 && t2 == 21 && t3 == 16 && m->pcs[5] == 0x000D && m->pcs[6] == 0x000D
          && m->pcs[7] == 0x000D && m->pcs[8] == 0x000F && r.bc == 0 && copied && i->pcs[6] == V_IM1
          && word(i, 0x7FFE) == 0x000D && i->pcs[7] == 0x000D && i->pcs[9] == 0x000F && i->mem[0x5002] == 0x33,
          "LDIR %d, %d, %d T at %04X %04X %04X, then %04X; an INT between: %04X pushed, back at %04X, the copy %s",
          t1, t2, t3, (unsigned)m->pcs[5], (unsigned)m->pcs[6], (unsigned)m->pcs[7], (unsigned)m->pcs[8],
          word(i, 0x7FFE), (unsigned)i->pcs[7], i->mem[0x5002] == 0x33 ? "finished" : "NOT finished");
    done(m);
    done(i);
}

static void t_prefix_run(void)
{
    machine *m = make(), *f = make();
    z80_regs r;
    int k, t1, t2;
    uint64_t ran;
    ld_sp(m);                                  /* 0000 */
    op(m, 1, 0xFB);                            /* 0003 EI */
    op(m, 1, 0x00);                            /* 0004 NOP */
    for (k = 0; k < 20; k++)
        op(m, 1, 0xDD);                        /* 0005 twenty prefixes: 19 of them 4 T each, and */
    op(m, 3, 0x21, 0x34, 0x12);                /* 0019 the last with its LD IX,1234h: 14 T */
    act(m, 3, Z80_INT, 1);                     /* at the run's first boundary: inside the instruction from then on */
    steps(m, 3);
    t1 = steps(m, 1);
    t2 = steps(m, 1);
    steps(m, 1);
    regs(m, &r);
    memset(f->mem, 0xDD, sizeof f->mem);
    ran = z80_run(f->cpu, 1000);
    check("prefix_run", t1 == 64 && t1 + t2 == 19 * 4 + 14 && m->pcs[3] == 0x0005 && m->pcs[4] == 0x0005
          && r.ix == 0x1234 && m->pcs[5] == V_IM1 && word(m, 0x7FFE) == 0x001C && ran >= 1000 && ran < 1000 + 64,
          "twenty DDh and LD IX,nn: %d T, then %d, both steps at %04X %04X; the INT after it (%04X pushed); nothing "
          "but DDh: run(1000) = %llu", t1, t2, (unsigned)m->pcs[3], (unsigned)m->pcs[4], word(m, 0x7FFE),
          (unsigned long long)ran);
    done(m);
    done(f);
}

static void t_io(void)
{
    static const char kinds[] = "ooii";
    static const uint16_t ports[] = {0x1278, 0x3456, 0x129A, 0x3456};
    static const uint8_t data[] = {0x12, 0x34, 0x5A, 0x5A};
    machine *m = make();
    z80_regs r;
    int t[4], k, seen = 0, ok = 1;
    op(m, 2, 0x3E, 0x12);                      /* 0000 LD A,12h */
    op(m, 3, 0x01, 0x56, 0x34);                /* 0002 LD BC,3456h */
    op(m, 2, 0xD3, 0x78);                      /* 0005 OUT (78h),A */
    op(m, 2, 0xED, 0x41);                      /* 0007 OUT (C),B */
    op(m, 2, 0xDB, 0x9A);                      /* 0009 IN A,(9Ah) */
    op(m, 2, 0xED, 0x50);                      /* 000B IN D,(C) */
    steps(m, 2);
    m->logging = 1;
    for (k = 0; k < 4; k++)
        t[k] = steps(m, 1);
    regs(m, &r);
    for (k = 0; k < m->n_log; k++)             /* the port accesses among the bus's */
        if (m->log[k].kind == 'i' || m->log[k].kind == 'o') {
            ok = ok && seen < 4 && m->log[k].kind == kinds[seen] && m->log[k].addr == ports[seen]
                && m->log[k].data == data[seen];
            seen++;
        }
    check("io", ok && seen == 4 && t[0] == 11 && t[1] == 12 && t[2] == 11 && t[3] == 12 && (r.af >> 8) == 0x5A
          && (r.de >> 8) == 0x5A, "OUT (n),A %d T, OUT (C),B %d, IN A,(n) %d, IN D,(C) %d; the ports %s", t[0], t[1],
          t[2], t[3], ok && seen == 4 ? "1278 3456 129A 3456" : "NOT 1278 3456 129A 3456");
    done(m);
}

static void t_r_register(void)
{
    machine *m = make();
    z80_regs r;
    op(m, 2, 0x3E, 0xFE);                      /* 0000 LD A,FEh */
    op(m, 2, 0xED, 0x4F);                      /* 0002 LD R,A: R = FEh */
    op(m, 2, 0x00, 0x00);                      /* 0004 NOP, NOP: 7Eh + 2 */
    op(m, 2, 0xED, 0x5F);                      /* 0006 LD A,R: its own two M1 cycles first: 02h, bit 7 kept */
    steps(m, 5);
    regs(m, &r);
    check("r_register", (r.af >> 8) == 0x82 && r.r == 0x82, "R = FEh, four M1 cycles on: A %02X, R %02X", r.af >> 8,
          r.r);
    done(m);
}

static void t_undocumented(void)
{
    machine *m = make();
    z80_regs r0, r1, r2, r3;
    int t[5];
    op(m, 2, 0x06, 0x80);                      /* 0000 LD B,80h */
    op(m, 2, 0xCB, 0x30);                      /* 0002 SLL B: B = 01h, carry */
    op(m, 3, 0xDD, 0x26, 0x12);                /* 0004 LD IXH,12h */
    op(m, 2, 0xDD, 0x00);                      /* 0007 a DDh before NOP */
    op(m, 2, 0xED, 0x00);                      /* 0009 an EDh opcode Zilog does not define */
    op(m, 3, 0x01, 0x56, 0x34);                /* 000B LD BC,3456h */
    op(m, 2, 0xED, 0x71);                      /* 000E OUT (C),0 */
    steps(m, 1);
    t[0] = steps(m, 1);
    regs(m, &r0);
    t[1] = steps(m, 1);
    regs(m, &r1);
    t[2] = steps(m, 1);
    regs(m, &r2);
    t[3] = steps(m, 1);
    regs(m, &r3);
    steps(m, 1);
    m->logging = 1;
    t[4] = steps(m, 1);
    check("undocumented", t[0] == 8 && (r0.bc >> 8) == 0x01 && (r0.af & 1) && t[1] == 11 && r1.ix == 0x12FF
          && t[2] == 8 && r2.pc == 0x0009 && m->pcs[4] == 0x0009 && ((r2.r - r1.r) & 0x7F) == 2 && t[3] == 8
          && r3.pc == 0x000B && r3.af == r2.af && t[4] == 12 && m->n_log == 3 && m->log[2].kind == 'o'
          && m->log[2].addr == 0x3456 && m->log[2].data == 0x00,
          "SLL B %d T, B %02X; LD IXH,n %d T, IX %04X; DDh NOP %d T, one step; ED 00 %d T; OUT (C),0 %d T, wrote %02X",
          t[0], r0.bc >> 8, t[1], r1.ix, t[2], t[3], t[4], m->n_log == 3 ? m->log[2].data : 0xEE);
    done(m);
}

static void t_every_opcode(void)
{
    /* every first byte, alone and after each prefix, each with four operand patterns: three steps, each returning
       with 4 to 64 T-states spent, and no fault */
    static const int prefix[7][3] = {{-1, -1, -1}, {0xCB, -1, -1}, {0xDD, -1, -1}, {0xED, -1, -1}, {0xFD, -1, -1},
                                     {0xDD, 0xCB, 0x05}, {0xFD, 0xCB, 0x05}};
    static const uint8_t fill[4] = {0x00, 0x55, 0xDD, 0xFF};
    machine *m = make();
    int p, o, f, k, bad = 0, first = -1, first_t = 0, first_fault = 0;
    long n = 0;
    for (p = 0; p < 7; p++)
        for (o = 0; o < 256; o++)
            for (f = 0; f < 4; f++) {
                z80_regs r;
                int low = 99, high = 0, t;
                memset(m->mem, 0, 0x2000);
                z80_reset(m->cpu);
                regs(m, &r);
                r.pc = 0x1000;
                r.sp = 0x8000;
                r.hl = 0x4000;
                r.ix = 0x4100;
                r.iy = 0x4200;
                r.bc = 0x0003;
                z80_regs_set(m->cpu, &r);
                at(m, 0x1000);
                for (k = 0; k < 3; k++)
                    if (prefix[p][k] >= 0)
                        op(m, 1, prefix[p][k]);
                op(m, 4, o, fill[f], fill[f], fill[f]);
                for (k = 0; k < 3; k++) {
                    t = z80_step(m->cpu);
                    if (t < low)
                        low = t;
                    if (t > high)
                        high = t;
                }
                regs(m, &r);
                n++;
                if ((low < 4 || high > 64 || r.fault) && !bad++) {
                    first = (prefix[p][0] >= 0 ? prefix[p][0] << 8 : 0) | o;
                    first_t = low < 4 ? low : high;
                    first_fault = r.fault;
                }
            }
    if (bad)
        check("every_opcode", 0, "%d of %ld wrong; the first: %04X with a step of %d T, fault %d", bad, n, first,
              first_t, first_fault);
    else
        check("every_opcode", 1, "%ld programs, three steps each: every step 4 to 64 T-states, no fault", n);
    done(m);
}

/* Zilog's T states: `setup` instructions first (their steps not counted), then the one timed.  A new core has every
   register 0 but IX = IY = FFFFh and F = 40h (Z set, the rest clear): NZ is not met, Z is; B and BC count from 0. */
typedef struct { const char *name; int setup, t, n; uint8_t code[8]; } timing;

static void t_cycles(void)
{
    static const timing table[] = {
        {"NOP", 0, 4, 1, {0x00}}, {"LD B,C", 0, 4, 1, {0x41}}, {"LD B,n", 0, 7, 2, {0x06, 0x12}},
        {"LD B,(HL)", 0, 7, 1, {0x46}}, {"LD (HL),B", 0, 7, 1, {0x70}}, {"LD (HL),n", 0, 10, 2, {0x36, 0x12}},
        {"LD B,(IX+d)", 0, 19, 3, {0xDD, 0x46, 0x05}}, {"LD (IY+d),B", 0, 19, 3, {0xFD, 0x70, 0x05}},
        {"LD (IX+d),n", 0, 19, 4, {0xDD, 0x36, 0x05, 0x12}},
        {"LD A,(BC)", 0, 7, 1, {0x0A}}, {"LD A,(nn)", 0, 13, 3, {0x3A, 0x00, 0x40}},
        {"LD (DE),A", 0, 7, 1, {0x12}}, {"LD (nn),A", 0, 13, 3, {0x32, 0x00, 0x40}},
        {"LD A,I", 0, 9, 2, {0xED, 0x57}}, {"LD R,A", 0, 9, 2, {0xED, 0x4F}},
        {"LD BC,nn", 0, 10, 3, {0x01, 0x34, 0x12}}, {"LD IX,nn", 0, 14, 4, {0xDD, 0x21, 0x34, 0x12}},
        {"LD HL,(nn)", 0, 16, 3, {0x2A, 0x00, 0x40}}, {"LD BC,(nn)", 0, 20, 4, {0xED, 0x4B, 0x00, 0x40}},
        {"LD IX,(nn)", 0, 20, 4, {0xDD, 0x2A, 0x00, 0x40}}, {"LD (nn),HL", 0, 16, 3, {0x22, 0x00, 0x40}},
        {"LD (nn),BC", 0, 20, 4, {0xED, 0x43, 0x00, 0x40}}, {"LD (nn),IY", 0, 20, 4, {0xFD, 0x22, 0x00, 0x40}},
        {"LD SP,HL", 0, 6, 1, {0xF9}}, {"LD SP,IX", 0, 10, 2, {0xDD, 0xF9}},
        {"PUSH BC", 0, 11, 1, {0xC5}}, {"PUSH IX", 0, 15, 2, {0xDD, 0xE5}},
        {"POP BC", 0, 10, 1, {0xC1}}, {"POP IY", 0, 14, 2, {0xFD, 0xE1}},
        {"EX DE,HL", 0, 4, 1, {0xEB}}, {"EX AF,AF'", 0, 4, 1, {0x08}}, {"EXX", 0, 4, 1, {0xD9}},
        {"EX (SP),HL", 0, 19, 1, {0xE3}}, {"EX (SP),IX", 0, 23, 2, {0xDD, 0xE3}},
        {"LDI", 0, 16, 2, {0xED, 0xA0}}, {"LDIR, BC not 0", 0, 21, 2, {0xED, 0xB0}},
        {"LDIR, BC 0", 1, 16, 5, {0x01, 0x01, 0x00, 0xED, 0xB0}}, {"LDD", 0, 16, 2, {0xED, 0xA8}},
        {"LDDR, BC not 0", 0, 21, 2, {0xED, 0xB8}}, {"CPI", 0, 16, 2, {0xED, 0xA1}},
        {"CPIR, going on", 0, 21, 2, {0xED, 0xB1}}, {"CPIR, BC 0", 1, 16, 5, {0x01, 0x01, 0x00, 0xED, 0xB1}},
        {"CPD", 0, 16, 2, {0xED, 0xA9}}, {"CPDR, going on", 0, 21, 2, {0xED, 0xB9}},
        {"ADD A,B", 0, 4, 1, {0x80}}, {"ADD A,n", 0, 7, 2, {0xC6, 0x01}}, {"ADD A,(HL)", 0, 7, 1, {0x86}},
        {"ADD A,(IX+d)", 0, 19, 3, {0xDD, 0x86, 0x05}}, {"SBC A,(IY+d)", 0, 19, 3, {0xFD, 0x9E, 0x05}},
        {"CP n", 0, 7, 2, {0xFE, 0x01}}, {"INC B", 0, 4, 1, {0x04}}, {"INC (HL)", 0, 11, 1, {0x34}},
        {"INC (IX+d)", 0, 23, 3, {0xDD, 0x34, 0x05}}, {"DEC (IY+d)", 0, 23, 3, {0xFD, 0x35, 0x05}},
        {"DAA", 0, 4, 1, {0x27}}, {"CPL", 0, 4, 1, {0x2F}}, {"NEG", 0, 8, 2, {0xED, 0x44}},
        {"CCF", 0, 4, 1, {0x3F}}, {"SCF", 0, 4, 1, {0x37}}, {"HALT", 0, 4, 1, {0x76}},
        {"DI", 0, 4, 1, {0xF3}}, {"EI", 0, 4, 1, {0xFB}}, {"IM 2", 0, 8, 2, {0xED, 0x5E}},
        {"ADD HL,BC", 0, 11, 1, {0x09}}, {"ADC HL,BC", 0, 15, 2, {0xED, 0x4A}}, {"SBC HL,DE", 0, 15, 2, {0xED, 0x52}},
        {"ADD IX,BC", 0, 15, 2, {0xDD, 0x09}}, {"INC BC", 0, 6, 1, {0x03}}, {"INC IX", 0, 10, 2, {0xDD, 0x23}},
        {"DEC SP", 0, 6, 1, {0x3B}}, {"RLCA", 0, 4, 1, {0x07}}, {"RRA", 0, 4, 1, {0x1F}},
        {"RLC B", 0, 8, 2, {0xCB, 0x00}}, {"RLC (HL)", 0, 15, 2, {0xCB, 0x06}},
        {"RLC (IX+d)", 0, 23, 4, {0xDD, 0xCB, 0x05, 0x06}}, {"SRL (IY+d)", 0, 23, 4, {0xFD, 0xCB, 0x05, 0x3E}},
        {"RLD", 0, 18, 2, {0xED, 0x6F}}, {"RRD", 0, 18, 2, {0xED, 0x67}},
        {"BIT 0,B", 0, 8, 2, {0xCB, 0x40}}, {"BIT 0,(HL)", 0, 12, 2, {0xCB, 0x46}},
        {"BIT 0,(IX+d)", 0, 20, 4, {0xDD, 0xCB, 0x05, 0x46}}, {"SET 0,B", 0, 8, 2, {0xCB, 0xC0}},
        {"SET 0,(HL)", 0, 15, 2, {0xCB, 0xC6}}, {"RES 0,(IY+d)", 0, 23, 4, {0xFD, 0xCB, 0x05, 0x86}},
        {"JP nn", 0, 10, 3, {0xC3, 0x00, 0x40}}, {"JP NZ,nn (not met)", 0, 10, 3, {0xC2, 0x00, 0x40}},
        {"JP Z,nn (met)", 0, 10, 3, {0xCA, 0x00, 0x40}}, {"JR e", 0, 12, 2, {0x18, 0x00}},
        {"JR Z,e (met)", 0, 12, 2, {0x28, 0x00}}, {"JR NZ,e (not met)", 0, 7, 2, {0x20, 0x00}},
        {"JR NC,e (met)", 0, 12, 2, {0x30, 0x00}}, {"JR C,e (not met)", 0, 7, 2, {0x38, 0x00}},
        {"JP (HL)", 0, 4, 1, {0xE9}}, {"JP (IX)", 0, 8, 2, {0xDD, 0xE9}},
        {"DJNZ (B not 0)", 0, 13, 2, {0x10, 0x00}}, {"DJNZ (B 0)", 1, 8, 4, {0x06, 0x01, 0x10, 0x00}},
        {"CALL nn", 0, 17, 3, {0xCD, 0x00, 0x40}}, {"CALL Z,nn (met)", 0, 17, 3, {0xCC, 0x00, 0x40}},
        {"CALL NZ,nn (not met)", 0, 10, 3, {0xC4, 0x00, 0x40}}, {"RET", 0, 10, 1, {0xC9}},
        {"RET Z (met)", 0, 11, 1, {0xC8}}, {"RET NZ (not met)", 0, 5, 1, {0xC0}},
        {"RETI", 0, 14, 2, {0xED, 0x4D}}, {"RETN", 0, 14, 2, {0xED, 0x45}}, {"RST 38h", 0, 11, 1, {0xFF}},
        {"IN A,(n)", 0, 11, 2, {0xDB, 0x10}}, {"IN B,(C)", 0, 12, 2, {0xED, 0x40}},
        {"INI", 0, 16, 2, {0xED, 0xA2}}, {"INIR, B not 0", 0, 21, 2, {0xED, 0xB2}},
        {"INIR, B 0", 1, 16, 4, {0x06, 0x01, 0xED, 0xB2}}, {"IND", 0, 16, 2, {0xED, 0xAA}},
        {"OUT (n),A", 0, 11, 2, {0xD3, 0x10}}, {"OUT (C),B", 0, 12, 2, {0xED, 0x41}},
        {"OUTI", 0, 16, 2, {0xED, 0xA3}}, {"OTIR, B not 0", 0, 21, 2, {0xED, 0xB3}},
        {"OUTD", 0, 16, 2, {0xED, 0xAB}},
    };
    int k, j, bad = 0, n = (int)(sizeof table / sizeof table[0]), got = 0;
    const char *first = "";
    for (k = 0; k < n; k++) {
        machine *m = make();
        int t;
        for (j = 0; j < table[k].n; j++)
            op(m, 1, table[k].code[j]);
        steps(m, table[k].setup);
        t = steps(m, 1);
        if (t != table[k].t && !bad++) {
            first = table[k].name;
            got = t;
        }
        done(m);
    }
    if (bad)
        check("cycles", 0, "%d of %d NOT Zilog's; the first: %s, %d T", bad, n, first, got);
    else
        check("cycles", 1, "%d instruction forms: Zilog's T states", n);
}

int main(void)
{
    t_zero_budget();
    t_reset();
    t_two_cores();
    t_boundary();
    t_call_return();
    t_regs_set();
    t_prefix();
    t_int_im1();
    t_int_im2();
    t_int_im0();
    t_int_masked();
    t_int_level();
    t_ei_delay();
    t_ei_nmi();
    t_nmi_edge();
    t_nmi_first();
    t_priority();
    t_halt();
    t_halt_masked();
    t_boundary_irq();
    t_block();
    t_prefix_run();
    t_io();
    t_r_register();
    t_undocumented();
    t_every_opcode();
    t_cycles();
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
