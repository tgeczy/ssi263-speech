/* test_ram_full.c -- a flash file moved into a Braille Lite's RAM past its first 256 KB, headless.
 *
 *   test_ram_full FIRMWARE STATE      the Braille Lite 2000 from its shipped state (bl2_2003_warm.state), or the
 *                                     Braille 'n Speak 2000 from its factory state (the same board and keys)
 *
 * The units have 768 KB of RAM, 40000h-FFFFFh: the firmware finds them at power-on (it writes each page and reads
 * it back) and gives files RAM up there.  Before the fix a state kept only the first 256 KB, so a file the unit had
 * put at 80000h or above came back as nothing (a unit in use lost 37 files that way, 2026-11-11).
 *
 * Two runs, each from the shipped state with made-up files (o-chord f o, the name, e-chord; o-chord f, the move
 * chord, y: the unit's own commands open a three-page flash file and move it to RAM):
 *   across  RAM filled to one free page below 80000h: the file goes to RAM across 80000h and must be whole there,
 *           with the file system's rules holding, after the state is saved -- and again after the unit is
 *           started from that state and saved once more (switched off and on);
 *   full    all 768 KB filled to one free page: three pages cannot go into one, the file must stay in flash,
 *           whole, and every filler file (the last ones past 80000h) must be whole after the save.
 *
 * Must fail: TEST_RAM_FULL_BREAK=1 (bl_board.h bl_ram_break) saves the RAM's first 256 KB only, as before the fix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#endif
#include "../../csrc/blazie/bl_board.h"
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"

#define STEP_CYCLES 122880ULL               /* 20 ms of the 6.144 MHz Z180 */
#define PAGE 4096UL
#define RAM_TOP 0x100000UL                  /* the RAM's end: 768 KB from 40000h */
#define OLD_TOP 0x80000UL                   /* the end of what a state kept before the fix */
#define BIG_N 10000UL                       /* three pages */
#define MAX_FILL 16
#define DATE ((46 << 9) | (6 << 5) | 1)

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-46s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

static unsigned char g_big[BIG_N];
static unsigned char *g_fill[MAX_FILL];     /* the filler files' text, kept to compare */
static unsigned long g_fill_n[MAX_FILL];
static int g_n_fill;

static void make_text(unsigned char *d, unsigned long n, unsigned seed)
{
    unsigned long i;
    for (i = 0; i < n; i++)
        d[i] = (i % 60 == 59) ? '\r' : (unsigned char)('a' + (i * 7 + seed) % 26);
    d[n - 1] = '\r';
}

/* the shipped state, its RAM filled with made-up files until one page is free below `top`, "big" in flash; saved
   to out */
static int prepare(const char *state, const char *out, unsigned long top)
{
    char err[256], name[16];
    bls_unit b;
    blf_fs *fs;
    unsigned long ram_free, flash_free;
    int ok;
    if (!bls_load(state, &b, err, sizeof err)) { printf("FAIL load %s: %s\n", state, err); return 0; }
    fs = blf_open(b.model, b.ram, b.flash, b.flash_size, err, sizeof err);
    ok = fs != NULL;
    for (g_n_fill = 0; ok && g_n_fill < MAX_FILL; g_n_fill++) {
        unsigned long pages, end, n;
        blf_free_space(fs, &ram_free, &flash_free);
        end = RAM_TOP - ram_free;           /* the byte after the last RAM file */
        if (end + PAGE >= top)
            break;
        pages = (top - end) / PAGE - 1;
        if (pages > 64) pages = 64;
        n = pages * PAGE - 100;             /* a few bytes short of whole pages, as text is */
        g_fill[g_n_fill] = (unsigned char *)realloc(g_fill[g_n_fill], n);
        g_fill_n[g_n_fill] = n;
        make_text(g_fill[g_n_fill], n, (unsigned)g_n_fill);
        snprintf(name, sizeof name, "fill%d", g_n_fill);
        ok = blf_add(fs, 0, name, g_fill[g_n_fill], n, 0, DATE, 0, err, sizeof err);
    }
    make_text(g_big, BIG_N, 99);
    ok = ok && blf_add(fs, 1, "big", g_big, BIG_N, 0, DATE, 0, err, sizeof err);
    if (fs) {
        blf_free_space(fs, &ram_free, &flash_free);
        printf("     prepared: %d filler files, RAM to %05lXh, %lu bytes free\n", g_n_fill, RAM_TOP - ram_free,
               ram_free);
        blf_close(fs);
    }
    ok = ok && bls_save(out, &b, err, sizeof err);
    if (!ok)
        printf("FAIL prepare: %s\n", err);
    bls_free(&b);
    return ok;
}

/* ---- keys ---------------------------------------------------------------------------------------------------------- */
typedef struct { double t; int key; } step_t;
typedef struct { step_t s[64]; int n; double t; } script;

static void key(script *sc, int k, double gap)
{
    if (sc->n < 64) {
        sc->s[sc->n].t = sc->t;
        sc->s[sc->n++].key = k;
    }
    sc->t += gap;
}

/* test_files.c's chords: o-chord, f, o, e-chord, the move chord; b, g, i, y in computer braille */
enum { B_OCHORD = 0x55, B_F = 0x0B, B_O = 0x15, B_ECHORD = 0x51, B_MOVE = 0x63, B_B = 0x03, B_G = 0x1B, B_I = 0x0A,
       B_Y = 0x3D };
#define AFTER_KEY 3.6                       /* each command's spoken answer over before the next key (test_files.c) */

/* the unit from in, the keys (move: open "big" and move it to RAM; else none), saved to out */
static int run_unit(const char *fw, const char *in, const char *out, int move)
{
    char err[256];
    script sc = {{{0}}, 0, 8.0};            /* the first key once the greeting is over (test_files.c) */
    bl_unit *u;
    int step, k = 0, n;
    if (move) {
        key(&sc, B_OCHORD, 2.6);            /* open "big" */
        key(&sc, B_F, AFTER_KEY);
        key(&sc, B_O, AFTER_KEY);
        key(&sc, B_B, 0.35);
        key(&sc, B_I, 0.35);
        key(&sc, B_G, 0.35);
        key(&sc, B_ECHORD, AFTER_KEY);
        key(&sc, B_OCHORD, 2.6);            /* move it to RAM: "all files" mode asks, y */
        key(&sc, B_F, AFTER_KEY);
        key(&sc, B_MOVE, AFTER_KEY);
        key(&sc, B_Y, 8.6);
        key(&sc, B_ECHORD, AFTER_KEY);
    }
    u = bl_create(fw, in, 40.0, NULL, NULL, 0, err, sizeof err);
    if (!u) { printf("FAIL create: %s\n", err); return 0; }
    bl_flash_timed(u, 0);
    n = (int)((sc.t + 6.0) * 50);
    for (step = 0; step < n; step++) {
        while (k < sc.n && sc.s[k].t * 50 <= step)
            bl_key(u, sc.s[k++].key);
        bl_run(u, STEP_CYCLES);
        bl_clear_events(u);
    }
    k = bl_save_state(u, out);
    bl_destroy(u);
    if (!k) printf("FAIL save %s\n", out);
    return k;
}

/* the saved state at path checked: the rules, "big" whole in RAM (want_ram) or in flash, the filler files whole */
static void check_state(const char *path, const char *what, int want_ram)
{
    char err[256], d[300], name[120];
    bls_unit b;
    blf_fs *fs;
    unsigned long n = 0;
    const unsigned char *x;
    int i, k, bad = -1;
    if (!bls_load(path, &b, err, sizeof err)) {
        snprintf(name, sizeof name, "%s: the state reads", what);
        check(name, 0, err);
        return;
    }
    fs = blf_open(b.model, b.ram, b.flash, b.flash_size, err, sizeof err);
    if (!fs) {
        snprintf(name, sizeof name, "%s: the state reads", what);
        check(name, 0, err);
        bls_free(&b);
        return;
    }
    k = blf_check(fs, d, sizeof d);
    snprintf(name, sizeof name, "%s: the file system's rules hold", what);
    check(name, k, k ? "blf_check" : d);
    i = blf_find(fs, "big");
    x = i >= 0 ? blf_data(fs, i, &n) : NULL;
    snprintf(d, sizeof d, "%s, %lu of %lu bytes%s", i < 0 ? "gone" : blf_get(fs, i)->in_flash ? "in flash" : "in RAM",
             n, BIG_N, x && n == BIG_N && memcmp(x, g_big, BIG_N) ? ", not its text" : "");
    snprintf(name, sizeof name, "%s: the file whole, %s", what, want_ram ? "in RAM" : "left in flash");
    check(name, x && n == BIG_N && !memcmp(x, g_big, BIG_N) && blf_get(fs, i)->in_flash == !want_ram, d);
    for (k = 0; k < g_n_fill && bad < 0; k++) {
        snprintf(name, sizeof name, "fill%d", k);
        i = blf_find(fs, name);
        x = i >= 0 ? blf_data(fs, i, &n) : NULL;
        if (!x || n != g_fill_n[k] || memcmp(x, g_fill[k], n))
            bad = k;
    }
    snprintf(d, sizeof d, bad < 0 ? "%d files" : "fill%d is not its text", bad < 0 ? g_n_fill : bad);
    snprintf(name, sizeof name, "%s: the filler files whole", what);
    check(name, bad < 0, d);
    blf_close(fs);
    bls_free(&b);
}

int main(int argc, char **argv)
{
    char in[64], out[64], again[64];
    int k;
    if (argc < 3) {
        fprintf(stderr, "usage: test_ram_full FIRMWARE STATE\n");
        return 2;
    }
    bl_ram_break = getenv("TEST_RAM_FULL_BREAK") && atoi(getenv("TEST_RAM_FULL_BREAK")) == 1;
    snprintf(in, sizeof in, "test_ram_full.%d.in.state", (int)_getpid());
    snprintf(out, sizeof out, "test_ram_full.%d.out.state", (int)_getpid());
    snprintf(again, sizeof again, "test_ram_full.%d.again.state", (int)_getpid());
    /* across: one page free below 80000h, the file moved there runs past it */
    k = prepare(argv[2], in, OLD_TOP) && run_unit(argv[1], in, out, 1);
    if (k) {
        check_state(out, "across", 1);
        if (run_unit(argv[1], out, again, 0))
            check_state(again, "across, switched off and on", 1);
    } else
        failures++;
    /* full: one page free in all 768 KB */
    k = prepare(argv[2], in, RAM_TOP) && run_unit(argv[1], in, out, 1);
    if (k)
        check_state(out, "full", 0);
    else
        failures++;
    remove(in);
    remove(out);
    remove(again);
    for (k = 0; k < MAX_FILL; k++)
        free(g_fill[k]);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
