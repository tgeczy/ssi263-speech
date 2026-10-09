/* test_m6502_contract.c -- CONTRACT.md 13's behaviour of the cpu.h 6502 core (m6502.c over Fake6502).
 *
 * Each test is a few instructions in a flat 64K, driven through cpu.h only, and checks one clause.  Cycles are the
 * NMOS 6502's (MOS Technology's MCS6500 programming manual, appendix A; an interrupt's entry 7).
 *   zero_budget     m6502_run(0) returns 0 and changes nothing
 *   reset           PC from FFFCh-FFFDh, SP FDh, I set, the counts zero
 *   two_cores       two instances interleaved step by step behave as each alone (the state is each core's own)
 *   boundary        the boundary callback once per step, with that step's PC, before its instruction's writes
 *   irq_entry       an IRQ: PC and P pushed (B clear, bit 5 set), I set, PC from FFFEh, 7 cycles
 *   irq_masked      with I set an asserted IRQ is not taken
 *   cli_delay       an IRQ CLI unmasks is taken after the instruction after CLI, not before it
 *   sei_delay       an IRQ pending at SEI is still taken right after SEI
 *   plp_delay       PLP clearing I: as CLI
 *   rti_at_once     RTI restoring I clear: a pending IRQ is taken right after RTI
 *   irq_level       IRQ is a level: dropped before acceptance, not taken
 *   nmi_edge        NMI is taken through I, once per edge; a held line is not taken again
 *   brk             BRK pushes PC + 2 and P with B set, I set, PC from FFFEh, 7 cycles
 *   jmp_ind_wrap    JMP ($10FF) takes its high byte from 1000h (the NMOS page wrap)
 *   cycles          LDA abs,X crossing a page 5, not crossing 4; STA abs,X 5 either way; a branch taken 3, taken
 *                   across a page 4, not taken 2
 *   decimal         ADC and SBC with D set are counted (m6502_regs.decimal)
 *   undocumented    an undocumented opcode is counted, with its address
 *
 *   build: gcc -std=gnu89 test_m6502_contract.c m6502.c
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1.
 * m6502_controls.py undoes each rule in a copy of the core: exactly its tests must then fail.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

typedef struct {
    uint8_t mem[65536];
    int pos;
    uint32_t pcs[256];
    int n_pcs;
    long writes_seen_at_boundary;
    long writes;
    m6502 *cpu;
} machine;

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

static uint8_t rd(void *ctx, uint32_t a) { return ((machine *)ctx)->mem[a & 0xFFFF]; }
static void wr(void *ctx, uint32_t a, uint8_t v) { machine *m = (machine *)ctx; m->mem[a & 0xFFFF] = v; m->writes++; }
static void bnd(void *ctx, uint32_t pc)
{
    machine *m = (machine *)ctx;
    if (m->n_pcs < 256)
        m->pcs[m->n_pcs++] = pc;
    m->writes_seen_at_boundary = m->writes;
}

static machine *make(uint16_t start)
{
    cpu_bus bus;
    machine *m = (machine *)calloc(1, sizeof *m);
    memset(&bus, 0, sizeof bus);
    bus.ctx = m;
    bus.read = rd;
    bus.write = wr;
    bus.boundary = bnd;
    m->mem[0xFFFC] = (uint8_t)(start & 0xFF);
    m->mem[0xFFFD] = (uint8_t)(start >> 8);
    m->mem[0xFFFE] = 0x00;             /* IRQ/BRK -> 0300h */
    m->mem[0xFFFF] = 0x03;
    m->mem[0xFFFA] = 0x00;             /* NMI -> 0380h */
    m->mem[0xFFFB] = 0x03 | 0x00;
    m->mem[0xFFFA] = 0x80;
    m->pos = start;
    m->cpu = m6502_create(&bus, 1022727.0);
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

static void done(machine *m)
{
    m6502_destroy(m->cpu);
    free(m);
}

static void regs(machine *m, m6502_regs *r) { m6502_regs_get(m->cpu, r); }

static void t_zero_budget(void)
{
    machine *m = make(0x0200);
    m6502_regs a, b;
    uint64_t n;
    op(m, 1, 0xEA);
    regs(m, &a);
    n = m6502_run(m->cpu, 0);
    regs(m, &b);
    check("zero_budget", n == 0 && a.pc == b.pc && m6502_steps(m->cpu) == 0, "run(0) = %llu, steps %llu",
          (unsigned long long)n, (unsigned long long)m6502_steps(m->cpu));
    done(m);
}

static void t_reset(void)
{
    machine *m = make(0x1234);
    m6502_regs r;
    regs(m, &r);
    check("reset", r.pc == 0x1234 && r.sp == 0xFD && (r.p & 0x04) && (r.p & 0x20) && m6502_cycles(m->cpu) == 0,
          "pc %04X sp %02X p %02X", r.pc, r.sp, r.p);
    done(m);
}

static void prog_count(machine *m)
{
    /* LDX #0; loop: INX; STX $10; TXA; ADC #3; JMP loop */
    op(m, 2, 0xA2, 0x00);
    op(m, 1, 0xE8);
    op(m, 2, 0x86, 0x10);
    op(m, 1, 0x8A);
    op(m, 2, 0x69, 0x03);
    op(m, 3, 0x4C, 0x02, 0x02);
}

static void t_two_cores(void)
{
    machine *a = make(0x0200), *b = make(0x0200), *c = make(0x0200);
    m6502_regs ra, rb, rc;
    int k;
    prog_count(a);
    prog_count(b);
    prog_count(c);
    for (k = 0; k < 100; k++) {        /* a and b interleaved; c alone */
        m6502_step(a->cpu);
        m6502_step(b->cpu);
        m6502_step(b->cpu);
    }
    for (k = 0; k < 200; k++)
        m6502_step(c->cpu);
    regs(a, &ra);
    regs(b, &rb);
    regs(c, &rc);
    check("two_cores", rb.a == rc.a && rb.x == rc.x && rb.pc == rc.pc && b->mem[0x10] == c->mem[0x10]
          && ra.x != rb.x && m6502_cycles(b->cpu) == m6502_cycles(c->cpu),
          "b: a %02X x %02X; alone: a %02X x %02X; a: x %02X", rb.a, rb.x, rc.a, rc.x, ra.x);
    done(a);
    done(b);
    done(c);
}

static void t_boundary(void)
{
    machine *m = make(0x0200);
    op(m, 2, 0xA9, 0x55);              /* LDA #$55 */
    op(m, 3, 0x8D, 0x00, 0x40);        /* STA $4000 */
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    check("boundary", m->n_pcs == 2 && m->pcs[0] == 0x0200 && m->pcs[1] == 0x0202 && m->writes_seen_at_boundary == 0
          && m->writes == 1, "pcs %d (%04X %04X), writes before the STA's boundary %ld", m->n_pcs,
          (unsigned)m->pcs[0], (unsigned)m->pcs[1], m->writes_seen_at_boundary);
    done(m);
}

static void t_irq_entry(void)
{
    machine *m = make(0x0200);
    m6502_regs r;
    int t;
    op(m, 1, 0x58);                    /* CLI */
    op(m, 1, 0xEA);                    /* NOP */
    op(m, 1, 0xEA);
    m6502_step(m->cpu);                /* CLI */
    m6502_step(m->cpu);                /* NOP (CLI's delay) */
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    t = m6502_step(m->cpu);            /* acceptance + the handler's first instruction (BRK at 0300h: 00h) */
    regs(m, &r);
    check("irq_entry", m->mem[0x1FD] == 0x02 && m->mem[0x1FC] == 0x02 && (m->mem[0x1FB] & 0x30) == 0x20
          && m->pcs[2] == 0x0300 && t == 7 + 7, "pushed %02X%02X p %02X, step at %04X, %d cycles (7 + BRK's 7)",
          m->mem[0x1FD], m->mem[0x1FC], m->mem[0x1FB], (unsigned)m->pcs[2], t);
    done(m);
}

static void t_irq_masked(void)
{
    machine *m = make(0x0200);
    int k;
    for (k = 0; k < 4; k++)
        op(m, 1, 0xEA);
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    for (k = 0; k < 4; k++)
        m6502_step(m->cpu);
    check("irq_masked", m->pcs[3] == 0x0203, "the fourth step at %04X", (unsigned)m->pcs[3]);
    done(m);
}

static void t_cli_delay(void)
{
    machine *m = make(0x0200);
    op(m, 1, 0x58);                    /* 0200 CLI */
    op(m, 1, 0xEA);                    /* 0201 NOP: runs before the IRQ */
    op(m, 1, 0xEA);                    /* 0202 */
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    check("cli_delay", m->pcs[1] == 0x0201 && m->pcs[2] == 0x0300,
          "after CLI: %04X, then %04X", (unsigned)m->pcs[1], (unsigned)m->pcs[2]);
    done(m);
}

static void t_sei_delay(void)
{
    /* CLI; SEI with the IRQ held throughout: CLI's poll still sees I set, SEI's sees I clear -- so the IRQ is taken
       right after SEI (the 6502's well-known CLI/SEI window) */
    machine *m = make(0x0200);
    op(m, 1, 0x58);                    /* 0200 CLI */
    op(m, 1, 0x78);                    /* 0201 SEI */
    op(m, 1, 0xEA);                    /* 0202 */
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    check("sei_delay", m->pcs[1] == 0x0201 && m->pcs[2] == 0x0300, "after CLI: %04X, after SEI: %04X",
          (unsigned)m->pcs[1], (unsigned)m->pcs[2]);
    done(m);
}

static void t_plp_delay(void)
{
    machine *m = make(0x0200);
    op(m, 2, 0xA9, 0x20);              /* LDA #$20 (I clear) */
    op(m, 1, 0x48);                    /* PHA */
    op(m, 1, 0x28);                    /* 0203 PLP */
    op(m, 1, 0xEA);                    /* 0204 NOP: runs before the IRQ */
    op(m, 1, 0xEA);
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    check("plp_delay", m->pcs[3] == 0x0204 && m->pcs[4] == 0x0300, "after PLP: %04X, then %04X",
          (unsigned)m->pcs[3], (unsigned)m->pcs[4]);
    done(m);
}

static void t_rti_at_once(void)
{
    machine *m = make(0x0200);
    /* push 0210h and P = 20h (I clear), RTI to it with the IRQ held */
    op(m, 2, 0xA9, 0x02); op(m, 1, 0x48);
    op(m, 2, 0xA9, 0x10); op(m, 1, 0x48);
    op(m, 2, 0xA9, 0x20); op(m, 1, 0x48);
    op(m, 1, 0x40);                    /* 0209 RTI */
    m->mem[0x0210] = 0xEA;
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    {
        int k;
        for (k = 0; k < 8; k++)
            m6502_step(m->cpu);
    }
    check("rti_at_once", m->pcs[6] == 0x0209 && m->pcs[7] == 0x0300, "after RTI: %04X", (unsigned)m->pcs[7]);
    done(m);
}

static void t_irq_level(void)
{
    machine *m = make(0x0200);
    op(m, 1, 0x58);
    op(m, 1, 0xEA);
    op(m, 1, 0xEA);
    op(m, 1, 0xEA);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_set_irq(m->cpu, M6502_IRQ, 1);
    m6502_set_irq(m->cpu, M6502_IRQ, 0);
    m6502_step(m->cpu);
    check("irq_level", m->pcs[2] == 0x0202, "step at %04X", (unsigned)m->pcs[2]);
    done(m);
}

static void t_nmi_edge(void)
{
    machine *m = make(0x0200);
    int k;
    for (k = 0; k < 6; k++)
        op(m, 1, 0xEA);
    m->mem[0x0380] = 0x40;             /* the NMI handler: RTI */
    m6502_set_irq(m->cpu, M6502_NMI, 1);
    m6502_step(m->cpu);                /* NMI through I */
    m6502_set_irq(m->cpu, M6502_NMI, 1);   /* a board refreshing its line: still held, no new edge */
    m6502_step(m->cpu);                /* (RTI ran in that step) */
    m6502_step(m->cpu);
    check("nmi_edge", m->pcs[0] == 0x0380 && m->pcs[1] == 0x0200 && m->pcs[2] == 0x0201,
          "steps at %04X %04X %04X (the line still held)", (unsigned)m->pcs[0], (unsigned)m->pcs[1],
          (unsigned)m->pcs[2]);
    done(m);
}

static void t_brk(void)
{
    machine *m = make(0x0200);
    int t;
    op(m, 2, 0x00, 0xFF);              /* BRK, its padding byte */
    m->mem[0x0300] = 0xEA;
    t = m6502_step(m->cpu);
    m6502_step(m->cpu);
    check("brk", m->mem[0x1FD] == 0x02 && m->mem[0x1FC] == 0x02 && (m->mem[0x1FB] & 0x30) == 0x30
          && m->pcs[1] == 0x0300 && t == 7, "pushed %02X%02X p %02X, then %04X, %d cycles", m->mem[0x1FD],
          m->mem[0x1FC], m->mem[0x1FB], (unsigned)m->pcs[1], t);
    done(m);
}

static void t_jmp_ind_wrap(void)
{
    machine *m = make(0x0200);
    m6502_regs r;
    op(m, 3, 0x6C, 0xFF, 0x10);        /* JMP ($10FF) */
    m->mem[0x10FF] = 0x34;
    m->mem[0x1000] = 0x12;
    m->mem[0x1100] = 0x99;
    m6502_step(m->cpu);
    regs(m, &r);
    check("jmp_ind_wrap", r.pc == 0x1234, "pc %04X", r.pc);
    done(m);
}

static int cycles_of(int n, ...)
{
    machine *m = make(0x0200);
    va_list ap;
    int k, t;
    va_start(ap, n);
    for (k = 0; k < n; k++)
        m->mem[m->pos++] = (uint8_t)va_arg(ap, int);
    va_end(ap);
    m6502_step(m->cpu);                /* LDX #$FF / CLC / SEC */
    t = m6502_step(m->cpu);
    done(m);
    return t;
}

static void t_cycles(void)
{
    int lda_cross = cycles_of(5, 0xA2, 0xFF, 0xBD, 0x80, 0x40);      /* LDX #$FF; LDA $4080,X */
    int lda_same = cycles_of(5, 0xA2, 0x01, 0xBD, 0x80, 0x40);
    int sta_cross = cycles_of(5, 0xA2, 0xFF, 0x9D, 0x80, 0x40);
    int bcc_taken = cycles_of(3, 0x18, 0x90, 0x02);                  /* CLC; BCC +2 */
    int bcc_not = cycles_of(3, 0x38, 0x90, 0x02);                    /* SEC; BCC */
    int bcc_page;
    {
        machine *m = make(0x02F0);
        m->mem[0x02F0] = 0x18;
        m->mem[0x02F1] = 0x90;
        m->mem[0x02F2] = 0x20;         /* to 0313h: across the page */
        m6502_step(m->cpu);
        bcc_page = m6502_step(m->cpu);
        done(m);
    }
    check("cycles", lda_cross == 5 && lda_same == 4 && sta_cross == 5 && bcc_taken == 3 && bcc_not == 2 && bcc_page == 4,
          "LDA abs,X %d/%d, STA abs,X %d, BCC taken %d, across %d, not %d", lda_cross, lda_same, sta_cross, bcc_taken,
          bcc_page, bcc_not);
}

static void t_decimal(void)
{
    machine *m = make(0x0200);
    m6502_regs r;
    op(m, 1, 0xF8);                    /* SED */
    op(m, 2, 0x69, 0x01);              /* ADC #1 */
    op(m, 2, 0xE9, 0x01);              /* SBC #1 */
    op(m, 1, 0xD8);                    /* CLD */
    op(m, 2, 0x69, 0x01);
    m6502_run(m->cpu, 10);
    regs(m, &r);
    check("decimal", r.decimal == 2, "%u counted", (unsigned)r.decimal);
    done(m);
}

static void t_undocumented(void)
{
    machine *m = make(0x0200);
    m6502_regs r;
    op(m, 1, 0xEA);
    op(m, 1, 0x1A);                    /* 0201: an undocumented NOP */
    op(m, 1, 0xEA);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    m6502_step(m->cpu);
    regs(m, &r);
    check("undocumented", r.undocumented == 1 && r.undocumented_at == 0x0201, "%u at %04X", (unsigned)r.undocumented,
          r.undocumented_at);
    done(m);
}

int main(void)
{
    t_zero_budget();
    t_reset();
    t_two_cores();
    t_boundary();
    t_irq_entry();
    t_irq_masked();
    t_cli_delay();
    t_sei_delay();
    t_plp_delay();
    t_rti_at_once();
    t_irq_level();
    t_nmi_edge();
    t_brk();
    t_jmp_ind_wrap();
    t_cycles();
    t_decimal();
    t_undocumented();
    printf(failures ? "FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
