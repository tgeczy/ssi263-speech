/* test_files.c -- files in and out of the units (Tomi: files in and out; ../../csrc/blazie/bl_files.h,
 * bl_files_xfer.h), headless, against the units' own commands.
 *
 *   test_files bl FIRMWARE STATE [--break=N]   a Braille Lite (English or Spanish) from its shipped state, or a
 *                                              Braille 'n Speak 2000 (English or Slovak) from its factory state
 *   test_files tns FIRMWARE [--break=N|cold]   the Type 'n Speak from its factory start: its cold reset, its own
 *                                              questions answered yes (tns_setup.h)
 *
 * Each run: the firmware makes files with its own commands, moves the first one to flash and leaves one open with
 * text typed since it was opened; the first file must be in a flash folder, whole (on the Type 'n Speak, before its
 * real cold reset, the first file lost its first character and a file moved to flash was lost: Timothy, Jayson), and
 * the export must hold every file with its exact bytes (the open one with the text typed since).  An image with new
 * files (RAM, flash, a new folder, CR LF line ends, a file of two pages), a flash file rewritten, that also drops a
 * file BEFORE the open one is imported; the unit, started again from it, types into its open file and lists its
 * files with its own command (the verbose list into the clipboard), which must name every file with its size; it
 * then moves an imported flash file to RAM and an imported RAM file to flash with its own commands, and the copies
 * must be byte for byte the imported text, the flash copy placed by the unit's own allocator in the next free block
 * below the imported ones (its free space is ours).  Export, import, export gives the same image byte for byte, on
 * the unit itself and on a fresh one.
 *
 * --break=N (bl_files.h's blf_break) puts one bug back; run_tests' controls must fail on exactly the checks that see
 * it: 1 the open file's live pointers ignored, 2 new flash blocks not marked used, 3 a new RAM file's end of text one
 * byte short, 4 an imported file's date dropped, 5 the open file's number not followed, 6 a binary file's line ends
 * converted on import (issue #16: a compiled BASIC program, prog.bas in the image); --break=cp the Slovak unit's
 * names in code page 850 (its folders reach the PC with a thorn for the s-caron).  --break=cold
 * (tns_board.h's tns_cold_break) starts the Type 'n Speak as the emulator did before: the unit's warm reset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include "emu_unit.h"
#include "tns_setup.h"
#include "../../csrc/blazie/bl_board.h"
#include "../../csrc/blazie/tns_board.h"
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"
#include "../../csrc/blazie/bl_files_xfer.h"
#include "../../csrc/blazie/fat_img.h"

#define STEP_CYCLES 122880ULL               /* 20 ms of the 6.144 MHz Z180 */
#define MAX_STEPS 800

static int failures;
static char g_tmp[64];                      /* this run's temporary file prefix */
static char g_yes = 'y';                    /* the unit's yes: s on the Spanish units */
/* the names the test types on the unit: with no dot on the Braille Lite (Spanish computer braille writes the
   period with another chord), so the same chords serve both languages */
static const char *g_doc, *g_imp, *g_imp_image;
/* a compiled BASIC program's start, as COMPILE.BNS writes one (issue #16: HACK.BAS's first bytes): binary, NUL
   bytes in it, its line numbers 10 and 20 the bytes 0Ah and 14h, a 0Dh 0Ah inside a string's length and number */
static const char PROG_BAS[] = "\0\n\0#\x01\x01\x1d\"Welcome!\"\0\x14\0\x19\x18\x06model$\r\n\"bns\"\0\x1e\0\x0b";

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-46s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

static const char *tmp(const char *what)
{
    static char path[8][128];
    static int k;
    k = (k + 1) % 8;
    snprintf(path[k], sizeof path[k], "%s.%s", g_tmp, what);
    return path[k];
}

/* ---- key scripts ------------------------------------------------------------------------------------------------ */
typedef struct { double t; int key; } step_t;
typedef struct { step_t s[MAX_STEPS]; int n; double t; int tns; } script;

static void key(script *sc, int k, double gap)
{
    if (sc->n < MAX_STEPS) {
        sc->s[sc->n].t = sc->t;
        sc->s[sc->n++].key = k;
    }
    sc->t += gap;
}

static void wait_s(script *sc, double s) { sc->t += s; }

/* braille: a letter's dots (computer braille as the Braille Lite takes it); space bar for ' ' */
static int dots(char c)
{
    static const char *ch = "abcdefghijklmnopqrstuvwxyz.";
    static const int d[] = {0x01, 0x03, 0x09, 0x19, 0x11, 0x0B, 0x1B, 0x13, 0x0A, 0x1A, 0x05, 0x07, 0x0D, 0x1D, 0x15,
                            0x0F, 0x1F, 0x17, 0x0E, 0x1E, 0x25, 0x27, 0x3A, 0x2D, 0x3D, 0x35, 0x28};
    const char *p = strchr(ch, c);
    return c == ' ' ? 0x40 : p ? d[p - ch] : 0x40;
}

/* the Type 'n Speak's key positions (tns_keymap_win.c, measured) */
static int tns_code(char c)
{
    static const char *ch = "abcdefghijklmnopqrstuvwxyz.";
    static const int k[] = {0x94, 0xB3, 0xB2, 0xB4, 0xB0, 0xBC, 0xC0, 0xC8, 0xCD, 0xC4, 0xCC, 0xD4, 0xC3, 0xBB, 0xD5,
                            0xDD, 0x90, 0xB8, 0xAC, 0xB5, 0xC5, 0xAB, 0x98, 0xAA, 0xBD, 0x92, 0xC2};
    const char *p = strchr(ch, c);
    return c == ' ' ? 0xA9 : p ? k[p - ch] : 0xA9;
}

#define TNS_PRESS 0x100                     /* a Type 'n Speak key down and up; else one event as it stands */
static void type_text(script *sc, const char *s)
{
    for (; *s; s++)
        key(sc, sc->tns ? TNS_PRESS | tns_code(*s) : dots(*s), 0.35);
}

enum { T_F1 = 0x8A, T_ENTER = 0xDB, T_ESC = 0x89, T_ALT = 0xA1, T_V = 0xAB, T_SHIFT = 0xE1, T_NUMLOCK = 0xD6 };
enum { B_OCHORD = 0x55, B_F = 0x0B, B_C = 0x09, B_O = 0x15, B_ECHORD = 0x51, B_VCHORD = 0x67, B_MOVE = 0x63 };

/* Commands are spaced so that each one's spoken answer is over before the next key: a key pressed while the unit
   speaks can be taken as "be quiet" (measured: with 2 s after o-chord instead of 2.6 a later chord was lost). */
#define AFTER_KEY 3.6
/* the first key after power-on: once the unit has said it is ready (a key during the greeting only silences it) */
#define SPOKEN_START 8.0
/* ... after the import, the Slovak Braille 'n Speak 2000's first key comes at 12 s: with it at 8 s the q never
   reached the open file (it stayed "abcxyz"), with it at 12 s it does -- its words at power-on take longer (its
   letter-to-sound rules are slower: a key's answer takes it ~443 ms against the English unit's ~247) */
static double g_spoken_start = SPOKEN_START;
/* the unit's names' code page (bl_files_xfer.h): 852 on the Slovak Braille 'n Speak 2000 (--break=cp: 850, the
   control -- its flash folder then reaches the PC as "fle\xC3\xBE", a thorn, not its s-caron) */
static int g_cp = BLX_CP850, g_cp_break, g_slovak;

/* the files menu: the Braille Lite's o-chord, f; the Type 'n Speak's F1 */
static void files_menu(script *sc)
{
    if (sc->tns)
        key(sc, TNS_PRESS | T_F1, AFTER_KEY);
    else {
        key(sc, B_OCHORD, 2.6);
        key(sc, B_F, AFTER_KEY);
    }
}

static void enter(script *sc)
{
    key(sc, sc->tns ? TNS_PRESS | T_ENTER : B_ECHORD, AFTER_KEY);
}

static void new_line(script *sc)            /* in a file: Enter; the Braille Lite's dots 4-6 chord */
{
    key(sc, sc->tns ? TNS_PRESS | T_ENTER : 0x68, sc->tns ? 0.4 : 0.6);
}

static void open_or_create(script *sc, int create, const char *name)
{
    files_menu(sc);
    key(sc, sc->tns ? TNS_PRESS | tns_code(create ? 'c' : 'o') : create ? B_C : B_O, AFTER_KEY);
    type_text(sc, name);
    enter(sc);
}

/* the verbose file list into the clipboard: the Braille Lite's v-chord, the Type 'n Speak's Alt+V (in the files
   menu) */
static void list_to_clipboard(script *sc)
{
    files_menu(sc);
    if (sc->tns) {
        key(sc, T_ALT, 0.2);
        key(sc, T_V, 0.2);
        key(sc, T_V & 0x7F, 0.2);
        key(sc, T_ALT & 0x7F, 5.0);
        key(sc, TNS_PRESS | T_ESC, AFTER_KEY);
    } else {
        key(sc, B_VCHORD, 5.0);
        key(sc, B_ECHORD, AFTER_KEY);
    }
}

/* the open file moved by the unit (RAM to flash, flash to RAM: in "all files" mode it asks, y) */
static void move_open_file(script *sc)
{
    files_menu(sc);
    if (sc->tns) {
        key(sc, T_SHIFT, 0.2);
        key(sc, T_NUMLOCK, 0.2);
        key(sc, T_NUMLOCK & 0x7F, 0.2);
        key(sc, T_SHIFT & 0x7F, AFTER_KEY);
        key(sc, TNS_PRESS | tns_code(g_yes), 8.6);
        key(sc, TNS_PRESS | T_ESC, AFTER_KEY);
    } else {
        key(sc, B_MOVE, AFTER_KEY);
        key(sc, dots(g_yes), 8.6);
        key(sc, B_ECHORD, AFTER_KEY);
    }
}

/* ---- running a unit ----------------------------------------------------------------------------------------------- */
static int run_bl(const char *fw, const char *in, const char *out, const script *sc)
{
    char err[256];
    bl_unit *u = bl_create(fw, in, 40.0, NULL, NULL, 0, err, sizeof err);
    int step, k = 0, n = (int)((sc->t + 6.0) * 50);
    if (!u) { printf("FAIL create: %s\n", err); return 0; }
    bl_flash_timed(u, 0);
    for (step = 0; step < n; step++) {
        while (k < sc->n && sc->s[k].t * 50 <= step) {
            if (getenv("TEST_FILES_TRACE"))  /* debugging: the keys, and a state every 10 s (out.<s>) */
                printf("  key %02X at %.2f s\n", sc->s[k].key, step / 50.0);
            bl_key(u, sc->s[k++].key);
        }
        bl_run(u, STEP_CYCLES);
        bl_clear_events(u);
        if (getenv("TEST_FILES_TRACE") && step % 500 == 499) {
            char p[200];
            snprintf(p, sizeof p, "%s.%d", out, (step + 1) / 50);
            bl_save_state(u, p);
        }
    }
    k = bl_save_state(u, out);
    bl_destroy(u);
    if (!k) printf("FAIL save %s\n", out);
    return k;
}

static int run_tns(const char *fw, const char *in, const char *out, const script *sc)
{
    char err[256];
    short buf[221];
    emu_unit *u = emu_create(EMU_TYPE_N_SPEAK, fw, in, 22050, 0, err, sizeof err);
    int step, k = 0, n = (int)((sc->t + 6.0) * 100);
    if (!u) { printf("FAIL create: %s\n", err); return 0; }
    emu_set_flash_timed(u, 0);
    for (step = 0; step < n; step++) {
        while (k < sc->n && sc->s[k].t * 100 <= step) {
            int c = sc->s[k++].key;
            if (c & TNS_PRESS) {
                emu_key(u, (c & 0x7F) | 0x80);
                emu_key(u, c & 0x7F);
            } else
                emu_key(u, c);
        }
        emu_render(u, buf, 221);
    }
    k = emu_save(u, out);
    emu_destroy(u);
    if (!k) printf("FAIL save %s\n", out);
    return k;
}

static int run(int tns, const char *fw, const char *in, const char *out, const script *sc)
{
    return tns ? run_tns(fw, in, out, sc) : run_bl(fw, in, out, sc);
}

/* ---- reading results ---------------------------------------------------------------------------------------------- */
typedef struct { bls_unit u; blf_fs *fs; } unit_t;

static int load(const char *path, unit_t *x)
{
    char err[256];
    if (!bls_load(path, &x->u, err, sizeof err)) { printf("FAIL load %s: %s\n", path, err); return 0; }
    x->fs = blf_open(x->u.model, x->u.ram, x->u.flash, x->u.flash_size, err, sizeof err);
    if (!x->fs) { printf("FAIL open %s: %s\n", path, err); bls_free(&x->u); return 0; }
    return 1;
}

static void unload(unit_t *x)
{
    blf_close(x->fs);
    bls_free(&x->u);
}

/* file `name`'s bytes in the unit (NULL if it has none); *in_flash */
static const unsigned char *unit_file(const unit_t *x, const char *name, unsigned long *n, int *in_flash)
{
    int i = blf_find(x->fs, name);
    if (i < 0) { *n = 0; return NULL; }
    if (in_flash) *in_flash = blf_get(x->fs, i)->in_flash;
    return blf_data(x->fs, i, n);
}

static int same(const unsigned char *a, unsigned long na, const char *b, unsigned long nb)
{
    return a && na == nb && !memcmp(a, b, nb);
}

/* a file's bytes in an image ("folder/name", the folder as the unit names it: the image's name is its UTF-8, the
   Slovak unit's "fles subory" with its own letters); NULL if absent */
static unsigned char *image_file(const unsigned char *img, unsigned long size, const char *folder, const char *name,
                                 unsigned long *n)
{
    char err[200], f8[200];
    fat_volume *v = fat_read(img, size, err, sizeof err);
    unsigned char *d = NULL;
    int i;
    if (!v) return NULL;
    blx_unit_to_utf8_cp(g_cp, folder, f8, sizeof f8);
    for (i = 0; i < fat_count(v); i++) {
        const fat_entry *e = fat_get(v, i);
        if (!e->dir && !strcmp(e->name, name) && e->parent >= 0 && !strcmp(fat_get(v, e->parent)->name, f8)) {
            d = fat_data(v, i, n);
            break;
        }
    }
    fat_close(v);
    return d;
}

static void show(char *out, size_t cap, const unsigned char *d, unsigned long n)
{
    size_t o = 0;
    unsigned long i;
    if (!d) { snprintf(out, cap, "(none)"); return; }
    for (i = 0; i < n && o + 5 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, d[i] == '\r' ? "\\r" : d[i] < 32 || d[i] > 126 ? "?" : "%c", d[i]);
    out[o] = 0;
}

/* the unit's verbose list names `name` with `bytes` after it (a line of its own, or after the RAM file's number);
   bytes < 0: with any size */
static int listed(const char *text, const char *name, long bytes)
{
    size_t n = strlen(name);
    const char *p;
    for (p = strstr(text, name); p; p = strstr(p + 1, name)) {
        char *end;
        long v;
        if (p > text && p[-1] != ' ' && p[-1] != '\r' && p[-1] != '\n')
            continue;
        if (p[n] != ' ')
            continue;
        v = strtol(p + n + 1, &end, 10);
        if (end != p + n + 1 && *end == ' ' && (bytes < 0 || v == bytes))
            return 1;
    }
    return 0;
}

/* ---- the checks -------------------------------------------------------------------------------------------------- */
static const char *g_book;                  /* a multi-block grade 2 text for flash */
static char g_big[5001];                    /* two pages of RAM text */

static void make_texts(void)
{
    static char book[3001];
    int i;
    const char *line = "the quick brown fox jumps over the lazy dog\r";
    for (i = 0; i < 3000; i++)
        book[i] = line[i % 44];
    book[3000] = 0;
    g_book = book;
    for (i = 0; i < 5000; i++)
        g_big[i] = (char)('a' + i % 26);
    g_big[4999] = '\r';
    g_big[5000] = 0;
}

#define NEW_NOTES "notes rewritten\r"   /* the flash file, new bytes from the PC */
#define GROWN 4500UL                    /* "grow" (one page, before the open file) rewritten to two pages */

/* the unit's export as an image with the import test's changes: one file dropped, two rewritten (one in RAM
   growing a page, before the open file; the Braille Lite's flash file), new ones added */
static unsigned char *changed_image(const unit_t *x, const char *drop, const char *ram_folder,
                                    const char *flash_folder, int tns, unsigned long *size)
{
    char err[200], u8[200], f8[200];
    fat_builder *b = fat_new(tns ? "TYPENSPEAK" : "BRAILLELITE");
    int dir_of[BLF_FOLDERS] = {0}, f, i, ram_dir = 0, flash_dir = 0, work;
    unsigned char *img;
    for (f = 0; f < BLF_FOLDERS; f++) {
        const blf_folder *fo = blf_folder_get(x->fs, f);
        if (!fo->name[0]) continue;
        blx_unit_to_utf8_cp(g_cp, fo->name, u8, sizeof u8);
        dir_of[f] = fat_add_dir(b, 0, u8, 0, 0x0021);
        if (!strcmp(fo->name, ram_folder)) ram_dir = dir_of[f];
        if (!strcmp(fo->name, flash_folder)) flash_dir = dir_of[f];
    }
    for (i = 0; i < blf_count(x->fs); i++) {
        const blf_file *fl = blf_get(x->fs, i);
        unsigned long n;
        const unsigned char *d = blf_data(x->fs, i, &n);
        if (!strcmp(fl->name, drop)) continue;
        if (!strcmp(fl->name, "grow")) {
            d = (const unsigned char *)g_big;
            n = GROWN;
        }
        if (!strcmp(fl->name, "notes")) {
            d = (const unsigned char *)NEW_NOTES;
            n = strlen(NEW_NOTES);
        }
        blx_unit_to_utf8_cp(g_cp, fl->name, f8, sizeof f8);
        fat_add_file(b, dir_of[fl->folder], f8, d, n, fl->dos_time, fl->dos_date, (fl->prot & 2) != 0);
    }
    /* new: a RAM file with a PC's CR LF, a RAM file of two pages, a flash file of several blocks, a new folder */
    fat_add_file(b, ram_dir, g_imp_image, (const unsigned char *)"first line\r\nsecond line\r\n", 25, 0x6000,
                 0x5521, 0);
    fat_add_file(b, ram_dir, "big.brl", (const unsigned char *)g_big, 5000, 0x6001, 0x5521, 0);
    fat_add_file(b, ram_dir, "prog.bas", (const unsigned char *)PROG_BAS, sizeof PROG_BAS - 1, 0x6004, 0x5521, 0);
    fat_add_file(b, flash_dir, tns ? "fbook.brl" : "book", (const unsigned char *)g_book, 3000, 0x6002, 0x5521, 0);
    work = fat_add_dir(b, 0, "work", 0, 0x5521);
    fat_add_file(b, work, "inwork.txt", (const unsigned char *)"in a folder\n", 12, 0x6003, 0x5521, 0);
    img = fat_build(b, size, err, sizeof err);
    fat_free(b);
    return img;
}

static unsigned char *export_of(const unit_t *x, unsigned long *size)
{
    char err[200];
    blx_report r;
    unsigned char *img;
    blx_report_init(&r);
    img = blx_export_cp(x->fs, x->u.model, g_cp, size, &r, err, sizeof err);
    blx_report_free(&r);
    return img;
}

/* the lowest flash block any file of x uses */
static unsigned long lowest_block(const unit_t *x)
{
    unsigned long low = ~0UL, n;
    int i;
    for (i = 0; i < blf_count(x->fs); i++)
        if (blf_get(x->fs, i)->in_flash) {
            unsigned long b = (unsigned long)(blf_data(x->fs, i, &n) - x->u.flash) / 512;
            if (b < low) low = b;
        }
    return low;
}

static void all_checks(int tns, const char *fw, const char *state)
{
    script sc;
    unit_t a, b, c;
    char d[600], s1[200], s2[200], err[300];
    const char *made = tmp("made.state"), *imported = tmp("imported.state"), *after = tmp("after.state");
    const char *fresh_tns = tmp("fresh.state");   /* the Type 'n Speak just after its cold reset: no files yet */
    const char *ram_folder, *flash_folder, *dropped = "temp", *book = tns ? "fbook.brl" : "book";
    unsigned long n, n2, size, size2, size3;
    const unsigned char *x;
    unsigned char *img, *img2, *img3, *exp;
    int fl, ok, imported_ok;
    blx_report r;

    /* 1. the firmware's own files: the first one made moved to flash, one left open with text typed since */
    if (tns) {
        /* the unit as it left the factory: a new Type 'n Speak's first start is its cold reset, and its own
           questions answered yes (tns_setup.h) set up its file system, its flash and its folders.  (Before, the
           start missed the cold reset: no file system, no folders -- the first file lost its first character and
           a file moved to flash was lost: Timothy, Jayson.) */
        if (!tns_factory_setup(fw, fresh_tns, err, sizeof err)) {
            printf("FAIL the factory start: %s\n", err);
            exit(1);
        }
        if (!load(fresh_tns, &a)) exit(1);
        snprintf(d, sizeof d, "RAM file system %s, flash %s, folders %s (\"%s\" \"%s\")",
                 blf_ram_ok(a.fs) ? "set up" : "NOT set up", blf_flash_ok(a.fs) ? "set up" : "NOT set up",
                 blf_folders_ok(a.fs) ? "set up" : "NOT set up", blf_folder_get(a.fs, 0)->name,
                 blf_folder_get(a.fs, 1)->name);
        check("the factory start set the unit up", blf_ram_ok(a.fs) && blf_flash_ok(a.fs) && blf_folders_ok(a.fs)
              && blf_check(a.fs, err, sizeof err), d);
        unload(&a);
    }
    memset(&sc, 0, sizeof sc);
    sc.tns = tns;
    sc.t = SPOKEN_START;
    open_or_create(&sc, 1, "notes");   /* the unit's first file (on the Type 'n Speak right after the program
                                          before the fix: its first character was never stored) */
    type_text(&sc, "hello world");
    new_line(&sc);
    type_text(&sc, "line two");
    wait_s(&sc, 2.0);
    open_or_create(&sc, 1, "temp");    /* a file before the one left open, for the import to drop */
    type_text(&sc, "t");
    wait_s(&sc, 2.0);
    open_or_create(&sc, 1, "grow");    /* one page, before the open file: the import makes it two */
    type_text(&sc, "g");
    wait_s(&sc, 2.0);
    open_or_create(&sc, 1, g_doc);
    type_text(&sc, "abc");
    wait_s(&sc, 2.0);
    open_or_create(&sc, 0, "notes");
    move_open_file(&sc);               /* notes to flash, with the unit's own command */
    open_or_create(&sc, 0, g_doc);
    type_text(&sc, "xyz");             /* left open: its directory entry still says "abc" */
    wait_s(&sc, 3.0);
    if (!run(tns, fw, tns ? fresh_tns : state, made, &sc)) exit(1);
    if (!load(made, &a)) exit(1);
    ram_folder = blf_folder_get(a.fs, 0)->name;
    flash_folder = blf_folder_get(a.fs, 1)->name;
    snprintf(d, sizeof d, "%d files; RAM %s, flash %s, folders \"%s\" \"%s\"%s", blf_count(a.fs),
             blf_ram_ok(a.fs) ? "set up" : "NOT set up", blf_flash_ok(a.fs) ? "set up" : "NOT set up", ram_folder,
             flash_folder, blf_check(a.fs, err, sizeof err) ? "" : ", rules broken");
    check("the unit's file system read", blf_ram_ok(a.fs) && blf_flash_ok(a.fs) && blf_folders_ok(a.fs)
          && blf_check(a.fs, err, sizeof err), d);
    x = unit_file(&a, "notes", &n, &fl);
    show(s1, sizeof s1, x, n);
    snprintf(d, sizeof d, "notes \"%s\" in %s, folder %d", s1, x ? (fl ? "flash" : "RAM") : "-",
             x ? blf_get(a.fs, blf_find(a.fs, "notes"))->folder : -1);
    check("the unit's first file, moved to flash, whole", x && fl && blf_get(a.fs, blf_find(a.fs, "notes"))->folder
          && same(x, n, "hello world\rline two", 20), d);

    /* 2. the export: the firmware's files, exact */
    exp = export_of(&a, &size);
    {
        unsigned char *e1 = image_file(exp, size, flash_folder, "notes", &n);
        unsigned char *e2 = image_file(exp, size, ram_folder, g_doc, &n2);
        const char *want2 = "abcxyz";
        show(s1, sizeof s1, e1, n);
        show(s2, sizeof s2, e2, n2);
        snprintf(d, sizeof d, "notes \"%s\" (in flash)", s1);
        check("export: a file the unit made", same(e1, n, "hello world\rline two", 20), d);
        snprintf(d, sizeof d, "the open file \"%s\" (want \"%s\": the text typed since it was opened)", s2, want2);
        check("export: the open file, typed into since opened", same(e2, n2, want2, strlen(want2)), d);
        free(e1);
        free(e2);
    }
    if (g_slovak) {                 /* its folders reach the PC in its own letters: cp852, not cp850 */
        static const char want[] = "fle\xC5\xA1 s\xC3\xBA" "bory";   /* "fles subory", s-caron and u-acute */
        char e[200];
        fat_volume *v = fat_read(exp, size, e, sizeof e);
        int found = 0, i;
        for (i = 0; v && i < fat_count(v); i++)
            if (fat_get(v, i)->dir && !strcmp(fat_get(v, i)->name, want))
                found = 1;
        if (v)
            fat_close(v);
        snprintf(d, sizeof d, "the flash folder in the image as UTF-8 \"fle\\xC5\\xA1 s\\xC3\\xBAbory\": %s",
                 found ? "yes" : "no");
        check("export: the Slovak unit's folder names", found, d);
    }

    /* 3. the import: temp dropped (before the open file: the open file moves down a slot), new files in RAM, in
       flash, in a new folder */
    img = changed_image(&a, dropped, ram_folder, flash_folder, tns, &size2);
    blx_report_init(&r);
    ok = img && blx_import_cp(a.fs, g_cp, img, size2, &r, err, sizeof err);
    if (ok && !bls_save(imported, &a.u, err, sizeof err)) {   /* saved whatever the rules say: the unit runs on */
        printf("FAIL save %s: %s\n", imported, err);
        exit(1);
    }
    imported_ok = ok;
    ok = ok && blf_check(a.fs, err, sizeof err);
    snprintf(d, sizeof d, "%d added, %d rewritten, %d deleted, %d unchanged, %d kept, %d skipped, %d new folders%s%s",
             r.added, r.replaced, r.deleted, r.unchanged, r.kept, r.skipped, r.folders_added, ok ? "" : ": ",
             ok ? "" : err);
    check("import: done, the file system's rules hold", ok && r.added == 5 && r.replaced == 2
          && r.deleted == 1 && r.skipped == 0 && r.folders_added == 1, d);
    if ((r.skipped || r.kept || getenv("TEST_FILES_KEEP")) && r.log)
        printf("%s", r.log);
    blx_report_free(&r);
    {   /* a compiled BASIC program (issue #16): its bytes as they came, the 0Ah of its line numbers not made 0Dh */
        int k = imported_ok ? blf_find(a.fs, "prog.bas") : -1;
        const unsigned char *x = k >= 0 ? blf_data(a.fs, k, &n) : NULL;
        int i, diff = -1;
        for (i = 0; x && i < (int)sizeof PROG_BAS - 1 && i < (int)n; i++)
            if (x[i] != (unsigned char)PROG_BAS[i] && diff < 0)
                diff = i;
        snprintf(d, sizeof d, "%lu bytes (type %c), %s", x ? n : 0, k >= 0 ? blf_get(a.fs, k)->type : '-',
                 !x ? "not imported" : diff >= 0 ? "differs" : n != sizeof PROG_BAS - 1 ? "another length" : "exact");
        if (x && diff >= 0)
            snprintf(d + strlen(d), sizeof d - strlen(d), " at byte %d: %02X, not %02X", diff, x[diff],
                     (unsigned char)PROG_BAS[diff]);
        check("import: a binary file byte for byte (a compiled BASIC program)", x && n == sizeof PROG_BAS - 1
              && diff < 0, d);
    }

    /* 4. export, import, export: the same image (on the unit itself, and on a fresh unit) */
    {
        unit_t f;
        img2 = export_of(&a, &size);
        blx_report_init(&r);
        ok = blx_import_cp(a.fs, g_cp, img2, size, &r, err, sizeof err);
        img3 = export_of(&a, &size3);
        snprintf(d, sizeof d, "%lu bytes; again %lu bytes, %s; %d unchanged, %d changed", size, size3,
                 img3 && size3 == size && !memcmp(img2, img3, size) ? "the same" : "DIFFERENT", r.unchanged,
                 r.added + r.replaced + r.deleted + r.moved);
        check("export, import, export: the same image", ok && img3 && size3 == size && !memcmp(img2, img3, size)
              && r.added + r.replaced + r.deleted + r.moved == 0, d);
        blx_report_free(&r);
        free(img3);
        if (load(tns ? fresh_tns : state, &f)) {   /* a fresh unit: the shipped state, or the cold reset's */
            blx_report_init(&r);
            ok = blx_import_cp(f.fs, g_cp, img2, size, &r, err, sizeof err);
            img3 = export_of(&f, &size3);
            snprintf(d, sizeof d, "%d files into a fresh unit; its export %s", blf_count(f.fs),
                     img3 && size3 == size && !memcmp(img2, img3, size) ? "the same" : "DIFFERENT");
            check("export, import into a fresh unit, export: the same", ok && img3 && size3 == size
                  && !memcmp(img2, img3, size), d);
            blx_report_free(&r);
            free(img3);
            unload(&f);
        }
        free(img2);
    }

    /* 5. the unit started from the imported state: types into its open file, lists its files (into the clipboard),
       moves an imported flash file to RAM and an imported RAM file to flash */
    if (!imported_ok) {             /* nothing imported (a unit whose folders were never set up): nothing to run */
        check("the unit started from the imported files", 0, "nothing was imported");
        unload(&a);
        free(img);
        free(exp);
        remove(made);
        remove(fresh_tns);
        return;
    }
    memset(&sc, 0, sizeof sc);
    sc.tns = tns;
    sc.t = g_spoken_start;
    type_text(&sc, "q");
    wait_s(&sc, 2.0);
    list_to_clipboard(&sc);
    open_or_create(&sc, 0, book);
    move_open_file(&sc);
    open_or_create(&sc, 0, g_imp);
    move_open_file(&sc);
    if (!run(tns, fw, imported, after, &sc)) exit(1);
    if (!load(after, &b)) exit(1);
    {
        unsigned long cn;
        const unsigned char *clip = unit_file(&b, "clipboard", &cn, NULL);
        static const struct { const char *name; long bytes; } want_bl[] = {
            {"doc", 7}, {"imported", 23}, {"big.brl", 5000}, {"book", 3000}, {"inwork.txt", 12},
            {"notes", 16}, {"grow", 4500}, {NULL, 0}}, want_tns[] = {
            {"doc.brl", 7}, {"imported.txt", 23}, {"big.brl", 5000}, {"fbook.brl", 3000}, {"inwork.txt", 12},
            {"notes", 16}, {"grow", 4500}, {NULL, 0}};
        char text[8000], missing[600] = "";
        int i;
        size_t m = cn < sizeof text - 1 ? cn : sizeof text - 1;
        memcpy(text, clip ? (const char *)clip : "", clip ? m : 0);
        text[clip ? m : 0] = 0;
        for (i = 0; (tns ? want_tns : want_bl)[i].name; i++) {
            const char *nm = (tns ? want_tns : want_bl)[i].name;
            long by = (tns ? want_tns : want_bl)[i].bytes;
            if (!listed(text, nm, by))
                snprintf(missing + strlen(missing), sizeof missing - strlen(missing), "%s%s %ld not listed",
                         missing[0] ? ", " : "", nm, by);
        }
        if (listed(text, dropped, -1))
            snprintf(missing + strlen(missing), sizeof missing - strlen(missing), "%s%s still listed",
                     missing[0] ? ", " : "", dropped);
        snprintf(d, sizeof d, "%lu bytes of list; %s", cn, missing[0] ? missing : "every file with its size");
        check("the unit lists the imported files, with sizes", clip && cn > 20 && !missing[0], d);
    }
    x = unit_file(&b, g_doc, &n, NULL);
    show(s1, sizeof s1, x, n);
    snprintf(d, sizeof d, "\"%s\" (its pointers and number followed the files that moved)", s1);
    check("the open file goes on after the import", same(x, n, "abcxyzq", 7), d);
    {
        unsigned long low = 0, start;
        x = unit_file(&b, book, &n, &fl);
        snprintf(d, sizeof d, "%lu bytes, in %s", n, x ? (fl ? "flash" : "RAM") : "-");
        check("the unit reads an imported flash file (moved to RAM)", x && !fl && same(x, n, g_book, 3000), d);
        x = unit_file(&b, g_imp, &n, &fl);
        show(s1, sizeof s1, x, n);
        snprintf(d, sizeof d, "\"%s\" in %s", s1, x ? (fl ? "flash" : "RAM") : "-");
        check("the unit reads an imported RAM file (moved to flash)", x && fl
              && same(x, n, "first line\rsecond line\r", 23), d);
        if (load(imported, &c)) {
            low = lowest_block(&c);
            unload(&c);
        }
        start = x ? (unsigned long)(x - b.u.flash) / 512 : 0;
        ok = blf_check(b.fs, err, sizeof err);
        snprintf(d, sizeof d, "the unit put it at block %lu (the imported files' lowest: %lu); rules %s", start, low,
                 ok ? "hold" : err);
        check("free space: the unit's allocator agrees", x && fl && start + 1 == low && ok, d);
    }
    unload(&b);
    unload(&a);
    free(img);
    free(exp);
    if (getenv("TEST_FILES_KEEP"))          /* debugging: the states stay, named in the output */
        return (void)printf("kept %s %s %s %s\n", made, imported, after, fresh_tns);
    remove(made);
    remove(imported);
    remove(after);
    remove(tmp("fresh.state"));
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3 || (strcmp(argv[1], "tns") && argc < 4)) {
        printf("usage: test_files bl FIRMWARE STATE | tns FIRMWARE  [--break=N]\n");
        return 2;
    }
    for (i = 3; i < argc; i++)
        if (!strcmp(argv[i], "--break=cp"))
            g_cp_break = 1;
        else if (!strcmp(argv[i], "--break=cold"))
            tns_cold_break = 1;
        else if (!strncmp(argv[i], "--break=", 8))
            blf_break = atoi(argv[i] + 8);
    snprintf(g_tmp, sizeof g_tmp, "test_files.%d", (int)_getpid());
    if (strstr(argv[2], "SPA") || strstr(argv[2], "spa"))
        g_yes = 's';
    if (strstr(argv[2], "SLL") || strstr(argv[2], "sll"))   /* the Slovak Braille 'n Speak 2000 (BS2SLL.BNS) */
        g_spoken_start = 12.0, g_cp = BLX_CP852, g_slovak = 1;
    if (g_cp_break)
        g_cp = BLX_CP850;
    g_doc = strcmp(argv[1], "tns") ? "doc" : "doc.brl";
    g_imp = strcmp(argv[1], "tns") ? "imported" : "imported.txt";
    g_imp_image = strcmp(argv[1], "tns") ? "Imported" : "Imported.txt";
    make_texts();
    all_checks(!strcmp(argv[1], "tns"), argv[2], strcmp(argv[1], "tns") ? argv[3] : NULL);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
