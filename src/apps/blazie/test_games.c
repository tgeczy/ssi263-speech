/* test_games.c -- Blazie's games on the emulated units, headless (RetroBunn, PR #9: Simon locked the unit up).
 *
 *   test_games simon bl FIRMWARE STATE GAME     the Braille Lite 2000, or the Braille 'n Speak 2000 on its board
 *   test_games simon tns FIRMWARE - GAME        the Type 'n Speak, from its factory setup (tns_setup.h)
 *   test_games hangman bl|tns FIRMWARE STATE|- GAME
 *
 * GAME is the game's .BNS file from Blazie's disks (simon.bns, hangman.bns: never in the repo; run_tests and
 * linux_tests.sh look in firmware/blazie/games/).  Absent, the test says "skip" and passes.  The game goes into the
 * unit's RAM files with the type the unit gives a file of that name (bl_files.h: X, a program), and the unit runs it
 * with its own command: o-chord x (the Type 'n Speak's F9 x), the name, e-chord (Enter).  Keys at fixed times in the
 * unit's own time, the host's clock fixed: every run the same.
 *
 * Simon: started with dot 1 (the Type 'n Speak's a), it plays its first tone -- a steady pitch -- and, with no
 * answer, times out and speaks again.  It waits on IN A,(34h) bit 0 before every tone (A not 0: the read goes out to
 * the board, where nothing drives the bus; bl_board.c's io_read): answered FFh it waited forever, silent.  Hangman
 * never reads there: a guess answered.  Both: up to the start key (Hangman: the whole run) the same samples with the
 * bus answered FFh -- the bus value reaches nothing else.
 *
 * The must-fail control (run_tests, linux_tests.sh): TEST_GAMES_BREAK=1 answers FFh again (bl_board.h
 * bl_bus_break): Simon's tone and its time-out must fail.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#endif
#include "emu_unit.h"
#include "tns_setup.h"
#include "../../csrc/blazie/bl_board.h"   /* bl_bus_break */
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"

#define RATE 22050
#define WIN (RATE / 20)                     /* 50 ms windows */
#define HOST_TIME 1790000000LL              /* the host's clock, fixed (the Type 'n Speak reads it at its start) */

static int failures;
static int g_tns;
static char g_tmp[64];

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-46s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

static unsigned char *read_all(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p = NULL;
    if (f && fseek(f, 0, SEEK_END) == 0 && (*n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (p = (unsigned char *)malloc((size_t)*n)) != NULL && fread(p, 1, (size_t)*n, f) != (size_t)*n) {
        free(p);
        p = NULL;
    }
    if (f)
        fclose(f);
    return p;
}

/* ---- the game into the unit's files -------------------------------------------------------------------------------- */
/* the state with the game added to the RAM startup folder (as name), saved to out; 1, or 0 with the reason printed */
static int add_game(const char *state, const char *game, const char *name, const char *out)
{
    char err[256];
    bls_unit b;
    blf_fs *fs;
    long n = 0;
    unsigned char *data = read_all(game, &n);
    int ok;
    if (!data) { printf("FAIL read %s\n", game); return 0; }
    if (!bls_load(state, &b, err, sizeof err)) { printf("FAIL load %s: %s\n", state, err); free(data); return 0; }
    fs = blf_open(b.model, b.ram, b.flash, b.flash_size, err, sizeof err);
    ok = fs && blf_add(fs, 0, name, data, (unsigned long)n, 0, (46 << 9) | (6 << 5) | 1, 0, err, sizeof err);
    if (ok && blf_get(fs, blf_find(fs, name))->type != 'X') {
        snprintf(err, sizeof err, "not a program (type %c)", blf_get(fs, blf_find(fs, name))->type);
        ok = 0;
    }
    if (fs)
        blf_close(fs);
    ok = ok && bls_save(out, &b, err, sizeof err);
    if (!ok)
        printf("FAIL the game into the unit: %s\n", err);
    bls_free(&b);
    free(data);
    return ok;
}

/* ---- keys ----------------------------------------------------------------------------------------------------------- */
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

static int dots(char c)                     /* computer braille, as test_files.c types it */
{
    static const char *ch = "abcdefghijklmnopqrstuvwxyz";
    static const int d[] = {0x01, 0x03, 0x09, 0x19, 0x11, 0x0B, 0x1B, 0x13, 0x0A, 0x1A, 0x05, 0x07, 0x0D, 0x1D, 0x15,
                            0x0F, 0x1F, 0x17, 0x0E, 0x1E, 0x25, 0x27, 0x3A, 0x2D, 0x3D, 0x35};
    return d[strchr(ch, c) - ch];
}

static int tns_code(char c)                 /* the Type 'n Speak's key positions (tns_keymap_win.c) */
{
    static const char *ch = "abcdefghijklmnopqrstuvwxyz";
    static const int k[] = {0x94, 0xB3, 0xB2, 0xB4, 0xB0, 0xBC, 0xC0, 0xC8, 0xCD, 0xC4, 0xCC, 0xD4, 0xC3, 0xBB, 0xD5,
                            0xDD, 0x90, 0xB8, 0xAC, 0xB5, 0xC5, 0xAB, 0x98, 0xAA, 0xBD, 0x92};
    return k[strchr(ch, c) - ch];
}

static int letter(char c) { return g_tns ? tns_code(c) : dots(c); }
static int enter_key(void) { return g_tns ? 0xDB : 0x51; }     /* Enter; e-chord */

/* the unit's own command to run a program, from its main menu once its greeting is over; the time of the Enter */
static double run_program(script *sc, const char *name)
{
    double at;
    sc->t = 10.0;
    key(sc, g_tns ? 0xC6 : 0x55, 2.6);      /* F9; o-chord */
    key(sc, letter('x'), 3.6);              /* "enter program to execute" */
    for (; *name; name++)
        key(sc, letter(*name), 0.35);
    at = sc->t;
    key(sc, enter_key(), 0);
    return at;
}

/* the unit from in, the keys, `seconds` of its audio into out (seconds * RATE samples); the bus as bl_bus_break */
static int render(const char *fw, const char *in, const script *sc, double seconds, short *out)
{
    char err[256];
    emu_unit *u;
    int n = (int)(seconds * RATE), i, k = 0;
    emu_fake_host_time(HOST_TIME);
    u = emu_create(g_tns ? EMU_TYPE_N_SPEAK : EMU_BRAILLE_LITE, fw, in, RATE, 0, err, sizeof err);
    if (!u) { printf("FAIL create: %s\n", err); return 0; }
    emu_set_flash_timed(u, 0);
    for (i = 0; i < n; i += WIN) {
        while (k < sc->n && (int)(sc->s[k].t * RATE) < i + WIN) {
            int c = sc->s[k++].key;
            if (g_tns) {
                emu_key(u, c | 0x80);       /* down, then up */
                emu_key(u, c & 0x7F);
            } else
                emu_key(u, c);
        }
        emu_render(u, out + i, n - i < WIN ? n - i : WIN);
    }
    emu_destroy(u);
    return 1;
}

/* ---- the audio --------------------------------------------------------------------------------------------------- */
static double rms(const short *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return n ? sqrt(s / n) / 32768.0 : 0;
}

/* rising crossings through +-500 (the hiss stays inside): a steady pitch counts the same in every window */
static int crossings(const short *x, int n)
{
    int i, c = 0, low = 0;
    for (i = 0; i < n; i++) {
        if (x[i] < -500)
            low = 1;
        else if (x[i] > 500 && low) {
            c++;
            low = 0;
        }
    }
    return c;
}

/* the first steady tone from `from` to `to` s: at least `need` windows in a row heard (rms > 0.02), each counting
   the same crossings within 1, at 200-2000 Hz; its start in *at and pitch in *hz; 0 when none */
static int steady_tone(const short *pcm, double from, double to, int need, double *at, double *hz)
{
    int w, run = 0, first = 0, last = -1;
    for (w = (int)(from * 20); w < (int)(to * 20); w++) {
        const short *x = pcm + (long)w * WIN;
        int c = crossings(x, WIN), ok = rms(x, WIN) > 0.02 && c >= 10 && c <= 100;
        if (ok && run && abs(c - last) <= 1)
            run++;
        else {
            run = ok;
            first = w;
        }
        last = c;
        if (run >= need) {
            *at = first / 20.0;
            *hz = c * 20.0;
            return 1;
        }
    }
    return 0;
}

/* seconds heard (rms > 0.01) from `from` to `to` s */
static double heard(const short *pcm, double from, double to)
{
    int w, k = 0;
    for (w = (int)(from * 20); w < (int)(to * 20); w++)
        k += rms(pcm + (long)w * WIN, WIN) > 0.01;
    return k / 20.0;
}

/* the first sample from `from` s where a and b differ, as a time; -1 when none up to n samples */
static double first_difference(const short *a, const short *b, long from, long n)
{
    long i;
    for (i = from; i < n; i++)
        if (a[i] != b[i])
            return (double)i / RATE;
    return -1;
}

/* ---- the games ------------------------------------------------------------------------------------------------------ */
static void simon(const char *fw, const char *state)
{
    script sc;
    double enter, start, total, at = 0, hz = 0, diff;
    long n, s0;
    short *pcm, *ffh;
    int brk = bl_bus_break, tone;
    char d[200];
    memset(&sc, 0, sizeof sc);
    enter = run_program(&sc, "simon");
    start = enter + 12.0;
    total = start + 14.0;
    n = (long)(total * RATE);
    s0 = (long)(start * RATE);
    pcm = (short *)calloc((size_t)n, sizeof(short));
    ffh = (short *)calloc((size_t)n, sizeof(short));
    sc.t = start;
    key(&sc, letter('a'), 0);                        /* "any other key to start the game" */
    if (!pcm || !ffh || !render(fw, state, &sc, total, pcm))
        exit(1);
    bl_bus_break = 1;                                /* the same run with the bus answered FFh */
    if (!render(fw, state, &sc, total, ffh))
        exit(1);
    bl_bus_break = brk;
    snprintf(d, sizeof d, "%.1f s heard after the name", heard(pcm, enter, start));
    check("the game runs: its welcome and prompt", heard(pcm, enter, start) > 3.0, d);
    diff = first_difference(pcm, ffh, 0, s0);
    snprintf(d, sizeof d, diff < 0 ? "%.0f s, every sample" : "first differs at %.3f s", diff < 0 ? start : diff);
    check("to the start key, the same with the bus FFh", diff < 0, d);
    tone = steady_tone(pcm, start, start + 2.0, 6, &at, &hz);
    if (tone)
        snprintf(d, sizeof d, "%.0f Hz from %.2f s after the key", hz, at - start);
    else
        snprintf(d, sizeof d, "none in 2 s (%.2f s heard)", heard(pcm, start, start + 2.0));
    check("the first tone after the start key", tone, d);
    snprintf(d, sizeof d, "%.1f s heard 3-14 s after the key", heard(pcm, start + 3.0, total));
    check("the game goes on: its time-out answered", heard(pcm, start + 3.0, total) > 2.0, d);
    free(pcm);
    free(ffh);
}

static void hangman(const char *fw, const char *state)
{
    script sc;
    double enter, guess, total, diff;
    long n;
    short *pcm, *ffh;
    int brk = bl_bus_break;
    char d[200];
    memset(&sc, 0, sizeof sc);
    enter = run_program(&sc, "hangman");
    guess = enter + 15.0;
    total = guess + 8.0;
    n = (long)(total * RATE);
    pcm = (short *)calloc((size_t)n, sizeof(short));
    ffh = (short *)calloc((size_t)n, sizeof(short));
    sc.t = guess;
    key(&sc, letter('e'), 1.0);                      /* a guess: e, Enter */
    key(&sc, enter_key(), 0);
    if (!pcm || !ffh || !render(fw, state, &sc, total, pcm))
        exit(1);
    bl_bus_break = 1;
    if (!render(fw, state, &sc, total, ffh))
        exit(1);
    bl_bus_break = brk;
    snprintf(d, sizeof d, "%.1f s heard after the name", heard(pcm, enter, guess));
    check("the game runs: its welcome and prompt", heard(pcm, enter, guess) > 3.0, d);
    snprintf(d, sizeof d, "%.1f s heard after it, %.1f s in the 3 s before", heard(pcm, guess + 1.0, total),
             heard(pcm, guess - 3.0, guess));
    check("a guess answered", heard(pcm, guess + 1.0, total) > 1.0 && heard(pcm, guess - 3.0, guess) < 0.3, d);
    diff = first_difference(pcm, ffh, 0, n);
    snprintf(d, sizeof d, diff < 0 ? "%.0f s, every sample" : "first differs at %.3f s", diff < 0 ? total : diff);
    check("the whole run the same with the bus FFh", diff < 0, d);
    free(pcm);
    free(ffh);
}

int main(int argc, char **argv)
{
    char err[256], with_game[96], factory[96];
    const char *fw, *state, *game;
    int is_simon;
    FILE *f;
    if (argc != 6 || (strcmp(argv[1], "simon") && strcmp(argv[1], "hangman"))
            || (strcmp(argv[2], "bl") && strcmp(argv[2], "tns"))) {
        printf("usage: test_games simon|hangman bl|tns FIRMWARE STATE|- GAME\n");
        return 2;
    }
    is_simon = !strcmp(argv[1], "simon");
    g_tns = !strcmp(argv[2], "tns");
    fw = argv[3];
    state = argv[4];
    game = argv[5];
    if (!(f = fopen(game, "rb"))) {
        printf("skip test_games %s: no game at %s\n", argv[1], game);
        return 0;
    }
    fclose(f);
    bl_bus_break = getenv("TEST_GAMES_BREAK") && atoi(getenv("TEST_GAMES_BREAK")) == 1;
    snprintf(g_tmp, sizeof g_tmp, "test_games.%d", (int)_getpid());
    snprintf(with_game, sizeof with_game, "%s.game.state", g_tmp);
    snprintf(factory, sizeof factory, "%s.factory.state", g_tmp);
    if (!strcmp(state, "-")) {
        if (!g_tns) { printf("FAIL the Braille Lite needs its state\n"); return 1; }
        emu_fake_host_time(HOST_TIME);
        if (!tns_factory_setup(fw, factory, err, sizeof err)) { printf("FAIL factory setup: %s\n", err); return 1; }
        state = factory;
    }
    if (add_game(state, game, is_simon ? "simon.bns" : "hangman.bns", with_game)) {
        if (is_simon)
            simon(fw, with_game);
        else
            hangman(fw, with_game);
    } else
        failures++;
    remove(with_game);
    remove(factory);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
