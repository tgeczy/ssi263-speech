/* tns_board.c -- the Type 'n Speak board (see tns_board.h).  Structured as bl_board.c: the board drives its CPU only
 * through ../cpu/cpu.h, every callback gets its tns_unit, no globals.  The serial port (ASCI0; port B0h bit 0 = its
 * line drivers on) is unplugged unless tns_serial_attach carries it to a real port (bl_serial.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cpu/cpu.h"
#include "tns_board.h"
#include "bl_clock.h"
#include "flash29.h"

#define RAM_SIZE 0x100000
#define IMAGE_MAX 0x40000
#define FLASH_SIZE 0x400000
#define FLASH_WINDOW 0xE0000
#define SLICE 10000
#define CLOCK_HZ 6144000.0
#define KEYQ 64

int tns_cold_break;

struct tns_unit {
    unsigned char *ram, *fdata;
    unsigned long image_len;
    flash29 ff;
    z180 *cpu;
    unsigned char ssi[5];
    int ssi_ctl, ssi_ar, ssi_mode;
    unsigned char port_f0, port_b0;
    bl_serial_line *line;                    /* tns_serial_attach: the serial port carried to a real port */
    unsigned char keyq[KEYQ], key_latch;
    int n_keyq, key_ready;
    bl_event *ev;
    int n_ev, cap_ev;
    unsigned char ppi_c;                     /* the 8255's port C outputs (control word at port C3h) */
    blc_clock *clk;                          /* tns_clock_on: the clock controller on the CSI/O (bl_clock.h) */
    unsigned char clk_tail[BLC_SAVE_SIZE];   /* the state's saved controller, until tns_clock_on */
    int has_clk_tail;
    long long clk_wall;
};

static void event(tns_unit *u, unsigned char type, unsigned char a, unsigned char b)
{
    if (u->n_ev == u->cap_ev) {
        int cap = u->cap_ev ? u->cap_ev * 2 : 256;
        bl_event *e = (bl_event *)realloc(u->ev, (size_t)cap * sizeof(bl_event));
        if (!e)
            return;
        u->ev = e;
        u->cap_ev = cap;
    }
    u->ev[u->n_ev].type = type;
    u->ev[u->n_ev].a = a;
    u->ev[u->n_ev].b = b;
    u->n_ev++;
}

static void ar_line(tns_unit *u)
{
    z180_set_irq(u->cpu, Z180_INT1, u->ssi_ar && u->ssi_mode ? 1 : 0);
}

static int in_window(const tns_unit *u, uint32_t A)
{
    return A >= FLASH_WINDOW && (u->port_f0 & 0x20);
}

static unsigned long window_off(const tns_unit *u, uint32_t A)
{
    return ((unsigned long)(u->port_f0 & 0x1F) << 17) | (A & 0x1FFFF);
}

/* ---- the bus the CPU sees (cpu.h) ------------------------------------------------------------------------------ */
static uint8_t mem_read(void *ctx, uint32_t A)
{
    tns_unit *u = (tns_unit *)ctx;
    A &= 0xFFFFF;
    if (in_window(u, A))
        return flash29_read(&u->ff, window_off(u, A), z180_cycles(u->cpu));
    return u->ram[A];
}

static void mem_write(void *ctx, uint32_t A, uint8_t V)
{
    tns_unit *u = (tns_unit *)ctx;
    A &= 0xFFFFF;
    if (in_window(u, A)) {
        flash29_write(&u->ff, window_off(u, A), V, z180_cycles(u->cpu));
        return;
    }
    if (A < u->image_len)
        return;                              /* the program itself is not written */
    u->ram[A] = V;
}

static uint8_t io_read(void *ctx, uint16_t Port)
{
    tns_unit *u = (tns_unit *)ctx;
    int p = Port & 0xFF;
    if (p >= 0x90 && p <= 0x94)
        return u->ssi_ar ? 0x80 : 0x00;
    if (p == 0xC1)                           /* the 8255's port B: bit 7 = the SSI-263 busy (its A/R request NOT
                                                asserted), the rest high.  The options menu's up arrow polls it;
                                                answered FFh the unit waited forever (Tomi).  The flash erase's clicks
                                                wait on it for 0 before each write: with bit 7 = A/R itself the unit
                                                hung at the first click, the chip idle and requesting */
        return (unsigned char)((u->ssi_ar ? 0x00 : 0x80) | 0x7F);
    if (p == 0xE0)                           /* status: battery good, switched on; bit 0 low while a key waits */
        return (unsigned char)(0xFE | (u->key_ready ? 0 : 1));
    if (p == 0xD0) {
        unsigned char v = u->key_latch;
        if (u->key_ready) {
            u->key_ready = 0;
            z180_set_irq(u->cpu, Z180_INT2, 0);
        }
        return v;
    }
    if (p < 0x40 && (Port >> 8))             /* the Z180's registers answer only a high byte of 0: the read goes out
                                                to a bus nothing drives, which still holds the port number of an
                                                IN A,(n) -- SIMON.BNS's IN A,(34h) wait (bl_board.c's io_read) */
        return (unsigned char)p;
    return 0xFF;                             /* 80h (watchdog) and the rest */
}

static void io_write(void *ctx, uint16_t Port, uint8_t V)
{
    tns_unit *u = (tns_unit *)ctx;
    int p = Port & 0xFF;
    if (p == 0xC3)
        blc_ppi_control(&u->ppi_c, V);       /* the 8255's control word: port C bit 4 calls the clock controller */
    if (p == 0xF0)
        u->port_f0 = V;
    else if (p == 0xB0)
        u->port_b0 = V;
    else if (p >= 0x90 && p <= 0x94) {
        int reg = p - 0x90;
        u->ssi[reg] = V;
        if (reg == 3)
            u->ssi_ctl = V >> 7;
        event(u, 'W', (unsigned char)reg, V);
        if (reg == 0 && u->ssi_ctl) {
            u->ssi_mode = V >> 6;
            ar_line(u);
        } else if (reg == 0) {
            u->ssi_ar = 0;                   /* the host raises it again when the chip asks */
            ar_line(u);
        }
    }
}

static void serial_note(tns_unit *u)         /* the line status now, in order with the bytes sent */
{
    z180_asci_regs r;
    bl_serial_status s;
    z180_asci_get(u->cpu, 0, &r);
    bl_serial_decode(&r, CLOCK_HZ, u->port_b0 & 1, &s);
    bl_serial_note(u->line, &s);
}

static int asci_rx(void *ctx, int channel)
{
    tns_unit *u = (tns_unit *)ctx;
    return u->line && channel == 0 ? bl_serial_next(u->line) : -1;
}

static void asci_tx(void *ctx, int channel, uint8_t data)
{
    tns_unit *u = (tns_unit *)ctx;
    if (u->line && channel == 0) {
        serial_note(u);
        bl_serial_sent(u->line, data);
    }
}
static int serial_pin(void *ctx, int pin) { (void)ctx; return pin == Z180_PIN_DCD0 ? 1 : 0; }

static void boundary(void *ctx, uint32_t pc)
{
    tns_unit *u = (tns_unit *)ctx;
    (void)pc;
    if (u->clk)
        blc_step(u->clk, u->cpu, (u->ppi_c & 0x10) != 0);
    if (u->n_keyq && !u->key_ready) {
        u->key_latch = u->keyq[0];
        memmove(u->keyq, u->keyq + 1, (size_t)--u->n_keyq);
        u->key_ready = 1;
        z180_set_irq(u->cpu, Z180_INT2, 1);
    }
}

/* the ROM image in an update file: the first 1000h-aligned offset from 2000h that starts F3 C3 .. .. FF "COPYRIGHT"
   (3000h in the 2000 revision, 5000h in the 1998 one) */
static long image_offset(const unsigned char *d, long n)
{
    long o;
    for (o = 0x2000; o + 16 < n; o += 0x1000)
        if (d[o] == 0xF3 && d[o + 1] == 0xC3 && d[o + 4] == 0xFF && !memcmp(d + o + 5, "COPYRIGHT", 9))
            return o;
    return -1;
}

long tns_program_end(const char *firmware)
{
    unsigned char *file = (unsigned char *)malloc(0x100000);
    FILE *f = file ? fopen(firmware, "rb") : NULL;
    long n, off;
    if (!f) { free(file); return -1; }
    n = (long)fread(file, 1, 0x100000, f);
    fclose(f);
    off = image_offset(file, n);
    free(file);
    return off < 0 || n - off > IMAGE_MAX ? -1 : n - off;
}

tns_unit *tns_create(const char *firmware, const char *state, char *err, int errlen)
{
    tns_unit *u = (tns_unit *)calloc(1, sizeof(tns_unit));
    unsigned char *file = NULL;
    long n, off;
    FILE *f;
    cpu_bus bus;
    if (!u) { snprintf(err, errlen, "out of memory"); return NULL; }
    u->ram = (unsigned char *)calloc(1, RAM_SIZE);
    u->fdata = (unsigned char *)malloc(FLASH_SIZE);
    file = (unsigned char *)malloc(0x100000);
    u->clk_wall = -1;
    if (!u->ram || !u->fdata || !file) { snprintf(err, errlen, "out of memory"); free(file); tns_destroy(u); return NULL; }
    memset(u->ram, 0xFF, IMAGE_MAX);
    memset(u->fdata, 0xFF, FLASH_SIZE);
    u->ff.data = u->fdata;                   /* an Am29F016 */
    u->ff.size = FLASH_SIZE;
    u->ff.maker = 0x01;
#ifdef BLAZIE_FLASH_BREAK
    u->ff.device = 0xA4;                     /* test_emu_unit's must-fail control: a 29F040's ID, which the firmware
                                                refuses ("no superflash detected", no erase) */
#else
    u->ff.device = 0xAD;
#endif
    u->ff.short_decode = 1;
    if (state) {
        FILE *s = fopen(state, "rb");
        if (!s || fread(u->ram, 1, RAM_SIZE, s) != RAM_SIZE || fread(u->fdata, 1, FLASH_SIZE, s) != FLASH_SIZE) {
            if (s) fclose(s);
            snprintf(err, errlen, "cannot read state %s", state);
            free(file);
            tns_destroy(u);
            return NULL;
        }
        u->has_clk_tail = blc_read_tail(s, u->clk_tail);   /* the clock controller, when it was on */
        fclose(s);
    }
    f = fopen(firmware, "rb");
    if (!f) { snprintf(err, errlen, "cannot open %s", firmware); free(file); tns_destroy(u); return NULL; }
    n = (long)fread(file, 1, 0x100000, f);
    fclose(f);
    off = image_offset(file, n);
    if (off < 0 || n - off > IMAGE_MAX) {
        snprintf(err, errlen, "no Type 'n Speak ROM image in %s", firmware);
        free(file);
        tns_destroy(u);
        return NULL;
    }
    u->image_len = (unsigned long)(n - off);
    memcpy(u->ram, file + off, u->image_len);   /* over a saved state too: the program always comes from the file */
    free(file);
    if (!state) {
        /* a cold start: Ctrl+Alt+Del held at power-on, the unit's own cold reset (the Type 'n Speak's real cold
           reset; Timothy, Jayson).  Blank RAM leaves the volume at 0 (every phoneme goes out with R3's amplitude
           0); the reset sets R3 = 56h, as the Braille Lite's defaults do.  Measured by watching the key port's
           reads: the firmware reads one key very early (that byte is used up), then reads three more, each of
           which must be a different one of Ctrl, Alt and Delete down -- so Ctrl goes twice.  Three of them: the
           cold reset, which asks to set up the file system, the flash and the folders (tns_setup.h); a key up
           among them (the old Ctrl, Alt, Del, Del up): the warm reset, which sets up none of them, so a new file
           went into the program's last byte (its first character lost) and a file moved to flash into folder 0,
           the deleted mark.  Then the keys come up. */
        static const unsigned char cold[] = {0x81, 0x81, 0xA1, 0xC9, 0x49, 0x21, 0x01};
        static const unsigned char old[] = {0x81, 0xA1, 0xC9, 0x49, 0x21, 0x01};
        const unsigned char *keys = tns_cold_break ? old : cold;
        int k, n = tns_cold_break ? (int)sizeof old : (int)sizeof cold;
        for (k = 0; k < n; k++)
            tns_key(u, keys[k]);
    }
    memset(&bus, 0, sizeof bus);
    bus.ctx = u;
    bus.read = mem_read;
    bus.write = mem_write;
    bus.in = io_read;
    bus.out = io_write;
    bus.serial_rx = asci_rx;
    bus.serial_tx = asci_tx;
    bus.serial_pin = serial_pin;
    bus.boundary = boundary;
    u->cpu = z180_create(&bus, CLOCK_HZ);
    if (!u->cpu) { snprintf(err, errlen, "cannot create the Z180"); tns_destroy(u); return NULL; }
    return u;
}

void tns_destroy(tns_unit *u)
{
    if (!u)
        return;
    if (u->cpu)
        z180_destroy(u->cpu);
    bl_serial_free(u->line);
    free(u->clk);
    free(u->ram);
    free(u->fdata);
    free(u->ev);
    free(u);
}

/* as bl_board.c's run_cycles: built with BL_Z180_MAME (MAME's core, ../cpu/z180_mame.cpp -- the emulator's, 0.7),
   whole steps; otherwise z180emu's legacy slice (a development reference only) */
#ifdef BL_Z180_MAME
#define TNS_RUN_SLICE z180_run
#else
#define TNS_RUN_SLICE z180_run_legacy
#endif
void tns_run(tns_unit *u, unsigned long long cycles)
{
    while (cycles > 0) {
        unsigned long long done = TNS_RUN_SLICE(u->cpu, cycles > SLICE ? SLICE : cycles);
        cycles = done >= cycles ? 0 : cycles - done;
    }
}

void tns_set_ar(tns_unit *u, int requesting)
{
    u->ssi_ar = requesting ? 1 : 0;
    ar_line(u);
}

int tns_key(tns_unit *u, int code)
{
    if (u->n_keyq == KEYQ)
        return 0;
    u->keyq[u->n_keyq++] = (unsigned char)code;
    return 1;
}

unsigned long long tns_cycles(const tns_unit *u)
{
    return z180_cycles(u->cpu);
}

int tns_events(const tns_unit *u, const bl_event **events)
{
    *events = u->ev;
    return u->n_ev;
}

void tns_clear_events(tns_unit *u)
{
    u->n_ev = 0;
}

int tns_serial_attach(tns_unit *u, int on)
{
    if (on && !u->line) {
        u->line = bl_serial_new();
        if (!u->line)
            return 0;
        serial_note(u);
    } else if (!on && u->line) {
        bl_serial_free(u->line);
        u->line = NULL;
    }
    return 1;
}

int tns_serial_write(tns_unit *u, const unsigned char *bytes, int n)
{
    return u->line ? bl_serial_put(u->line, bytes, n) : 0;
}

int tns_serial_space(const tns_unit *u)
{
    return u->line ? bl_serial_room(u->line) : 0;
}

int tns_serial_read(tns_unit *u, unsigned char *out, int cap, bl_serial_status *status)
{
    if (!u->line) {                          /* unplugged: nothing sent, the status now */
        z180_asci_regs r;
        z180_asci_get(u->cpu, 0, &r);
        if (status)
            bl_serial_decode(&r, CLOCK_HZ, u->port_b0 & 1, status);
        return 0;
    }
    serial_note(u);
    return bl_serial_take(u->line, out, cap, status);
}

int tns_save_state(const tns_unit *u, const char *path)
{
    FILE *s = fopen(path, "wb");
    int ok;
    if (!s)
        return 0;
    ok = fwrite(u->ram, 1, RAM_SIZE, s) == RAM_SIZE && fwrite(u->fdata, 1, FLASH_SIZE, s) == FLASH_SIZE;
    if (ok && u->clk)
        ok = blc_write_tail(s, u->clk, u->clk_wall);
    return fclose(s) == 0 && ok;
}

int tns_clock_on(tns_unit *u, const blc_time *now, long long unix_now)
{
    long long saved_at = -1;
    if (u->clk)
        return 1;
    u->clk = (blc_clock *)malloc(sizeof(blc_clock));
    if (!u->clk)
        return 0;
    blc_init(u->clk, CLOCK_HZ, z180_cycles(u->cpu));
    if (u->has_clk_tail && blc_load(u->clk, u->clk_tail, &saved_at)) {
        if (saved_at >= 0 && unix_now >= 0)
            blc_advance_off(u->clk, (double)(unix_now - saved_at));   /* it kept time while the unit was off */
        blc_migrate(u->clk, now);            /* a unit saved on 0.7.0-0.7.5's year (issue #3) */
    } else if (now)
        blc_set(u->clk, now);
    u->clk_wall = unix_now;
    return 1;
}

int tns_clock_time(const tns_unit *u, int alarm, blc_time *t)
{
    if (!u->clk)
        return 0;
    blc_get(u->clk, alarm, t);
    return 1;
}

void tns_clock_wall(tns_unit *u, long long unix_now)
{
    u->clk_wall = unix_now;
}

int tns_memory(const tns_unit *u, int which, const unsigned char **bytes)
{
    *bytes = which ? u->fdata : u->ram;
    return which ? FLASH_SIZE : RAM_SIZE;
}

void tns_flash_timed(tns_unit *u, int on)
{
    u->ff.hz = on ? CLOCK_HZ : 0.0;
}

int tns_flash_busy(const tns_unit *u, unsigned long *chip_erases, unsigned long *sector_erases)
{
    if (chip_erases) *chip_erases = u->ff.n_chip_erase;
    if (sector_erases) *sector_erases = u->ff.n_sector_erase;
    return flash29_busy(&u->ff, z180_cycles(u->cpu));
}
