/* mb_board.c -- the Apple II and its Mockingboard, as far as Sweet Micro Systems' text-to-speech uses them
 * (mb_board.h).  MIT.
 */
#include "mb_board.h"

#include <stdlib.h>
#include <string.h>

#include "../cpu/cpu.h"

struct mb_board {
    m6502 *cpu;
    mb_chip chip;
    uint8_t ram[0xC000];
    uint8_t lc_d000[2][0x1000];        /* the language card's two D000-DFFF banks: [0] bank 1, [1] bank 2 */
    uint8_t lc_e000[0x2000];
    uint8_t rom[0x3000];               /* ours: D000-FFFF, all zero but FFF0-FFFF */
    int lc_read_ram, lc_bank, lc_write, lc_prewrite;
    uint8_t via_pcr, via_ifr, via_ier, via_reg[16];
    int ar;                            /* A/R asserted (the pin LOW) */
    long vector_reads, vector_reads_lc;
    uint16_t guard_lo, guard_hi;       /* writes in this range are counted (mb_board_guard) */
    long guard_writes;
};

/* our "ROM", FFF0h-FFFFh: STA $45 / JMP ($03FE) at FFF0h; JMP $FFF5 (the idle loop) at FFF5h; two unused bytes;
   the vectors, low byte first: NMI and RESET to the idle loop, IRQ to FFF0h */
static const uint8_t STUB[16] = {0x85, 0x45, 0x6C, 0xFE, 0x03,
                                 0x4C, 0xF5, 0xFF,
                                 0x00, 0x00,
                                 0xF5, 0xFF, 0xF5, 0xFF, 0xF0, 0xFF};

static void update_irq(mb_board *b)
{
    m6502_set_irq(b->cpu, M6502_IRQ, (b->via_ifr & b->via_ier & 0x7F) != 0);
}

/* $C080-$C08F, on any access: bank (A3), what is read (A0 == A1: RAM), write enable after two reads of an odd one */
static void lc_switch(mb_board *b, uint16_t addr, int is_read)
{
    b->lc_bank = (addr & 8) ? 1 : 2;
    b->lc_read_ram = ((addr & 3) == 0 || (addr & 3) == 3);
    if (addr & 1) {
        if (is_read) {
            if (b->lc_prewrite)
                b->lc_write = 1;
            b->lc_prewrite = 1;
        } else
            b->lc_prewrite = 0;
    } else {
        b->lc_write = 0;
        b->lc_prewrite = 0;
    }
}

static uint8_t *lc_ram(mb_board *b, uint16_t addr)
{
    if (addr < 0xE000)
        return &b->lc_d000[b->lc_bank == 1 ? 0 : 1][addr - 0xD000];
    return &b->lc_e000[addr - 0xE000];
}

static uint8_t via_read(mb_board *b, int r)
{
    switch (r) {
    case 0x0C: return b->via_pcr;
    case 0x0D: return (uint8_t)(b->via_ifr | ((b->via_ifr & b->via_ier & 0x7F) ? 0x80 : 0));
    case 0x0E: return (uint8_t)(b->via_ier | 0x80);
    default: return b->via_reg[r];
    }
}

static void via_write(mb_board *b, int r, uint8_t v)
{
    switch (r) {
    case 0x0C: b->via_pcr = v; break;
    case 0x0D: b->via_ifr &= (uint8_t)~(v & 0x7F); break;
    case 0x0E:
        if (v & 0x80)
            b->via_ier |= (uint8_t)(v & 0x7F);
        else
            b->via_ier &= (uint8_t)~(v & 0x7F);
        break;
    default: b->via_reg[r] = v; break;
    }
    update_irq(b);
}

static uint8_t bus_read(void *ctx, uint32_t a32)
{
    mb_board *b = (mb_board *)ctx;
    uint16_t addr = (uint16_t)a32;
    if (addr < 0xC000)
        return b->ram[addr];
    if (addr >= 0xD000) {
        if (addr >= 0xFFFE) {
            b->vector_reads++;
            if (b->lc_read_ram)
                b->vector_reads_lc++;
        }
        return b->lc_read_ram ? *lc_ram(b, addr) : b->rom[addr - 0xD000];
    }
    if (addr >= 0xC080 && addr <= 0xC08F) {
        lc_switch(b, addr, 1);
        return 0;
    }
    if (addr >= 0xC480 && addr <= 0xC48F)
        return via_read(b, addr & 15);
    return 0;
}

static void bus_write(void *ctx, uint32_t a32, uint8_t v)
{
    mb_board *b = (mb_board *)ctx;
    uint16_t addr = (uint16_t)a32;
    if (addr >= b->guard_lo && addr <= b->guard_hi)
        b->guard_writes++;
    if (addr < 0xC000) {
        b->ram[addr] = v;
        return;
    }
    if (addr >= 0xD000) {
        if (b->lc_write)
            *lc_ram(b, addr) = v;
        return;
    }
    if (addr >= 0xC080 && addr <= 0xC08F)
        lc_switch(b, addr, 0);
    else if (addr >= 0xC440 && addr <= 0xC447) {
        if (b->chip.write)
            b->chip.write(b->chip.ctx, addr & 7, v);
    } else if (addr >= 0xC480 && addr <= 0xC48F)
        via_write(b, addr & 15, v);
}

mb_board *mb_board_create(const mb_chip *chip)
{
    cpu_bus bus;
    mb_board *b = (mb_board *)calloc(1, sizeof *b);
    if (!b)
        return NULL;
    if (chip)
        b->chip = *chip;
    memcpy(b->rom + 0x2FF0, STUB, sizeof STUB);
    b->lc_bank = 2;                    /* power-up: read "ROM", bank 2, writes off */
    b->guard_lo = 0xFFFF;              /* no guard */
    b->guard_hi = 0;
    memset(&bus, 0, sizeof bus);
    bus.ctx = b;
    bus.read = bus_read;
    bus.write = bus_write;
    b->cpu = m6502_create(&bus, MB_CPU_HZ);    /* resets: PC from our RESET vector, the idle loop */
    if (!b->cpu) {
        free(b);
        return NULL;
    }
    return b;
}

void mb_board_destroy(mb_board *b)
{
    if (!b)
        return;
    m6502_destroy(b->cpu);
    free(b);
}

int mb_board_load(mb_board *b, uint16_t addr, const unsigned char *data, size_t n)
{
    size_t k;
    if ((size_t)addr + n > 0x10000)
        return 0;
    for (k = 0; k < n; k++) {
        uint16_t a = (uint16_t)(addr + k);
        if (a < 0xC000)
            b->ram[a] = data[k];
        else if (a >= 0xD000)
            *(a < 0xE000 ? &b->lc_d000[0][a - 0xD000] : &b->lc_e000[a - 0xE000]) = data[k];
        else
            return 0;                  /* the I/O page: no file goes there */
    }
    return 1;
}

uint8_t mb_board_peek(const mb_board *b, uint16_t addr)
{
    if (addr < 0xC000)
        return b->ram[addr];
    if (addr >= 0xD000)
        return b->lc_read_ram ? *lc_ram((mb_board *)b, addr) : b->rom[addr - 0xD000];
    return 0;
}

void mb_board_poke(mb_board *b, uint16_t addr, uint8_t v)
{
    bus_write(b, addr, v);
}

void mb_board_ar(mb_board *b, int asserted)
{
    int rising_pin = b->ar && !asserted;           /* the pin goes LOW -> HIGH: the request ends */
    int falling_pin = !b->ar && asserted;          /* HIGH -> LOW: the chip requests */
    b->ar = asserted != 0;
    if ((b->via_pcr & 1) ? rising_pin : falling_pin) {
        b->via_ifr |= 0x02;                        /* CA1 */
        update_irq(b);
    }
}

void mb_board_call(mb_board *b, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    m6502_regs r;
    m6502_regs_get(b->cpu, &r);
    r.sp = 0xFF;
    b->ram[0x1FF] = (uint8_t)((MB_IDLE - 1) >> 8);       /* JSR's return address: the idle loop's, less one */
    b->ram[0x1FE] = (uint8_t)((MB_IDLE - 1) & 0xFF);
    r.sp = 0xFD;
    r.pc = addr;
    r.a = a;
    r.x = x;
    r.y = y;
    m6502_regs_set(b->cpu, &r);
}

int mb_board_idle(const mb_board *b)
{
    return mb_board_pc(b) == MB_IDLE && !b->lc_read_ram;
}

uint16_t mb_board_pc(const mb_board *b)
{
    m6502_regs r;
    m6502_regs_get(b->cpu, &r);
    return r.pc;
}

void mb_board_guard(mb_board *b, uint16_t lo, uint16_t hi)
{
    b->guard_lo = lo;
    b->guard_hi = hi;
    b->guard_writes = 0;
}

void mb_board_abort(mb_board *b)
{
    m6502_regs r;
    m6502_regs_get(b->cpu, &r);
    r.pc = MB_IDLE;
    r.sp = 0xFF;
    r.p = (uint8_t)(r.p | 0x04);
    m6502_regs_set(b->cpu, &r);
    lc_switch(b, 0xC08A, 1);           /* "ROM" read, as the firmware leaves it */
}

uint64_t mb_board_run(mb_board *b, uint64_t cycles)
{
    return m6502_run(b->cpu, cycles);
}

uint64_t mb_board_cycles(const mb_board *b)
{
    return m6502_cycles(b->cpu);
}

long mb_board_get(const mb_board *b, const char *name)
{
    m6502_regs r;
    m6502_regs_get(b->cpu, &r);
    if (!strcmp(name, "ifr")) return b->via_ifr;
    if (!strcmp(name, "ier")) return b->via_ier;
    if (!strcmp(name, "pcr")) return b->via_pcr;
    if (!strcmp(name, "irq")) return (b->via_ifr & b->via_ier & 0x7F) != 0;
    if (!strcmp(name, "lc_read_ram")) return b->lc_read_ram;
    if (!strcmp(name, "lc_bank")) return b->lc_bank;
    if (!strcmp(name, "lc_write")) return b->lc_write;
    if (!strcmp(name, "vector_reads")) return b->vector_reads;
    if (!strcmp(name, "vector_reads_lc")) return b->vector_reads_lc;
    if (!strcmp(name, "guard_writes")) return b->guard_writes;
    if (!strcmp(name, "decimal")) return (long)r.decimal;
    if (!strcmp(name, "undocumented")) return (long)r.undocumented;
    if (!strcmp(name, "pc")) return r.pc;
    return -1;
}
