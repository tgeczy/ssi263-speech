/* sb_host.c -- src/hosts/soundingboard.py's SoundingBoard in C, line for line (sb_host.h).  Each part names the Python
 * it ports; every comparison, order of operations, rounding and register access is Python's (a register write is a
 * read of all of them and a write back, as pc86.py's reg_write).  Change them together.  The Accent-mini's host
 * (../accentmini/am_host.c) is the same machine for another driver.  MIT.
 */
#include "sb_host.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc86.h"
#include "../numwords.h"

#define IDLE_SEG 0x0050            /* 0050:0000 STI; JMP $ -- "DOS" between interrupts */
#define CLIENT_SEG 0x0058          /* 0058:0000 a tiny application printing DS:SI, CX chars via INT 17h */
#define ENV_SEG 0x0068             /* an environment block: no variables, the program's path */
#define TEXT_SEG 0x0070            /* the client's text buffer (16 KB) */
#define FIRST_PSP 0x0800           /* SBLOAD's PSP; SB's follows SBLOAD's resident part */
#define STACK_SEG 0x9000           /* the host's own stack, for far calls and interrupts from "DOS" */
#define BIOS_SEG 0xF000
#define IRET_OFF 0xFF53            /* F000:FF53, where every unclaimed vector points */
#define RETURN_OFF 0xFF00          /* F000:FF00: far calls into the driver return here (HLT) */
#define IRQ_EOI_OFF 0xFF60         /* F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET */
#define TIMER_OFF 0xFF70           /* F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI */
#define DOS_OFF 0xFF90             /* F000:FF90, DOS's INT 21h for a driver that chains to it: STI; INT E1h; RETF 2 */
#define INDOS_OFF 0xFE00           /* F000:FE00, the InDOS flag (always 0) and the critical-error flag before it */
#define CARD_BASE 0x300            /* factory setting of the SPEECH BASE ADDRESS switches (/!B48) */
#define CARD_IRQ 2                 /* factory jumper (/!I2): vector 0Ah */
#define LPT 2                      /* LPT3, the driver's default port (/!L3), as INT 17h numbers it */
#define TIMER_HZ (1193182.0 / 65536)
#define RING_WRITE 0x14C           /* SB.COM 2.7's frame ring: where the rules write ... */
#define RING_READ 0x14E            /* ... and where the IRQ handler reads */
#define RING_SIZE 0xFF5            /* the ring's bytes: 817 frames, about 40 s of speech; each pointer drops by it as it wraps */
#define FEED_BELOW 0x800           /* text is printed to the driver while the ring holds less than this (about 20 s) */
#define PIECE 32                   /* bytes printed at a time: a few words' frames, far from filling the ring */
#define RET_LINEAR ((unsigned long)BIOS_SEG * 16 + RETURN_OFF)
#define NO_COUNT ((uint64_t)1 << 62)

struct sb_host {
    pc86 *p;
    uint8_t *mem;
    ssi263 *chip;
    int own_chip;
    double out_rate, cpu_ips;
    /* the card and the PIC */
    int pic_mask, in_service, irq_latched, last_request;  /* in_service: the IRQs in service, a bit each */
    double timer_due;
    double last_speech, say_time;
    int preparing;
    /* the programs */
    int resident;                                         /* paragraphs once a program has gone resident, else -1 */
    unsigned long dta;
    int current_psp, sb_seg;
    int stopped;                                          /* self.stopped: a reason, for this call */
    int fault;                                            /* something raised: the host is dead */
    char why[200];
    /* audio */
    double *pend, *buf;
    int n_pend, cap_pend, n_buf, cap_buf;
    long long insns;
    int log_on;
    sbh_write *wl;
    int n_wl, cap_wl;
    /* text said and not yet printed to the driver (feed) */
    unsigned char *queue;
    int q_len, q_pos, q_cap;
    int pace_break;                                       /* SSI263_SB_PACE_BREAK=1, the tests' control: all in one call */
    int tail_break;                                       /* SSI263_SO_TAIL_BREAK=1, the tests' control: no held() */
};

/* ---- faults: what soundingboard.py raises ------------------------------------------------------------------------- */
static void raise_(sb_host *h, const char *fmt, ...)
{
    va_list ap;
    if (h->fault)
        return;
    h->fault = 1;
    va_start(ap, fmt);
    vsnprintf(h->why, sizeof h->why, fmt, ap);
    va_end(ap);
    pc86_stop(h->p);
}

/* _stop: the reason kept, the run stopped; the host call that ran it raises */
static void stop_(sb_host *h, const char *fmt, ...)
{
    va_list ap;
    h->stopped = 1;
    if (!h->fault) {
        va_start(ap, fmt);
        vsnprintf(h->why, sizeof h->why, fmt, ap);
        va_end(ap);
    }
    pc86_stop(h->p);
}

/* ---- registers and memory, as pc86.py's reg_read / reg_write / mem_write ----------------------------------------- */
enum { AX, BX, CX, DX, SI, DI, BP, SP, CS, DS, ES, SS, IP, FL };

static int r(sb_host *h, int which)
{
    i86_regs g;
    pc86_regs_get(h->p, &g);
    switch (which) {
    case AX: return g.ax; case BX: return g.bx; case CX: return g.cx; case DX: return g.dx;
    case SI: return g.si; case DI: return g.di; case BP: return g.bp; case SP: return g.sp;
    case CS: return g.cs; case DS: return g.ds; case ES: return g.es; case SS: return g.ss;
    case IP: return g.ip; default: return g.flags;
    }
}

static void w(sb_host *h, int which, int value)
{
    i86_regs g;
    uint16_t v = (uint16_t)(value & 0xFFFF);
    pc86_regs_get(h->p, &g);
    switch (which) {
    case AX: g.ax = v; break; case BX: g.bx = v; break; case CX: g.cx = v; break; case DX: g.dx = v; break;
    case SI: g.si = v; break; case DI: g.di = v; break; case BP: g.bp = v; break; case SP: g.sp = v; break;
    case CS: g.cs = v; break; case DS: g.ds = v; break; case ES: g.es = v; break; case SS: g.ss = v; break;
    case IP: g.ip = v; break; default: g.flags = v; break;
    }
    pc86_regs_set(h->p, &g);
}

static int rd16(const sb_host *h, unsigned long a) { return h->mem[a & 0xFFFFF] | h->mem[(a + 1) & 0xFFFFF] << 8; }
static void wr16(sb_host *h, unsigned long a, int v)
{
    h->mem[a & 0xFFFFF] = (uint8_t)(v & 0xFF);
    h->mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8 & 0xFF);
}
static int wrmem(sb_host *h, unsigned long a, const void *d, size_t n)
{
    if (a + n > PC86_MEM_SIZE) {
        raise_(h, "write outside 1 MB");
        return 0;
    }
    memcpy(h->mem + a, d, n);
    return 1;
}

static unsigned long here(sb_host *h) { return (unsigned long)r(h, CS) * 16 + (unsigned long)r(h, IP); }

static void push(sb_host *h, int v)
{
    int sp = (r(h, SP) - 2) & 0xFFFF;
    w(h, SP, sp);
    wr16(h, (unsigned long)r(h, SS) * 16 + (unsigned long)sp, v & 0xFFFF);
}

static void carry(sb_host *h, int on)
{
    int fl = r(h, FL);
    w(h, FL, on ? (fl | 1) : (fl & ~1));
}

static void vector(sb_host *h, int n, int *seg, int *off)
{
    *off = rd16(h, (unsigned long)n * 4);
    *seg = rd16(h, (unsigned long)n * 4 + 2);
}

/* _dispatch: what INT n does on a real CPU: push flags, CS, IP; clear IF, TF; jump */
static void dispatch(sb_host *h, int n)
{
    int seg, off, fl;
    vector(h, n, &seg, &off);
    fl = r(h, FL);
    push(h, fl);
    push(h, r(h, CS));
    push(h, r(h, IP));
    w(h, FL, fl & ~0x300);
    w(h, CS, seg);
    w(h, IP, off);
}

/* emu_start(begin, until, count) on pc86.py: begin = CS:IP now; then the aliased-opcode check */
static void emu_start(sb_host *h, uint32_t until, uint64_t count)
{
    uint32_t addr;
    uint8_t op;
    if (h->fault)
        return;
    pc86_run(h->p, count ? count : NO_COUNT, until);
    if (!h->fault && pc86_aliased(h->p, &addr, &op))
        raise_(h, "opcode %02Xh at %05X: its meaning differs on the 80186 and later; the 8086 core is not the CPU "
               "these drivers need", op, (unsigned)addr);
}

static int grow(void **p, int *cap, int need, size_t size)
{
    void *q;
    int c;
    if (need <= *cap)
        return 1;
    c = *cap ? *cap : 4096;
    while (c < need)
        c *= 2;
    q = realloc(*p, (size_t)c * size);
    if (!q)
        return 0;
    *p = q;
    *cap = c;
    return 1;
}

/* ---- interrupts: the drivers' own handlers run; BIOS and DOS are emulated ----------------------------------------- */
static void go_resident(sb_host *h, int paras)
{
    h->resident = paras;
    pc86_stop(h->p);
}

/* _dos */
static void dos(sb_host *h, int ah, int al)
{
    carry(h, 0);
    if (ah == 0x09 || ah == 0x02) {
        /* "DOS print": the log only */
    } else if (ah == 0x30) {
        w(h, AX, 0x0005);                               /* DOS 5.0 */
        w(h, BX, 0);
        w(h, CX, 0);
    } else if (ah == 0x35) {
        int seg, off;
        vector(h, al, &seg, &off);
        w(h, ES, seg);
        w(h, BX, off);
    } else if (ah == 0x25) {
        wr16(h, (unsigned long)al * 4, r(h, DX));
        wr16(h, (unsigned long)al * 4 + 2, r(h, DS));
    } else if (ah == 0x31) {                            /* keep DX paragraphs resident */
        go_resident(h, r(h, DX));
    } else if (ah == 0x34) {                            /* InDOS flag address */
        w(h, ES, BIOS_SEG);
        w(h, BX, INDOS_OFF);
    } else if (ah == 0x51 || ah == 0x62) {
        w(h, BX, h->current_psp);
    } else if (ah == 0x50) {
        h->current_psp = r(h, BX);
    } else if (ah == 0x1A) {
        h->dta = (unsigned long)r(h, DS) * 16 + (unsigned long)r(h, DX);
    } else if (ah == 0x2F) {
        w(h, ES, (int)(h->dta >> 4));
        w(h, BX, (int)(h->dta & 15));
    } else if (ah == 0x49 || ah == 0x4A) {
        /* free / resize memory: always fine */
    } else if (ah == 0x3D) {                            /* open: no SPEECH.DIC here */
        w(h, AX, 2);
        carry(h, 1);
    } else if (ah == 0x2A) {                            /* date: 1996-01-22, a Monday */
        w(h, CX, 1996);
        w(h, DX, 0x0116);
        w(h, AX, (r(h, AX) & 0xFF00) | 1);
    } else if (ah == 0x2C) {
        w(h, CX, 0x0C00);
        w(h, DX, 0);
    } else if (ah == 0x5D && al == 0x06) {              /* swappable data area */
        w(h, DS, BIOS_SEG);
        w(h, SI, INDOS_OFF - 1);
        w(h, CX, 0x80);
        w(h, DX, 0x40);
    } else if (ah == 0x4C) {
        stop_(h, "program exited (code %d) without staying resident", al);
    } else
        stop_(h, "unimplemented DOS AH=%02X AL=%02X", ah, al);
}

/* _int.  pc86.py: a serviced interrupt (1). */
static int on_int(void *user, int n, int kind)
{
    sb_host *h = (sb_host *)user;
    int seg, off, ah, al;
    (void)kind;
    if (h->fault)
        return 1;
    vector(h, n, &seg, &off);
    if (seg != BIOS_SEG) {
        dispatch(h, n);                                 /* a vector a driver installed */
        return 1;
    }
    ah = r(h, AX) >> 8;
    al = r(h, AX) & 0xFF;
    if (n == 0x21 || n == 0xE1)
        dos(h, ah, al);
    else if (n == 0x27)                                 /* terminate and stay resident, DX bytes */
        go_resident(h, (r(h, DX) + 15) / 16);
    else if (n == 0x20)
        stop_(h, "program ended without staying resident");
    else if (n == 0x2F) {
        /* multiplex: nobody else installed (AL stays 0) */
    } else if (n == 0x16) {                             /* keyboard: nothing typed */
        if (ah == 0x01 || ah == 0x11)
            w(h, FL, r(h, FL) | 0x40);
        else if (ah == 0x02 || ah == 0x12)
            w(h, AX, r(h, AX) & 0xFF00);
        else
            stop_(h, "unimplemented INT 16 AH=%02X", ah);
    } else if (n == 0x10 || n == 0x14 || n == 0x15 || n == 0x17 || n == 0x1A || n == 0x1C || n == 0x28
               || n == 0x2A) {
        /* video, serial, AT services, printer, clock, tick, DOS idle: nothing answers (SB.COM asks INT 17h AX=FFFF
           whether it is loaded) */
    } else
        stop_(h, "unimplemented INT %02X AX=%04X", n, r(h, AX));
    return 1;
}

/* ---- the card and the PIC ----------------------------------------------------------------------------------------- */
static int on_in(void *user, int port)
{
    sb_host *h = (sb_host *)user;
    if (h->fault)
        return 0xFF;
    if (port == 0x21)
        return h->pic_mask;
    if (port == 0x61 || port == 0x60)
        return 0x00;
    if (port >= CARD_BASE && port < CARD_BASE + 16)
        return ssi263_request(h->chip) ? 0x7F : 0xFF;   /* the SSI-263 drives D7 with A/R* (low = requesting) */
    stop_(h, "unimplemented IN %04X", port);
    return 0xFF;
}

/* a potentially unfinished phoneme (not PA, powered up): so_host.c's held(), the same front end */
static int held(const sb_host *h)
{
    return !h->tail_break && (ssi263_reg(h->chip, 0) & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80);
}

static void on_out(void *user, int port, int value)
{
    sb_host *h = (sb_host *)user;
    if (h->fault)
        return;
    value &= 0xFF;
    if (port == 0x21)
        h->pic_mask = value;
    else if (port == 0x20) {
        if (value == 0x20 && h->in_service)
            h->in_service &= h->in_service - 1;         /* discard(min(in_service)) */
    } else if (port == 0x61 || port == 0x42 || port == 0x43 || port == 0xE2 || port == 0xE3) {
        /* speaker and timer 2 (the bell), Toshiba config */
    } else if (port >= CARD_BASE && port < CARD_BASE + 5) {
        int reg = port - CARD_BASE;
        int sounding = reg == 0 && held(h);         /* a phoneme playing that this write ends */
        ssi263_write(h->chip, reg, value);
        if (h->log_on && grow((void **)&h->wl, &h->cap_wl, h->n_wl + 1, sizeof *h->wl)) {
            h->wl[h->n_wl].t = ssi263_time(h->chip);
            h->wl[h->n_wl].reg = reg;
            h->wl[h->n_wl].val = value;
            h->n_wl++;
        }
        if (sounding)                               /* speech until now: busy()'s `quiet` lets its fall die away */
            h->last_speech = ssi263_time(h->chip);
        if (reg == 0 && (value & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80)) {
            h->last_speech = ssi263_time(h->chip);
            h->preparing = 0;
        }
    } else
        stop_(h, "unimplemented OUT %04X <- %02X", port, value);
}

/* _try_irq: IRQ2 from the chip's A/R (the PIC latches the rising edge of the request), and the BIOS timer */
static int try_irq(sb_host *h)
{
    int line = ssi263_request(h->chip) != 0;
    if (line && !h->last_request)
        h->irq_latched = 1;
    h->last_request = line;
    if (!line)
        h->irq_latched = 0;
    if (!(r(h, FL) & 0x200))
        return 0;
    if (ssi263_time(h->chip) >= h->timer_due) {
        h->timer_due = ssi263_time(h->chip) + 1.0 / TIMER_HZ;
        if (!(h->pic_mask & 1) && !h->in_service) {
            dispatch(h, 0x08);
            h->in_service |= 1;
            return 1;
        }
    }
    if (h->irq_latched && !(h->pic_mask & (1 << CARD_IRQ)) && !(h->in_service & ((2 << CARD_IRQ) - 1))) {
        dispatch(h, 0x08 + CARD_IRQ);
        h->in_service |= 1 << CARD_IRQ;
        h->irq_latched = 0;
        return 1;
    }
    return 0;
}

/* ---- audio -------------------------------------------------------------------------------------------------------- */
/* one slice of the chip: run_until_request(step) if it is not requesting, else run(step / 4); appended to *buf */
static int chip_slice(sb_host *h, double step, double **buf, int *n, int *cap)
{
    long want, got;
    if (!ssi263_request(h->chip)) {
        want = (long)(step * h->out_rate);              /* int(): truncation */
        if (!grow((void **)buf, cap, *n + (int)want + 1, sizeof(double)))
            return 0;
        got = ssi263_run_until_request(h->chip, want, *buf + *n);
    } else {
        want = (long)nearbyint(step / 4 * h->out_rate); /* round(): half to even */
        if (!grow((void **)buf, cap, *n + (int)want + 1, sizeof(double)))
            return 0;
        got = ssi263_run(h->chip, want, *buf + *n);
    }
    *n += (int)got;
    return 1;
}

static int pending_slice(sb_host *h, double step)
{
    if (!chip_slice(h, step, &h->pend, &h->n_pend, &h->cap_pend)) {
        raise_(h, "out of memory");
        return 0;
    }
    return 1;
}

static uint64_t steps_for(sb_host *h, double dt)
{
    long long n = (long long)(h->cpu_ips * dt);         /* int() */
    return (uint64_t)(n > 100 ? n : 100);               /* max(100, ...) */
}

static double dt_since(sb_host *h, double before)
{
    double dt = ssi263_time(h->chip) - before;
    return dt > 1e-5 ? dt : 1e-5;                       /* max(..., 1e-5) */
}

/* ---- loading a .COM program as COMMAND.COM would: run until it stays resident; its paragraphs, or -1 ------------ */
static int exec_com(sb_host *h, const unsigned char *data, size_t n, int psp, const char *name, char *err, int errlen)
{
    uint8_t p[256];
    char env[64];
    int k;
    double waited = 0.0;
    if ((unsigned long)psp * 16 + 0x100 + n > (unsigned long)STACK_SEG * 16) {
        snprintf(err, (size_t)errlen, "%s is too large", name);
        return -1;
    }
    memset(p, 0, sizeof p);
    p[0] = 0xCD;
    p[1] = 0x20;
    p[2] = 0x00;                                        /* first paragraph beyond the program: A000 */
    p[3] = 0xA0;
    p[0x2C] = ENV_SEG & 0xFF;
    p[0x2D] = ENV_SEG >> 8;
    p[0x80] = 0;                                        /* no arguments */
    p[0x81] = '\r';
    wrmem(h, (unsigned long)psp * 16, p, sizeof p);
    wrmem(h, (unsigned long)psp * 16 + 0x100, data, n);
    /* the emulated DOS's drive letter passed in (no host path in the source) */
    k = snprintf(env, sizeof env, "COMSPEC=%c:\\COMMAND.COM%c%c%c%c%c:\\SB\\%s", 'C', 0, 0, 1, 0, 'C', name);
    wrmem(h, (unsigned long)ENV_SEG * 16, env, (size_t)k + 1);
    w(h, CS, psp);
    w(h, DS, psp);
    w(h, ES, psp);
    w(h, SS, psp);
    w(h, SP, 0xFFFE);
    wr16(h, (unsigned long)psp * 16 + 0xFFFE, 0);       /* RET to PSP:0000, INT 20h */
    w(h, IP, 0x100);
    h->dta = (unsigned long)psp * 16 + 0x80;
    h->current_psp = psp;
    h->resident = -1;
    h->stopped = 0;
    /* the driver talks to the card while it starts (it checks the chip answers): run the chip and the IRQ beside it */
    emu_start(h, 0, 1000000);
    while (h->resident < 0 && !h->stopped && !h->fault && waited < 10.0) {
        double before, dt;
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!pending_slice(h, 0.0005))
            break;
        dt = dt_since(h, before);
        emu_start(h, 0, steps_for(h, dt));
        waited += dt;
    }
    if (h->resident < 0) {
        snprintf(err, (size_t)errlen, "%s did not go resident: %s", name,
                 h->fault || h->stopped ? h->why : "ran out of time");
        return -1;
    }
    return h->resident;
}

/* ---- driving the drivers ------------------------------------------------------------------------------------------ */
/* _cpu */
static int cpu(sb_host *h, uint64_t count)
{
    emu_start(h, 0, count);                             /* until 0: pc86.py passes the linear address 0 */
    h->insns += (long long)count;
    if (h->stopped)
        h->fault = 1;
    return h->fault ? -1 : 0;
}

/* _far_call: far-call seg:off from the host; run until it returns, with the card running if it waits */
static int far_call(sb_host *h, int seg, int off, uint64_t budget, double wait_limit, double step)
{
    double waited = 0.0;
    if (h->fault)
        return -1;
    h->stopped = 0;
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    push(h, BIOS_SEG);
    push(h, RETURN_OFF);
    w(h, CS, seg);
    w(h, IP, off);
    emu_start(h, (uint32_t)RET_LINEAR, budget);
    while (!h->stopped && !h->fault && here(h) != RET_LINEAR) {
        double before, dt;
        if (waited > wait_limit) {
            stop_(h, "call %04X:%04X still waiting after %.0f s", seg, off, waited);
            break;
        }
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!pending_slice(h, step))
            return -1;
        dt = dt_since(h, before);
        emu_start(h, (uint32_t)RET_LINEAR, steps_for(h, dt));
        waited += dt;
    }
    if (h->stopped)
        h->fault = 1;
    if (h->fault)
        return -1;
    w(h, CS, IDLE_SEG);
    w(h, IP, 0);
    return 0;
}

/* _settle: let an interrupt handler a CPU slice stopped inside finish before calling the driver */
static int settle(sb_host *h, uint64_t budget)
{
    uint64_t n = 0;
    while (r(h, CS) != IDLE_SEG && n < budget) {
        if (cpu(h, 1000) < 0)
            return -1;
        n += 1000;
    }
    return 0;
}

/* _print: bytes to LPT3 now, the client's call running until the driver has taken them all */
static void print_(sb_host *h, const unsigned char *bytes, int n)
{
    int k;
    if (settle(h, 400000) < 0)
        return;
    for (k = 0; k < n; k += 0x4000) {
        int len = n - k < 0x4000 ? n - k : 0x4000;
        if (!wrmem(h, (unsigned long)TEXT_SEG * 16, bytes + k, (size_t)len))
            return;
        w(h, DS, TEXT_SEG);
        w(h, SI, 0);
        w(h, CX, len);
        if (far_call(h, CLIENT_SEG, 0, 2000000, 30.0, 0.0005) < 0)
            return;
    }
}

/* _ring_fill: the frame ring's bytes not yet sent; each pointer drops by RING_SIZE as it wraps (measured) */
static int ring_fill(const sb_host *h)
{
    unsigned long base = (unsigned long)h->sb_seg * 16;
    int d = (rd16(h, base + RING_WRITE) - rd16(h, base + RING_READ)) % RING_SIZE;
    return d < 0 ? d + RING_SIZE : d;
}

/* _feed: the waiting text, a piece at a time, while the ring is under FEED_BELOW: the ring then always has 20 s of
   speech ahead of the chip, and never fills, so a piece's printer call never waits */
static void feed(sb_host *h)
{
    while (!h->fault && h->q_pos < h->q_len && (h->pace_break || ring_fill(h) < FEED_BELOW)) {
        int len = h->q_len - h->q_pos < PIECE || h->pace_break ? h->q_len - h->q_pos : PIECE;   /* control: all */
        h->q_pos += len;
        print_(h, h->queue + h->q_pos - len, len);
    }
    if (h->q_pos == h->q_len)
        h->q_pos = h->q_len = 0;
}

/* say: SB.COM turns each word into frames as it arrives and makes the printer call wait while its frame ring is full
   (its status never says busy), so a long text held the call for as long as it took to speak all but the ring's last
   20 s: past the 30 s wait, the host faulted and the whole utterance was lost.  The text waits here instead (feed). */
static void say_(sb_host *h, const unsigned char *bytes, int n, int speech)
{
    if (h->fault || n <= 0)
        return;
    if (settle(h, 400000) < 0)
        return;
    if (speech) {
        h->preparing = 1;
        h->say_time = ssi263_time(h->chip);
    }
    if (h->q_len + n > h->q_cap) {
        int cap = h->q_cap ? h->q_cap : 4096;
        unsigned char *q;
        while (cap < h->q_len + n)
            cap *= 2;
        q = (unsigned char *)realloc(h->queue, (size_t)cap);
        if (!q) {
            raise_(h, "out of memory");
            return;
        }
        h->queue = q;
        h->q_cap = cap;
    }
    memcpy(h->queue + h->q_len, bytes, (size_t)n);
    h->q_len += n;
    feed(h);
}

SB_API void sbh_say(sb_host *h, const unsigned char *bytes, int n)
{
    int i, speech = 0;
    for (i = 0; i < n && !speech; i++)                  /* the text's characters >= " ", any isalnum() */
        speech = bytes[i] >= 0x20 && nw_isalnum(bytes[i]);
    say_(h, bytes, n, speech && bytes[0] != 0x05);      /* and not a ^E setting (so_host.h's rule) */
}

SB_API int sbh_run(sb_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0, *swap;
    int cap;
    *audio = h->buf;
    if (h->fault)
        return -1;
    feed(h);
    if (h->fault)
        return -1;
    /* out = self.pending: the run's audio follows what the blocked calls made */
    swap = h->buf; h->buf = h->pend; h->pend = swap;
    cap = h->cap_buf; h->cap_buf = h->cap_pend; h->cap_pend = cap;
    h->n_buf = h->n_pend;
    h->n_pend = 0;
    while (t < seconds) {
        double before, dt;
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!chip_slice(h, step, &h->buf, &h->n_buf, &h->cap_buf)) {
            raise_(h, "out of memory");
            return -1;
        }
        dt = dt_since(h, before);
        if (cpu(h, steps_for(h, dt)) < 0)
            return -1;
        t += dt;
    }
    *audio = h->buf;
    return h->n_buf;
}

SB_API double sbh_skip(sb_host *h, double seconds, double step)
{
    double t = 0.0;
    if (h->fault)
        return -1.0;
    feed(h);
    if (h->fault)
        return -1.0;
    h->n_pend = 0;
    while (t < seconds - 1e-9) {
        double before, dt, s = seconds - t;
        try_irq(h);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, step < s ? step : s);      /* min(step, seconds - t) */
        dt = dt_since(h, before);
        if (cpu(h, steps_for(h, dt)) < 0)
            return -1.0;
        t += dt;
    }
    return t;
}

SB_API int sbh_input_pending(const sb_host *h)
{
    unsigned long base = (unsigned long)h->sb_seg * 16;
    return h->q_len > h->q_pos || rd16(h, base + RING_WRITE) != rd16(h, base + RING_READ);
}

SB_API int sbh_busy(const sb_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    if (sbh_input_pending(h) || (now - h->last_speech) < quiet || (held(h) && (now - h->last_speech) < patience))
        return 1;
    return h->preparing && (now - h->say_time) < patience;
}

/* boot: the card's own greeting ("Sounding board version 2.7") played out silently -- whatever `seconds` asks: the
   Speak-Out's short boot would leave it running into the first utterance */
SB_API void sbh_boot(sb_host *h, double seconds)
{
    double t, s;
    (void)seconds;
    t = sbh_skip(h, 0.2, 0.002);
    while (t >= 0 && t < SBH_GREETING_LIMIT && sbh_busy(h, 0.05, 1.5)) {
        if ((s = sbh_skip(h, 0.05, 0.002)) < 0)
            return;
        t += s;
    }
}

SB_API void sbh_cancel(sb_host *h, double limit)
{
    static const unsigned char CTRL_X[] = {0x18};
    double t, s;
    if (h->fault)
        return;
    h->preparing = 0;
    h->q_pos = h->q_len = 0;                    /* the text not yet printed goes too; ^X itself at once */
    print_(h, CTRL_X, 1);
    t = sbh_skip(h, 0.01, 0.002);
    while (t >= 0 && t < limit && sbh_busy(h, 0.05, 1.5)) {
        if ((s = sbh_skip(h, 0.01, 0.002)) < 0)
            return;
        t += s;
    }
}

SB_API int sbh_fault(const sb_host *h) { return h->fault; }
SB_API const char *sbh_error(const sb_host *h) { return h->fault ? h->why : ""; }
SB_API ssi263 *sbh_chip(sb_host *h) { return h->chip; }

/* ---- creation: the BIOS, "DOS", the card; SBLOAD, then SB right after what SBLOAD keeps resident --------------- */
SB_API sb_host *sbh_create(const unsigned char *sbload, size_t sbload_len, const unsigned char *sb, size_t sb_len,
                           ssi263 *chip, double out_rate, char *err, int errlen)
{
    /* mov al,[si]; mov ah,0; mov dx,LPT; push cx; push si; push ds; int 17h; pop ds; pop si; pop cx; inc si; loop;
       retf */
    static const uint8_t client[19] = {0x8A, 0x04, 0xB4, 0x00, 0xBA, LPT, 0x00, 0x51, 0x56, 0x1E, 0xCD, 0x17, 0x1F,
                                       0x5E, 0x59, 0x46, 0xE2, 0xEE, 0xCB};
    static const uint8_t eoi[7] = {0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF};       /* push ax; EOI; pop ax; iret */
    /* push ax; push ds; xor ax,ax; mov ds,ax; inc word [46Ch]; jnz $+6; inc word [46Eh]; int 1Ch; mov al,20h;
       out 20h,al; pop ds; pop ax; iret */
    static const uint8_t timer[25] = {0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04, 0x75, 0x04, 0xFF,
                                      0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20, 0xE6, 0x20, 0x1F, 0x58, 0xCF};
    static const uint8_t dos_code[6] = {0xFB, 0xCD, 0xE1, 0xCA, 0x02, 0x00};       /* sti; int E1h; retf 2 */
    static const uint8_t idle[3] = {0xFB, 0xEB, 0xFE};
    char e[200];
    sb_host *h;
    uint8_t *m;
    int v, psp, paras;
    if (!err || errlen <= 0) {
        err = e;
        errlen = (int)sizeof e;
    }
    if (!sbload || !sb || sbload_len < 16 || sb_len < 16) {
        snprintf(err, (size_t)errlen, "the Sounding Board's SBLOAD.COM and SB.COM are needed");
        return NULL;
    }
    h = (sb_host *)calloc(1, sizeof *h);
    if (!h) {
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    h->pace_break = getenv("SSI263_SB_PACE_BREAK") && !strcmp(getenv("SSI263_SB_PACE_BREAK"), "1");
    h->tail_break = getenv("SSI263_SO_TAIL_BREAK") && !strcmp(getenv("SSI263_SO_TAIL_BREAK"), "1");
    h->p = pc86_create();
    if (!h->p) {
        free(h);
        snprintf(err, (size_t)errlen, "pc86_create failed");
        return NULL;
    }
    if (!chip) {
        ssi263_params prm;
        ssi263_default_params(&prm);
        chip = ssi263_new(&prm, ssi263_default_rom(), out_rate);
        if (!chip) {
            pc86_destroy(h->p);
            free(h);
            snprintf(err, (size_t)errlen, "ssi263_new failed");
            return NULL;
        }
        h->own_chip = 1;
    }
    h->chip = chip;
    h->out_rate = out_rate;
    h->cpu_ips = SBH_CPU_IPS;
    h->mem = m = pc86_mem(h->p);
    /* BIOS: every vector an IRET, hardware IRQs EOI, 640 KB, no printers, the far-call trap */
    m[(unsigned long)BIOS_SEG * 16 + IRET_OFF] = 0xCF;
    m[(unsigned long)BIOS_SEG * 16 + RETURN_OFF] = 0xF4;
    memcpy(m + (unsigned long)BIOS_SEG * 16 + IRQ_EOI_OFF, eoi, sizeof eoi);
    for (v = 0; v < 256; v++) {
        wr16(h, (unsigned long)v * 4, v >= 0x08 && v <= 0x0F ? IRQ_EOI_OFF : IRET_OFF);
        wr16(h, (unsigned long)v * 4 + 2, BIOS_SEG);
    }
    memcpy(m + (unsigned long)BIOS_SEG * 16 + TIMER_OFF, timer, sizeof timer);
    wr16(h, 0x08 * 4, TIMER_OFF);
    /* INT 21h: SB.COM hooks it and jumps on to the old vector, so DOS has to be code to jump to */
    memcpy(m + (unsigned long)BIOS_SEG * 16 + DOS_OFF, dos_code, sizeof dos_code);
    wr16(h, 0x21 * 4, DOS_OFF);
    m[0xFFFFE] = 0xFC;                                  /* model byte: AT */
    wr16(h, 0x413, 640);
    memcpy(m + (unsigned long)IDLE_SEG * 16, idle, sizeof idle);
    memcpy(m + (unsigned long)CLIENT_SEG * 16, client, sizeof client);
    /* the card and the PIC */
    h->pic_mask = 0xB8;                                 /* as the BIOS leaves it: the timer, keyboard, IRQ2, floppy */
    h->last_speech = -1.0;
    h->resident = -1;
    pc86_set_hooks(h->p, on_in, on_out, on_int, h);
    /* load SBLOAD, then SB right after what SBLOAD keeps resident */
    psp = FIRST_PSP;
    paras = exec_com(h, sbload, sbload_len, psp, "SBLOAD.COM", err, errlen);
    if (paras > 0) {
        psp += paras;
        paras = exec_com(h, sb, sb_len, psp, "SB.COM", err, errlen);
    }
    if (paras <= 0) {
        if (paras == 0)
            snprintf(err, (size_t)errlen, "a Sounding Board driver kept no memory resident");
        sbh_destroy(h);
        return NULL;
    }
    h->sb_seg = psp;                                    /* SB.COM, whose data SBLOAD's code also works in */
    h->n_pend = 0;
    w(h, CS, IDLE_SEG);
    w(h, IP, 0);
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    return h;
}

SB_API void sbh_destroy(sb_host *h)
{
    if (!h)
        return;
    pc86_destroy(h->p);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->pend);
    free(h->buf);
    free(h->wl);
    free(h->queue);
    free(h);
}

/* ---- state ------------------------------------------------------------------------------------------------------- */
SB_API double sbh_get_double(const sb_host *h, const char *name)
{
    if (!strcmp(name, "cpu_ips")) return h->cpu_ips;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

SB_API void sbh_set_double(sb_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_ips")) h->cpu_ips = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
}

SB_API int sbh_get_int(const sb_host *h, const char *name)
{
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    return -1;
}

SB_API void sbh_set_int(sb_host *h, const char *name, int v)
{
    if (!strcmp(name, "preparing"))
        h->preparing = v != 0;
    else if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
}

SB_API int sbh_writes(const sb_host *h, const sbh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

SB_API void sbh_clear_writes(sb_host *h) { h->n_wl = 0; }
