/* test_as_board.c -- the Accent-PC board's own rules (as_board.h), on small programs in a synthetic u2: no firmware.
 *   rom_sizes     u2 64 KB, u3 and u4 32 KB exactly, or no board (and the reason)
 *   memory_map    u2 below 7800h, RAM 7800-7FFF, ROM writes ignored; port 40h bits 0-1 bank the window: u2's upper
 *                 half, u3, u4, nothing (FFh)
 *   chip_ports    OUT 03-07 are R4-R0 (reversed), in order; IN 07 has A/R in bit 7; IN 40h the switches; other
 *                 ports read 0 and ignore writes
 *   trap_gate     TRAP = A/R AND port 40h bit 4, sampled at a slice's start: none with the gate shut, one per rise
 *   usart_mode    after a reset the 8251 takes a mode word, then commands; a command with bit 6 re-arms the mode word
 *   rxrdy         no byte is loaded while RTS is down; a loaded byte is status bit 1 and RST 6.5; reading port 20h
 *                 drops both (the handler runs once per byte); bytes in order
 *   drop_input    dropping the input empties the queue, not the loaded byte
 *   rst75         an RST 7.5 edge is latched and taken when unmasked
 *   python_split  (python_slices) an acceptance that reaches the budget ends the slice; the vector's instruction is
 *                 left unexecuted until the next slice; off, the step is whole
 *   split_effects  the vector's I/O, memory and register effects must not happen in the acceptance-only slice
 *   ei_trap       (python_slices) a TRAP raised in EI's shadow is held through the instruction after the EI; off, it
 *                 is taken at once (the chip's rule)
 *   two_boards    two boards interleaved write what each writes alone
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as_board.h"

static int failures;

static void report(const char *name, int ok, const char *detail)
{
    printf("%-4s %-14s %s\n", ok ? "ok" : "FAIL", name, detail);
    if (!ok)
        failures++;
}

/* ---- a stub SSI-263: A/R as the test sets it, the writes logged ------------------------------------------------- */
typedef struct {
    int request;
    int n;
    int reg[256], val[256];
} stub;

static void stub_write(void *ctx, int reg, int val)
{
    stub *s = (stub *)ctx;
    if (s->n < 256) {
        s->reg[s->n] = reg;
        s->val[s->n] = val;
        s->n++;
    }
}

static int stub_request(void *ctx)
{
    return ((stub *)ctx)->request;
}

static uint8_t U2[AS_U2_SIZE], U3[AS_U3_SIZE], U4[AS_U4_SIZE];

/* a fresh u2: 0000 JMP 0040h, the rest NOPs; u2[8000h] 11h, u3[0] 22h, u4[0] 33h */
static void roms(void)
{
    memset(U2, 0, sizeof U2);
    memset(U3, 0, sizeof U3);
    memset(U4, 0, sizeof U4);
    U2[0] = 0xC3; U2[1] = 0x40; U2[2] = 0x00;
    U2[0x8000] = 0x11;
    U3[0] = 0x22;
    U4[0] = 0x33;
}

static void put(uint16_t at, const uint8_t *p, int n)
{
    memcpy(U2 + at, p, (size_t)n);
}

#define PUT(at, ...) do { static const uint8_t p_[] = {__VA_ARGS__}; put((at), p_, (int)sizeof p_); } while (0)

static as_board *board(stub *s)
{
    as_chip c;
    char err[128];
    as_board *b;
    c.ctx = s;
    c.write = stub_write;
    c.request = stub_request;
    b = as_board_create(U2, sizeof U2, U3, sizeof U3, U4, sizeof U4, &c, err, (int)sizeof err);
    if (!b) {
        printf("FAIL board: %s\n", err);
        exit(1);
    }
    return b;
}

static void run(as_board *b, int slices, int budget)
{
    while (slices--) {
        as_board_serial(b);
        as_board_trap(b);
        as_board_run(b, (uint64_t)budget);
    }
}

/* ---- the tests ------------------------------------------------------------------------------------------------- */

static void t_rom_sizes(void)
{
    stub s;
    as_chip c;
    char err[128] = "";
    as_board *b;
    int ok;
    memset(&s, 0, sizeof s);
    c.ctx = &s;
    c.write = stub_write;
    c.request = stub_request;
    roms();
    b = as_board_create(U2, sizeof U2 - 1, U3, sizeof U3, U4, sizeof U4, &c, err, (int)sizeof err);
    ok = !b && strstr(err, "u2") != NULL;
    b = as_board_create(U2, sizeof U2, U3, sizeof U3, U4, sizeof U4 + 1, &c, err, (int)sizeof err);
    ok = ok && !b && strstr(err, "u4") != NULL;
    b = as_board_create(U2, sizeof U2, U3, sizeof U3, U4, sizeof U4, &c, err, (int)sizeof err);
    ok = ok && b != NULL;
    as_board_destroy(b);
    report("rom_sizes", ok, "short u2 and long u4 refused, exact sizes taken");
}

static void t_memory_map(void)
{
    stub s;
    as_board *b;
    char d[160];
    uint8_t got[5];
    int i, ok;
    static const uint8_t want[5] = {0x11, 0x22, 0x33, 0xFF, 0x5A};
    memset(&s, 0, sizeof s);
    roms();
    U2[0x0100] = 0x5A;
    PUT(0x40, 0x31, 0x00, 0x80,                             /* LXI SP,8000h */
        0x3A, 0x00, 0x80, 0x32, 0x00, 0x78,                 /* LDA 8000h; STA 7800h (bank 0) */
        0x3E, 0x01, 0xD3, 0x40, 0x3A, 0x00, 0x80, 0x32, 0x01, 0x78,   /* bank 1 */
        0x3E, 0x02, 0xD3, 0x40, 0x3A, 0x00, 0x80, 0x32, 0x02, 0x78,   /* bank 2 */
        0x3E, 0x03, 0xD3, 0x40, 0x3A, 0x00, 0x80, 0x32, 0x03, 0x78,   /* bank 3 */
        0x3E, 0xAA, 0x32, 0x00, 0x01,                       /* MVI A,AAh; STA 0100h: ROM, ignored */
        0x3A, 0x00, 0x01, 0x32, 0x04, 0x78,                 /* LDA 0100h; STA 7804h */
        0x3E, 0x00, 0xD3, 0x40, 0x76);                      /* bank 0; HLT */
    b = board(&s);
    run(b, 10, 200);
    for (i = 0; i < 5; i++)
        got[i] = as_board_peek(b, (uint16_t)(0x7800 + i));
    ok = !memcmp(got, want, 5) && as_board_get(b, "bank") == 0 && as_board_peek(b, 0x8000) == 0x11
         && as_board_get(b, "halted") == 1;
    snprintf(d, sizeof d, "window by bank 0-3: %02X %02X %02X %02X, ROM after a write %02X (want 11 22 33 FF, 5A)",
             got[0], got[1], got[2], got[3], got[4]);
    report("memory_map", ok, d);
    as_board_destroy(b);
}

static void t_chip_ports(void)
{
    stub s;
    as_board *b;
    char d[200];
    int ok, i;
    memset(&s, 0, sizeof s);
    s.request = 1;
    roms();
    PUT(0x40, 0x3E, 0x01, 0xD3, 0x07, 0x3E, 0x02, 0xD3, 0x06, 0x3E, 0x03, 0xD3, 0x05,   /* R0 = 1, R1 = 2, R2 = 3 */
        0x3E, 0x04, 0xD3, 0x04, 0x3E, 0x05, 0xD3, 0x03,                               /* R3 = 4, R4 = 5 */
        0x3E, 0x09, 0xD3, 0x02, 0xD3, 0x08, 0xD3, 0x80,                               /* ports 02, 08, 80: none */
        0xDB, 0x07, 0x32, 0x00, 0x78,                                                 /* IN 07h -> 7800h */
        0xDB, 0x80, 0x32, 0x01, 0x78,                                                 /* IN 80h -> 7801h */
        0xDB, 0x40, 0x32, 0x02, 0x78,                                                 /* IN 40h -> 7802h */
        0x76);
    b = board(&s);
    as_board_set(b, "switches", 0x5C);
    run(b, 5, 200);
    ok = s.n == 5;
    for (i = 0; i < s.n && i < 5; i++)
        ok = ok && s.reg[i] == i && s.val[i] == i + 1;
    ok = ok && as_board_peek(b, 0x7800) == 0x80 && as_board_peek(b, 0x7801) == 0x00 && as_board_peek(b, 0x7802) == 0x5C;
    snprintf(d, sizeof d, "%d writes (R%d=%d first), IN 07h %02X, IN 80h %02X, IN 40h %02X (want 5 writes R0=1..R4=5, "
             "80 00 5C)", s.n, s.n ? s.reg[0] : -1, s.n ? s.val[0] : -1, as_board_peek(b, 0x7800),
             as_board_peek(b, 0x7801), as_board_peek(b, 0x7802));
    report("chip_ports", ok, d);
    as_board_destroy(b);
}

/* 0040: SP; loop: IN 40h (the switches); OUT 40h (the latch); JMP loop.  TRAP (0024h) -> 0200h: INR [7800h]; RET */
static void trap_program(void)
{
    roms();
    PUT(0x24, 0xC3, 0x00, 0x02);
    PUT(0x40, 0x31, 0x00, 0x80, 0xDB, 0x40, 0xD3, 0x40, 0xC3, 0x43, 0x00);
    PUT(0x200, 0x21, 0x00, 0x78, 0x34, 0xC9);
}

static void t_trap_gate(void)
{
    stub s;
    as_board *b;
    char d[160];
    int shut, one, held, again;
    memset(&s, 0, sizeof s);
    trap_program();
    b = board(&s);
    s.request = 1;
    run(b, 4, 200);                         /* A/R, the gate shut */
    shut = as_board_peek(b, 0x7800);
    as_board_set(b, "switches", 0x10);
    run(b, 4, 200);                         /* the gate opens during the first slice: the TRAP at the next */
    one = as_board_peek(b, 0x7800);
    run(b, 4, 200);                         /* held high: no second */
    held = as_board_peek(b, 0x7800);
    s.request = 0;
    run(b, 1, 200);
    s.request = 1;
    run(b, 2, 200);                         /* a new rise */
    again = as_board_peek(b, 0x7800);
    snprintf(d, sizeof d, "TRAPs: gate shut %d, opened %d, held %d, risen again %d (want 0 1 1 2)", shut, one, held,
             again);
    report("trap_gate", shut == 0 && one == 1 && held == 1 && again == 2, d);
    as_board_destroy(b);
}

static void t_usart_mode(void)
{
    stub s;
    as_board *b;
    char d[160];
    int cmd, next;
    memset(&s, 0, sizeof s);
    roms();
    PUT(0x40, 0x3E, 0x4E, 0xD3, 0x21,      /* the mode word */
        0x3E, 0x37, 0xD3, 0x21,             /* a command */
        0x3E, 0x40, 0xD3, 0x21,             /* internal reset */
        0x3E, 0x4E, 0xD3, 0x21,             /* the mode word again, not a command */
        0x76);
    b = board(&s);
    run(b, 2, 200);
    cmd = as_board_get(b, "usart_cmd");
    next = as_board_get(b, "usart_mode_next");
    snprintf(d, sizeof d, "command %02X, mode word next %d (want 40, 0)", cmd, next);
    report("usart_mode", cmd == 0x40 && next == 0, d);
    as_board_destroy(b);
}

/* 0040: SP; HL = 7800h; mode 4Eh; RST 6.5 unmasked (SIM 0Dh); EI; loop: IN 40h; OUT 21h (the switches as the
   command); JMP loop.  RST 6.5 (0034h) -> 0200h: PUSH PSW; IN 21h -> [7820h]; IN 20h; MOV M,A; OUT 07h (R0); INX H;
   POP PSW; EI; RET */
static void rx_program(void)
{
    roms();
    PUT(0x34, 0xC3, 0x00, 0x02);
    PUT(0x40, 0x31, 0x00, 0x80, 0x21, 0x00, 0x78, 0x3E, 0x4E, 0xD3, 0x21, 0x3E, 0x0D, 0x30, 0xFB,
        0xDB, 0x40, 0xD3, 0x21, 0xC3, 0x4E, 0x00);
    PUT(0x200, 0xF5, 0xDB, 0x21, 0x32, 0x20, 0x78, 0xDB, 0x20, 0x77, 0xD3, 0x07, 0x23, 0xF1, 0xFB, 0xC9);
}

static void t_rxrdy(void)
{
    stub s;
    as_board *b;
    char d[200];
    int before, loaded, ready, status, hl;
    memset(&s, 0, sizeof s);
    rx_program();
    b = board(&s);
    as_board_set(b, "switches", 0x15);      /* RTS down */
    run(b, 2, 200);
    as_board_send(b, (const uint8_t *)"AB", 2);
    run(b, 4, 200);
    before = as_board_peek(b, 0x7800);
    as_board_set(b, "switches", 0x37);      /* RTS up */
    run(b, 1, 200);                         /* the command goes out */
    as_board_serial(b);                     /* one byte loaded */
    loaded = as_board_get(b, "rx");
    ready = as_board_get(b, "rx_ready");
    run(b, 6, 200);
    status = as_board_peek(b, 0x7820);
    hl = as_board_get(b, "hl");
    snprintf(d, sizeof d, "RTS down: [7800] %02X; loaded: %d queued, ready %d; then %c%c%02X, status %02X, HL %04X, "
             "%d chip writes (want 00; 1, 1; AB00, 87, 7802, 2)", before, loaded, ready,
             as_board_peek(b, 0x7800), as_board_peek(b, 0x7801), as_board_peek(b, 0x7802), status, hl, s.n);
    report("rxrdy", before == 0 && loaded == 1 && ready == 1 && as_board_peek(b, 0x7800) == 'A'
           && as_board_peek(b, 0x7801) == 'B' && as_board_peek(b, 0x7802) == 0 && status == 0x87 && hl == 0x7802
           && s.n == 2 && as_board_get(b, "rx_ready") == 0, d);
    as_board_destroy(b);
}

static void t_drop_input(void)
{
    stub s;
    as_board *b;
    char d[160];
    int queued, ready;
    memset(&s, 0, sizeof s);
    rx_program();
    b = board(&s);
    as_board_set(b, "switches", 0x37);
    run(b, 2, 200);
    as_board_send(b, (const uint8_t *)"XYZ", 3);
    as_board_serial(b);
    as_board_drop_input(b);
    queued = as_board_get(b, "rx");
    ready = as_board_get(b, "rx_ready");
    run(b, 6, 200);
    snprintf(d, sizeof d, "after the drop: %d queued, ready %d; read %c then %02X (want 0, 1; X then 00)", queued,
             ready,
             as_board_peek(b, 0x7800), as_board_peek(b, 0x7801));
    report("drop_input", queued == 0 && ready == 1 && as_board_peek(b, 0x7800) == 'X' && as_board_peek(b, 0x7801) == 0,
           d);
    as_board_destroy(b);
}

static void t_rst75(void)
{
    stub s;
    as_board *b;
    char d[160];
    int before, after;
    memset(&s, 0, sizeof s);
    roms();
    PUT(0x3C, 0xC3, 0x00, 0x02);
    PUT(0x40, 0x31, 0x00, 0x80, 0x3E, 0x0B, 0x30, 0xFB, 0xC3, 0x47, 0x00);   /* SIM 0Bh: 7.5 unmasked; EI; JMP $ */
    PUT(0x200, 0x3E, 0x01, 0x32, 0x00, 0x78, 0x76);
    b = board(&s);
    run(b, 2, 200);
    before = as_board_peek(b, 0x7800);
    as_board_rst75(b);
    run(b, 2, 200);
    after = as_board_peek(b, 0x7800);
    snprintf(d, sizeof d, "handler ran: before the edge %d, after %d (want 0 1)", before, after);
    report("rst75", before == 0 && after == 1, d);
    as_board_destroy(b);
}

/* 0040: SP; mode, command 37h (RTS up); SIM 0Dh; EI; JMP $ (004Fh).  RST 6.5 -> 0200h: IN 20h; JMP $ (0202h) */
static void t_python_split(void)
{
    char d[200];
    uint64_t r1[2], r2[2], steps[2];
    int splits[2], pc[2], at_split[2], mode;
    for (mode = 1; mode >= 0; mode--) {
        stub s;
        as_board *b;
        uint64_t s0;
        memset(&s, 0, sizeof s);
        roms();
        PUT(0x34, 0xC3, 0x00, 0x02);
        PUT(0x40, 0x31, 0x00, 0x80, 0x3E, 0x4E, 0xD3, 0x21, 0x3E, 0x37, 0xD3, 0x21, 0x3E, 0x0D, 0x30, 0xFB,
            0xC3, 0x4F, 0x00);
        PUT(0x200, 0xDB, 0x20, 0xC3, 0x02, 0x02);
        b = board(&s);
        as_board_set(b, "python_slices", mode);
        as_board_run(b, 200);
        as_board_send(b, (const uint8_t *)"Q", 1);
        as_board_serial(b);
        r1[mode] = as_board_run(b, 5);      /* the acceptance (12 T) reaches the budget */
        at_split[mode] = as_board_get(b, "pc");
        s0 = as_board_steps(b);
        r2[mode] = as_board_run(b, 15);
        steps[mode] = as_board_steps(b) - s0;
        splits[mode] = as_board_get(b, "splits");
        pc[mode] = as_board_get(b, "pc");
        as_board_destroy(b);
    }
    snprintf(d, sizeof d, "python_slices: %d T then %d T in %d step(s), %d split; off: %d T then %d T in %d steps "
             "(want 12, 20 in 2, 1; 22, 20 in 2)", (int)r1[1], (int)r2[1], (int)steps[1], splits[1], (int)r1[0],
             (int)r2[0], (int)steps[0]);
    report("python_split", r1[1] == 12 && at_split[1] == 0x34 && r2[1] == 20 && steps[1] == 2 && splits[1] == 1 && pc[1] == 0x202
           && r1[0] == 22 && r2[0] == 20 && steps[0] == 2 && splits[0] == 0 && pc[0] == 0x202, d);
}

static void t_split_effects(void)
{
    int k, ok = 1;
    for (k = 0; k < 3; k++) {
        stub s;
        as_board *b;
        uint64_t n, t;
        memset(&s, 0, sizeof s);
        roms();
        PUT(0x40, 0x31, 0x00, 0x80, 0x3E, 0x4E, 0xD3, 0x21, 0x3E, 0x37, 0xD3, 0x21,
            0x3E, 0x0D, 0x30, 0xFB, 0xC3, 0x4F, 0x00);
        if (k == 0) { PUT(0x34, 0xD3, 0x07, 0x76); }       /* OUT R0 */
        if (k == 1) { PUT(0x34, 0x32, 0x00, 0x78, 0x76); } /* STA RAM */
        if (k == 2) { PUT(0x34, 0x3E, 0x77, 0x76); }       /* MVI A */
        b = board(&s);
        as_board_run(b, 200);
        as_board_send(b, (const uint8_t *)"Q", 1);
        as_board_serial(b);
        n = as_board_steps(b);
        t = as_board_cycles(b);
        ok = (as_board_run(b, 5) == 12) && ok;
        ok = ok && as_board_get(b, "pc") == 0x34 && as_board_steps(b) == n
             && as_board_cycles(b) == t + 12 && s.n == 0 && as_board_peek(b, 0x7800) == 0
             && (as_board_get(b, "af") >> 8) == 0x0D;
        ok = (as_board_run(b, 0) == 0) && ok;
        ok = ok && as_board_get(b, "pc") == 0x34 && as_board_steps(b) == n;
        t = as_board_run(b, 1);
        ok = ok && as_board_steps(b) == n + 1;
        if (k == 0) ok = ok && t == 10 && s.n == 1 && s.reg[0] == 0 && s.val[0] == 0x0D;
        if (k == 1) ok = ok && t == 13 && as_board_peek(b, 0x7800) == 0x0D && s.n == 0;
        if (k == 2) ok = ok && t == 7 && (as_board_get(b, "af") >> 8) == 0x77 && s.n == 0;
        as_board_destroy(b);
    }
    report("split_effects", ok, "OUT, STA and MVI unexecuted after acceptance/zero budget; each executes once on resume");
}

/* 0040: SP; the gate open (OUT 40h, 10h); EI; NOP; NOP; HLT.  TRAP -> 0300h: HLT */
static void t_ei_trap(void)
{
    char d[200];
    int pc1[2], pc2[2], held[2], mode;
    for (mode = 1; mode >= 0; mode--) {
        stub s;
        as_board *b;
        int i;
        memset(&s, 0, sizeof s);
        roms();
        PUT(0x24, 0xC3, 0x00, 0x03);
        PUT(0x40, 0x31, 0x00, 0x80, 0x3E, 0x10, 0xD3, 0x40, 0xFB, 0x00, 0x00, 0x76);
        PUT(0x300, 0x76);
        b = board(&s);
        as_board_set(b, "python_slices", mode);
        for (i = 0; i < 5; i++)
            as_board_run(b, 1);             /* JMP, LXI, MVI, OUT, EI: one step each */
        s.request = 1;
        as_board_trap(b);                   /* a TRAP in EI's shadow */
        as_board_run(b, 1);
        pc1[mode] = as_board_get(b, "pc");
        as_board_run(b, 1);
        pc2[mode] = as_board_get(b, "pc");
        held[mode] = as_board_get(b, "ei_traps");
        as_board_destroy(b);
    }
    snprintf(d, sizeof d, "python_slices: PC %04X then %04X, %d held; off: %04X then %04X, %d held "
             "(want 0049 0024 1; 0300, 0)", pc1[1], pc2[1], held[1], pc1[0], pc2[0], held[0]);
    report("ei_trap", pc1[1] == 0x49 && pc2[1] == 0x24 && held[1] == 1 && pc1[0] == 0x300 && held[0] == 0, d);
}

static void t_two_boards(void)
{
    stub s1, s2, a1, a2;
    as_board *b1, *b2;
    char d[160];
    int i, same;
    rx_program();
    memset(&a1, 0, sizeof a1);
    memset(&a2, 0, sizeof a2);
    b1 = board(&a1);
    as_board_set(b1, "switches", 0x37);
    as_board_send(b1, (const uint8_t *)"HELLO", 5);
    run(b1, 30, 150);
    as_board_destroy(b1);
    b2 = board(&a2);
    as_board_set(b2, "switches", 0x37);
    as_board_send(b2, (const uint8_t *)"WORLD!", 6);
    run(b2, 30, 170);
    as_board_destroy(b2);
    memset(&s1, 0, sizeof s1);
    memset(&s2, 0, sizeof s2);
    b1 = board(&s1);
    b2 = board(&s2);
    as_board_set(b1, "switches", 0x37);
    as_board_set(b2, "switches", 0x37);
    as_board_send(b1, (const uint8_t *)"HELLO", 5);
    as_board_send(b2, (const uint8_t *)"WORLD!", 6);
    for (i = 0; i < 30; i++) {
        run(b1, 1, 150);
        run(b2, 1, 170);
    }
    same = s1.n == a1.n && s2.n == a2.n && !memcmp(s1.val, a1.val, sizeof s1.val)
           && !memcmp(s2.val, a2.val, sizeof s2.val);
    snprintf(d, sizeof d, "writes alone %d and %d, interleaved %d and %d, %s", a1.n, a2.n, s1.n, s2.n,
             same ? "identical" : "DIFFERENT");
    report("two_boards", same && a1.n == 5 && a2.n == 6, d);
    as_board_destroy(b1);
    as_board_destroy(b2);
}

int main(void)
{
    t_rom_sizes();
    t_memory_map();
    t_chip_ports();
    t_trap_gate();
    t_usart_mode();
    t_rxrdy();
    t_drop_input();
    t_rst75();
    t_python_split();
    t_split_effects();
    t_ei_trap();
    t_two_boards();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
