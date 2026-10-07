/* bl_board.c -- the Blazie Braille Lite 2000 board around a Z180, per instance (see bl_board.h); the Braille 'n
 * Speak 2000's too, which is the same board without the braille display (bl_model).
 *
 * z180emu/bns.c's --live path, line for line, with the tracing and logging left out.  The board drives its CPU only
 * through ../cpu/cpu.h: every callback gets its bl_unit as ctx, so there is no shared or thread-local state here.
 * Today the Z180 behind it is z180emu's, on the legacy path (../cpu/z180_legacy.c), which also carries what the board
 * used to reach into -- the ASCI's baud ticking and request level, /DCD0 -- so the golden vectors hold bit for bit.
 *
 * Memory: ROM image from file offset 3000h at physical 00000h (256 KB, FFh-padded); 40000h..FFFFFh RAM, except that
 * port E0h bit 3 maps the file flash over 80000h..FFFFFh: 512 KB of a 2 MB 29F016-style chip, bits 0-1 picking which
 * (measured: the firmware's file system is 2 MB -- its file list says "2045 k flash" free -- and a file moved to flash
 * is programmed with E0h = 0Bh at window offset 7FE00h, the top of the fourth 512 KB).
 * E0h bit 4 picks the program flash's other bank (p-chord l: the firmware checks a program is there and
 * restarts into it): the same image is in both here, so the unit restarts in its own language.  SSI-263 at
 * ports C0h..C4h, its A/R
 * request on /INT1.  Braille keyboard on port 40h, /INT2.  Serial on ASCI0 (9600 bit/s at 6.144 MHz), with the
 * host honouring the unit's XON/XOFF.  Port A0h bit 0 switches the serial port's line drivers on (bl_serial.h).
 * An 8255 at 80h-83h: its control word at 83h sets and clears port C's bits (bits 0-2 clock the braille display;
 * bit 4 calls the clock controller, which the firmware talks to over the Z180's CSI/O: bl_clock.h, on with
 * bl_clock_on).  Port 40h also reads the keys held down (bl_keys_down) while no chord waits.
 * With bl_serial_attach the serial port is carried to a real port instead (the emulator app's COM port): bytes
 * both ways through a bl_serial_line, the host's XON/XOFF handling left to the far end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cpu/cpu.h"
#include "bl_board.h"
#include "bl_serial.h"
#include "bl_clock.h"
#include "bl_display.h"
#include "flash29.h"

#ifdef BH_TRACE                              /* a scratch build's trace (never in a release build) */
FILE *bh_trace_fp;
unsigned bh_watch_lo[8], bh_watch_hi[8];
int bh_n_watch;
int bh_trace_on;
#define TR(...) do { if (bh_trace_fp && bh_trace_on) fprintf(bh_trace_fp, __VA_ARGS__); } while (0)
#else
#define TR(...) do { } while (0)
#endif

#define ROM_SIZE 0x40000
#define FILE_ROM_OFFSET 0x3000
#define SLICE_DEFAULT 10000
#define FLASH_BASE 0x80000
#define FFLASH_SIZE 0x200000UL               /* the file flash: a 29F016 */
#define FFLASH_OLD 0x80000UL                 /* a state saved before the 2 MB chip: its first 512 KB */
#define MAX_KEYS 64
#define FIFO 0x10000

struct bl_unit {
    unsigned char *flash, *ram, *fflash;
    flash29 ff;                              /* the file flash's command logic, over fflash */
    z180 *cpu;
    double clock_hz, phon_ms;
    int irq_line;
    unsigned char ssi_ready_value, ssi[5];
    int ssi_ctl, ssi_ar, ssi_mode, live_on;
    unsigned long long ssi_ready_at;
    unsigned char *lfifo;
    unsigned lhead, ltail;
    int urgent, host_xoff;
    unsigned long long key_at[MAX_KEYS];
    unsigned char key_val[MAX_KEYS], key_latch;
    int n_keys, next_key, key_latched, hold_chord;
    unsigned char live_keys[16];             /* bl_key: chords pressed live, delivered one per boundary when free */
    int n_live_keys;
    int batt_level, batt_bit, batt_fresh;    /* the battery gauge's serial A/D converter (bl_battery) */
    unsigned char port_e0, port_a0;
    bl_serial_line *line;                    /* bl_serial_attach: the serial port carried to a real port */
    unsigned site_release;
    uint32_t instr_pc;
    bl_event *ev;
    int n_ev, cap_ev;
    int ev_lost, fail_ev;                    /* events that could not be stored; tests: the n-th to fail */
    int held;                                /* bl_keys_down: the braille keys physically down now */
    int boot_phase;                          /* from a (re)start until the firmware reads its first chord */
    int held_seen;                           /* the firmware read the keys held now during its start */
    int starts;                              /* bl_starts: times the firmware ran from its reset vector */
    unsigned char ppi_c;                     /* the 8255's port C outputs (control word at port 83h) */
    blc_clock *clk;                          /* bl_clock_on: the clock controller on the CSI/O */
    unsigned char clk_tail[BLC_SAVE_SIZE];   /* the state's saved controller, until bl_clock_on */
    int has_clk_tail;
    long long clk_wall;                      /* bl_clock_wall: the host's time for the next save */
    int model;                               /* bl_model */
    bld_display display;                    /* Latched dots, independent of RAM/text translation. */
    int braille_bars;                        /* Forward/back contacts on PPI B6/B7, active low. */
};

static void event(bl_unit *u, unsigned char type, unsigned char a, unsigned char b)
{
    int fail = u->fail_ev > 0 && --u->fail_ev == 0;       /* bl_fail_event (tests) */
    if (u->n_ev == u->cap_ev || fail) {
        int cap = u->cap_ev ? u->cap_ev * 2 : 256;
        bl_event *e = fail ? NULL : (bl_event *)realloc(u->ev, (size_t)cap * sizeof(bl_event));
        if (!e) {
            u->ev_lost++;                                  /* lost: counted, for the host (bl_events_lost) */
            return;
        }
        u->ev = e;
        u->cap_ev = cap;
    }
    u->ev[u->n_ev].type = type;
    u->ev[u->n_ev].a = a;
    u->ev[u->n_ev].b = b;
    u->n_ev++;
}

static void ar_line(bl_unit *u)             /* A/R request drives the INT line (level, active = asserted) */
{
    if (u->irq_line >= 0)
        z180_set_irq(u->cpu, u->irq_line, u->ssi_ar && u->ssi_mode ? 1 : 0);
}

static int flash_window(const bl_unit *u)
{
    return (u->port_e0 & 0x08) != 0;
}

static unsigned long flash_off(const bl_unit *u, uint32_t A)   /* the chip address behind the window */
{
#ifdef BLAZIE_FLASH_BREAK                    /* test_emu_unit's must-fail control: the old 512 KB, every bank on it */
    return A - FLASH_BASE;
#else
    return ((unsigned long)(u->port_e0 & 0x03) << 19) | (A - FLASH_BASE);
#endif
}

/* ---- the bus the CPU sees (cpu.h) -------------------------------------------------------------------------------- */
static uint8_t mem_read(void *ctx, uint32_t A)
{
    bl_unit *u = (bl_unit *)ctx;
    A &= 0xFFFFF;
    if (A < ROM_SIZE) return u->flash[A];
    if (A >= FLASH_BASE && flash_window(u)) return flash29_read(&u->ff, flash_off(u, A), z180_cycles(u->cpu));
    return u->ram[A];
}

static void mem_write(void *ctx, uint32_t A, uint8_t V)
{
    bl_unit *u = (bl_unit *)ctx;
    A &= 0xFFFFF;
    if (A < ROM_SIZE)
        return;                              /* ROM: bns.c counts and logs these; after a hard reset there are none */
    if (A >= FLASH_BASE && flash_window(u)) {
        flash29_write(&u->ff, flash_off(u, A), V, z180_cycles(u->cpu));
        return;
    }
#ifdef BH_TRACE
    {
        int i;
        for (i = 0; i < bh_n_watch; i++)
            if (A >= bh_watch_lo[i] && A <= bh_watch_hi[i])
                TR("MW %llu pc=%04X %05X %02X->%02X\n", (unsigned long long)z180_cycles(u->cpu), u->instr_pc,
                   (unsigned)A, u->ram[A], V);
    }
#endif
    u->ram[A] = V;
}

static uint8_t io_read(void *ctx, uint16_t Port)
{
    bl_unit *u = (bl_unit *)ctx;
    int p = Port & 0xFF;
    unsigned char v;
    if (p >= 0xC0 && p <= 0xC4)
        return u->ssi_ar ? u->ssi_ready_value : (unsigned char)(u->ssi_ready_value ^ 0x80);
    /* 00h-3Fh with the address's high byte not 0: the Z180's own registers answer there only when it is 0 (IN0), so
       the read goes out to the board, where nothing drives the data bus (the keyboard handler's own IN A,(20h) throws
       its value away).  The bus still holds the last byte put on it, for IN A,(n) the port number.  Blazie's game
       SIMON.BNS waits on IN A,(34h) bit 0 before every tone, A not 0 as traced: answered FFh it waited forever and the
       unit locked up at the first key.  Inferred from the game playing on the units, not measured on one. */
    if (p < 0x40 && (Port >> 8))
        return (unsigned char)p;
    v = p == 0x40 ? 0x00 : 0xFF;
    if (p == 0x81 && u->model == BL_MODEL_BRAILLE_LITE)
        v &= (unsigned char)~((u->braille_bars & 3) << 6);
    /* The battery gauge (status menu, %): a serial A/D converter, found by running the firmware.  Each read of B0h
       clocks it; the first read of 81h after the clock shows bit 3 low (the firmware waits for that), later reads
       carry the next data bit in bit 3, least significant first, 8 bits.  Without it bit 3 stayed high and the unit
       waited forever. */
    if (p == 0xB0 && u->batt_level >= 0) {
        u->batt_bit = (u->batt_bit + 1) & 7;
        u->batt_fresh = 1;
    }
    if (p == 0x81 && u->batt_level >= 0) {
        if (u->batt_fresh)
            v &= (unsigned char)~0x08;
        else if (!((u->batt_level >> ((u->batt_bit + 7) & 7)) & 1))
            v &= (unsigned char)~0x08;
        u->batt_fresh = 0;
    }
    if (p == 0x40 && u->hold_chord >= 0) {
        if (u->instr_pc == u->site_release || u->instr_pc == u->site_release + 8)
            u->hold_chord = -1;              /* the firmware now waits for release */
        else
            v = (unsigned char)u->hold_chord;
    }
    if (p == 0x40 && u->hold_chord < 0 && u->held) {
        v = (unsigned char)u->held;          /* keys held down (bl_keys_down): read as they are */
        if (u->boot_phase)
            u->held_seen = 1;                /* the start-up read them: their chord is the firmware's already */
    }
    if (p == 0x40 && u->key_latched) {
        v = u->key_latch;
        u->key_latched = 0;
        u->boot_phase = 0;
        z180_set_irq(u->cpu, Z180_INT2, 0);
    }
    return v;
}

static void io_write(void *ctx, uint16_t Port, uint8_t V)
{
    bl_unit *u = (bl_unit *)ctx;
    int p = Port & 0xFF;
    if (p == 0xE0)
        u->port_e0 = V;
    if (p == 0x83) {
        unsigned char before = u->ppi_c;
        blc_ppi_control(&u->ppi_c, V);       /* the 8255's control word: port C bit 4 calls the clock controller */
        bld_port(&u->display, before, u->ppi_c, (V & 0x80) != 0);
    }
    if (p == 0x82) {
        unsigned char before = u->ppi_c;
        u->ppi_c = V;
        bld_port(&u->display, before, V, 0);
    }
    if (p == 0xA0)
        u->port_a0 = V;                      /* bit 0: the serial port's line drivers on; bit 1: the speech
                                                channel's power (bl_port_a0) */
    if (p >= 0xC0 && p <= 0xC4) {
        int reg = p - 0xC0;
        unsigned long long cyc = z180_cycles(u->cpu);
        u->ssi[reg] = V;
        TR("IO %llu pc=%04X r%d=%02X\n", cyc, u->instr_pc, reg, V);
        if (reg == 3)
            u->ssi_ctl = V >> 7;
        event(u, 'W', (unsigned char)reg, V);
        if (reg == 0 && u->ssi_ctl) {
            u->ssi_mode = V >> 6;
            ar_line(u);
        } else if (reg == 0) {
            u->ssi_ar = 0;
            u->ssi_ready_at = u->live_on ? ~0ULL
                            : cyc + (unsigned long long)(u->phon_ms * u->clock_hz / 1000.0);
            ar_line(u);
        }
    }
}

int bl_braille(const bl_unit *u, unsigned char *cells, int capacity)
{
    int n = u->display.count;
    if (u->model != BL_MODEL_BRAILLE_LITE || capacity < n) return 0;
    if (n) memcpy(cells, u->display.cells, (size_t)n);
    return n;
}

void bl_braille_bars(bl_unit *u, int down)
{
    u->braille_bars = down & 3;
}

static int asci_rx(void *ctx, int channel)
{
    bl_unit *u = (bl_unit *)ctx;
    int b;
    if (u->line)                             /* a real port: what arrived on it, paced by the ASCI's own clock */
        return channel == 0 ? bl_serial_next(u->line) : -1;
    if (u->live_on) {
        if (channel == 0 && u->urgent >= 0) {
            b = u->urgent;
            u->urgent = -1;
            TR("RX %llu urgent %02X\n", (unsigned long long)z180_cycles(u->cpu), b);
            return b;
        }
        if (channel != 0 || u->lhead == u->ltail || u->host_xoff)
            return -1;
        TR("RX %llu %02X left=%u\n", (unsigned long long)z180_cycles(u->cpu), u->lfifo[u->ltail & (FIFO - 1)],
           u->lhead - u->ltail - 1);
        return u->lfifo[u->ltail++ & (FIFO - 1)];
    }
    return -1;                               /* before live mode nothing is queued (bns: no --serial in live runs) */
}

static void serial_note(bl_unit *u)          /* the line status now, in order with the bytes sent */
{
    z180_asci_regs r;
    bl_serial_status s;
    z180_asci_get(u->cpu, 0, &r);
    bl_serial_decode(&r, u->clock_hz, u->port_a0 & 1, &s);
    bl_serial_note(u->line, &s);
}

static void asci_tx(void *ctx, int channel, uint8_t data)
{
    bl_unit *u = (bl_unit *)ctx;
    if (channel != 0)
        return;
    if (u->line) {
        serial_note(u);
        bl_serial_sent(u->line, data);
        return;
    }
    event(u, 'T', data, 0);
    TR("TX %llu %02X\n", (unsigned long long)z180_cycles(u->cpu), data);
    if (data == 0x13)
        u->host_xoff = 1;
    else if (data == 0x11)
        u->host_xoff = 0;
}

static int serial_pin(void *ctx, int pin)
{
    (void)ctx;
    return pin == Z180_PIN_DCD0 ? 1 : 0;     /* /DCD0 = carrier present (see bns.c) */
}

/* Every step's boundary (cpu.h phase D), after the core has counted the step and caught its ASCI up. */
static void boundary(void *ctx, uint32_t pc)
{
    bl_unit *u = (bl_unit *)ctx;
    u->instr_pc = pc & 0xFFFF;               /* read by the key-release check (port reads) */
    if (!u->instr_pc) {
        u->boot_phase = 1;                   /* the reset vector: power-on, or the firmware restarting itself */
        u->starts++;
    }
    if (u->clk)
        blc_step(u->clk, u->cpu, (u->ppi_c & 0x10) != 0);
    if (!u->live_on && !u->ssi_ar && z180_cycles(u->cpu) >= u->ssi_ready_at) {
        u->ssi_ar = 1;
        ar_line(u);
    }
    if (u->next_key < u->n_keys && z180_steps(u->cpu) >= u->key_at[u->next_key]) {
        if (u->hold_chord >= 0)
            u->hold_chord = -1;              /* a hand pressing a key has let go of the power-on chord */
        u->key_latch = u->key_val[u->next_key++];
        u->key_latched = 1;
        z180_set_irq(u->cpu, Z180_INT2, 1);
    } else if (u->n_live_keys && !u->key_latched) {  /* a live chord, once the last one has been read */
        u->hold_chord = -1;
        u->key_latch = u->live_keys[0];
        memmove(u->live_keys, u->live_keys + 1, (size_t)--u->n_live_keys);
        u->key_latched = 1;
        z180_set_irq(u->cpu, Z180_INT2, 1);
    }
}

/* a byte signature ("F6 40 D3 C0", "??" = any byte) in the ROM image: the offset of the unique match plus `off`,
   or 0 if absent or ambiguous (bns.c's detect_sites) */
static unsigned find_sig(const bl_unit *u, const char *sig, int off, long img_len)
{
    int pat[32], n = 0, hits = 0;
    unsigned at = 0;
    long i;
    const char *s = sig;
    while (*s && n < 32) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] == '?') pat[n++] = -1;
        else { unsigned v; sscanf(s, "%2x", &v); pat[n++] = (int)v; }
        s += 2;
    }
    for (i = 0; i + n <= img_len; i++) {
        int k;
        for (k = 0; k < n; k++)
            if (pat[k] >= 0 && u->flash[i + k] != pat[k]) break;
        if (k == n) { hits++; at = (unsigned)i + off; }
    }
    return hits == 1 ? at : 0;
}

bl_unit *bl_create(const char *firmware, const char *state, double phon_ms,
                   const unsigned long long *key_at, const unsigned char *key_val, int n_keys,
                   char *err, int errlen)
{
    bl_unit *u = (bl_unit *)calloc(1, sizeof(bl_unit));
    FILE *f;
    long n;
    int i;
    cpu_bus bus;
    if (!u) { snprintf(err, errlen, "out of memory"); return NULL; }
    u->flash = (unsigned char *)malloc(ROM_SIZE);
    u->ram = (unsigned char *)calloc(1, 0x100000);
    u->fflash = (unsigned char *)malloc(FFLASH_SIZE);
    u->lfifo = (unsigned char *)malloc(FIFO);
    if (!u->flash || !u->ram || !u->fflash || !u->lfifo) { snprintf(err, errlen, "out of memory"); bl_destroy(u); return NULL; }
    u->ff.data = u->fflash;                  /* an Am29F016 (the firmware never reads its ID) */
    u->ff.size = FFLASH_SIZE;
    u->ff.maker = 0x01;
    u->ff.device = 0xAD;
    u->clock_hz = 6144000.0;
    u->phon_ms = phon_ms;
    u->irq_line = Z180_INT1;
    u->ssi_ready_value = 0x80;
    u->ssi_ar = 1;
    u->urgent = -1;
    u->batt_level = -1;                      /* no battery gauge unless asked for (bl_battery) */
    u->hold_chord = -1;
    u->boot_phase = 1;
    u->clk_wall = -1;
    for (i = 0; i < n_keys && i < MAX_KEYS; i++) {
        u->key_at[i] = key_at[i];
        u->key_val[i] = key_val[i];
    }
    u->n_keys = n_keys < MAX_KEYS ? n_keys : MAX_KEYS;
    memset(u->flash, 0xFF, ROM_SIZE);
    memset(u->fflash, 0xFF, FFLASH_SIZE);
    if (state) {                             /* battery-backed RAM + file flash, as a real unit keeps them: the whole
                                                2 MB, or its first 512 KB (bl_save_state's format while the rest is
                                                erased, and every state saved before the 2 MB chip) */
        /* by its size: RAM, then the flash (512 KB or 2 MB), then the clock controller's tail when it was on
           (bl_clock_on) -- four sizes in all; the tail is never read as flash */
        FILE *s = fopen(state, "rb");
        long size = -1, rest;
        unsigned long fl = 0;
        int tail = 0;
        if (s && fseek(s, 0, SEEK_END) == 0)
            size = ftell(s);
        rest = size - 0x40000L;
        if (rest == (long)(FFLASH_OLD + BLC_SAVE_SIZE) || rest == (long)(FFLASH_SIZE + BLC_SAVE_SIZE)) {
            tail = 1;
            fl = (unsigned long)rest - BLC_SAVE_SIZE;
        } else if (rest == (long)FFLASH_OLD || rest == (long)FFLASH_SIZE)
            fl = (unsigned long)rest;
        if (!s || !fl || fseek(s, 0, SEEK_SET) != 0 || fread(u->ram + 0x40000, 1, 0x40000, s) != 0x40000
            || fread(u->fflash, 1, fl, s) != fl) {
            if (s) fclose(s);
            snprintf(err, errlen, "cannot read state %s", state);
            bl_destroy(u);
            return NULL;
        }
        u->has_clk_tail = tail && blc_read_tail(s, u->clk_tail);
        fclose(s);
    }
    f = fopen(firmware, "rb");
    if (!f) { snprintf(err, errlen, "cannot open %s", firmware); bl_destroy(u); return NULL; }
    fseek(f, FILE_ROM_OFFSET, SEEK_SET);
    n = (long)fread(u->flash, 1, ROM_SIZE, f);
    fclose(f);
    {                                        /* bns.c refuses a firmware without these sites; so does this */
        unsigned send = find_sig(u, "F6 40 D3 C0", 2, n);
        unsigned prime = find_sig(u, "F6 E0 D3 C4 CD ?? ?? 3E C0 D3 C0", 9, n);
        unsigned fetch = find_sig(u, "2A 17 D6 7E 23 22 17 D6", 3, n);
        unsigned halt = find_sig(u, "21 ?? ?? 7E B7 20 06 76 CD", 7, n);
        if (!halt) {                         /* the idle loop of Blazie's other units: with the sites above, only the
                                                Braille 'n Speak 2000's images on Tomi's disks have it (the Braille
                                                Lite 18 and 40, the BNS 640 and the Type 'n Speak lack the others) */
            halt = find_sig(u, "21 ?? ?? 7E B7 20 09 3A ?? ?? 76 CD", 10, n);
            u->model = BL_MODEL_BNS2000;
        }
        u->site_release = find_sig(u, "DB 40 D3 20 E6 7F 20 F8", 0, n);
        if (!(send && prime && fetch && halt && u->site_release)) {
            snprintf(err, errlen, "could not locate the firmware sites in %s", firmware);
            bl_destroy(u);
            return NULL;
        }
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
    u->cpu = z180_create(&bus, u->clock_hz);
    if (!u->cpu) { snprintf(err, errlen, "cannot create the Z180"); bl_destroy(u); return NULL; }
    return u;
}

void bl_destroy(bl_unit *u)
{
    if (!u)
        return;
    if (u->cpu)
        z180_destroy(u->cpu);
    bl_serial_free(u->line);
    free(u->clk);
    free(u->flash);
    free(u->ram);
    free(u->fflash);
    free(u->lfifo);
    free(u->ev);
    free(u);
}

/* the legacy path's own slicing (bns.c's): the golden vectors depend on it (CONTRACT.md 3).  Built with
   BL_Z180_MAME (the MAME core, ../cpu/z180_mame.cpp), the corrected path runs whole steps instead: the same
   slices, but a slice boundary changes nothing there (CONTRACT.md 2). */
#ifdef BL_Z180_MAME
#define BL_RUN_SLICE z180_run
#else
#define BL_RUN_SLICE z180_run_legacy
#endif
static void run_cycles(bl_unit *u, unsigned long long n)
{
    while (n > 0) {
        unsigned long long done = BL_RUN_SLICE(u->cpu, n > SLICE_DEFAULT ? SLICE_DEFAULT : n);
        n = done >= n ? 0 : n - done;
    }
}

void bl_boot(bl_unit *u, unsigned long long target_instr)
{
    while (z180_steps(u->cpu) < target_instr)
        run_cycles(u, SLICE_DEFAULT);
}

void bl_live(bl_unit *u)
{
    u->live_on = 1;
    u->ssi_ar = 0;
    u->ssi_ready_at = ~0ULL;
    ar_line(u);
}

void bl_run(bl_unit *u, unsigned long long cycles)
{
    run_cycles(u, cycles);
}

void bl_set_ar(bl_unit *u, int requesting)
{
    TR("AR %llu %d\n", (unsigned long long)z180_cycles(u->cpu), requesting ? 1 : 0);
    u->ssi_ar = requesting ? 1 : 0;
    ar_line(u);
}

void bl_queue(bl_unit *u, const unsigned char *bytes, int n)
{
    int i;
    TR("Q %llu n=%d\n", (unsigned long long)z180_cycles(u->cpu), n);
    for (i = 0; i < n; i++)
        u->lfifo[u->lhead++ & (FIFO - 1)] = bytes[i];
}

void bl_urgent(bl_unit *u, int byte)
{
    TR("URG %llu %02X (pending %d)\n", (unsigned long long)z180_cycles(u->cpu), byte & 0xFF, u->urgent);
    u->urgent = byte & 0xFF;
}

int bl_drop(bl_unit *u)
{
    int n6 = 0;
    TR("DROP %llu n=%u\n", (unsigned long long)z180_cycles(u->cpu), u->lhead - u->ltail);
    while (u->ltail != u->lhead) {
        if (u->lfifo[u->ltail & (FIFO - 1)] == 0x06)
            n6++;
        u->ltail++;
    }
    return n6;
}

int bl_save_state(const bl_unit *u, const char *path)
{
    FILE *s = fopen(path, "wb");
    int ok;
    unsigned long n = FFLASH_SIZE, i;
    if (!s)
        return 0;
    for (i = FFLASH_OLD; i < FFLASH_SIZE && u->fflash[i] == 0xFF; i++)
        ;
    if (i == FFLASH_SIZE)
        n = FFLASH_OLD;                      /* the rest erased: the 786432-byte format every reader knows */
    ok = fwrite(u->ram + 0x40000, 1, 0x40000, s) == 0x40000 && fwrite(u->fflash, 1, n, s) == n;
    if (ok && u->clk)
        ok = blc_write_tail(s, u->clk, u->clk_wall);
    return fclose(s) == 0 && ok;
}

void bl_flash_timed(bl_unit *u, int on)
{
    u->ff.hz = on ? u->clock_hz : 0.0;
}

int bl_flash_busy(const bl_unit *u, unsigned long *chip_erases, unsigned long *sector_erases)
{
    if (chip_erases) *chip_erases = u->ff.n_chip_erase;
    if (sector_erases) *sector_erases = u->ff.n_sector_erase;
    return flash29_busy(&u->ff, z180_cycles(u->cpu));
}

void bl_battery(bl_unit *u, int level)
{
    u->batt_level = level;
    u->batt_bit = 7;
    u->batt_fresh = 0;
}

int bl_key(bl_unit *u, int chord)
{
    if (u->held_seen && !bl_keys_break) {    /* held through a start, which read it (bl_keys_down): not again */
        u->held_seen = 0;
        return 1;
    }
    if (u->n_live_keys == (int)sizeof u->live_keys)
        return 0;
    u->live_keys[u->n_live_keys++] = (unsigned char)chord;
    return 1;
}

void bl_hold(bl_unit *u, int chord)
{
    u->hold_chord = chord & 0x7F;            /* bns.c's --hold */
}

int bl_keys_break;

int bl_starts(const bl_unit *u)
{
    return u->starts;
}

int bl_model(const bl_unit *u)
{
    return u->model;
}

void bl_keys_down(bl_unit *u, int bits)
{
    if (!u->held && bits)
        u->held_seen = 0;                    /* a new chord begins */
    u->held = bits & 0xFF;
}

int bl_clock_on(bl_unit *u, const blc_time *now, long long unix_now)
{
    long long saved_at = -1;
    if (u->clk)
        return 1;
    u->clk = (blc_clock *)malloc(sizeof(blc_clock));
    if (!u->clk)
        return 0;
    blc_init(u->clk, u->clock_hz, z180_cycles(u->cpu));
    if (u->has_clk_tail && blc_load(u->clk, u->clk_tail, &saved_at)) {
        if (saved_at >= 0 && unix_now >= 0)
            blc_advance_off(u->clk, (double)(unix_now - saved_at));   /* it kept time while the unit was off */
        blc_migrate(u->clk, now);            /* a unit saved on 0.7.0-0.7.5's year (issue #3) */
    } else if (now)
        blc_set(u->clk, now);
    u->clk_wall = unix_now;
    return 1;
}

int bl_clock_time(const bl_unit *u, int alarm, blc_time *t)
{
    if (!u->clk)
        return 0;
    blc_get(u->clk, alarm, t);
    return 1;
}

void bl_clock_wall(bl_unit *u, long long unix_now)
{
    u->clk_wall = unix_now;
}

unsigned long long bl_cycles(const bl_unit *u)
{
    return z180_cycles(u->cpu);
}

int bl_serial_attach(bl_unit *u, int on)
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

int bl_serial_write(bl_unit *u, const unsigned char *bytes, int n)
{
    return u->line ? bl_serial_put(u->line, bytes, n) : 0;
}

int bl_serial_space(const bl_unit *u)
{
    return u->line ? bl_serial_room(u->line) : 0;
}

int bl_serial_read(bl_unit *u, unsigned char *out, int cap, bl_serial_status *status)
{
    if (!u->line) {                          /* unplugged: nothing sent, the status now */
        z180_asci_regs r;
        z180_asci_get(u->cpu, 0, &r);
        if (status)
            bl_serial_decode(&r, u->clock_hz, u->port_a0 & 1, status);
        return 0;
    }
    serial_note(u);
    return bl_serial_take(u->line, out, cap, status);
}

int bl_port_a0(const bl_unit *u)
{
    return u->port_a0;
}

void bl_probe_get(const bl_unit *u, bl_probe *p)
{
    z180_regs r;
    z180_asci_regs a;
    int i;
    memset(p, 0, sizeof *p);
    z180_regs_get(u->cpu, &r);
    z180_asci_get(u->cpu, 0, &a);
    p->cycles = z180_cycles(u->cpu);
    p->pc = r.pc;
    p->sp = r.sp;
    p->iff1 = r.iff1;
    p->iff2 = r.iff2;
    p->im = r.im;
    p->halted = r.halted;
    p->sleeping = r.sleeping;
    for (i = 0; i < 5; i++)
        p->ssi[i] = u->ssi[i];
    p->ssi_ar = u->ssi_ar;
    p->ssi_mode = u->ssi_mode;
    p->int1 = u->ssi_ar && u->ssi_mode;
    p->key_latched = u->key_latched;
    p->int2 = u->key_latched;
    p->n_live_keys = u->n_live_keys;
    p->queued = (int)(u->lhead - u->ltail);
    p->urgent = u->urgent;
    p->host_xoff = u->host_xoff;
    p->port_a0 = u->port_a0;
    p->port_e0 = u->port_e0;
    p->asci_cntla = a.cntla;
    p->asci_cntlb = a.cntlb;
    p->asci_stat = a.stat;
    p->asci_asext = a.asext;
    p->asci_astc = a.astc;
}

int bl_memory(const bl_unit *u, int which, const unsigned char **bytes)
{
    *bytes = which ? u->fflash : u->ram;
    return which ? (int)FFLASH_SIZE : 0x100000;
}

int bl_events(const bl_unit *u, const bl_event **events)
{
    *events = u->ev;
    return u->n_ev;
}

void bl_clear_events(bl_unit *u)
{
    u->n_ev = 0;
}

int bl_events_lost(const bl_unit *u)
{
    return u->ev_lost;
}

void bl_clear_events_lost(bl_unit *u)
{
    u->ev_lost = 0;
}

void bl_fail_event(bl_unit *u, int n)
{
    u->fail_ev = n > 0 ? n : 0;
}
