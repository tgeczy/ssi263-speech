/* test_z80_zex.c -- runs a CP/M Z80 instruction exerciser (zexdoc.com or zexall.com) on the cpu.h Z80 core, and
 * judges it: unlike test_z180_zex.c, the exit status says whether every group passed.
 *
 * The exercisers (Frank Cringle's, GPL) are test inputs only and are not in this repository; pass the path.  zexdoc
 * compares the documented flags, zexall every bit of F (bits 3 and 5 too, and the undocumented opcodes' results).
 * A minimal CP/M: the program at 0100h, "JP BDOS" at 0005h, BDOS functions 2 (print E) and 9 (print $-string at DE)
 * answered through three OUTs of a stub at FF00h, and a HALT at 0000h for the warm boot.  Memory is a flat 64K.
 *
 * Each group prints its name and then "OK" or "ERROR **** crc expected:... found:...".  The run passes when every
 * group of the exerciser's table printed OK, none printed ERROR and the program ended at the warm boot.
 *
 *   test_z80_zex <zexdoc.com | zexall.com> [--only <text>]
 * --only keeps the groups whose name contains <text> (the table at the address the program's start loads into HL is
 * rewritten before the run): a control that breaks one rule need not wait for the whole exerciser.
 *
 *   build: gcc -O2 -I. -c test_z80_zex.c; g++ -O3 -std=c++17 -fno-exceptions -fno-rtti -I. -c z80_mame.cpp;
 *          g++ -o test_z80_zex test_z80_zex.o z80_mame.o          (../blazie/build_board.py does it)
 * The last line: "zex: N of M groups OK, E ERROR; <where it ended>: all passed" or "... FAILED"; exit status 0 / 1
 * (2: no file, or not an exerciser this runner knows).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

typedef struct {
    uint8_t mem[65536];
    uint8_t c, e, d;
    int oks, errors;
    char line[256];
    int line_n;
} cpm;

static uint8_t rd(void *ctx, uint32_t a) { return ((cpm *)ctx)->mem[a & 0xFFFF]; }
static void wr(void *ctx, uint32_t a, uint8_t v) { ((cpm *)ctx)->mem[a & 0xFFFF] = v; }
static uint8_t in(void *ctx, uint16_t p) { (void)ctx; (void)p; return 0xFF; }

static void put(cpm *m, char ch)
{
    putchar(ch);
    if (ch == '\n' || m->line_n == (int)sizeof m->line - 1) {
        m->line[m->line_n] = 0;
        if (strstr(m->line, "ERROR"))
            m->errors++;
        else if (m->line_n >= 2 && !strcmp(m->line + m->line_n - 2, "OK"))
            m->oks++;
        m->line_n = 0;
    } else if (ch != '\r') {
        m->line[m->line_n++] = ch;
    }
}

static void out(void *ctx, uint16_t p, uint8_t v)
{
    cpm *m = (cpm *)ctx;
    switch (p & 0xFF) {                                 /* the Z80 drives A on the port's high byte: masked */
    case 0x80: m->c = v; break;
    case 0x81: m->e = v; break;
    case 0x82:
        m->d = v;
        if (m->c == 2) {
            put(m, (char)m->e);
        } else if (m->c == 9) {
            unsigned a = (unsigned)(m->d << 8 | m->e);
            while (m->mem[a & 0xFFFF] != '$')
                put(m, (char)m->mem[a++ & 0xFFFF]);
        }
        fflush(stdout);
        break;
    }
}

/* The exerciser's table of groups: its start code is "... LD HL,tests" at 011Fh (21h, then the address), the table
   a list of addresses ended by 0000h; a group's name is 65 bytes into its block (the flag mask, three 20-byte
   vectors, the expected CRC), ended by '$'.  Returns the groups kept, or -1 when this is not such a program. */
static int groups(cpm *m, const char *only)
{
    unsigned table, a, kept = 0, k;
    if (m->mem[0x011F] != 0x21)
        return -1;
    table = (unsigned)(m->mem[0x0120] | m->mem[0x0121] << 8);
    for (a = table; a < 0xFE00; a += 2) {
        unsigned test = (unsigned)(m->mem[a] | m->mem[a + 1] << 8);
        char name[40];
        if (!test)
            break;
        if (test < 0x100 || test > 0xFD00)
            return -1;
        for (k = 0; k < sizeof name - 1 && m->mem[test + 65 + k] != '$'; k++)
            name[k] = (char)m->mem[test + 65 + k];
        name[k] = 0;
        if (only && !strstr(name, only))
            continue;
        m->mem[table + 2 * kept] = (uint8_t)test;
        m->mem[table + 2 * kept + 1] = (uint8_t)(test >> 8);
        kept++;
    }
    if (a >= 0xFE00)
        return -1;
    m->mem[table + 2 * kept] = m->mem[table + 2 * kept + 1] = 0;
    return (int)kept;
}

int main(int argc, char **argv)
{
    static cpm m;
    static const uint8_t bdos[] = {
        0x79, 0xD3, 0x80,       /* LD A,C ; OUT (80h),A */
        0x7B, 0xD3, 0x81,       /* LD A,E ; OUT (81h),A */
        0x7A, 0xD3, 0x82,       /* LD A,D ; OUT (82h),A */
        0xC9                    /* RET */
    };
    const char *only = argc == 4 && !strcmp(argv[2], "--only") ? argv[3] : NULL;
    cpu_bus bus;
    z80 *c;
    z80_regs r;
    FILE *f;
    size_t n;
    int want, ok;
    unsigned long long total = 0;

    if (argc != 2 && !only) {
        fprintf(stderr, "usage: %s <zexdoc.com | zexall.com> [--only <text>]\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    n = fread(m.mem + 0x100, 1, 0xFE00 - 0x100, f);
    fclose(f);
    want = n ? groups(&m, only) : -1;
    if (want <= 0) {
        fprintf(stderr, "%s: %s\n", argv[1], want ? "not an exerciser with its table at (011Fh)" : "no such group");
        return 2;
    }
    m.mem[0x0000] = 0xC3; m.mem[0x0001] = 0x00; m.mem[0x0002] = 0x01;   /* the reset vector: JP 0100h */
    m.mem[0x0005] = 0xC3; m.mem[0x0006] = 0x00; m.mem[0x0007] = 0xFF;   /* JP FF00h; (6) = top of the TPA */
    memcpy(m.mem + 0xFF00, bdos, sizeof bdos);

    memset(&bus, 0, sizeof bus);
    bus.ctx = &m;
    bus.read = rd;
    bus.write = wr;
    bus.in = in;
    bus.out = out;
    c = z80_create(&bus, 4000000.0);
    if (!c)
        return 2;
    for (;;) {
        total += z80_run(c, 1000000);
        if (m.mem[0x0000] == 0xC3 && z80_pc(c) >= 0x0100)
            m.mem[0x0000] = 0x76, m.mem[0x0001] = 0, m.mem[0x0002] = 0;   /* once running: warm boot halts */
        z80_regs_get(c, &r);
        if (r.halted || r.fault)
            break;
    }
    /* a halted Z80's PC is the address after its HALT */
    ok = m.oks == want && !m.errors && r.halted && r.pc == 0x0001;
    printf("\n[zex] %llu T-states\n", total);
    printf("zex: %d of %d groups OK, %d ERROR; %s at %04X: %s\n", m.oks, want, m.errors,
           r.fault ? "the core faulted" : r.pc == 0x0001 ? "the warm boot" : "halted, NOT the warm boot",
           (unsigned)((r.pc - (r.halted ? 1 : 0)) & 0xFFFF), ok ? "all passed" : "FAILED");
    z80_destroy(c);
    return ok ? 0 : 1;
}
