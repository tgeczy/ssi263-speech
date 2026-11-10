/* test_brand.c -- Blazie's name brander (BSNAME.BNS) on the emulated Braille Lite 2000, headless, and the emulator
 * keeping the name it brands.
 *
 *   test_brand FIRMWARE STATE BRANDER
 *
 * FIRMWARE + STATE: the English unit (BL2ENG.BNS, bl2_2003_warm.state); BRANDER: BSNAME.BNS (the June 2003 disk's,
 * with its BSNAME.TXT).  Never in the repo: run_tests passes them, or the test is skipped.
 *
 * What the brander does (its code, disassembled): it reads physical 00090h, the program flash, and when that is FFh
 * asks for a name and address (39 characters), then programs the 40 bytes at 00090h-000B7h one by one with the
 * Am29F040's byte program (AAh/55h/A0h, no erase: once only, as BSNAME.TXT warns).  The firmware's p-chord th (dots
 * 1-4-5-6, command 39h) says "serial number" and the 16 bytes at 00080h (not when FFh or 00h), then "owner is" and the
 * 40 at 00090h (not when FFh).  Both sit in the image's blank gap before its code at 000B8h, and Blazie's updater
 * saves 00080h-000B7h before it erases a bank and puts them back: a real unit keeps its name through cold resets and
 * updates.  The emulator loads the program flash from the firmware on every start, so it keeps the name beside the
 * state (emu_unit.h emu_save: STATE.program) and puts it back.
 *
 * Checks: the brander, run as Blazie's guide says (o-chord x, the name, e-chord), programs the name at 00090h and
 * nothing else; p-chord th then says "owner is" and the name, where an unbranded unit says only "ok"; and the emulator
 * keeping it: a branded unit's save writes STATE.program, a unit started from that state has the name back and the
 * rest of bank a the firmware's; the name blanked again (as the updater's own un-branding leaves it), the save removes
 * the file; and a .program of another firmware does not give its name.
 *
 * Must fail: test_brand_nokeep.exe, built with EMU_NO_OWNER_KEEP (bank a's name not kept, as before: the brander's
 * checks pass, the keeping checks fail); test_brand_break.exe, built with BLAZIE_PROGRAM_FLASH_BREAK (the program
 * flash's writes dropped: the brander programs nothing).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#endif
#include "../../csrc/blazie/bl_board.h"
#include "../../csrc/blazie/bl_files.h"
#include "../../csrc/blazie/bl_files_state.h"
#include "emu_unit.h"

#define STEP_CYCLES 122880ULL                /* 20 ms of the Braille Lite's 6.144 MHz */
#define ROM_OFFSET 0x3000L                   /* the .BNS's program image starts here (bl_board.c FILE_ROM_OFFSET) */
#define BANK 0x40000L
#define NAME_AT 0x90L                        /* the owner's name, 40 bytes (the brander's 0028h) */
#define NAME_LEN 0x28L
#define GAP 1.6                              /* between keys, in the unit's seconds */
#define LISTEN 12.0                          /* p-chord th's answer, in the unit's seconds */
#define BATTERY_LEVEL 128                    /* emu_unit.c's: the status menu's % reads the gauge (without it: frozen) */
#define HOST_TIME 1790000000LL               /* the clock controller's time, fixed: every run the same */

#define PHONES 32

static const char OWNER[] = "ann lee";
/* p-chord th's answers as the firmware says them (measured here; the serial number is blank, so not said):
   "owner is" O OU N ER I Z, then the name (ann lee: AE N L E); unbranded only "ok", O OU K A E1 */
static const int OWNER_IS[] = {0x11, 0x12, 0x38, 0x1C, 0x07, 0x2F};
static const int OK[] = {0x11, 0x12, 0x29, 0x08, 0x02};
#define OWNER_IS_N ((int)(sizeof OWNER_IS / sizeof OWNER_IS[0]))
#define OK_N ((int)(sizeof OK / sizeof OK[0]))
static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-44s %s\n", ok ? "ok" : "FAIL", name, detail);
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

static long file_size(const char *path)
{
    long n = -1;
    FILE *f = fopen(path, "rb");
    if (f && fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    if (f)
        fclose(f);
    return n;
}

/* a bank as the board loads a .BNS: the image from ROM_OFFSET, FFh-padded to 256 KB */
static void bank_of(const unsigned char *bns, long n, unsigned char *out)
{
    long m = n - ROM_OFFSET;
    memset(out, 0xFF, BANK);
    if (m > 0)
        memcpy(out, bns + ROM_OFFSET, (size_t)(m < BANK ? m : BANK));
}

/* the bytes of bank a that differ from want outside the name; the first at *first (-1 none) */
static int diffs_outside(const unsigned char *bank, const unsigned char *want, long *first)
{
    long i;
    int n = 0;
    *first = -1;
    for (i = 0; i < BANK; i++)
        if ((i < NAME_AT || i >= NAME_AT + NAME_LEN) && bank[i] != want[i]) {
            if (*first < 0)
                *first = i;
            n++;
        }
    return n;
}

/* the state with the brander added to the flash startup folder as nam.bns (test_update.c's way), saved to out */
static int add_brander(const char *state, const unsigned char *data, long n, const char *out)
{
    char err[256];
    bls_unit b;
    blf_fs *fs;
    int ok;
    if (!bls_load(state, &b, err, sizeof err)) { printf("FAIL load %s: %s\n", state, err); return 0; }
    fs = blf_open(b.model, b.ram, b.flash, b.flash_size, err, sizeof err);
    ok = fs && blf_add(fs, 1, "nam.bns", data, (unsigned long)n, 0, (46 << 9) | (6 << 5) | 1, 0, err, sizeof err);
    if (ok && blf_get(fs, blf_find(fs, "nam.bns"))->type != 'X') {
        snprintf(err, sizeof err, "not a program (type %c)", blf_get(fs, blf_find(fs, "nam.bns"))->type);
        ok = 0;
    }
    if (fs)
        blf_close(fs);
    ok = ok && bls_save(out, &b, err, sizeof err);
    if (!ok)
        printf("FAIL the brander into the unit: %s\n", err);
    bls_free(&b);
    return ok;
}

/* ---- running the unit ------------------------------------------------------------------------------------------- */
/* a unit as the emulator makes it (emu_unit.c): the battery gauge and the clock controller on */
static bl_unit *unit_new(const char *fw, const char *st)
{
    char err[256];
    blc_time now;
    time_t secs = (time_t)HOST_TIME;
    struct tm *tm = gmtime(&secs);
    bl_unit *u = bl_create(fw, st, 20.0, NULL, NULL, 0, err, sizeof err);
    if (!u) {
        printf("FAIL create: %s\n", err);
        exit(1);
    }
    bl_battery(u, BATTERY_LEVEL);
    memset(&now, 0, sizeof now);
    now.year = tm->tm_year + 1900;
    now.month = tm->tm_mon + 1;
    now.day = tm->tm_mday;
    now.hour = tm->tm_hour;
    now.minute = tm->tm_min;
    now.second = tm->tm_sec;
    if (!bl_clock_on(u, &now, HOST_TIME)) {
        printf("FAIL the clock: out of memory\n");
        exit(1);
    }
    return u;
}

/* run to `secs`, pressing keys[i] at at[i]; the phonemes loaded from `from` on (R0, control bit clear, not the pause)
   into out (at most PHONES), their number returned */
static int run(bl_unit *u, const int *keys, const double *at, int n_keys, double secs, double from, int *out)
{
    int i, k, ctl = 0, phones = 0, steps = (int)(secs * 50);
    for (i = 0; i < steps; i++) {
        const bl_event *ev;
        int m;
        for (k = 0; k < n_keys; k++)
            if ((int)(at[k] * 50 + 0.5) == i)
                bl_key(u, keys[k]);
        bl_run(u, STEP_CYCLES);
        m = bl_events(u, &ev);
        for (k = 0; k < m; k++) {
            if (ev[k].type != 'W')
                continue;
            if (ev[k].a == 3)
                ctl = ev[k].b >> 7;
            else if (ev[k].a == 0 && !ctl && (ev[k].b & 0x3F) && i * 0.02 >= from) {
                if (out && phones < PHONES)
                    out[phones] = ev[k].b & 0x3F;
                phones++;
            }
        }
        bl_clear_events(u);
    }
    return phones;
}

/* computer braille for the letters used here (as test_games.c types them) */
static int cell(char c)
{
    switch (c) {
    case 'a': return 0x01;
    case 'b': return 0x03;
    case 'e': return 0x11;
    case 'l': return 0x07;
    case 'm': return 0x0D;
    case 'n': return 0x1D;
    case 's': return 0x0E;
    case '.': return 0x28;                   /* dots 4-6 */
    case ' ': return 0x40;
    }
    return 0;
}

/* the brander, from the unit's main menu: into the flash folder (test_update.c's keys), o-chord x nam.bns e-chord,
   the name, e-chord; the time after its last key */
static double script(int *keys, double *at, int *n_keys)
{
    static const char *prog = "nam.bns";
    int n = 0, i;
    double t = 4.0;
#define KEY(k) do { keys[n] = (k); at[n++] = t; t += GAP; } while (0)
    KEY(0x4C);                               /* the status menu (dots 3-4 chord) */
    for (i = 0; i < 42; i++)
        KEY(0x48);                           /* on to item 43, "allow folder mode" */
    KEY(0x3D);                               /* y */
    KEY(0x51);                               /* e-chord: out */
    KEY(0x55); KEY(0x0B); KEY(0x40); KEY(0x70); KEY(0x51);   /* o-chord f, space (folder mode), next folder, out */
    KEY(0x55); KEY(0x2D);                    /* o-chord x: "enter program to execute" */
    for (i = 0; i < (int)strlen(prog); i++)
        KEY(cell(prog[i]));
    KEY(0x51);                               /* e-chord: run it */
    t += 14.0;                               /* its prompt: name and address, no grade 2, e-chord when done */
    for (i = 0; i < (int)strlen(OWNER); i++)
        KEY(cell(OWNER[i]));
    KEY(0x51);                               /* e-chord: brand it */
#undef KEY
    *n_keys = n;
    return t;
}

/* p-chord th from the main menu, on a unit with this program flash: the phonemes of its answer (in out), their
   number */
static int owner_phones(const char *fw, const char *st, const unsigned char *pflash, int *out)
{
    static const int keys[] = {0x4F, 0x39};  /* p-chord, th (dots 1-4-5-6) */
    static const double at[] = {6.0, 7.6};
    unsigned char *pf;
    int n;
    bl_unit *u = unit_new(fw, st);
    bl_program_flash(u, &pf, NULL);
    memcpy(pf, pflash, 2 * BANK);
    n = run(u, keys, at, 2, 7.6 + LISTEN, 7.6, out);
    bl_destroy(u);
    return n;
}

static void print_phones(const char *label, const int *p, int n)
{
    int i;
    printf("     %s:", label);
    for (i = 0; i < n && i < PHONES; i++)
        printf(" %02X", p[i]);
    printf("\n");
}

/* ---- the emulator keeping the name (emu_save, emu_create) ------------------------------------------------------- */
static void keep_checks(const char *fw, const char *state, const unsigned char *branded, const unsigned char *image)
{
    char err[256], d[300], st[64], prog[80];
    unsigned char *pf, *file;
    long n, first;
    int outside, named;
    emu_unit *u;
    snprintf(st, sizeof st, "test_brand_keep.%d.state", (int)_getpid());
    snprintf(prog, sizeof prog, "%s.program", st);
    remove(prog);
    /* a branded unit, saved */
    u = emu_create(EMU_BRAILLE_LITE, fw, state, 22050.0, 0, err, sizeof err);
    if (!u) { printf("FAIL emu_create: %s\n", err); failures++; return; }
    n = emu_program_flash(u, &pf);
    memcpy(pf, branded, (size_t)n);
    emu_save(u, st);
    emu_destroy(u);
    snprintf(d, sizeof d, "%s: %ld bytes", prog, file_size(prog));
    check("the name kept beside the state", file_size(prog) == n, d);
    /* started again from that state: the name back, the rest of bank a the firmware's, bank b untouched */
    u = emu_create(EMU_BRAILLE_LITE, fw, st, 22050.0, 0, err, sizeof err);
    if (!u) { printf("FAIL emu_create: %s\n", err); failures++; return; }
    emu_program_flash(u, &pf);
    named = memcmp(pf + NAME_AT, branded + NAME_AT, NAME_LEN) == 0;
    outside = diffs_outside(pf, image, &first);
    snprintf(d, sizeof d, "00090h %02X %02X ...%s; %d other bytes of bank a not the firmware's; bank b %s",
             pf[NAME_AT], pf[NAME_AT + 1], named ? " (the name)" : " (NOT the name)", outside,
             memcmp(pf + n / 2, image, (size_t)n / 2) == 0 ? "the firmware's" : "CHANGED");
    check("the name put back on the next start", named && outside == 0 && memcmp(pf + n / 2, image, (size_t)n / 2) == 0,
          d);
    /* the name blanked (what the updater's un-branding leaves), saved: the file goes */
    memset(pf + NAME_AT, 0xFF, NAME_LEN);
    emu_save(u, st);
    emu_destroy(u);
    snprintf(d, sizeof d, "%s: %s", prog, file_size(prog) < 0 ? "removed" : "STILL THERE");
    check("removed when the name is blanked", file_size(prog) < 0, d);
    /* a .program of another firmware (one code byte not this one's) with a name: not applied */
    file = (unsigned char *)malloc((size_t)n);
    memcpy(file, branded, (size_t)n);
    file[0x100] ^= 0xFF;
    {
        FILE *f = fopen(prog, "wb");
        if (f) { fwrite(file, 1, (size_t)n, f); fclose(f); }
    }
    u = emu_create(EMU_BRAILLE_LITE, fw, st, 22050.0, 0, err, sizeof err);
    if (!u) { printf("FAIL emu_create: %s\n", err); failures++; free(file); return; }
    emu_program_flash(u, &pf);
    named = pf[NAME_AT] != 0xFF;
    emu_destroy(u);
    snprintf(d, sizeof d, "00090h %s", named ? "NAMED from it" : "blank");
    check("another firmware's name not given", !named, d);
    free(file);
    remove(prog);
    remove(st);
}

int main(int argc, char **argv)
{
    char d[400], path[64];
    long n_fw, n_br, first;
    unsigned char *fw, *br, *image, *pf, *branded, *plain;
    int keys[96], n_keys, outside, named, said, said_plain, got[PHONES], got_plain[PHONES];
    double at[96], end;
    unsigned long writes = 0;
    bl_unit *u;
    if (argc < 4) {
        printf("usage: test_brand FIRMWARE STATE BRANDER\n");
        return 2;
    }
    fw = read_all(argv[1], &n_fw);
    br = read_all(argv[3], &n_br);
    if (!fw || !br) {
        printf("FAIL read the firmware\n");
        return 1;
    }
    image = (unsigned char *)malloc(BANK);
    branded = (unsigned char *)malloc(2 * BANK);
    plain = (unsigned char *)malloc(2 * BANK);
    bank_of(fw, n_fw, image);
    memcpy(plain, image, BANK);
    memcpy(plain + BANK, image, BANK);
    snprintf(path, sizeof path, "test_brand.%d.state", (int)_getpid());
    if (!add_brander(argv[2], br, n_br, path))
        return 1;

    /* the brander, with the flash's own timing (the emulator's) */
    u = unit_new(argv[1], path);
    bl_flash_timed(u, 1);
    end = script(keys, at, &n_keys);
    run(u, keys, at, n_keys, end + 20.0, 1e9, NULL);
    bl_program_flash(u, &pf, &writes);
    memcpy(branded, pf, 2 * BANK);
    bl_destroy(u);
    remove(path);
    named = memcmp(branded + NAME_AT, OWNER, strlen(OWNER)) == 0;
    outside = diffs_outside(branded, image, &first);
    snprintf(d, sizeof d, "00090h %02X %02X ...%s, %lu writes; %d other bytes of bank a changed%s",
             branded[NAME_AT], branded[NAME_AT + 1], named ? " (the name)" : " (NOT the name)", writes, outside, memcmp(branded + BANK, image, BANK) == 0 ? "; bank b untouched" : "; bank b CHANGED");
    if (first >= 0)
        printf("     first other change at %05lXh: %02Xh, want %02Xh\n", first, branded[first], image[first]);
    check("the brander writes the name", named && outside == 0 && memcmp(branded + BANK, image, BANK) == 0, d);

    /* the firmware reads it back: p-chord th says "owner is" and the name, where an unbranded unit says only "ok" */
    said = owner_phones(argv[1], argv[2], branded, got);
    said_plain = owner_phones(argv[1], argv[2], plain, got_plain);
    print_phones("branded", got, said);
    print_phones("unbranded", got_plain, said_plain);
    snprintf(d, sizeof d, "%d phonemes%s; %d unbranded%s", said,
             said >= OWNER_IS_N + 4 && memcmp(got, OWNER_IS, sizeof OWNER_IS) == 0 ? ", \"owner is\" and more" : "",
             said_plain, said_plain == OK_N && memcmp(got_plain, OK, sizeof OK) == 0 ? ", \"ok\"" : "");
    check("p-chord th says the owner", said >= OWNER_IS_N + 4 && memcmp(got, OWNER_IS, sizeof OWNER_IS) == 0
          && said_plain == OK_N && memcmp(got_plain, OK, sizeof OK) == 0, d);

    keep_checks(argv[1], argv[2], branded, image);
    printf("brand: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
