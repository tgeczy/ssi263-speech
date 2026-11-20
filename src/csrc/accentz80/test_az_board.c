/* test_az_board.c -- the Accent SA board's own rules (az_board.h), on small Z80 programs in a synthetic U5: no
 * firmware.
 *   rom_sizes     U5 and U4 64 KB exactly, or no board (and the reason)
 *   power_up      the latch 00h (MODE 0, NMI shut), the switches 10h, the foot switches 0Fh, PC 0, interrupts off
 *   memory_map    U5 below 7000h; RAM at 7000h-7FFFh; ROM ignores writes; the window by MODE: U5's upper half, U4's
 *                 upper half, U4's lower half, and in MODE 3 the RAM's OTHER half, mirrored, written and kept
 *   io_decode     the device is the port address's HIGH byte (A12-A15): the chip at 0xxx with its register on
 *                 A0-A2 and A/R in bit 7 of a read, the foot switches at 1xxx, the switches and the latch at 4xxx;
 *                 the low byte chooses nothing; 3xxx and 8xxx read FFh, and they and a write to 1xxx are counted
 *   nmi_gate      /NMI = A/R AND the latch's bit 4, taken on the edge: none with the gate shut; one when the firmware
 *                 opens it with A/R up (inside the slice); one per rise of A/R; none while it stays up
 *   nmi_release   a write to the chip that takes A/R down releases the line at once, so the next rise is an edge
 *   rxrdy         no byte is loaded while RTS is down; a loaded byte is status bit 1 and /INT (mode 1: RST 38h);
 *                 reading the data port drops both (the handler runs once per byte); bytes in order; dropped input
 *                 is not read
 *   take_pcs      the addresses of the instructions run, each once, since the last call
 *   two_boards    two boards interleaved write what each writes alone
 * Each line: "ok"/"FAIL", the test's name, the detail; the last line "all passed" or "FAILED"; exit status 0/1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "az_board.h"

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
    int drop_on_write;                     /* a write to R0 takes A/R down, as a phoneme does */
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
    if (s->drop_on_write && reg == 0)
        s->request = 0;
}

static int stub_request(void *ctx)
{
    return ((stub *)ctx)->request;
}

static uint8_t U5[AZ_U5_SIZE], U4[AZ_U4_SIZE];

#define MAIN 0x0200                        /* the tests' programs */
#define INT_AT 0x0100                      /* RST 38h's handler */
#define NMI_AT 0x0180                      /* 0066h's handler */

/* a fresh U5: 0000 JP MAIN, 0038 JP INT_AT, 0066 JP NMI_AT, the rest NOPs; U5[8000h] 11h, U4[8000h] 22h, U4[0] 33h */
static void roms(void)
{
    memset(U5, 0, sizeof U5);
    memset(U4, 0, sizeof U4);
    U5[0x00] = 0xC3; U5[0x01] = MAIN & 0xFF; U5[0x02] = MAIN >> 8;
    U5[0x38] = 0xC3; U5[0x39] = INT_AT & 0xFF; U5[0x3A] = INT_AT >> 8;
    U5[0x66] = 0xC3; U5[0x67] = NMI_AT & 0xFF; U5[0x68] = NMI_AT >> 8;
    U5[0x8000] = 0x11;
    U4[0x8000] = 0x22;
    U4[0x0000] = 0x33;
}

static void put(uint16_t at, const uint8_t *p, int n)
{
    memcpy(U5 + at, p, (size_t)n);
}

#define PUT(at, ...) do { static const uint8_t p_[] = {__VA_ARGS__}; put((at), p_, (int)sizeof p_); } while (0)

static az_board *board(stub *s)
{
    az_chip c;
    char err[128];
    az_board *b;
    c.ctx = s;
    c.write = stub_write;
    c.request = stub_request;
    b = az_board_create(U5, sizeof U5, U4, sizeof U4, &c, err, (int)sizeof err);
    if (!b) {
        printf("FAIL board: %s\n", err);
        exit(1);
    }
    return b;
}

static void run(az_board *b, int slices, int budget)
{
    while (slices--) {
        az_board_serial(b);
        az_board_nmi(b);
        az_board_run(b, (uint64_t)budget);
    }
}

/* the latch follows the switches: LD SP,8000h; LD BC,4040h; loop: IN A,(C); OUT (C),A; JR loop (0206h-020Bh) */
static void follow_switches(void)
{
    PUT(MAIN, 0x31, 0x00, 0x80, 0x01, 0x40, 0x40, 0xED, 0x78, 0xED, 0x79, 0x18, 0xFA);
}

/* 0066h: a count at 7010h, and the count written to the chip's R0 */
static void nmi_handler(void)
{
    PUT(NMI_AT, 0xF5, 0xC5, 0xE5,                           /* PUSH AF; PUSH BC; PUSH HL */
        0x21, 0x10, 0x70, 0x34,                             /* LD HL,7010h; INC (HL) */
        0x01, 0x00, 0x00, 0x7E, 0xED, 0x79,                 /* LD BC,0000h; LD A,(HL); OUT (C),A */
        0xE1, 0xC1, 0xF1, 0xED, 0x45);                      /* POP HL; POP BC; POP AF; RETN */
}

/* ---- the tests ------------------------------------------------------------------------------------------------- */

static void t_rom_sizes(void)
{
    stub s;
    az_chip c;
    char err[128] = "";
    az_board *b;
    int ok;
    memset(&s, 0, sizeof s);
    c.ctx = &s;
    c.write = stub_write;
    c.request = stub_request;
    roms();
    b = az_board_create(U5, sizeof U5 - 1, U4, sizeof U4, &c, err, (int)sizeof err);
    ok = !b && strstr(err, "U5") != NULL;
    b = az_board_create(U5, sizeof U5, U4, sizeof U4 / 2, &c, err, (int)sizeof err);
    ok = ok && !b && strstr(err, "U4") != NULL;
    b = az_board_create(U5, sizeof U5, U4, sizeof U4, &c, err, (int)sizeof err);
    ok = ok && b != NULL;
    az_board_destroy(b);
    report("rom_sizes", ok, "a short U5 and a 32 KB U4 refused, exact sizes taken");
}

static void t_power_up(void)
{
    stub s;
    az_board *b;
    char d[160];
    int ok;
    memset(&s, 0, sizeof s);
    roms();
    b = board(&s);
    ok = az_board_get(b, "latch") == 0x00 && az_board_get(b, "mode") == 0 && az_board_get(b, "switches") == 0x10
         && az_board_get(b, "foot") == 0x0F && az_board_get(b, "pc") == 0 && az_board_get(b, "iff1") == 0
         && az_board_get(b, "nmi_line") == 0 && az_board_peek(b, 0x0000) == 0xC3 && az_board_peek(b, 0x8000) == 0x11
         && az_board_get(b, "no such name") == -1;
    snprintf(d, sizeof d, "latch %02X, switches %02X, foot %02X, PC %04X, IFF1 %d", az_board_get(b, "latch"),
             az_board_get(b, "switches"), az_board_get(b, "foot"), az_board_get(b, "pc"), az_board_get(b, "iff1"));
    report("power_up", ok, d);
    az_board_destroy(b);
}

static void t_memory_map(void)
{
    stub s;
    az_board *b;
    char d[160];
    uint8_t got[9];
    int i, ok;
    static const uint8_t want[9] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x00, 0x5A, 0x11, 0x44};
    memset(&s, 0, sizeof s);
    roms();
    U5[0x0150] = 0x5A;
    PUT(MAIN, 0x31, 0x00, 0x80, 0x01, 0x40, 0x40,           /* LD SP,8000h; LD BC,4040h */
        0x3E, 0x08, 0xED, 0x79, 0x3A, 0x00, 0x80, 0x32, 0x00, 0x70,   /* MODE 0: LD A,(8000h); LD (7000h),A */
        0x3E, 0x09, 0xED, 0x79, 0x3A, 0x00, 0x80, 0x32, 0x01, 0x70,   /* MODE 1 */
        0x3E, 0x0A, 0xED, 0x79, 0x3A, 0x00, 0x80, 0x32, 0x02, 0x70,   /* MODE 2 */
        0x3E, 0x0B, 0xED, 0x79,                             /* MODE 3 */
        0x3E, 0x44, 0x32, 0x05, 0x80,                       /* LD A,44h; LD (8005h),A */
        0x3A, 0x05, 0xF0, 0x32, 0x03, 0x70,                 /* LD A,(F005h); LD (7003h),A: the last mirror */
        0x3E, 0x55, 0x32, 0xFF, 0x9F,                       /* LD A,55h; LD (9FFFh),A */
        0x3A, 0xFF, 0x8F, 0x32, 0x04, 0x70,                 /* LD A,(8FFFh); LD (7004h),A: the first */
        0x3A, 0x03, 0x80, 0x32, 0x05, 0x70,                 /* LD A,(8003h); LD (7005h),A: not 7003h's byte */
        0x3E, 0xAA, 0x32, 0x50, 0x01,                       /* LD A,AAh; LD (0150h),A: ROM, ignored */
        0x3A, 0x50, 0x01, 0x32, 0x06, 0x70,                 /* LD A,(0150h); LD (7006h),A */
        0x3E, 0x08, 0xED, 0x79,                             /* MODE 0 */
        0x3E, 0x77, 0x32, 0x00, 0x80,                       /* LD A,77h; LD (8000h),A: ROM, ignored */
        0x3A, 0x00, 0x80, 0x32, 0x07, 0x70,                 /* LD A,(8000h); LD (7007h),A */
        0x3E, 0x0B, 0xED, 0x79,                             /* MODE 3 again */
        0x3A, 0x05, 0x80, 0x32, 0x08, 0x70,                 /* LD A,(8005h); LD (7008h),A: the RAM kept it */
        0x3E, 0x08, 0xED, 0x79, 0x76);                      /* MODE 0; HALT */
    b = board(&s);
    run(b, 10, 400);
    for (i = 0; i < 9; i++)
        got[i] = az_board_peek(b, (uint16_t)(0x7000 + i));
    ok = !memcmp(got, want, 9) && az_board_get(b, "halted") == 1;
    snprintf(d, sizeof d, "MODE 0-3 %02X %02X %02X %02X, mirror %02X, other half %02X, ROM %02X %02X, kept %02X",
             got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7], got[8]);
    report("memory_map", ok, d);
    az_board_destroy(b);
}

static void t_io_decode(void)
{
    stub s;
    az_board *b;
    char d[200];
    uint8_t got[7];
    int i, ok;
    static const uint8_t want[7] = {0x80, 0x9E, 0x05, 0xFF, 0x80, 0x9E, 0xFF};
    static const int wreg[4] = {3, 1, 4, 7}, wval[4] = {0xA1, 0xA2, 0x00, 0xE7};
    memset(&s, 0, sizeof s);
    s.request = 1;
    roms();
    PUT(MAIN, 0x31, 0x00, 0x80,
        0x01, 0x00, 0x00, 0xED, 0x78, 0x32, 0x00, 0x70,     /* LD BC,0000h; IN A,(C); LD (7000h),A: the chip, A/R */
        0x01, 0x00, 0x40, 0xED, 0x78, 0x32, 0x01, 0x70,     /* 4000h: the switches */
        0x01, 0x00, 0x10, 0xED, 0x78, 0x32, 0x02, 0x70,     /* 1000h: the foot switches */
        0x01, 0x00, 0x80, 0xED, 0x78, 0x32, 0x03, 0x70,     /* 8000h: nothing */
        0x01, 0x40, 0x00, 0xED, 0x78, 0x32, 0x04, 0x70,     /* 0040h: the chip (the low byte chooses nothing) */
        0x3E, 0x40, 0xDB, 0x00, 0x32, 0x05, 0x70,           /* LD A,40h; IN A,(00h): 4000h, the switches */
        0x01, 0x00, 0x30, 0xED, 0x78, 0x32, 0x06, 0x70,     /* 3000h: nothing */
        0x01, 0x03, 0x00, 0x3E, 0xA1, 0xED, 0x79,           /* LD BC,0003h; LD A,A1h; OUT (C),A: R3 */
        0x01, 0x41, 0x00, 0x3E, 0xA2, 0xED, 0x79,           /* 0041h: R1 */
        0x3E, 0x00, 0xD3, 0x04,                             /* LD A,00h; OUT (04h),A: 0004h, R4 */
        0x01, 0x07, 0x00, 0x3E, 0xE7, 0xED, 0x79,           /* 0007h: A0-A2 = 7 */
        0x01, 0x00, 0x80, 0x3E, 0x01, 0xED, 0x79,           /* 8000h: nowhere */
        0x01, 0x00, 0x10, 0x3E, 0x01, 0xED, 0x79,           /* 1000h: read only */
        0x01, 0x00, 0x40, 0x3E, 0x0A, 0xED, 0x79, 0x76);    /* 4000h: the latch; HALT */
    b = board(&s);
    az_board_set(b, "switches", 0x9E);
    az_board_set(b, "foot", 0x05);
    run(b, 10, 400);
    for (i = 0; i < 7; i++)
        got[i] = az_board_peek(b, (uint16_t)(0x7000 + i));
    ok = !memcmp(got, want, 7) && s.n == 4 && az_board_get(b, "latch") == 0x0A && az_board_get(b, "mode") == 2
         && az_board_get(b, "unmapped_in") == 2 && az_board_get(b, "unmapped_out") == 2
         && az_board_get(b, "halted") == 1;
    for (i = 0; ok && i < 4; i++)
        ok = s.reg[i] == wreg[i] && s.val[i] == wval[i];
    snprintf(d, sizeof d, "reads %02X %02X %02X %02X %02X %02X %02X; %d chip writes, first R%d=%02X; latch %02X; "
             "unmapped %d in, %d out", got[0], got[1], got[2], got[3], got[4], got[5], got[6], s.n, s.reg[0], s.val[0],
             az_board_get(b, "latch"), az_board_get(b, "unmapped_in"), az_board_get(b, "unmapped_out"));
    report("io_decode", ok, d);
    az_board_destroy(b);
}

static void t_nmi_gate(void)
{
    stub s;
    az_board *b;
    char d[200];
    int c[6], ok;
    memset(&s, 0, sizeof s);
    roms();
    follow_switches();
    nmi_handler();
    b = board(&s);
    az_board_set(b, "switches", 0x08);     /* the gate shut */
    s.request = 1;
    run(b, 20, 200);
    c[0] = az_board_peek(b, 0x7010);       /* 0: A/R up, the gate shut */
    az_board_set(b, "switches", 0x18);
    az_board_run(b, 600);                  /* the firmware opens the gate: the edge is inside this slice */
    c[1] = az_board_peek(b, 0x7010);       /* 1 */
    run(b, 20, 200);
    c[2] = az_board_peek(b, 0x7010);       /* 1: the line stays up, taken once */
    s.request = 0;
    run(b, 2, 200);
    s.request = 1;
    run(b, 5, 200);
    c[3] = az_board_peek(b, 0x7010);       /* 2: A/R rose */
    az_board_set(b, "switches", 0x08);
    run(b, 5, 200);
    az_board_set(b, "switches", 0x18);
    run(b, 5, 200);
    c[4] = az_board_peek(b, 0x7010);       /* 3: the gate shut and opened again */
    az_board_set(b, "switches", 0x08);
    run(b, 5, 200);
    s.request = 0;
    run(b, 2, 200);
    s.request = 1;
    run(b, 5, 200);
    c[5] = az_board_peek(b, 0x7010);       /* 3: A/R rose with the gate shut */
    ok = c[0] == 0 && c[1] == 1 && c[2] == 1 && c[3] == 2 && c[4] == 3 && c[5] == 3
         && az_board_get(b, "nmi_edges") == 3 && s.n == 3 && s.reg[2] == 0 && s.val[2] == 3;
    snprintf(d, sizeof d, "NMIs: shut %d, opened %d, held %d, A/R rose %d, reopened %d, rose while shut %d; edges %d",
             c[0], c[1], c[2], c[3], c[4], c[5], az_board_get(b, "nmi_edges"));
    report("nmi_gate", ok, d);
    az_board_destroy(b);
}

static void t_nmi_release(void)
{
    stub s;
    az_board *b;
    char d[160];
    int line, c1, c2, ok;
    memset(&s, 0, sizeof s);
    s.drop_on_write = 1;
    roms();
    PUT(MAIN, 0x31, 0x00, 0x80, 0x01, 0x40, 0x40,           /* LD SP,8000h; LD BC,4040h */
        0x3E, 0x18, 0xED, 0x79, 0x18, 0xFE);                /* LD A,18h; OUT (C),A; JR self: the latch written once */
    nmi_handler();
    b = board(&s);
    run(b, 5, 200);                        /* the gate open, A/R down */
    s.request = 1;
    az_board_nmi(b);
    az_board_run(b, 2000);                 /* the handler writes R0: A/R down */
    line = az_board_get(b, "nmi_line");    /* released by the write itself, no az_board_nmi between */
    c1 = az_board_peek(b, 0x7010);
    s.request = 1;                         /* the next phoneme's request */
    az_board_nmi(b);
    az_board_run(b, 2000);
    c2 = az_board_peek(b, 0x7010);
    ok = line == 0 && c1 == 1 && c2 == 2 && s.n == 2;
    snprintf(d, sizeof d, "after the handler's write the line is %d; NMIs %d, then %d", line, c1, c2);
    report("nmi_release", ok, d);
    az_board_destroy(b);
}

/* the serial program: the 8251's mode and RTS down; wait for the switches' bit 1; RTS up; EI; idle.  RST 38h: the
   status to 7011h, the byte to the chip's R0, a count at 7010h */
static void serial_program(void)
{
    PUT(MAIN, 0x31, 0x00, 0x80, 0xED, 0x56,                 /* LD SP,8000h; IM 1 */
        0x01, 0x21, 0x21, 0x3E, 0x4E, 0xED, 0x79,           /* LD BC,2121h; LD A,4Eh; OUT (C),A: the mode word */
        0x3E, 0x15, 0xED, 0x79,                             /* LD A,15h; OUT (C),A: RTS down */
        0x01, 0x40, 0x40, 0xED, 0x78, 0xE6, 0x02, 0x28, 0xFA,   /* LD BC,4040h; w: IN A,(C); AND 02h; JR Z,w */
        0x01, 0x21, 0x21, 0x3E, 0x37, 0xED, 0x79,           /* RTS up */
        0xFB, 0x18, 0xFE);                                  /* EI; JR self */
    PUT(INT_AT, 0xF5, 0xC5, 0xE5,
        0x01, 0x21, 0x21, 0xED, 0x78, 0x32, 0x11, 0x70,     /* LD BC,2121h; IN A,(C); LD (7011h),A */
        0x01, 0x20, 0x20, 0xED, 0x78,                       /* LD BC,2020h; IN A,(C): the byte */
        0x01, 0x00, 0x00, 0xED, 0x79,                       /* LD BC,0000h; OUT (C),A */
        0x21, 0x10, 0x70, 0x34,                             /* LD HL,7010h; INC (HL) */
        0xE1, 0xC1, 0xF1, 0xFB, 0xC9);                      /* POP HL; POP BC; POP AF; EI; RET */
}

static void t_rxrdy(void)
{
    stub s;
    az_board *b;
    char d[200];
    int n0, rx0, ready0, i, ok;
    memset(&s, 0, sizeof s);
    roms();
    serial_program();
    b = board(&s);
    az_board_send(b, (const uint8_t *)"HELLO", 5);
    run(b, 30, 300);                       /* RTS down: nothing is loaded */
    n0 = s.n;
    rx0 = az_board_get(b, "rx");
    ready0 = az_board_get(b, "rx_ready");
    az_board_set(b, "switches", 0x12);
    run(b, 60, 300);
    ok = n0 == 0 && rx0 == 5 && ready0 == 0 && s.n == 5 && az_board_peek(b, 0x7010) == 5
         && az_board_peek(b, 0x7011) == 0x87 && az_board_get(b, "rx") == 0 && az_board_get(b, "rx_ready") == 0;
    for (i = 0; ok && i < 5; i++)
        ok = s.reg[i] == 0 && s.val[i] == "HELLO"[i];
    az_board_send(b, (const uint8_t *)"XYZ", 3);
    az_board_drop_input(b);
    run(b, 20, 300);
    ok = ok && s.n == 5;
    snprintf(d, sizeof d, "RTS down: %d read, %d queued; RTS up: %d bytes in order, %d interrupts, status %02X; "
             "dropped input read: %d", n0, rx0, s.n < 5 ? s.n : 5, az_board_peek(b, 0x7010), az_board_peek(b, 0x7011),
             s.n - 5);
    report("rxrdy", ok, d);
    az_board_destroy(b);
}

static void t_take_pcs(void)
{
    stub s;
    az_board *b;
    char d[160];
    uint16_t pcs[16];
    int armed, n, again, ok;
    memset(&s, 0, sizeof s);
    roms();
    follow_switches();
    b = board(&s);
    run(b, 5, 200);                        /* past the set-up, into the loop */
    armed = az_board_take_pcs(b, pcs, 16); /* the first call starts the watch */
    run(b, 5, 200);
    n = az_board_take_pcs(b, pcs, 16);
    ok = armed == 0 && n == 3 && pcs[0] == 0x0206 && pcs[1] == 0x0208 && pcs[2] == 0x020A;
    again = az_board_take_pcs(b, pcs, 16); /* nothing ran since */
    ok = ok && again == 0;
    snprintf(d, sizeof d, "armed %d; the loop: %d addresses, %04X %04X %04X; then %d", armed, n, pcs[0], pcs[1], pcs[2],
             again);
    report("take_pcs", ok, d);
    az_board_destroy(b);
}

static void t_two_boards(void)
{
    stub a1, a2, s1, s2;
    az_board *b1, *b2;
    char d[160];
    int i, same;
    roms();
    serial_program();
    memset(&a1, 0, sizeof a1);
    memset(&a2, 0, sizeof a2);
    b1 = board(&a1);
    az_board_set(b1, "switches", 0x12);
    az_board_send(b1, (const uint8_t *)"HELLO", 5);
    run(b1, 60, 300);
    az_board_destroy(b1);
    b2 = board(&a2);
    az_board_set(b2, "switches", 0x12);
    az_board_send(b2, (const uint8_t *)"WORLD!", 6);
    run(b2, 60, 340);
    az_board_destroy(b2);
    memset(&s1, 0, sizeof s1);
    memset(&s2, 0, sizeof s2);
    b1 = board(&s1);
    b2 = board(&s2);
    az_board_set(b1, "switches", 0x12);
    az_board_set(b2, "switches", 0x12);
    az_board_send(b1, (const uint8_t *)"HELLO", 5);
    az_board_send(b2, (const uint8_t *)"WORLD!", 6);
    for (i = 0; i < 60; i++) {
        run(b1, 1, 300);
        run(b2, 1, 340);
    }
    same = s1.n == a1.n && s2.n == a2.n && !memcmp(s1.val, a1.val, sizeof s1.val)
           && !memcmp(s2.val, a2.val, sizeof s2.val);
    snprintf(d, sizeof d, "writes alone %d and %d, interleaved %d and %d, %s", a1.n, a2.n, s1.n, s2.n,
             same ? "identical" : "DIFFERENT");
    report("two_boards", same && a1.n == 5 && a2.n == 6, d);
    az_board_destroy(b1);
    az_board_destroy(b2);
}

int main(void)
{
    t_rom_sizes();
    t_power_up();
    t_memory_map();
    t_io_decode();
    t_nmi_gate();
    t_nmi_release();
    t_rxrdy();
    t_take_pcs();
    t_two_boards();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
