/* st_host.c -- src/hosts/smartalk.py's SmarTalk in C, line for line (st_host.h).  Each part names the Python it ports;
 * every comparison, order of operations, rounding and register access is Python's (a register write is a read of all
 * of them and a write back, as pc86.py's reg_write).  Change them together.  The Sounding Board's host
 * (../soundingboard/sb_host.c) is the same machine for other drivers.  MIT.
 */
#include "st_host.h"

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
#define ENV_SEG 0x0068             /* an environment block: COMSPEC, then the program's path */
#define TEXT_SEG 0x0070            /* the client's text buffer (16 KB) */
#define PSP_SEG 0x0800             /* S_TALK's PSP; its image loads 10h paragraphs above */
#define STACK_SEG 0x9000           /* the host's own stack, for far calls and interrupts from "DOS" */
#define BIOS_SEG 0xF000
#define IRET_OFF 0xFF53            /* F000:FF53, where every unclaimed vector points */
#define RETURN_OFF 0xFF00          /* F000:FF00: far calls into the TSR return here (HLT) */
#define IRQ_EOI_OFF 0xFF60         /* F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET */
#define TIMER_OFF 0xFF70           /* F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI */
#define DOS_OFF 0xFF90             /* F000:FF90, DOS's INT 21h for a program that chains to it: STI; INT E1h; RETF 2 */
#define INDOS_OFF 0xFE00           /* F000:FE00, the InDOS flag (always 0) */
#define LPT_BASE 0x378             /* LPT1, as the BIOS data area lists it (0040:0008) */
#define LPT_IRQ 7                  /* the TSR's default (I7): vector 0Fh */
#define ARGS "L1 I7 T0 F3 S1"      /* SMARTALK.BAT's line for S_TALK, as MAKEIT.BAT writes it */
#define DATA_SEG_AT 0x538          /* CS:0538 holds the TSR's data segment */
#define QUEUED 0x1E2               /* in it: bytes of phonemes waiting (4 each) ... */
#define SPEAKING 0x1EC             /* ... and 1 while it speaks */
#define MARK_COUNT 0x2DA           /* ... and the index marks passed since its last status call or Ctrl-X: a word */
#define BANNER "SmarTalk Version 1.21"
#define TIMER_HZ (1193182.0 / 65536)
#define RET_LINEAR ((unsigned long)BIOS_SEG * 16 + RETURN_OFF)
#define NO_COUNT ((uint64_t)1 << 62)

static const char START_LINES[] = "{ smartalk ready\nUsing conventional memory.\n";   /* what ST_RUN prints */

struct st_host {
    pc86 *p;
    uint8_t *mem;
    ssi263 *chip;
    int own_chip;
    double out_rate, cpu_ips;
    /* the port, the box and the PIC */
    int data, control, last_ack;
    int pic_mask, in_service, irq_latched;                /* in_service: the IRQs in service, a bit each */
    double timer_due;
    double last_speech, say_time;
    int preparing;
    /* the program */
    int resident;                                         /* paragraphs once it has gone resident, else -1 */
    int load, data_seg;
    unsigned long dta;
    int current_psp;
    int stopped;                                          /* self.stopped: a reason, for this call */
    int fault;                                            /* something raised: the host is dead */
    char why[200];
    /* audio */
    double *pend, *buf;
    int n_pend, cap_pend, n_buf, cap_buf;
    long long insns;
    int log_on;
    sth_write *wl;
    int n_wl, cap_wl;
    /* index marks (sth_replies) */
    sth_reply rp[STH_REPLIES];
    int n_rp, rp_read;                                    /* rp_read: the list is a finished run's (the next mark
                                                             starts a new one) */
    int mark_seen, marks;                                 /* the TSR's count as last read; every mark passed */
};

/* ---- faults: what smartalk.py raises ------------------------------------------------------------------------------ */
static void raise_(st_host *h, const char *fmt, ...)
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
static void stop_(st_host *h, const char *fmt, ...)
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

static int r(st_host *h, int which)
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

static void w(st_host *h, int which, int value)
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

static int rd16(const st_host *h, unsigned long a) { return h->mem[a & 0xFFFFF] | h->mem[(a + 1) & 0xFFFFF] << 8; }
static void wr16(st_host *h, unsigned long a, int v)
{
    h->mem[a & 0xFFFFF] = (uint8_t)(v & 0xFF);
    h->mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8 & 0xFF);
}
static int wrmem(st_host *h, unsigned long a, const void *d, size_t n)
{
    if (a + n > PC86_MEM_SIZE) {
        raise_(h, "write outside 1 MB");
        return 0;
    }
    memcpy(h->mem + a, d, n);
    return 1;
}

static unsigned long here(st_host *h) { return (unsigned long)r(h, CS) * 16 + (unsigned long)r(h, IP); }

static void push(st_host *h, int v)
{
    int sp = (r(h, SP) - 2) & 0xFFFF;
    w(h, SP, sp);
    wr16(h, (unsigned long)r(h, SS) * 16 + (unsigned long)sp, v & 0xFFFF);
}

static void carry(st_host *h, int on)
{
    int fl = r(h, FL);
    w(h, FL, on ? (fl | 1) : (fl & ~1));
}

static void vector(st_host *h, int n, int *seg, int *off)
{
    *off = rd16(h, (unsigned long)n * 4);
    *seg = rd16(h, (unsigned long)n * 4 + 2);
}

/* _dispatch: what INT n does on a real CPU: push flags, CS, IP; clear IF, TF; jump */
static void dispatch(st_host *h, int n)
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
static void emu_start(st_host *h, uint32_t until, uint64_t count)
{
    uint32_t addr;
    uint8_t op;
    if (h->fault)
        return;
    pc86_run(h->p, count ? count : NO_COUNT, until);
    if (!h->fault && pc86_aliased(h->p, &addr, &op))
        raise_(h, "opcode %02Xh at %05X: its meaning differs on the 80186 and later; the 8086 core is not the CPU "
               "this program needs", op, (unsigned)addr);
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

static int word(const st_host *h, int off) { return rd16(h, (unsigned long)h->data_seg * 16 + (unsigned long)off); }

/* ---- interrupts: the TSR's own handlers run; BIOS and DOS are emulated --------------------------------------------- */
static void go_resident(st_host *h, int paras)
{
    h->resident = paras;
    pc86_stop(h->p);
}

/* _dos */
static void dos(st_host *h, int ah, int al)
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
    } else if (ah == 0x4C) {
        stop_(h, "program exited (code %d) without staying resident", al);
    } else
        stop_(h, "unimplemented DOS AH=%02X AL=%02X", ah, al);
}

/* _int.  pc86.py: a serviced interrupt (1). */
static int on_int(void *user, int n, int kind)
{
    st_host *h = (st_host *)user;
    int seg, off, ah, al;
    (void)kind;
    if (h->fault)
        return 1;
    vector(h, n, &seg, &off);
    if (seg != BIOS_SEG) {
        dispatch(h, n);                                 /* a vector the TSR installed */
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
    else if (n == 0x16) {                               /* keyboard: nothing typed; AH=1Ch, "is SmarTalk loaded?": no */
        if (ah == 0x01 || ah == 0x11)
            w(h, FL, r(h, FL) | 0x40);
        else if (ah == 0x02 || ah == 0x12)
            w(h, AX, r(h, AX) & 0xFF00);
    } else if (n == 0x10 || n == 0x14 || n == 0x15 || n == 0x17 || n == 0x1A || n == 0x1C || n == 0x28
               || n == 0x2A || n == 0x2F) {
        /* video, serial, AT services, printer, clock, tick, DOS idle, multiplex: nothing else answers */
    } else
        stop_(h, "unimplemented INT %02X AX=%04X", n, r(h, AX));
    return 1;
}

/* ---- the parallel port, the box and the PIC ------------------------------------------------------------------------ */
/* register(): the SSI-263 register that control bits 1-3 select (RS0 = NOT bit 1, RS1 = bit 2, RS2 = bit 3) */
static int select_reg(int control)
{
    if (control & 0x08)
        return 4;
    return (control & 0x04 ? 2 : 0) | (control & 0x02 ? 0 : 1);
}

/* status(): 379h: ACK = A/R, ERROR = INIT, SELECT high; BUSY and PAPER-OUT at a powered box's levels */
static int status(const st_host *h)
{
    return 0x07 | (ssi263_request(h->chip) ? 0x40 : 0) | 0x20 | 0x10 | (h->control & 0x04 ? 0x08 : 0);
}

static int on_in(void *user, int port)
{
    st_host *h = (st_host *)user;
    if (h->fault)
        return 0xFF;
    if (port == 0x21)
        return h->pic_mask;
    if (port == 0x60 || port == 0x61)
        return 0x00;
    if (port == LPT_BASE)
        return h->data;
    if (port == LPT_BASE + 1)
        return status(h);
    if (port == LPT_BASE + 2)
        return h->control | 0xE0;                       /* bits 5-7 read high on a plain port */
    stop_(h, "unimplemented IN %04X", port);
    return 0xFF;
}

static void on_out(void *user, int port, int value)
{
    st_host *h = (st_host *)user;
    if (h->fault)
        return;
    value &= 0xFF;
    if (port == 0x21)
        h->pic_mask = value;
    else if (port == 0x20) {
        if (value == 0x20 && h->in_service)
            h->in_service &= h->in_service - 1;         /* discard(min(in_service)) */
    } else if (port == 0x61 || port == 0x42 || port == 0x43) {
        /* the PC speaker and timer 2 (its beeps) */
    } else if (port == LPT_BASE)
        h->data = value;
    else if (port == LPT_BASE + 2) {
        int old = h->control;
        h->control = value;
        if ((old & 1) && !(value & 1)) {                /* /STROBE released: latch the data into the selected register */
            int reg = select_reg(old);
            ssi263_write(h->chip, reg, h->data);
            if (h->log_on && grow((void **)&h->wl, &h->cap_wl, h->n_wl + 1, sizeof *h->wl)) {
                h->wl[h->n_wl].t = ssi263_time(h->chip);
                h->wl[h->n_wl].reg = reg;
                h->wl[h->n_wl].val = h->data;
                h->n_wl++;
            }
            if (reg == 0 && (h->data & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80)) {
                h->last_speech = ssi263_time(h->chip);
                h->preparing = 0;
            }
        }
    } else
        stop_(h, "unimplemented OUT %04X <- %02X", port, value);
}

/* _try_irq: IRQ7 on ACK's rising edge while the port's interrupt is enabled; the timer at 18.2 Hz.  The PIC is fully
   nested: a request waits while one of its own or a higher priority is served. */
static int try_irq(st_host *h)
{
    int ack = ssi263_request(h->chip) != 0;
    if (ack && !h->last_ack && (h->control & 0x10))
        h->irq_latched = 1;
    h->last_ack = ack;
    if (!(r(h, FL) & 0x200))
        return 0;
    if (ssi263_time(h->chip) >= h->timer_due) {
        h->timer_due = ssi263_time(h->chip) + 1.0 / TIMER_HZ;
        if (!(h->pic_mask & 1) && !(h->in_service & 1)) {
            dispatch(h, 0x08);
            h->in_service |= 1;
            return 1;
        }
    }
    if (h->irq_latched && !(h->pic_mask & (1 << LPT_IRQ)) && !(h->in_service & ((2 << LPT_IRQ) - 1))) {
        dispatch(h, 0x08 + LPT_IRQ);
        h->in_service |= 1 << LPT_IRQ;
        h->irq_latched = 0;
        return 1;
    }
    return 0;
}

/* ---- audio -------------------------------------------------------------------------------------------------------- */
/* one slice of the chip: run_until_request(step) if it is not requesting, else run(step / 4); appended to *buf */
static int chip_slice(st_host *h, double step, double **buf, int *n, int *cap)
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

static int pending_slice(st_host *h, double step)
{
    if (!chip_slice(h, step, &h->pend, &h->n_pend, &h->cap_pend)) {
        raise_(h, "out of memory");
        return 0;
    }
    return 1;
}

static uint64_t steps_for(st_host *h, double dt)
{
    long long n = (long long)(h->cpu_ips * dt);         /* int() */
    return (uint64_t)(n > 100 ? n : 100);               /* max(100, ...) */
}

static double dt_since(st_host *h, double before)
{
    double dt = ssi263_time(h->chip) - before;
    return dt > 1e-5 ? dt : 1e-5;                       /* max(..., 1e-5) */
}

/* ---- loading an .EXE as COMMAND.COM would: run until it stays resident; its paragraphs, or -1 ------------------- */
static int exec_exe(st_host *h, const unsigned char *data, size_t n, int psp, const char *args, char *err, int errlen)
{
    uint8_t p[256];
    uint16_t hd[14];
    char env[64];
    int i, k, load, tail_len;
    size_t header, image_len;
    double waited = 0.0;
    if (n < 28 || data[0] != 'M' || data[1] != 'Z') {
        snprintf(err, (size_t)errlen, "S_TALK.EXE: expected an MZ executable");
        return -1;
    }
    for (i = 0; i < 14; i++)
        hd[i] = (uint16_t)(data[2 * i] | data[2 * i + 1] << 8);
    load = psp + 0x10;
    header = (size_t)hd[4] * 16;
    if (header > n) {
        snprintf(err, (size_t)errlen, "S_TALK.EXE: its header is longer than the file");
        return -1;
    }
    image_len = n - header;
    if ((unsigned long)load * 16 + image_len > (unsigned long)STACK_SEG * 16) {
        snprintf(err, (size_t)errlen, "S_TALK.EXE is too large");
        return -1;
    }
    /* the image, then its relocations: each word at seg:off plus the load segment */
    if (!wrmem(h, (unsigned long)load * 16, data + header, image_len))
        return -1;
    for (i = 0; i < hd[3]; i++) {
        size_t at = (size_t)hd[12] + (size_t)i * 4;
        unsigned long a;
        if (at + 4 > n) {
            snprintf(err, (size_t)errlen, "S_TALK.EXE: a relocation outside the file");
            return -1;
        }
        a = (unsigned long)(data[at + 2] | data[at + 3] << 8) * 16 + (unsigned long)(data[at] | data[at + 1] << 8);
        if (a + 2 > image_len) {
            snprintf(err, (size_t)errlen, "S_TALK.EXE: a relocation outside its image");
            return -1;
        }
        wr16(h, (unsigned long)load * 16 + a, (rd16(h, (unsigned long)load * 16 + a) + load) & 0xFFFF);
    }
    /* the PSP: INT 20h, the memory top, the environment, the command tail (" " + args, then "\r") */
    memset(p, 0, sizeof p);
    p[0] = 0xCD;
    p[1] = 0x20;
    p[2] = 0x00;                                        /* first paragraph beyond the program: A000 */
    p[3] = 0xA0;
    p[0x2C] = ENV_SEG & 0xFF;
    p[0x2D] = ENV_SEG >> 8;
    tail_len = args && *args ? 1 + (int)strlen(args) : 0;
    if (tail_len > 126)
        tail_len = 126;
    p[0x80] = (uint8_t)tail_len;
    if (tail_len) {
        p[0x81] = ' ';
        memcpy(p + 0x82, args, (size_t)tail_len - 1);
    }
    p[0x81 + tail_len] = '\r';
    wrmem(h, (unsigned long)psp * 16, p, sizeof p);
    /* the emulated DOS's drive letter passed in (no host path in the source) */
    k = snprintf(env, sizeof env, "COMSPEC=%c:\\COMMAND.COM%c%c%c%c%c:\\SMARTALK\\S_TALK.EXE", 'C', 0, 0, 1, 0, 'C');
    wrmem(h, (unsigned long)ENV_SEG * 16, env, (size_t)k + 1);
    w(h, DS, psp);
    w(h, ES, psp);
    w(h, SS, load + hd[7]);
    w(h, SP, hd[8]);
    w(h, CS, load + hd[11]);
    w(h, IP, hd[10]);
    h->load = load;
    h->dta = (unsigned long)psp * 16 + 0x80;
    h->current_psp = psp;
    h->resident = -1;
    h->stopped = 0;
    /* the power-up talks to the box for over two seconds, timed by the timer: let the chip and the PC's interrupts
       run beside it, silently */
    emu_start(h, 0, 1000000);
    while (h->resident < 0 && !h->stopped && !h->fault && waited < 10.0) {
        double before, dt;
        try_irq(h);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, 0.0005);
        dt = dt_since(h, before);
        emu_start(h, 0, steps_for(h, dt));
        waited += dt;
    }
    if (h->resident < 0) {
        snprintf(err, (size_t)errlen, "S_TALK.EXE did not go resident: %s",
                 h->fault || h->stopped ? h->why : "ran out of time");
        return -1;
    }
    return h->resident;
}

/* ---- driving the TSR ----------------------------------------------------------------------------------------------- */
/* _cpu */
/* the TSR's own count of marks passed, read after a slice: those new since the last look, at `sample` of the audio
   being made (the pending audio's or the run's: one buffer follows the other) */
static void marks_poll(st_host *h, int sample)
{
    int count, fresh;
    if (h->data_seg <= 0)
        return;
    count = word(h, MARK_COUNT);
    if (count < h->mark_seen)                           /* zeroed (a Ctrl-X): it counts from nothing again */
        h->mark_seen = 0;
    fresh = count - h->mark_seen;
    h->mark_seen = count;
    if (!fresh)
        return;
    if (h->rp_read) { h->n_rp = 0; h->rp_read = 0; }
    for (; fresh > 0; fresh--) {
        h->marks++;
        if (h->n_rp < STH_REPLIES)
            h->rp[h->n_rp++].sample = sample;
    }
}

static int cpu(st_host *h, uint64_t count)
{
    emu_start(h, 0, count);                             /* until 0: pc86.py passes the linear address 0 */
    h->insns += (long long)count;
    if (h->stopped)
        h->fault = 1;
    return h->fault ? -1 : 0;
}

/* _far_call: far-call seg:off from the host; run until it returns, with the box running if it waits (the TSR keeps
   the CPU while its buffer is full) */
static int far_call(st_host *h, int seg, int off, uint64_t budget, double wait_limit, double step)
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
        marks_poll(h, h->n_pend);                       /* (a mark passed while a print call waits) */
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

/* _settle: let an interrupt handler a CPU slice stopped inside finish before calling the TSR */
static int settle(st_host *h, uint64_t budget)
{
    uint64_t n = 0;
    while (r(h, CS) != IDLE_SEG && n < budget) {
        if (cpu(h, 1000) < 0)
            return -1;
        n += 1000;
    }
    return 0;
}

static void say_(st_host *h, const unsigned char *bytes, int n, int speech)
{
    int k;
    if (h->fault || n <= 0)
        return;
    if (settle(h, 400000) < 0)
        return;
    if (speech) {
        h->preparing = 1;
        h->say_time = ssi263_time(h->chip);
    }
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

ST_API void sth_say(st_host *h, const unsigned char *bytes, int n)
{
    int i, speech = 0;
    for (i = 0; i < n && !speech; i++)                  /* any(ch.isalnum() for ch in text) */
        speech = bytes[i] >= 0x20 && nw_isalnum(bytes[i]);
    say_(h, bytes, n, speech);
}

ST_API int sth_run(st_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0, *swap;
    int cap;
    *audio = h->buf;
    if (h->fault)
        return -1;
    /* out = self.pending: the run's audio follows what the blocked calls made */
    if (h->rp_read) { h->n_rp = 0; h->rp_read = 0; }    /* (what a blocked call passed stays: its audio comes first) */
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
        marks_poll(h, h->n_buf);
        t += dt;
    }
    h->rp_read = 1;
    *audio = h->buf;
    return h->n_buf;
}

ST_API int sth_replies(const st_host *h, const sth_reply **out)
{
    *out = h->rp;
    return h->rp_read ? h->n_rp : 0;
}

ST_API double sth_skip(st_host *h, double seconds, double step)
{
    double t = 0.0;
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
        marks_poll(h, 0);                               /* (no audio is made here: counted, at the next run's start) */
        t += dt;
    }
    return t;
}

ST_API int sth_queued(const st_host *h) { return word(h, QUEUED); }
ST_API int sth_input_pending(const st_host *h) { return word(h, QUEUED) != 0; }
ST_API int sth_speaking(const st_host *h) { return word(h, SPEAKING) != 0; }

ST_API int sth_busy(const st_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    if (sth_input_pending(h) || sth_speaking(h) || (now - h->last_speech) < quiet)
        return 1;
    return h->preparing && (now - h->say_time) < patience;
}

/* boot: ST_RUN's part -- the "{" that lets the rules speak, in its own two lines, played out silently */
ST_API void sth_boot(st_host *h, double limit)
{
    double t, s;
    sth_say(h, (const unsigned char *)START_LINES, (int)strlen(START_LINES));
    t = sth_skip(h, 0.05, 0.002);
    while (t >= 0 && t < limit && sth_busy(h, 0.05, 1.5)) {
        if ((s = sth_skip(h, 0.05, 0.002)) < 0)
            return;
        t += s;
    }
}

/* cancel: Ctrl-X, the shut-up command: it silences speech and empties the buffer at once */
ST_API void sth_cancel(st_host *h, double limit)
{
    static const unsigned char CTRL_X[] = {0x18};
    double t, s;
    if (h->fault)
        return;
    h->preparing = 0;
    say_(h, CTRL_X, 1, 0);
    t = sth_skip(h, 0.01, 0.002);
    while (t >= 0 && t < limit && sth_busy(h, 0.05, 1.5)) {
        if ((s = sth_skip(h, 0.01, 0.002)) < 0)
            return;
        t += s;
    }
    h->n_rp = 0;                                /* the cancelled text's marks: none is given */
    h->rp_read = 0;
    h->mark_seen = word(h, MARK_COUNT);
}

ST_API int sth_fault(const st_host *h) { return h->fault; }
ST_API const char *sth_error(const st_host *h) { return h->fault ? h->why : ""; }
ST_API ssi263 *sth_chip(st_host *h) { return h->chip; }

/* ---- creation: the BIOS, "DOS", the port; S_TALK.EXE until it stays resident ------------------------------------ */
static int has_banner(const unsigned char *d, size_t n)
{
    size_t k, m = sizeof BANNER - 1;
    for (k = 0; k + m <= n; k++)
        if (d[k] == 'S' && !memcmp(d + k, BANNER, m))
            return 1;
    return 0;
}

ST_API st_host *sth_create(const unsigned char *exe, size_t n, ssi263 *chip, double out_rate, char *err, int errlen)
{
    /* mov al,[si]; mov ah,0; mov dx,0 (LPT1); push cx; push si; push ds; int 17h; pop ds; pop si; pop cx; inc si;
       loop; retf */
    static const uint8_t client[19] = {0x8A, 0x04, 0xB4, 0x00, 0xBA, 0x00, 0x00, 0x51, 0x56, 0x1E, 0xCD, 0x17, 0x1F,
                                       0x5E, 0x59, 0x46, 0xE2, 0xEE, 0xCB};
    static const uint8_t eoi[7] = {0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF};       /* push ax; EOI; pop ax; iret */
    /* push ax; push ds; xor ax,ax; mov ds,ax; inc word [46Ch]; jnz $+6; inc word [46Eh]; int 1Ch; mov al,20h;
       out 20h,al; pop ds; pop ax; iret */
    static const uint8_t timer[25] = {0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04, 0x75, 0x04, 0xFF,
                                      0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20, 0xE6, 0x20, 0x1F, 0x58, 0xCF};
    static const uint8_t dos_code[6] = {0xFB, 0xCD, 0xE1, 0xCA, 0x02, 0x00};       /* sti; int E1h; retf 2 */
    static const uint8_t idle[3] = {0xFB, 0xEB, 0xFE};
    char e[200];
    st_host *h;
    uint8_t *m;
    int v, paras;
    if (!err || errlen <= 0) {
        err = e;
        errlen = (int)sizeof e;
    }
    if (!exe || n < 64) {
        snprintf(err, (size_t)errlen, "the SmarTalk's S_TALK.EXE is needed");
        return NULL;
    }
    if (!has_banner(exe, n)) {
        snprintf(err, (size_t)errlen, "not S_TALK.EXE version 1.21 (no \"" BANNER "\" in it)");
        return NULL;
    }
    h = (st_host *)calloc(1, sizeof *h);
    if (!h) {
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
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
    h->cpu_ips = STH_CPU_IPS;
    h->mem = m = pc86_mem(h->p);
    /* BIOS: every vector an IRET, hardware IRQs EOI, LPT1 at 378h, 640 KB, the far-call trap */
    m[(unsigned long)BIOS_SEG * 16 + IRET_OFF] = 0xCF;
    m[(unsigned long)BIOS_SEG * 16 + RETURN_OFF] = 0xF4;
    memcpy(m + (unsigned long)BIOS_SEG * 16 + IRQ_EOI_OFF, eoi, sizeof eoi);
    for (v = 0; v < 256; v++) {
        wr16(h, (unsigned long)v * 4, v >= 0x08 && v <= 0x0F ? IRQ_EOI_OFF : IRET_OFF);
        wr16(h, (unsigned long)v * 4 + 2, BIOS_SEG);
    }
    memcpy(m + (unsigned long)BIOS_SEG * 16 + TIMER_OFF, timer, sizeof timer);
    wr16(h, 0x08 * 4, TIMER_OFF);
    memcpy(m + (unsigned long)BIOS_SEG * 16 + DOS_OFF, dos_code, sizeof dos_code);
    wr16(h, 0x21 * 4, DOS_OFF);
    m[0xFFFFE] = 0xFC;                                  /* model byte: AT */
    wr16(h, 0x408, LPT_BASE);
    wr16(h, 0x40A, 0);
    wr16(h, 0x40C, 0);
    wr16(h, 0x410, 0x4000);                             /* equipment word: one printer port */
    wr16(h, 0x413, 640);
    memcpy(m + (unsigned long)IDLE_SEG * 16, idle, sizeof idle);
    memcpy(m + (unsigned long)CLIENT_SEG * 16, client, sizeof client);
    /* the port, the box and the PIC */
    h->pic_mask = 0xB8;                                 /* as the BIOS leaves it: the timer, keyboard, IRQ2, floppy */
    h->last_speech = -1.0;
    h->resident = -1;
    pc86_set_hooks(h->p, on_in, on_out, on_int, h);
    paras = exec_exe(h, exe, n, PSP_SEG, ARGS, err, errlen);
    if (paras <= 0) {
        if (paras == 0)
            snprintf(err, (size_t)errlen, "S_TALK.EXE kept no memory resident");
        sth_destroy(h);
        return NULL;
    }
    h->data_seg = rd16(h, (unsigned long)h->load * 16 + DATA_SEG_AT);
    h->n_pend = 0;
    w(h, CS, IDLE_SEG);
    w(h, IP, 0);
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    return h;
}

ST_API void sth_destroy(st_host *h)
{
    if (!h)
        return;
    pc86_destroy(h->p);
    if (h->own_chip)
        ssi263_free(h->chip);
    free(h->pend);
    free(h->buf);
    free(h->wl);
    free(h);
}

/* ---- state ------------------------------------------------------------------------------------------------------- */
ST_API double sth_get_double(const st_host *h, const char *name)
{
    if (!strcmp(name, "cpu_ips")) return h->cpu_ips;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

ST_API void sth_set_double(st_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_ips")) h->cpu_ips = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
}

ST_API int sth_get_int(const st_host *h, const char *name)
{
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "marks")) return h->marks;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    if (!strcmp(name, "control")) return h->control;
    return -1;
}

ST_API void sth_set_int(st_host *h, const char *name, int v)
{
    if (!strcmp(name, "preparing"))
        h->preparing = v != 0;
    else if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
}

ST_API int sth_writes(const st_host *h, const sth_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

ST_API void sth_clear_writes(st_host *h) { h->n_wl = 0; }
