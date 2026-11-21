/* am_host.c -- src/hosts/accent.py's Accent in C, line for line (am_host.h).  Each part names the Python it ports;
 * every comparison, order of operations, rounding and register access is Python's (a register write is a read of all
 * of them and a write back, as pc86.py's reg_write).  Change them together.  MIT.
 */
#include "am_host.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc86.h"
#include "../accent_text.h"
#include "../blazie/bl_fopen.h"

#define LOAD_SEG 0x0800            /* the driver image sits at 0800:0000 */
#define MARK_COUNT 0x6E3           /* ... and counts the index marks speech has passed in this byte of it */
#define PACKET_SEG 0x0060          /* DOS request packets */
#define TEXT_SEG 0x0070            /* a write request's transfer buffer */
#define STACK_SEG 0x9000
#define EMM_SEG 0xC800             /* a fake EMM.SYS: device header with "EMMXXXX0" */
#define FRAME_SEG 0xD000           /* EMS page frame: four 16 KB windows */
#define BIOS_SEG 0xF000
#define IRET_OFF 0xFF53            /* F000:FF53, where every unclaimed vector points */
#define RETURN_OFF 0xFF00          /* F000:FF00: far calls into the driver return here (HLT) */
#define IRQ_EOI_OFF 0xFF60         /* F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET */
#define DATA_PORT 0x3EF
#define CONTROL_PORT 0x3EE
#define IDLE_SEG 0x0050            /* 0050:0000 STI; JMP $ -- the CPU between interrupts */
#define CLIENT_SEG 0x0058          /* 0058:0000 a tiny application printing DS:SI, CX chars via INT 17h */
#define PAGE 0x4000
#define EMM_CALL_RET 0x30          /* EMM_SEG:0030, INT 67h: "the routine EMS 56h called has returned" */
#define RET_LINEAR ((unsigned long)BIOS_SEG * 16 + RETURN_OFF)
#define NO_COUNT ((uint64_t)1 << 62)

typedef struct { int handle, logical; uint8_t *data; } ems_page;          /* ems_pages: (handle, logical) -> bytes */
typedef struct { int handle, count; } ems_handle;                         /* ems_handles: handle -> pages */
typedef struct { int handle; uint8_t blob[16]; } ems_saved;               /* ems_saved: handle -> _ems_state() */
typedef struct { int cs, ip, handle; unsigned long ptr; int count, by_segment; } ems_call;

struct am_host {
    pc86 *p;
    uint8_t *mem;
    ssi263 *chip;
    int own_chip;
    double out_rate, cpu_ips;
    int strategy, interrupt, lpt;
    /* EMS */
    ems_page *pages;
    int n_pages, cap_pages;
    ems_handle *handles;
    int n_handles, cap_handles;
    int map_set[4], map_h[4], map_l[4];                   /* ems_map: None, or (handle, logical) */
    ems_call *calls;
    int n_calls, cap_calls;
    ems_saved *saved;
    int n_saved, cap_saved;
    int ems_total;
    /* the card and the PIC */
    int data_latch, pic_mask[2], in_service, irq_latched, last_request, control;
    double last_speech, say_time;
    int preparing;
    int stopped;                                          /* self.stopped: a reason, for this call */
    int fault;                                            /* something raised: the host is dead */
    char why[200];
    /* a background driver call (say(background=True)) */
    int call;
    double call_waited, call_limit;
    unsigned long cx_at;
    /* audio */
    double *pend, *buf;
    int n_pend, cap_pend, n_buf, cap_buf;
    /* index marks (amh_replies) */
    unsigned long mark_at;                                /* the driver's count of marks passed: its byte, 0 unknown */
    amh_reply rp[AMH_REPLIES];
    int n_rp, rp_read;                                    /* rp_read: the list is a finished run's (the next mark
                                                             starts a new one) */
    int mark_seen, marks;                                 /* the driver's count as last read; every mark passed */
    int settles;
    long long insns;
    int log_on;
    amh_write *wl;
    int n_wl, cap_wl;
};

/* ---- faults: what accent.py raises ------------------------------------------------------------------------------- */
static void raise_(am_host *h, const char *fmt, ...)
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
static void stop_(am_host *h, const char *fmt, ...)
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

static int r(am_host *h, int which)
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

static void w(am_host *h, int which, int value)
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

static int rd16(am_host *h, unsigned long a) { return h->mem[a & 0xFFFFF] | h->mem[(a + 1) & 0xFFFFF] << 8; }
static void wr16(am_host *h, unsigned long a, int v)
{
    h->mem[a & 0xFFFFF] = (uint8_t)(v & 0xFF);
    h->mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8 & 0xFF);
}
static void wrmem(am_host *h, unsigned long a, const void *d, size_t n)
{
    if (a + n > PC86_MEM_SIZE) {
        raise_(h, "write outside 1 MB");
        return;
    }
    memcpy(h->mem + a, d, n);
}

static unsigned long here(am_host *h) { return (unsigned long)r(h, CS) * 16 + (unsigned long)r(h, IP); }

static void push(am_host *h, int v)
{
    int sp = (r(h, SP) - 2) & 0xFFFF;
    w(h, SP, sp);
    wr16(h, (unsigned long)r(h, SS) * 16 + (unsigned long)sp, v & 0xFFFF);
}

static void carry(am_host *h, int on)
{
    int fl = r(h, FL);
    w(h, FL, on ? (fl | 1) : (fl & ~1));
}

static void vector(am_host *h, int n, int *seg, int *off)
{
    *off = rd16(h, (unsigned long)n * 4);
    *seg = rd16(h, (unsigned long)n * 4 + 2);
}

/* _dispatch: what INT n does on a real CPU: push flags, CS, IP; clear IF, TF; jump */
static void dispatch(am_host *h, int n)
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

/* emu_start(begin, until, count) on pc86.py: begin = CS:IP now, so IP is left alone; then the aliased-opcode check */
static void emu_start(am_host *h, uint32_t until, uint64_t count)
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

/* ---- EMS: LIM 3.2/4.0, as much as the driver asks for --------------------------------------------------------- */
static int grow(void **p, int *cap, int need, size_t size)
{
    if (need > *cap) {
        int c = *cap ? *cap * 2 : 16;
        void *q;
        while (c < need)
            c *= 2;
        q = realloc(*p, (size_t)c * size);
        if (!q)
            return 0;
        *p = q;
        *cap = c;
    }
    return 1;
}

static ems_page *page_find(am_host *h, int handle, int logical)
{
    int i;
    for (i = 0; i < h->n_pages; i++)
        if (h->pages[i].handle == handle && h->pages[i].logical == logical)
            return &h->pages[i];
    return NULL;
}

static ems_handle *handle_find(am_host *h, int handle)
{
    int i;
    for (i = 0; i < h->n_handles; i++)
        if (h->handles[i].handle == handle)
            return &h->handles[i];
    return NULL;
}

static int handles_used(am_host *h)
{
    int i, s = 0;
    for (i = 0; i < h->n_handles; i++)
        s += h->handles[i].count;
    return s;
}

/* _ems_save */
static void ems_save(am_host *h, int window)
{
    ems_page *pg;
    if (!h->map_set[window])
        return;
    pg = page_find(h, h->map_h[window], h->map_l[window]);
    if (!pg) {
        if (!grow((void **)&h->pages, &h->cap_pages, h->n_pages + 1, sizeof *h->pages)) {
            raise_(h, "out of memory");
            return;
        }
        pg = &h->pages[h->n_pages];
        pg->data = (uint8_t *)malloc(PAGE);
        if (!pg->data) {
            raise_(h, "out of memory");
            return;
        }
        pg->handle = h->map_h[window];
        pg->logical = h->map_l[window];
        h->n_pages++;
    }
    memcpy(pg->data, h->mem + (unsigned long)FRAME_SEG * 16 + (unsigned long)window * PAGE, PAGE);
}

/* _ems_put: map (handle, logical), or None (set = 0), into a window, copying only on a change */
static void ems_put(am_host *h, int window, int set, int handle, int logical)
{
    uint8_t *frame;
    if (window < 0)
        window += 4;                                    /* a Python list's negative index */
    if (window < 0 || window > 3) {
        raise_(h, "EMS window %d out of range", window);
        return;
    }
    if (h->map_set[window] == set && (!set || (h->map_h[window] == handle && h->map_l[window] == logical)))
        return;
    ems_save(h, window);
    frame = h->mem + (unsigned long)FRAME_SEG * 16 + (unsigned long)window * PAGE;
    if (set) {
        ems_page *pg = page_find(h, handle, logical);
        if (pg)
            memcpy(frame, pg->data, PAGE);
        else
            memset(frame, 0, PAGE);
    }
    h->map_set[window] = set;
    h->map_h[window] = handle;
    h->map_l[window] = logical;
}

static void ems_state(am_host *h, uint8_t *out)
{
    int i;
    for (i = 0; i < 4; i++) {
        int hh = h->map_set[i] ? h->map_h[i] : 0xFFFF, ll = h->map_set[i] ? h->map_l[i] : 0xFFFF;
        out[i * 4] = (uint8_t)(hh & 0xFF);
        out[i * 4 + 1] = (uint8_t)(hh >> 8);
        out[i * 4 + 2] = (uint8_t)(ll & 0xFF);
        out[i * 4 + 3] = (uint8_t)(ll >> 8);
    }
}

static void ems_restore(am_host *h, const uint8_t *blob)
{
    int i;
    for (i = 0; i < 4; i++) {
        int hh = blob[i * 4] | blob[i * 4 + 1] << 8, ll = blob[i * 4 + 2] | blob[i * 4 + 3] << 8;
        ems_put(h, i, hh != 0xFFFF, hh, ll);
    }
}

static void ems_map_list(am_host *h, int handle, unsigned long ptr, int count, int by_segment)
{
    int i;
    for (i = 0; i < count && !h->fault; i++) {
        int logical = rd16(h, ptr + (unsigned long)i * 4), where = rd16(h, ptr + (unsigned long)i * 4 + 2);
        int window = by_segment ? (int)floor((where - FRAME_SEG) / (double)0x400) : where;   /* // floors */
        ems_put(h, window, logical != 0xFFFF, handle, logical);
    }
}

static void ems_alter_and_call(am_host *h, int al)
{
    /* DS:SI -> target far pointer, new map (count, far ptr), old map (count, far ptr): "<HHBHHBHH" */
    unsigned long a = (unsigned long)r(h, DS) * 16 + (unsigned long)r(h, SI);
    int t_off = rd16(h, a), t_seg = rd16(h, a + 2), n_new = h->mem[(a + 4) & 0xFFFFF];
    int new_off = rd16(h, a + 5), new_seg = rd16(h, a + 7), n_old = h->mem[(a + 9) & 0xFFFFF];
    int old_off = rd16(h, a + 10), old_seg = rd16(h, a + 12);
    int handle = r(h, DX);
    ems_call *c;
    ems_map_list(h, handle, (unsigned long)new_seg * 16 + (unsigned long)new_off, n_new, al == 1);
    if (!grow((void **)&h->calls, &h->cap_calls, h->n_calls + 1, sizeof *h->calls)) {
        raise_(h, "out of memory");
        return;
    }
    c = &h->calls[h->n_calls++];
    c->cs = r(h, CS);
    c->ip = r(h, IP);
    c->handle = handle;
    c->ptr = (unsigned long)old_seg * 16 + (unsigned long)old_off;
    c->count = n_old;
    c->by_segment = al == 1;
    push(h, EMM_SEG);
    push(h, EMM_CALL_RET);
    w(h, CS, t_seg);
    w(h, IP, t_off);
}

static void ems_call_return(am_host *h)
{
    ems_call c;
    if (!h->n_calls) {
        raise_(h, "pop from empty list");
        return;
    }
    c = h->calls[--h->n_calls];
    ems_map_list(h, c.handle, c.ptr, c.count, c.by_segment);
    w(h, AX, r(h, AX) & 0xFF);                          /* AH = 0: success */
    w(h, CS, c.cs);
    w(h, IP, c.ip);
}

static ems_saved *saved_find(am_host *h, int handle)
{
    int i;
    for (i = 0; i < h->n_saved; i++)
        if (h->saved[i].handle == handle)
            return &h->saved[i];
    return NULL;
}

/* _ems: the page frame is real memory and mapping copies pages in and out of it */
static void ems(am_host *h, int ah, int al)
{
    int ok = 0;
    if (ah == 0x40) {                                   /* status */
    } else if (ah == 0x41) {                            /* page frame segment */
        w(h, BX, FRAME_SEG);
    } else if (ah == 0x42) {                            /* unallocated / total pages */
        w(h, BX, h->ems_total - handles_used(h));
        w(h, DX, h->ems_total);
    } else if (ah == 0x43) {                            /* allocate BX pages -> DX handle */
        int n = r(h, BX);
        if (n > h->ems_total - handles_used(h))
            ok = 0x88;
        else {
            int i, handle = 0;
            for (i = 0; i < h->n_handles; i++)
                if (h->handles[i].handle > handle)
                    handle = h->handles[i].handle;
            handle += 1;
            if (!grow((void **)&h->handles, &h->cap_handles, h->n_handles + 1, sizeof *h->handles)) {
                raise_(h, "out of memory");
                return;
            }
            h->handles[h->n_handles].handle = handle;
            h->handles[h->n_handles].count = n;
            h->n_handles++;
            w(h, DX, handle);
        }
    } else if (ah == 0x44) {                            /* map logical BX of handle DX to window AL */
        int window = al, logical = r(h, BX), handle = r(h, DX);
        ems_handle *eh = handle_find(h, handle);
        if (!eh || window > 3)
            ok = !eh ? 0x83 : 0x8B;
        else if (logical != 0xFFFF && logical >= eh->count)
            ok = 0x8A;
        else
            ems_put(h, window, logical != 0xFFFF, handle, logical);
    } else if (ah == 0x45) {                            /* deallocate */
        ems_handle *eh = handle_find(h, r(h, DX));
        if (eh) {
            *eh = h->handles[h->n_handles - 1];
            h->n_handles--;
        }
    } else if (ah == 0x46) {                            /* version */
        w(h, AX, (r(h, AX) & 0xFF00) | 0x40);
    } else if (ah == 0x4B) {                            /* handle count */
        w(h, BX, h->n_handles);
    } else if (ah == 0x56 && (al == 0 || al == 1)) {    /* alter page map and call */
        ems_alter_and_call(h, al);
        return;
    } else if (ah == 0x56 && al == 2) {                 /* stack space 56h needs */
        w(h, BX, 10);
    } else if (ah == 0x4E) {                            /* get / set page map */
        if (al == 0 || al == 2) {
            uint8_t blob[16];
            ems_state(h, blob);
            wrmem(h, (unsigned long)r(h, ES) * 16 + (unsigned long)r(h, DI), blob, 16);
        }
        if (al == 1 || al == 2) {
            uint8_t blob[16];
            unsigned long a = (unsigned long)r(h, DS) * 16 + (unsigned long)r(h, SI);
            int i;
            for (i = 0; i < 16; i++)
                blob[i] = h->mem[(a + (unsigned long)i) & 0xFFFFF];
            ems_restore(h, blob);
        }
        if (al == 3) {
            w(h, AX, 16);                               /* AH = 0 (ok), AL = size of a map */
            return;
        }
    } else if (ah == 0x47) {                            /* save the map with handle DX */
        int handle = r(h, DX);
        ems_saved *s = saved_find(h, handle);
        if (!s) {
            if (!grow((void **)&h->saved, &h->cap_saved, h->n_saved + 1, sizeof *h->saved)) {
                raise_(h, "out of memory");
                return;
            }
            s = &h->saved[h->n_saved++];
            s->handle = handle;
        }
        ems_state(h, s->blob);
    } else if (ah == 0x48) {                            /* restore it */
        ems_saved *s = saved_find(h, r(h, DX));
        if (!s)
            ok = 0x8E;
        else
            ems_restore(h, s->blob);
    } else {
        stop_(h, "unimplemented EMS AH=%02X AL=%02X", ah, al);
        return;
    }
    w(h, AX, ok << 8 | (r(h, AX) & 0xFF));
}

/* _dos */
static void dos(am_host *h, int ah, int al)
{
    carry(h, 0);
    if (ah == 0x09 || ah == 0x02) {
        /* "DOS print" / "DOS char": the log only */
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
    } else
        stop_(h, "unimplemented DOS AH=%02X AL=%02X", ah, al);
}

/* _int: the driver's own handlers run; BIOS, DOS and EMS are emulated.  pc86.py: a serviced interrupt (1). */
static int on_int(void *user, int n, int kind)
{
    am_host *h = (am_host *)user;
    int seg, off, ah, al;
    (void)kind;
    if (h->fault)
        return 1;
    if (n == 0x67 && r(h, CS) == EMM_SEG && r(h, IP) == EMM_CALL_RET + 2) {
        ems_call_return(h);
        return 1;
    }
    vector(h, n, &seg, &off);
    if (seg != BIOS_SEG && seg != EMM_SEG) {
        dispatch(h, n);                                 /* a vector the driver (or client) installed */
        return 1;
    }
    ah = r(h, AX) >> 8;
    al = r(h, AX) & 0xFF;
    if (n == 0x21)
        dos(h, ah, al);
    else if (n == 0x67)
        ems(h, ah, al);
    else
        stop_(h, "unimplemented INT %02X AX=%04X", n, r(h, AX));
    return 1;
}

/* ---- the card and the PIC ------------------------------------------------------------------------------------- */
/* _try_irq: the card's IRQ line = A/R AND its interrupt enable (control bit 7); the PIC latches the line's rising
   edge, and a request that has dropped is gone (accent.py: Tomi's freeze scrolling Mastodon, 2026-09-25) */
static int try_irq(am_host *h)
{
    int line = ssi263_request(h->chip) && (h->control & 0x80);
    if (line && !h->last_request)
        h->irq_latched = 1;
    h->last_request = line;
    if (!line) {
        h->irq_latched = 0;
        return 0;
    }
    if (!h->irq_latched)
        return 0;
    if (!(r(h, FL) & 0x200) || h->in_service)
        return 0;
    if ((h->pic_mask[0] & 0x04) && (h->pic_mask[1] & 0x02))    /* IRQ2 and IRQ9 both masked */
        return 0;
    dispatch(h, 0x0A);
    h->in_service = 1;
    h->irq_latched = 0;
    return 1;
}

static int on_in(void *user, int port)
{
    am_host *h = (am_host *)user;
    if (h->fault)
        return 0xFF;
    if (port == 0x21)
        return h->pic_mask[0];
    if (port == 0xA1)
        return h->pic_mask[1];
    if (port == CONTROL_PORT)
        return ssi263_request(h->chip) ? 0x03 : 0x00;
    if (port == DATA_PORT)
        return 0xFF;
    stop_(h, "unimplemented IN %04X", port);
    return 0xFF;
}

static void on_out(void *user, int port, int value)
{
    am_host *h = (am_host *)user;
    if (h->fault)
        return;
    value &= 0xFF;
    if (port == 0x21)
        h->pic_mask[0] = value;
    else if (port == 0xA1)
        h->pic_mask[1] = value;
    else if (port == 0x20 || port == 0xA0) {
        if (value == 0x20)
            h->in_service = 0;
    } else if (port == DATA_PORT)
        h->data_latch = value;
    else if (port == CONTROL_PORT) {
        h->control = value;
        try_irq(h);                                     /* enabling with A/R already low raises the line */
        if ((value & 0x18) == 0x10) {
            int reg = value & 7, v = h->data_latch;
            ssi263_write(h->chip, reg, v);
            if (h->log_on && grow((void **)&h->wl, &h->cap_wl, h->n_wl + 1, sizeof *h->wl)) {
                h->wl[h->n_wl].t = ssi263_time(h->chip);
                h->wl[h->n_wl].reg = reg;
                h->wl[h->n_wl].val = v;
                h->n_wl++;
            }
            if (reg == 0 && (v & 0x3F) && !(ssi263_reg(h->chip, 3) & 0x80)) {
                h->last_speech = ssi263_time(h->chip);  /* PA (00) is not speech */
                h->preparing = 0;
            }
        }
    } else
        stop_(h, "unimplemented OUT %04X <- %02X", port, value);
}

/* ---- audio -------------------------------------------------------------------------------------------------------- */
/* one slice of the chip: run_until_request(step) if it is not requesting, else run(step / 4); appended to *buf */
static int chip_slice(am_host *h, double step, double **buf, int *n, int *cap)
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

/* the driver's own count of marks passed (modulo 256), read between slices: those new since the last look, at
   `sample` of the audio being made (the pending audio's or the run's: one buffer follows the other) */
static void marks_poll(am_host *h, int sample)
{
    int count, fresh;
    if (!h->mark_at)
        return;
    count = h->mem[h->mark_at];
    fresh = (count - h->mark_seen) & 0xFF;
    h->mark_seen = count;
    if (!fresh)
        return;
    if (h->rp_read) { h->n_rp = 0; h->rp_read = 0; }
    for (; fresh > 0; fresh--) {
        h->marks++;
        if (h->n_rp < AMH_REPLIES)
            h->rp[h->n_rp++].sample = sample;
    }
}

static int pending_slice(am_host *h, double step)
{
    marks_poll(h, h->n_pend);                           /* (a mark passed while a driver call waits) */
    if (!chip_slice(h, step, &h->pend, &h->n_pend, &h->cap_pend)) {
        raise_(h, "out of memory");
        return 0;
    }
    return 1;
}

static uint64_t steps_for(am_host *h, double dt)
{
    long long n = (long long)(h->cpu_ips * dt);         /* int() */
    return (uint64_t)(n > 100 ? n : 100);               /* max(100, ...) */
}

static double dt_since(am_host *h, double before)
{
    double dt = ssi263_time(h->chip) - before;
    return dt > 1e-5 ? dt : 1e-5;                       /* max(..., 1e-5) */
}

/* ---- driving the driver ----------------------------------------------------------------------------------------- */

/* _far_call: far-call seg:off from the host and run until it returns to the RETURN trap; a call that has not
   returned within `budget` instructions waits on the card, which runs meanwhile (its audio kept in pending).
   background: such a call is left running.  1 when it returned, 0 when left in the background, -1 on a fault. */
static int far_call(am_host *h, int seg, int off, uint64_t budget, double wait_limit, double step, int background)
{
    int sp0, ss0;
    double waited = 0.0;
    if (h->fault)
        return -1;
    h->stopped = 0;
    sp0 = r(h, SP);
    ss0 = r(h, SS);
    push(h, BIOS_SEG);
    push(h, RETURN_OFF);
    w(h, CS, seg);
    w(h, IP, off);
    emu_start(h, (uint32_t)RET_LINEAR, budget);
    if (h->fault)
        return -1;
    if (background && !h->stopped && here(h) != RET_LINEAR) {
        /* the client pushed CX (characters left) just below the return address -- on the stack the call began on.
           By now the driver is inside its INT 17h on a stack of its own (SS 0800h): the slot was taken in that
           segment, so drop_call's "stop after this character" was written elsewhere, and every cancel while the
           driver still took a text ran the rest of it through the card (2,400 characters: 42 s of card time;
           nvda/tools/am_cancel_capital.py).  (amh_drop_ss_break = 1, the test's control: the SS of now.) */
        h->call = 1;
        h->call_waited = 0.0;
        h->call_limit = wait_limit;
        h->cx_at = (unsigned long)(amh_drop_ss_break ? r(h, SS) : ss0) * 16 + (unsigned long)((sp0 - 6) & 0xFFFF);
        return 0;
    }
    while (!h->stopped && here(h) != RET_LINEAR) {
        double before, dt;
        uint64_t count;
        if (waited > wait_limit) {
            stop_(h, "driver call %04X:%04X still waiting after %.0f s", seg, off, waited);
            break;
        }
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!pending_slice(h, step))
            return -1;
        dt = dt_since(h, before);
        count = steps_for(h, dt);
        emu_start(h, (uint32_t)RET_LINEAR, count);
        if (h->fault)
            return -1;
        h->insns += (long long)count;
        waited += dt;
    }
    if (h->stopped) {
        h->fault = 1;
        return -1;
    }
    w(h, CS, IDLE_SEG);                                 /* back to "DOS", idle, interrupts on */
    w(h, IP, 0);
    return 1;
}

/* the tests' control (nvda/tools/am_long_text.py): 1 counts a background call's limit from its start, as it was */
AM_API int amh_call_stall_break = 0;
/* the tests' control (nvda/tools/am_cancel_capital.py): 1 takes the client's CX slot in the stack segment the driver
   is on when the call is left running, as it was */
AM_API int amh_drop_ss_break = 0;

/* _call_step: the CPU's share of one step while a background call runs.  The call that hands the driver a text runs
   as long as its speech -- the driver takes characters only as its buffer empties -- so it is stalled only when the
   card has said nothing for the limit, not when the call is older than it: a text item of 2,600 characters was cut
   at 30 s mid-sentence, the card rebooted and the rest lost (#67) */
static int call_step(am_host *h, double dt)
{
    uint64_t count = steps_for(h, dt);
    emu_start(h, (uint32_t)RET_LINEAR, count);
    h->insns += (long long)count;
    if (h->stopped)
        h->fault = 1;
    if (h->fault)
        return -1;
    h->call_waited += dt;
    if (here(h) == RET_LINEAR) {
        h->call = 0;
        w(h, CS, IDLE_SEG);
        w(h, IP, 0);
    } else if (h->call_waited > h->call_limit
               && (amh_call_stall_break || ssi263_time(h->chip) - h->last_speech > h->call_limit)) {
        raise_(h, "driver call still waiting after %.0f s", h->call_waited);
        return -1;
    }
    return 0;
}

/* _finish_call: run a background call to its end, keeping what the card says */
static int finish_call(am_host *h, double step)
{
    while (h->call) {
        double before;
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!pending_slice(h, step))
            return -1;
        if (call_step(h, dt_since(h, before)) < 0)
            return -1;
    }
    return 0;
}

/* _drop_call: end a background call fast and silently, for a flush: the client stops after the character the driver
   is taking now, and the card runs without sound until it has */
static int drop_call(am_host *h, double step)
{
    if (!h->call)
        return 0;
    wr16(h, h->cx_at, 1);
    while (h->call) {
        double before;
        try_irq(h);
        before = ssi263_time(h->chip);
        ssi263_skip(h->chip, step);
        if (call_step(h, dt_since(h, before)) < 0)
            return -1;
    }
    h->n_pend = 0;
    return 0;
}

/* _cpu */
static int cpu(am_host *h, uint64_t count)
{
    emu_start(h, 0, count);                             /* until 0: pc86.py passes the linear address 0 */
    h->insns += (long long)count;
    if (h->stopped)
        h->fault = 1;
    return h->fault ? -1 : 0;
}

/* _settle: let a handler that a CPU slice stopped inside finish before the host calls into the driver */
static int settle(am_host *h, uint64_t budget, double limit, double step)
{
    uint64_t n = 0;
    double waited = 0.0;
    if (r(h, CS) == IDLE_SEG)
        return 0;
    while (r(h, CS) != IDLE_SEG && n < budget) {
        if (cpu(h, 1000) < 0)
            return -1;
        n += 1000;
    }
    while (r(h, CS) != IDLE_SEG && waited < limit) {
        double before, dt;
        try_irq(h);
        before = ssi263_time(h->chip);
        if (!pending_slice(h, step))
            return -1;
        dt = dt_since(h, before);
        if (cpu(h, steps_for(h, dt)) < 0)
            return -1;
        waited += dt;
    }
    h->settles++;
    return 0;
}

/* _request: a DOS request packet to the strategy routine, then the interrupt routine */
static int request(am_host *h, int command, const uint8_t *extra, int n_extra, int length)
{
    uint8_t pkt[64];
    unsigned long at = (unsigned long)PACKET_SEG * 16;
    memset(pkt, 0, sizeof pkt);
    pkt[0] = (uint8_t)(length ? length : 13 + n_extra);
    pkt[2] = (uint8_t)command;
    memcpy(pkt + 13, extra, (size_t)n_extra);
    wrmem(h, at, pkt, (size_t)(13 + n_extra));
    w(h, ES, PACKET_SEG);
    w(h, BX, 0);
    if (far_call(h, LOAD_SEG, h->strategy, 1000000, 30.0, 0.0005, 0) < 0)
        return -1;
    w(h, ES, PACKET_SEG);
    w(h, BX, 0);
    if (far_call(h, LOAD_SEG, h->interrupt, 50000000, 30.0, 0.0005, 0) < 0)
        return -1;
    return rd16(h, at + 3);
}

AM_API int amh_init(am_host *h)
{
    static const char cmdline[] = "SPKEMS.DVC\r\n";
    uint8_t extra[10];                                  /* "<BIIB", 0, 0, TEXT_SEG << 16, 0 */
    memset(extra, 0, sizeof extra);
    extra[7] = TEXT_SEG & 0xFF;
    extra[8] = TEXT_SEG >> 8;
    wrmem(h, (unsigned long)TEXT_SEG * 16, cmdline, sizeof cmdline - 1);
    return request(h, 0, extra, 10, 23);
}

AM_API int amh_say(am_host *h, const unsigned char *data, int n, int speech, int background)
{
    int k;
    if (h->fault)
        return -1;
    if (n <= 0)
        return 0;
    if (speech < 0)
        speech = at_is_speech(data, n);
    if (h->call && finish_call(h, 0.0005) < 0)
        return -1;
    /* before touching a register: the CPU may be inside the driver's IRQ handler */
    if (settle(h, 400000, 0.5, 0.0005) < 0)
        return -1;
    if (speech) {
        h->preparing = 1;
        h->say_time = ssi263_time(h->chip);
    }
    for (k = 0; k < n; k += 0x4000) {
        int len = n - k < 0x4000 ? n - k : 0x4000, got;
        wrmem(h, (unsigned long)TEXT_SEG * 16, data + k, (size_t)len);
        w(h, DS, TEXT_SEG);
        w(h, SI, 0);
        w(h, CX, len);
        got = far_call(h, CLIENT_SEG, 0, 1000000, 30.0, 0.0005, background && k + 0x4000 >= n);
        if (got < 0)
            return -1;
        if (!got)
            break;
    }
    return 0;
}

/* ---- time ---------------------------------------------------------------------------------------------------------- */
AM_API int amh_run(am_host *h, double seconds, double step, const double **audio)
{
    double t = 0.0;
    double *swap;
    int cap;
    *audio = h->buf;
    if (h->fault)
        return -1;
    /* out = self.pending: the run's audio follows what the blocked calls made */
    if (h->rp_read) { h->n_rp = 0; h->rp_read = 0; }    /* (what a blocked call passed stays: its audio comes first) */
    marks_poll(h, h->n_pend);
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
        if (h->call) {
            if (call_step(h, dt) < 0)
                return -1;
        } else if (cpu(h, steps_for(h, dt)) < 0)
            return -1;
        marks_poll(h, h->n_buf);
        t += dt;
    }
    h->rp_read = 1;
    *audio = h->buf;
    return h->n_buf;
}

AM_API int amh_replies(const am_host *h, const amh_reply **out)
{
    *out = h->rp;
    return h->rp_read ? h->n_rp : 0;
}

AM_API int amh_speaking(const am_host *h)
{
    return (h->control & 0x80) ? 1 : 0;
}

AM_API int amh_busy(const am_host *h, double quiet, double patience)
{
    double now = ssi263_time(h->chip);
    if (h->call || amh_speaking(h) || (now - h->last_speech) < quiet)
        return 1;
    return h->preparing && (now - h->say_time) < patience;
}

AM_API double amh_skip(am_host *h, double seconds, double step)
{
    double t = 0.0;
    int k;
    if (h->fault)
        return -1.0;
    if (h->call && finish_call(h, 0.0005) < 0)
        return -1.0;
    h->n_pend = 0;
    if (!h->rp_read)                                    /* (their audio is dropped: at the next run's start) */
        for (k = 0; k < h->n_rp; k++)
            h->rp[k].sample = 0;
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

AM_API double amh_boot(am_host *h, double limit)
{
    static const unsigned char SETUP[] = {0x1B, '=', 'F', 0x1B, '=', 'M', 0x18};   /* ESC =F, ESC =M, Ctrl-X */
    double t, s;
    if (amh_init(h) < 0)
        return -1.0;
    if (amh_say(h, SETUP, (int)sizeof SETUP, 0, 0) < 0)
        return -1.0;
    t = amh_skip(h, 0.02, 0.002);
    if (t < 0)
        return -1.0;
    while (t < limit && amh_busy(h, 0.03, 1.5)) {
        if ((s = amh_skip(h, 0.02, 0.002)) < 0)
            return -1.0;
        t += s;
    }
    return t;
}

static void marks_forget(am_host *h)
{
    h->n_rp = 0;
    h->rp_read = 0;
    if (h->mark_at)
        h->mark_seen = h->mem[h->mark_at];
}

AM_API double amh_cancel(am_host *h, double limit)
{
    static const unsigned char CTRL_X[] = {0x18};
    double t, s;
    if (h->fault)
        return -1.0;
    h->preparing = 0;
    if (drop_call(h, 0.002) < 0)
        return -1.0;
    if (amh_say(h, CTRL_X, 1, 0, 0) < 0)
        return -1.0;
    t = amh_skip(h, 0.01, 0.002);
    if (t < 0)
        return -1.0;
    while (t < limit && amh_busy(h, 0.03, 1.5)) {
        if ((s = amh_skip(h, 0.01, 0.002)) < 0)
            return -1.0;
        t += s;
    }
    marks_forget(h);                                    /* the cut text's marks: none is given (the driver counted
                                                           one more while a call was being dropped: measured) */
    return t;
}

/* ---- creation: the driver loaded as DOS would, the BIOS, the EMS manager, the card ---------------------------- */
AM_API am_host *amh_create(const unsigned char *dvc, size_t n, ssi263 *chip, double out_rate, char *err, int errlen)
{
    /* mov al,[si]; mov ah,0; mov dx,lpt; push cx; push si; push ds; int 17h; pop ds; pop si; pop cx; inc si;
       loop; retf */
    uint8_t client[19] = {0x8A, 0x04, 0xB4, 0x00, 0xBA, 0, 0x00, 0x51, 0x56, 0x1E, 0xCD, 0x17, 0x1F, 0x5E, 0x59,
                          0x46, 0xE2, 0xEE, 0xCB};
    /* push ax; mov al,20h; out 0A0h,al; out 20h,al; pop ax; iret */
    static const uint8_t eoi[9] = {0x50, 0xB0, 0x20, 0xE6, 0xA0, 0xE6, 0x20, 0x58, 0xCF};
    static const uint8_t idle[3] = {0xFB, 0xEB, 0xFE};
    am_host *h;
    unsigned long hdr, size, i;
    int relocs, reloc_at, v;
    uint8_t *m;
    if (n < 28 || dvc[0] != 'M' || dvc[1] != 'Z') {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "expected an EXE-format device driver");
        return NULL;
    }
    hdr = (unsigned long)(dvc[8] | dvc[9] << 8) * 16;
    relocs = dvc[6] | dvc[7] << 8;
    reloc_at = dvc[24] | dvc[25] << 8;
    if (hdr >= n || (unsigned long)reloc_at + (unsigned long)relocs * 4 > n
        || (unsigned long)LOAD_SEG * 16 + (n - hdr) > PC86_MEM_SIZE) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "the driver's EXE header does not fit it");
        return NULL;
    }
    h = (am_host *)calloc(1, sizeof *h);
    if (!h) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    h->p = pc86_create();
    if (!h->p) {
        free(h);
        if (err && errlen > 0)
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
            if (err && errlen > 0)
                snprintf(err, (size_t)errlen, "ssi263_new failed");
            return NULL;
        }
        h->own_chip = 1;
    }
    h->chip = chip;
    h->out_rate = out_rate;
    h->cpu_ips = AMH_CPU_IPS;
    h->mem = m = pc86_mem(h->p);
    /* the image at 0800:0000, every relocation's segment word moved by LOAD_SEG */
    size = n - hdr;
    memcpy(m + (unsigned long)LOAD_SEG * 16, dvc + hdr, size);
    for (i = 0; i < (unsigned long)relocs; i++) {
        unsigned long e = (unsigned long)reloc_at + i * 4;
        unsigned long at = (unsigned long)(dvc[e + 2] | dvc[e + 3] << 8) * 16 + (unsigned long)(dvc[e] | dvc[e + 1] << 8);
        unsigned long a = (unsigned long)LOAD_SEG * 16 + at;
        if (at + 2 > size) {
            amh_destroy(h);
            if (err && errlen > 0)
                snprintf(err, (size_t)errlen, "a relocation outside the driver's image");
            return NULL;
        }
        v = (m[a] | m[a + 1] << 8) + LOAD_SEG;
        m[a] = (uint8_t)(v & 0xFF);
        m[a + 1] = (uint8_t)(v >> 8 & 0xFF);
    }
    h->strategy = m[(unsigned long)LOAD_SEG * 16 + 6] | m[(unsigned long)LOAD_SEG * 16 + 7] << 8;
    h->interrupt = m[(unsigned long)LOAD_SEG * 16 + 8] | m[(unsigned long)LOAD_SEG * 16 + 9] << 8;
    /* BIOS: every vector an IRET, AT machine id, 640 KB, the far-call return trap */
    m[(unsigned long)BIOS_SEG * 16 + IRET_OFF] = 0xCF;
    m[(unsigned long)BIOS_SEG * 16 + RETURN_OFF] = 0xF4;
    memcpy(m + (unsigned long)BIOS_SEG * 16 + IRQ_EOI_OFF, eoi, sizeof eoi);
    for (v = 0; v < 256; v++) {
        int hw = (v >= 0x08 && v <= 0x0F) || (v >= 0x70 && v <= 0x77);
        wr16(h, (unsigned long)v * 4, hw ? IRQ_EOI_OFF : IRET_OFF);
        wr16(h, (unsigned long)v * 4 + 2, BIOS_SEG);
    }
    m[0xFFFFE] = 0xFC;
    wr16(h, 0x413, 640);
    /* EMM.SYS: detected by the name in the device header its INT 67h vector points into */
    memcpy(m + (unsigned long)EMM_SEG * 16 + 0x0A, "EMMXXXX0", 8);
    m[(unsigned long)EMM_SEG * 16 + 0x20] = 0xCF;
    m[(unsigned long)EMM_SEG * 16 + EMM_CALL_RET] = 0xCD;            /* 56h's called routine returns here */
    m[(unsigned long)EMM_SEG * 16 + EMM_CALL_RET + 1] = 0x67;
    wr16(h, 0x67 * 4, 0x20);
    wr16(h, 0x67 * 4 + 2, EMM_SEG);
    h->ems_total = 128;                                 /* 2 MB of expanded memory */
    /* the card */
    h->pic_mask[0] = h->pic_mask[1] = 0xFF;
    h->last_speech = -1.0;
    memcpy(m + (unsigned long)IDLE_SEG * 16, idle, sizeof idle);
    h->lpt = m[(unsigned long)LOAD_SEG * 16 + 0x53];    /* the driver answers INT 17h for this printer */
    /* its count of index marks passed is the byte at 0800:06E3 (found by watching memory while marked text was
       spoken): known by the one place the driver counts it up, MOV AL,[06E3] / INC AL / MOV [06E3],AL.  Another
       driver image without exactly that gives no marks. */
    {
        static const uint8_t count_up[8] = {0xA0, 0xE3, 0x06, 0xFE, 0xC0, 0xA2, 0xE3, 0x06};
        unsigned long at, found = 0;
        for (at = 0; at + sizeof count_up <= size; at++)
            if (!memcmp(m + (unsigned long)LOAD_SEG * 16 + at, count_up, sizeof count_up))
                found++;
        h->mark_at = found == 1 ? (unsigned long)LOAD_SEG * 16 + MARK_COUNT : 0;
        h->mark_seen = h->mark_at ? m[h->mark_at] : 0;
    }
    client[5] = (uint8_t)h->lpt;
    memcpy(m + (unsigned long)CLIENT_SEG * 16, client, sizeof client);
    pc86_set_hooks(h->p, on_in, on_out, on_int, h);
    w(h, SS, STACK_SEG);
    w(h, SP, 0xFFFE);
    return h;
}

AM_API am_host *amh_create_file(const char *path, ssi263 *chip, double out_rate, char *err, int errlen)
{
    FILE *f = bl_fopen(path, "rb");
    long len;
    unsigned char *d;
    am_host *h;
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "cannot open the driver");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "cannot read the driver");
        return NULL;
    }
    d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, (size_t)errlen, "cannot read the driver");
        return NULL;
    }
    fclose(f);
    h = amh_create(d, (size_t)len, chip, out_rate, err, errlen);
    free(d);
    return h;
}

AM_API void amh_destroy(am_host *h)
{
    int i;
    if (!h)
        return;
    pc86_destroy(h->p);
    if (h->own_chip)
        ssi263_free(h->chip);
    for (i = 0; i < h->n_pages; i++)
        free(h->pages[i].data);
    free(h->pages);
    free(h->handles);
    free(h->calls);
    free(h->saved);
    free(h->pend);
    free(h->buf);
    free(h->wl);
    free(h);
}

AM_API ssi263 *amh_chip(am_host *h) { return h->chip; }
AM_API const char *amh_error(const am_host *h) { return h->fault ? h->why : ""; }

/* ---- state ------------------------------------------------------------------------------------------------------- */
AM_API double amh_get_double(const am_host *h, const char *name)
{
    if (!strcmp(name, "cpu_ips")) return h->cpu_ips;
    if (!strcmp(name, "last_speech")) return h->last_speech;
    if (!strcmp(name, "say_time")) return h->say_time;
    if (!strcmp(name, "time")) return ssi263_time(h->chip);
    return -1.0;
}

AM_API void amh_set_double(am_host *h, const char *name, double v)
{
    if (!strcmp(name, "cpu_ips")) h->cpu_ips = v;
    else if (!strcmp(name, "last_speech")) h->last_speech = v;
    else if (!strcmp(name, "say_time")) h->say_time = v;
}

AM_API int amh_get_int(const am_host *h, const char *name)
{
    if (!strcmp(name, "preparing")) return h->preparing;
    if (!strcmp(name, "log_writes")) return h->log_on;
    if (!strcmp(name, "control")) return h->control;
    if (!strcmp(name, "in_service")) return h->in_service;
    if (!strcmp(name, "irq_latched")) return h->irq_latched;
    if (!strcmp(name, "call")) return h->call;
    if (!strcmp(name, "settles")) return h->settles;
    if (!strcmp(name, "request")) return ssi263_request(h->chip) ? 1 : 0;
    if (!strcmp(name, "marks")) return h->marks;
    if (!strcmp(name, "marks_known")) return h->mark_at ? 1 : 0;
    return -1;
}

AM_API void amh_set_int(am_host *h, const char *name, int v)
{
    if (!strcmp(name, "preparing"))
        h->preparing = v != 0;
    else if (!strcmp(name, "log_writes"))
        h->log_on = v != 0;
    else if (!strcmp(name, "marks")) {
        h->marks = v;
        marks_forget(h);
    }
}

AM_API int amh_writes(const am_host *h, const amh_write **writes)
{
    *writes = h->wl;
    return h->n_wl;
}

AM_API void amh_clear_writes(am_host *h) { h->n_wl = 0; }
