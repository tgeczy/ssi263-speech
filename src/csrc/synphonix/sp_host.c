/* sp_host.c -- src/hosts/synphonix.py's SynPhonix 215 in C, line for line (sp_host.h).  Each part names the Python it
 * ports; every comparison, order of operations, rounding and register access is Python's (a register write is a read
 * of all of them and a write back, as pc86.py's reg_write).  Change them together.  The SmarTalk's host
 * (../smartalk/st_host.c) is the same bare PC for another DOS TSR.  MIT.
 */
#include "sp_host.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc86.h"

#define IDLE_SEG 0x0050            /* 0050:0000 STI; JMP $ -- "DOS" between interrupts */
#define ENV_SEG 0x0068             /* an environment block: COMSPEC, then the program's path */
#define TEXT_SEG 0x0070            /* the caller's text buffer (16 KB) */
#define PSP_SEG 0x0800             /* VEST's PSP; the .COM loads at PSP:0100 */
#define STACK_SEG 0x9000           /* the host's own stack, for far calls */
#define BIOS_SEG 0xF000
#define IRET_OFF 0xFF53            /* F000:FF53, where every unclaimed vector points */
#define RETURN_OFF 0xFF00          /* F000:FF00: far calls into the TSR return here (HLT) */
#define IRQ_EOI_OFF 0xFF60         /* F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET */
#define TIMER_OFF 0xFF70           /* F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI */
#define KEY_BASE 0x2E0             /* the first ISA address VEST tries (A00C): the key here, the chip 8 above */
#define CHIP_BASE (KEY_BASE + 8)
#define KEY_LEN 6
#define CARD_IRQ 7                 /* VEST without /I= (BV.BAT) */
#define TIMER_HZ (1193182.0 / 65536)
#define ENTRY_AT 0x1E4             /* in VEST's segment: the entry Business Vision calls ... */
#define QUEUE_IN 0x190             /* ... the frame queue's write and read pointers (5 bytes a frame, AC1 and D56) ... */
#define QUEUE_OUT 0x192
#define QUEUE_START 0x194          /* ... the queue's ring: its first byte and its end (set at load from /B; the queue */
#define QUEUE_END 0x196            /* routines at 0AC1 and 0D5E wrap there) */
#define IDLE_FLAG 0x16D            /* ... and 1 once the IRQ handler has found the queue empty (0BAB) */
#define TEXT_MAX 0x4000
#define RET_LINEAR ((unsigned long)BIOS_SEG * 16 + RETURN_OFF)
#define NO_COUNT ((uint64_t)1 << 62)
#define SCREEN_MAX 4096

/* what Business Vision looks for (BVISION 9B67): words at these offsets, on a paragraph boundary */
static const int SIG[3][2] = {{0x1E0, 0x0038}, {0x1E2, 0x1D2D}, {0x12A, 0x7A83}};

struct sp_host {
    pc86 *p;
    uint8_t *mem;
    ssi263 *chip;
    int own_chip;
    double out_rate, cpu_ips;
    unsigned char key[KEY_LEN];
    int brk_rotate, brk_eoi, brk_cancel;
    /* the card and the PIC */
    int pic_mask, in_service, irq_latched, last_req;     /* in_service: the IRQs in service, a bit each */
    double timer_due;
    double last_speech;
    /* the program */
    long resident;                                        /* bytes once it has gone resident, else -1 */
    int seg, entry;
    int stopped;                                          /* self.stopped: a reason, for this call */
    int fault;                                            /* something raised: the host is dead */
    char why[300];
    char screen[SCREEN_MAX];
    int n_screen;
    /* audio */
    double *pend, *buf;
    int n_pend, cap_pend, n_buf, cap_buf;
    long long insns;
    int log_on;
    int fail_cancel;                                      /* tests: the next cancel raises (sph_set_int) */
    sph_write *wl;
    int n_wl, cap_wl;
};

/* ---- faults: what synphonix.py raises ----------------------------------------------------------------------------- */
static void raise_(sp_host *h, const char *fmt, ...)
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
static void stop_(sp_host *h, const char *fmt, ...)
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

static int r(const sp_host *h, int which)
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

static void w(sp_host *h, int which, int value)
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

static int rd16(const sp_host *h, unsigned long a) { return h->mem[a & 0xFFFFF] | h->mem[(a + 1) & 0xFFFFF] << 8; }
static void wr16(sp_host *h, unsigned long a, int v)
{
    h->mem[a & 0xFFFFF] = (uint8_t)(v & 0xFF);
    h->mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8 & 0xFF);
}
static int wrmem(sp_host *h, unsigned long a, const void *d, size_t n)
{
    if (a + n > PC86_MEM_SIZE) {
        raise_(h, "write outside 1 MB");
        return 0;
    }
    memcpy(h->mem + a, d, n);
    return 1;
}

static unsigned long here(const sp_host *h) { return (unsigned long)r(h, CS) * 16 + (unsigned long)r(h, IP); }

static void push(sp_host *h, int v)
{
    int sp = (r(h, SP) - 2) & 0xFFFF;
    w(h, SP, sp);
    wr16(h, (unsigned long)r(h, SS) * 16 + (unsigned long)sp, v & 0xFFFF);
}

static void carry(sp_host *h, int on)
{
    int fl = r(h, FL);
    w(h, FL, on ? (fl | 1) : (fl & ~1));
}

static void vector(const sp_host *h, int n, int *seg, int *off)
{
    *off = rd16(h, (unsigned long)n * 4);
    *seg = rd16(h, (unsigned long)n * 4 + 2);
}

/* _dispatch: what INT n does on a real CPU: push flags, CS, IP; clear IF, TF; jump */
static void dispatch(sp_host *h, int n)
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
static void emu_start(sp_host *h, uint32_t until, uint64_t count)
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

/* word(), byte(): VEST's data (its own segment) */
static int vword(const sp_host *h, int off) { return rd16(h, (unsigned long)h->seg * 16 + (unsigned long)off); }
static int vbyte(const sp_host *h, int off) { return h->mem[((unsigned long)h->seg * 16 + (unsigned long)off) & 0xFFFFF]; }

/* the screen: VEST's banner and messages (self.screen) */
static void screen_put(sp_host *h, int ch)
{
    if (h->n_screen < SCREEN_MAX - 1)
        h->screen[h->n_screen++] = (char)ch;
    h->screen[h->n_screen] = 0;
}

/* " ".join(text.split()): the screen's words, one space apart */
static void screen_words(const sp_host *h, char *out, size_t len)
{
    size_t k = 0;
    int i, gap = 0;
    for (i = 0; i < h->n_screen && k + 1 < len; i++) {
        unsigned char c = (unsigned char)h->screen[i];
        if (c == ' ' || (c >= 9 && c <= 13) || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F) {
            gap = k > 0;
            continue;
        }
        if (gap && k + 2 < len)
            out[k++] = ' ';
        gap = 0;
        out[k++] = (char)c;
    }
    out[k] = 0;
}

/* ---- the card's key: synphonix.py _find_serial and card_key ------------------------------------------------------ */
/* the serial check's code, VEST 1.60 at 9657: MOV SI,serial; MOV DI,buffer; CMP BYTE [SI],0; JZ; MOV AX,[SI];
   SUB AL,A7h; SUB AH,A7h -- each part bytes to match, or a count to skip */
static long find_serial(const unsigned char *image, size_t n)
{
    static const unsigned char P2[4] = {0x80, 0x3C, 0x00, 0x74};
    static const unsigned char P3[7] = {0x8B, 0x04, 0x2C, 0xA7, 0x80, 0xEC, 0xA7};
    size_t at;
    for (at = 0; at + 24 < n; at++) {
        size_t p = at;
        if (image[p] != 0xBE)
            continue;
        p += 1 + 2;
        if (image[p] != 0xBF)
            continue;
        p += 1 + 2;
        if (memcmp(image + p, P2, sizeof P2))
            continue;
        p += sizeof P2 + 1;
        if (p + sizeof P3 > n || memcmp(image + p, P3, sizeof P3))
            continue;
        return (long)(image[at + 1] | image[at + 2] << 8) - 0x100;
    }
    return -1;
}

static int card_key(const unsigned char *image, size_t n, int brk_key, unsigned char key[KEY_LEN], char *err,
                    int errlen)
{
    int raw[8], want[8], pairs[8], k = 0, i;
    long at = find_serial(image, n);
    if (at < 0) {
        snprintf(err, (size_t)errlen, "not VEST 1.60: the serial check's code is not there");
        return -1;
    }
    while (at >= 0 && (size_t)at + 1 < n && image[at]) {
        if (k + 2 > 8) {
            k = 9;
            break;
        }
        raw[k++] = (image[at + 1] - 0xA7 - 0x30) & 0xFF;            /* ASCII digits */
        raw[k++] = (image[at] - 0xA7 - 0x30) & 0xFF;
        at += 2;
    }
    for (i = 0; i < k && k == 8; i++)
        if (raw[i] > 9)
            k = 9;
    if (k != 8) {
        snprintf(err, (size_t)errlen, "VEST's serial is not eight digits");
        return -1;
    }
    for (i = 0; i < 4; i++) {                                         /* the four digit pairs, reversed */
        pairs[2 * i] = raw[2 * (3 - i)];
        pairs[2 * i + 1] = raw[2 * (3 - i) + 1];
    }
    want[0] = 2;                                                      /* 25 then the last six */
    want[1] = 5;
    for (i = 2; i < 8; i++)
        want[i] = pairs[i];
    if (brk_key)
        want[7] = (want[7] + 1) % 10;
    key[0] = 0x41;
    key[1] = 0x52;
    for (i = 0; i < 4; i++) {                                         /* byte i gives the pair that ends up (3 - i)th */
        int lo = want[2 * (3 - i)], hi = want[2 * (3 - i) + 1];
        key[2 + i] = (unsigned char)(((hi << 4) | lo) ^ 0xFF);
    }
    return 0;
}

SP_API int sph_card_key(const unsigned char *com, size_t n, unsigned char key[6], char *err, int errlen)
{
    char e[200];
    const char *b = getenv("SYNPHONIX_HOST_BREAK");
    if (!err || errlen <= 0) {
        err = e;
        errlen = (int)sizeof e;
    }
    return card_key(com, n, b && !strcmp(b, "key"), key, err, errlen);
}

/* ---- interrupts: the TSR's own handlers run; BIOS and DOS are emulated --------------------------------------------- */
static void go_resident(sp_host *h, long bytes)
{
    h->resident = bytes;
    pc86_stop(h->p);
}

/* _dos */
static void dos(sp_host *h, int ah, int al)
{
    carry(h, 0);
    if (ah == 0x09) {                                   /* print DS:DX up to "$" (at most 400 bytes) */
        unsigned long a = (unsigned long)r(h, DS) * 16 + (unsigned long)r(h, DX);
        int i;
        for (i = 0; i < 400; i++) {
            int c = h->mem[(a + (unsigned long)i) & 0xFFFFF];
            if (c == '$')
                break;
            screen_put(h, c < 0x80 ? c : '?');
        }
    } else if (ah == 0x02) {
        screen_put(h, r(h, DX) & 0x7F);
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
        go_resident(h, (long)r(h, DX) * 16);
    } else if (ah == 0x51 || ah == 0x62) {
        w(h, BX, PSP_SEG);
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
    sp_host *h = (sp_host *)user;
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
    if (n == 0x21)
        dos(h, ah, al);
    else if (n == 0x27)                                 /* terminate and stay resident, DX bytes */
        go_resident(h, r(h, DX));
    else if (n == 0x20) {
        char words[SCREEN_MAX];
        screen_words(h, words, sizeof words);
        stop_(h, "program ended without staying resident: %s", words);
    } else if (n == 0x10) {                             /* video: its banner and messages, by teletype */
        if (ah == 0x0E)
            screen_put(h, al < 0x80 ? al : ' ');
        else if (ah == 0x0F) {
            w(h, AX, 0x5003);                           /* 80 columns, mode 3 */
            w(h, BX, r(h, BX) & 0x00FF);
        }
    } else if (n == 0x15) {                             /* AT services: not a PS/2 (no POS registers: C400h, C401h) */
        if (ah == 0xC4) {
            w(h, AX, (0x86 << 8) | al);
            carry(h, 1);
        }
    } else if (n == 0x16 || n == 0x1A || n == 0x1C || n == 0x28 || n == 0x2F) {
        /* keyboard, clock, tick, DOS idle, multiplex: nothing answers */
    } else
        stop_(h, "unimplemented INT %02X AX=%04X", n, r(h, AX));
    return 1;
}

/* ---- the card and the PIC ------------------------------------------------------------------------------------------ */
/* ror1: the chip's byte from the PC's: VEST rotated it left one bit for the card's wiring */
static int ror1(int v) { return ((v >> 1) | (v << 7)) & 0xFF; }

static int on_in(void *user, int port)
{
    sp_host *h = (sp_host *)user;
    if (h->fault)
        return 0xFF;
    if (port == 0x21)
        return h->pic_mask;
    if (port >= KEY_BASE && port < KEY_BASE + KEY_LEN)
        return h->key[port - KEY_BASE];
    if (port == CHIP_BASE)
        return 0xFF;                                    /* the pedals: none pressed (a GUESS) */
    return 0xFF;                                        /* VEST's probes of other cards' places: nothing there */
}

static void on_out(void *user, int port, int value)
{
    sp_host *h = (sp_host *)user;
    if (h->fault)
        return;
    value &= 0xFF;
    if (port == 0x21)
        h->pic_mask = value;
    else if (port == 0x20) {
        if (value == 0x20 && h->in_service)             /* non-specific EOI: the highest priority in service */
            h->in_service &= h->in_service - 1;         /* discard(min(in_service)) */
        else if ((value & 0xF8) == 0x60 && !h->brk_eoi) /* specific EOI (VEST's 0C04: 60h + its IRQ) */
            h->in_service &= ~(1 << (value & 7));
    } else if (port >= CHIP_BASE && port <= CHIP_BASE + 4) {
        int reg = port - CHIP_BASE;
        int v = h->brk_rotate ? value : ror1(value);
        ssi263_write(h->chip, reg, v);
        if (h->log_on && grow((void **)&h->wl, &h->cap_wl, h->n_wl + 1, sizeof *h->wl)) {
            h->wl[h->n_wl].t = ssi263_time(h->chip);
            h->wl[h->n_wl].reg = reg;
            h->wl[h->n_wl].val = v;
            h->n_wl++;
        }
        if (reg == 0 && (v & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80))
            h->last_speech = ssi263_time(h->chip);
    } else
        stop_(h, "unimplemented OUT %04X <- %02X", port, value);
}

/* _try_irq: IRQ 7 on A/R's rising edge (an ISA line, edge-triggered); the timer at 18.2 Hz.  The PIC is fully nested:
   a request waits while one of its own or a higher priority is served. */
static int try_irq(sp_host *h)
{
    int req = ssi263_request(h->chip) != 0;
    if (req && !h->last_req)
        h->irq_latched = 1;
    h->last_req = req;
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
static int chip_slice(sp_host *h, double step, double **buf, int *n, int *cap)
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

static uint64_t steps_for(const sp_host *h, double dt)
{
    long long n = (long long)(h->cpu_ips * dt);         /* int() */
    return (uint64_t)(n > 100 ? n : 100);               /* max(100, ...) */
}

static double dt_since(const sp_host *h, double before)
{
    double dt = ssi263_time(h->chip) - before;
    return dt > 1e-5 ? dt : 1e-5;                       /* max(..., 1e-5) */
}

/* ---- loading a .COM as COMMAND.COM would: _exec -------------------------------------------------------------------- */
static int exec_com(sp_host *h, const unsigned char *image, size_t n, char *err, int errlen)
{
    char env[80];
    uint8_t p[256];
    double waited = 0.0;
    int k;
    if ((unsigned long)PSP_SEG * 16 + 0x100 + n > (unsigned long)STACK_SEG * 16) {
        snprintf(err, (size_t)errlen, "VEST.COM is too large");
        return -1;
    }
    memset(p, 0, sizeof p);
    p[0] = 0xCD;
    p[1] = 0x20;
    p[2] = 0x00;                                        /* first paragraph beyond the program: A000 */
    p[3] = 0xA0;
    p[0x2C] = ENV_SEG & 0xFF;
    p[0x2D] = ENV_SEG >> 8;
    p[0x81] = 0x0D;                                     /* no switches (BV.BAT) */
    wrmem(h, (unsigned long)PSP_SEG * 16, p, sizeof p);
    wrmem(h, (unsigned long)PSP_SEG * 16 + 0x100, image, n);
    /* the emulated DOS's drive letter passed in (no host path in the source), as st_host.c */
    k = snprintf(env, sizeof env, "COMSPEC=%c:\\COMMAND.COM%c%c%c%c%c:\\ARTIC\\VISION\\VEST.COM", 'C', 0, 0, 1, 0, 'C');
    wrmem(h, (unsigned long)ENV_SEG * 16, env, (size_t)k + 1);
    w(h, CS, PSP_SEG);
    w(h, DS, PSP_SEG);
    w(h, ES, PSP_SEG);
    w(h, SS, PSP_SEG);
    w(h, SP, 0xFFFE);
    wr16(h, (unsigned long)PSP_SEG * 16 + 0xFFFE, 0);   /* a RET from the program ends it (INT 20h at PSP:0) */
    w(h, IP, 0x100);
    /* VEST starts the chip and speaks its announcement before it goes resident, timing its wait for the card's
       interrupts by the timer (9717): the chip and the PC's interrupts run beside it, silently */
    while (h->resident < 0 && !h->stopped && !h->fault && waited < 20.0) {
        double before, dt;
        try_irq(h);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, 0.0005);
        dt = dt_since(h, before);
        emu_start(h, 0, steps_for(h, dt));
        waited += dt;
    }
    if (h->resident < 0) {
        snprintf(err, (size_t)errlen, "VEST.COM did not go resident: %s",
                 h->fault || h->stopped ? h->why : "ran out of time");
        return -1;
    }
    if (strstr(h->screen, "ERROR")) {
        char words[SCREEN_MAX];
        screen_words(h, words, sizeof words);
        snprintf(err, (size_t)errlen, "VEST refused the card: %s", words);
        return -1;
    }
    return 0;
}

/* _find_vest: Business Vision's search (BVISION 9B67): VEST's signature on a paragraph boundary, then its entry */
static int find_vest(sp_host *h)
{
    int seg, k;
    for (seg = 0x100; seg < 0xFF00; seg++) {
        for (k = 0; k < 3; k++)
            if (rd16(h, (unsigned long)seg * 16 + (unsigned long)SIG[k][0]) != SIG[k][1])
                break;
        if (k == 3) {
            h->seg = seg;
            h->entry = rd16(h, (unsigned long)seg * 16 + ENTRY_AT);
            return 0;
        }
    }
    return -1;
}

/* ---- driving the TSR ----------------------------------------------------------------------------------------------- */
/* _cpu */
static int cpu(sp_host *h, uint64_t count)
{
    emu_start(h, 0, count);                             /* until 0: pc86.py passes the linear address 0 */
    h->insns += (long long)count;
    if (h->stopped)
        h->fault = 1;
    return h->fault ? -1 : 0;
}

/* _settle: let an interrupt handler a CPU slice stopped inside finish before calling the TSR */
static int settle(sp_host *h, uint64_t budget)
{
    uint64_t n = 0;
    while (r(h, CS) != IDLE_SEG && n < budget) {
        if (cpu(h, 1000) < 0)
            return -1;
        n += 1000;
    }
    return 0;
}

/* call: PUSHF; CALL FAR VEST's entry with these registers (Business Vision's 5197); run until it returns, with the
   card running if VEST waits for room in its queue.  Returns AX, or -1. */
static int call_(sp_host *h, int ax, const int *regs, const int *vals, int nregs)
{
    const uint64_t budget = 5000000;
    const double wait_limit = 30.0, step = 0.0005;
    double waited = 0.0;
    int i;
    if (h->fault || settle(h, 400000) < 0)
        return -1;
    h->stopped = 0;
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    push(h, r(h, FL) | 0x200);
    push(h, BIOS_SEG);
    push(h, RETURN_OFF);
    w(h, AX, ax);
    for (i = 0; i < nregs; i++)
        w(h, regs[i], vals[i]);
    w(h, CS, h->seg);
    w(h, IP, h->entry);
    emu_start(h, (uint32_t)RET_LINEAR, budget);
    h->n_pend = 0;                                      /* self.pending = [] */
    while (!h->stopped && !h->fault && here(h) != RET_LINEAR) {
        double before, dt;
        if (waited > wait_limit) {
            stop_(h, "call AX=%04X still waiting after %.0f s", ax, waited);
            break;
        }
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!chip_slice(h, step, &h->pend, &h->n_pend, &h->cap_pend)) {
            raise_(h, "out of memory");
            return -1;
        }
        dt = dt_since(h, before);
        emu_start(h, (uint32_t)RET_LINEAR, steps_for(h, dt));
        waited += dt;
    }
    if (h->stopped)
        h->fault = 1;
    if (h->fault)
        return -1;
    ax = r(h, AX);
    w(h, CS, IDLE_SEG);
    w(h, IP, 0);
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    return ax;
}

SP_API int sph_call(sp_host *h, int ax, int bx, int cx, int dx)
{
    static const int REGS[3] = {BX, CX, DX};
    int vals[3];
    vals[0] = bx;
    vals[1] = cx;
    vals[2] = dx;
    return call_(h, ax, REGS, vals, 3);
}

SP_API void sph_say_ah(sp_host *h, const unsigned char *bytes, int n, int ah, int cx, int dx)
{
    static const int REGS[5] = {ES, SI, DI, CX, DX};
    int vals[5];
    if (h->fault || n <= 0)
        return;
    if (n > TEXT_MAX)
        n = TEXT_MAX;
    if (!wrmem(h, (unsigned long)TEXT_SEG * 16, bytes, (size_t)n))
        return;
    vals[0] = TEXT_SEG;
    vals[1] = 0;
    vals[2] = n;
    vals[3] = cx;
    vals[4] = dx;
    call_(h, ((ah & 0xFF) << 8) | 0x07, REGS, vals, 5);
}

SP_API void sph_say(sp_host *h, const unsigned char *bytes, int n, int cx, int dx)
{
    sph_say_ah(h, bytes, n, 0, cx, dx);
}

SP_API int sph_queued_frames(const sp_host *h)
{
    int in = vword(h, QUEUE_IN), out = vword(h, QUEUE_OUT);
    int start = vword(h, QUEUE_START), end = vword(h, QUEUE_END), size = end - start;
    if (size <= 0)
        return in != out;
    return ((in - out) % size + size) % size / 5;
}

SP_API void sph_cancel(sp_host *h)
{
    if (h->fail_cancel) {                                  /* a test's: this cancel faults (sp_voice's recovery) */
        h->fail_cancel = 0;
        raise_(h, "a test: the cancel faulted");
        return;
    }
    call_(h, h->brk_cancel ? 0x0000 : 0x0001, NULL, NULL, 0);
}

SP_API int sph_run(sp_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0, *swap;
    int cap;
    *audio = h->buf;
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

SP_API double sph_skip(sp_host *h, double seconds, double step)
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
        t += dt;
    }
    return t;
}

SP_API int sph_queued(const sp_host *h) { return vword(h, QUEUE_IN) != vword(h, QUEUE_OUT); }

SP_API int sph_busy(const sp_host *h, double quiet)
{
    return sph_queued(h) || !vbyte(h, IDLE_FLAG) || (ssi263_time(h->chip) - h->last_speech) < quiet;
}

/* boot: VEST's own announcement, played out silently */
SP_API void sph_boot(sp_host *h, double limit)
{
    double t = sph_skip(h, 0.05, 0.002), s;
    while (t >= 0 && t < limit && sph_busy(h, 0.05)) {
        if ((s = sph_skip(h, 0.05, 0.002)) < 0)
            return;
        t += s;
    }
}

SP_API int sph_fault(const sp_host *h) { return h->fault; }
SP_API const char *sph_error(const sp_host *h) { return h->fault ? h->why : ""; }
SP_API ssi263 *sph_chip(sp_host *h) { return h->chip; }

/* ---- creation: the BIOS, "DOS", the card; VEST.COM until it stays resident ---------------------------------------- */
SP_API sp_host *sph_create(const unsigned char *com, size_t n, ssi263 *chip, double out_rate, char *err, int errlen)
{
    static const uint8_t eoi[7] = {0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF};       /* push ax; EOI; pop ax; iret */
    /* push ax; push ds; xor ax,ax; mov ds,ax; inc word [46Ch]; jnz $+6; inc word [46Eh]; int 1Ch; mov al,20h;
       out 20h,al; pop ds; pop ax; iret */
    static const uint8_t timer[25] = {0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04, 0x75, 0x04, 0xFF,
                                      0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20, 0xE6, 0x20, 0x1F, 0x58, 0xCF};
    static const uint8_t idle[3] = {0xFB, 0xEB, 0xFE};
    const char *brk = getenv("SYNPHONIX_HOST_BREAK");
    char e[300];
    sp_host *h;
    uint8_t *m;
    int v;
    if (!err || errlen <= 0) {
        err = e;
        errlen = (int)sizeof e;
    }
    if (!brk)
        brk = "";
    if (!com || n < 64) {
        snprintf(err, (size_t)errlen, "the SynPhonix's VEST.COM is needed");
        return NULL;
    }
    h = (sp_host *)calloc(1, sizeof *h);
    if (!h) {
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    if (card_key(com, n, !strcmp(brk, "key"), h->key, err, errlen) < 0) {
        free(h);
        return NULL;
    }
    h->brk_rotate = !strcmp(brk, "rotate");
    h->brk_eoi = !strcmp(brk, "eoi");
    h->brk_cancel = !strcmp(brk, "cancel");
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
    h->cpu_ips = SPH_CPU_IPS;
    h->mem = m = pc86_mem(h->p);
    /* BIOS: every vector an IRET, hardware IRQs EOI, the timer, 640 KB, an AT's model byte, the far-call trap */
    m[(unsigned long)BIOS_SEG * 16 + IRET_OFF] = 0xCF;
    m[(unsigned long)BIOS_SEG * 16 + RETURN_OFF] = 0xF4;
    memcpy(m + (unsigned long)BIOS_SEG * 16 + IRQ_EOI_OFF, eoi, sizeof eoi);
    for (v = 0; v < 256; v++) {
        wr16(h, (unsigned long)v * 4, v >= 0x08 && v <= 0x0F ? IRQ_EOI_OFF : IRET_OFF);
        wr16(h, (unsigned long)v * 4 + 2, BIOS_SEG);
    }
    for (v = 0x60; v < 0x68; v++) {                     /* the user vectors free, as on a PC without SONIX */
        wr16(h, (unsigned long)v * 4, 0);
        wr16(h, (unsigned long)v * 4 + 2, 0);
    }
    memcpy(m + (unsigned long)BIOS_SEG * 16 + TIMER_OFF, timer, sizeof timer);
    wr16(h, 0x08 * 4, TIMER_OFF);
    wr16(h, 0x08 * 4 + 2, BIOS_SEG);
    m[0xFFFFE] = 0xFC;                                  /* model byte: AT (VEST: A44C) */
    wr16(h, 0x413, 640);
    m[0x449] = 0x03;                                    /* video mode 3, for its banner */
    memcpy(m + (unsigned long)IDLE_SEG * 16, idle, sizeof idle);
    /* the card and the PIC */
    h->pic_mask = 0xB8;                                 /* as the BIOS leaves it: the timer, keyboard, IRQ2, floppy */
    h->last_speech = -1.0;
    h->resident = -1;
    pc86_set_hooks(h->p, on_in, on_out, on_int, h);
    if (exec_com(h, com, n, err, errlen) < 0) {
        sph_destroy(h);
        return NULL;
    }
    if (find_vest(h) < 0) {
        snprintf(err, (size_t)errlen, "VEST's signature not found");
        sph_destroy(h);
        return NULL;
    }
    h->n_pend = 0;
    w(h, CS, IDLE_SEG);
    w(h, IP, 0);
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    return h;
}

SP_API void sph_destroy(sp_host *h)
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
SP_API double sph_get_double(const sp_host *h, const char *name)
{
    if (!strcmp(name, "cpu_ips")) return h->cpu_ips;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

SP_API void sph_set_double(sp_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_ips")) h->cpu_ips = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
}

SP_API int sph_get_int(const sp_host *h, const char *name)
{
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    if (!strcmp(name, "segment")) return h->seg;
    if (!strcmp(name, "entry")) return h->entry;
    return -1;
}

SP_API void sph_set_int(sp_host *h, const char *name, int v)
{
    if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
    else if (!strcmp(name, "fail_cancel"))                /* tests: the next cancel faults */
        h->fail_cancel = v != 0;
}

SP_API int sph_writes(const sp_host *h, const sph_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

SP_API void sph_clear_writes(sp_host *h) { h->n_wl = 0; }
