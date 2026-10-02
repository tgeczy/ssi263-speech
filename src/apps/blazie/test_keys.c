/* test_keys.c -- the Linux shell's keyboard, without a unit: a terminal's bytes as keys (term_keys.c), the Braille
 * Lite's chords from them (bl_keys.c: keys mode by time, letters mode by computer braille, the hold key, an input
 * device's keys going down and up), the Type 'n Speak's key events (tns_term.c with tns_keys.h's codes), key names
 * and the settings file (ini.c).
 *
 *   test_keys [scratch folder]
 *
 * The control: BLAZIE_KEYS_BREAK=1 swaps dots 1 and 4 in every chord bl_keys.c makes; then the Braille Lite's
 * checks must FAIL while the Type 'n Speak's and the decoder's still pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_keys.h"
#include "ini.h"
#include "keys.h"
#include "term_keys.h"
#include "tns_term.h"

static int failures, checks;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-40s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
    checks++;
}

/* ---- the terminal ---------------------------------------------------------------------------------------------- */
static void decode(const char *name, const char *bytes, int key, int mods)
{
    term_dec d;
    key_event e[8];
    char got[64], tmp[16];
    int n;
    term_dec_init(&d);
    n = term_dec_feed(&d, (const unsigned char *)bytes, (int)strlen(bytes), 0.0, e, 8);
    n += term_dec_flush(&d, 1.0, e + n, 8 - n);
    if (n)
        snprintf(got, sizeof got, "%d key(s): %s mods %d", n, key_name(e[0].key, tmp), e[0].mods);
    else
        snprintf(got, sizeof got, "no key");
    check(name, n == 1 && e[0].key == key && e[0].mods == mods && e[0].type == KE_PRESS, got);
}

static void terminal_checks(void)
{
    term_dec d;
    key_event e[8];
    int n;
    decode("terminal: a letter", "f", 'f', 0);
    decode("terminal: a capital", "F", 'F', 0);
    decode("terminal: F12 (xterm, VTE, console)", "\x1b[24~", K_F12, 0);
    decode("terminal: F11", "\x1b[23~", K_F11, 0);
    decode("terminal: F1 (xterm)", "\x1bOP", K_F1, 0);
    decode("terminal: F1 (the Linux console)", "\x1b[[A", K_F1, 0);
    decode("terminal: F4 (xterm)", "\x1bOS", K_F4, 0);
    decode("terminal: Ctrl+Left", "\x1b[1;5D", K_LEFT, KM_CTRL);
    decode("terminal: Up (application mode)", "\x1bOA", K_UP, 0);
    decode("terminal: Shift+F5", "\x1b[15;2~", K_F5, KM_SHIFT);
    decode("terminal: Alt+Shift+F", "\x1b" "F", 'F', KM_ALT);
    decode("terminal: Ctrl+O", "\x0f", 'o', KM_CTRL);
    decode("terminal: Ctrl+K", "\x0b", 'k', KM_CTRL);
    decode("terminal: Enter", "\r", K_ENTER, 0);
    decode("terminal: Backspace", "\x7f", K_BACKSPACE, 0);
    decode("terminal: Shift+Tab", "\x1b[Z", K_TAB, KM_SHIFT);
    decode("terminal: Esc alone, after its wait", "\x1b", K_ESC, 0);
    decode("terminal: Delete", "\x1b[3~", K_DELETE, 0);
    term_dec_init(&d);                          /* an ESC with no more yet waits; then its sequence completes it */
    n = term_dec_feed(&d, (const unsigned char *)"\x1b", 1, 0.0, e, 8);
    n += term_dec_flush(&d, 0.01, e + n, 8 - n);
    n += term_dec_feed(&d, (const unsigned char *)"[A", 2, 0.012, e + n, 8 - n);
    check("terminal: a sequence in two reads", n == 1 && e[0].key == K_UP, n == 1 ? "Up" : "split wrongly");
    term_dec_init(&d);                          /* UTF-8 beyond ASCII is skipped, the next key is not lost */
    n = term_dec_feed(&d, (const unsigned char *)"\xc3\xa9" "a", 3, 0.0, e, 8);
    check("terminal: UTF-8 skipped", n == 1 && e[0].key == 'a', n == 1 ? "a" : "wrong");
}

/* ---- the Braille Lite -------------------------------------------------------------------------------------------- */
typedef struct { double t; const char *bytes; } typed;

/* the bytes typed at their times, then time passing to `until`; the actions, as text: "C55 H4A ..." */
static void run_bl(bl_keys *k, const typed *in, int n, double until, char *out, int cap)
{
    term_dec d;
    bl_action a[BLK_MAX_ACTIONS];
    double t;
    int i = 0, j, m, len = 0;
    term_dec_init(&d);
    out[0] = 0;
    for (t = 0.0; t <= until + 1e-9; t += 0.001) {
        key_event e[16];
        int ne = 0, x;
        while (i < n && in[i].t <= t + 1e-9) {
            ne += term_dec_feed(&d, (const unsigned char *)in[i].bytes, (int)strlen(in[i].bytes), t, e + ne,
                                16 - ne);
            i++;
        }
        ne += term_dec_flush(&d, t, e + ne, 16 - ne);
        for (x = 0; x < ne; x++)
            blk_event(k, &e[x], t);
        blk_tick(k, t);
        m = blk_take(k, a, BLK_MAX_ACTIONS);
        for (j = 0; j < m && len < cap - 8; j++)
            len += snprintf(out + len, (size_t)(cap - len), "%s%c%02X", len ? " " : "",
                            a[j].type == BLA_CHORD ? 'C' : 'H', a[j].bits);
    }
}

static void bl_case(const char *name, int mode, const typed *in, int n, double until, const char *want)
{
    bl_keys k;
    char got[256], d[320];
    blk_defaults(&k);
    k.mode = mode;
    run_bl(&k, in, n, until, got, sizeof got);
    snprintf(d, sizeof d, "got [%s], want [%s]", got, want);
    check(name, !strcmp(got, want), d);
}

static void bl_checks(void)
{
    /* keys mode: o-chord (dots 1 3 5 and space) typed as f s k space, close together; then t (dots 2 3 4 5) */
    static const typed o_t[] = {{0.0, "f"}, {0.012, "s"}, {0.03, "k"}, {0.045, " "}, {1.0, "d"}, {1.01, "s"},
                                {1.02, "j"}, {1.03, "k"}};
    /* the same key held: the terminal repeats it after a delay -- one chord, not one per repeat (the console's
       10.9 a second and xterm's 25) */
    static const typed held[] = {{0.0, "f"}, {0.5, "f"}, {0.592, "f"}, {0.684, "f"}, {0.776, "f"},
                                 {2.0, "j"}, {2.66, "j"}, {2.7, "j"}, {2.74, "j"}};
    /* the same chord twice, deliberately (dot 1, a pause, dot 1) */
    static const typed twice[] = {{0.0, "f"}, {0.4, "f"}};
    /* the hold key: F12, i-chord (dots 2 4, space), then later F12 again: held, then let go and sent */
    static const typed hold[] = {{0.0, "\x1b[24~"}, {0.1, "d"}, {0.11, "j"}, {0.12, " "}, {2.0, "\x1b[24~"}};
    /* ... and with Ctrl+K, a chord typed while it is held is sent as usual (p-chord, hold, i-chord, l) */
    static const typed hold2[] = {{0.0, "fdsj "}, {0.5, "\x0b"}, {0.6, "dj "}, {1.0, "fds"}, {3.0, "\x0b"}};
    /* letters mode: p, P (dot 7: p-chord), Ctrl+C i (i-chord), 4 (dots 2 5 6), space, Up (dot-1 chord), Enter
       (e-chord), Ctrl+A (the advance bar), Backspace (b-chord), an unknown key (F5: nothing) */
    static const typed letters[] = {{0.0, "p"}, {0.1, "P"}, {0.2, "\x03"}, {0.25, "i"}, {0.3, "4"}, {0.4, " "},
                                    {0.5, "\x1b[A"}, {0.6, "\r"}, {0.7, "\x01"}, {0.8, "\x7f"}, {0.9, "\x1b[15~"}};
    bl_case("keys mode: o-chord, then t", BLK_KEYS, o_t, 8, 1.5, "C55 C1E");
    bl_case("keys mode: a key held repeats once only", BLK_KEYS, held, 9, 3.5, "C01 C08");
    bl_case("keys mode: the same chord twice", BLK_KEYS, twice, 2, 1.0, "C01 C01");
    bl_case("hold key: i-chord held, then let go", BLK_KEYS, hold, 5, 2.5, "H4A H00 C4A");
    bl_case("hold key: a chord sent while held", BLK_KEYS, hold2, 5, 3.5, "C4F H4A C07 H00 C4A");
    bl_case("letters mode: computer braille", BLK_LETTERS, letters, 11, 1.5,
            "C0F C4F C4A C32 C40 C41 C51 C80 C43");
    {   /* an input device: keys down and up; the chord when the last comes up, the keys held as they move */
        bl_keys k;
        key_event e;
        bl_action a[BLK_MAX_ACTIONS];
        static const struct { int key, type; } moves[] = {{'f', KE_DOWN}, {'s', KE_DOWN}, {'k', KE_DOWN},
            {' ', KE_DOWN}, {'f', KE_UP}, {'s', KE_UP}, {'k', KE_UP}, {' ', KE_UP}, {K_BRL4, KE_DOWN},
            {K_BRL4, KE_UP}};
        char got[256] = "", d[320];
        int i, j, n, len = 0;
        blk_defaults(&k);
        for (i = 0; i < 10; i++) {
            e.key = moves[i].key;
            e.type = moves[i].type;
            e.mods = 0;
            blk_event(&k, &e, i * 0.05);
            n = blk_take(&k, a, BLK_MAX_ACTIONS);
            for (j = 0; j < n; j++)
                len += snprintf(got + len, sizeof got - (size_t)len, "%s%c%02X", len ? " " : "",
                                a[j].type == BLA_CHORD ? 'C' : 'H', a[j].bits);
        }
        snprintf(d, sizeof d, "got [%s]", got);
        check("input device: o-chord down and up, dot 4", !strcmp(got,
              "H01 H05 H15 H55 H54 H50 H40 H00 C55 H08 H00 C08"), d);
    }
    {   /* the Braille Lite 2000's two bars: ; advance, a back, alone and with a chord's dots (keys mode) */
        static const typed bars[] = {{0.0, ";"}, {0.5, "a"}, {1.0, "a"}, {1.01, "f"}};
        bl_case("keys mode: the advance bar, the back bar, back with dot 1", BLK_KEYS, bars, 4, 1.5,
                "C80 C100 C101");
    }
    {   /* an input device: a bar is down while held and no part of the chord typed under it (dot 8 the advance
           bar, dot 7 back) */
        bl_keys k;
        key_event e;
        bl_action a[BLK_MAX_ACTIONS];
        static const struct { int key, type; } moves[] = {{K_BRL8, KE_DOWN}, {'f', KE_DOWN}, {'f', KE_UP},
            {K_BRL8, KE_UP}, {K_BRL7, KE_DOWN}, {K_BRL7, KE_UP}};
        char got[256] = "", d[320];
        int i, j, n, len = 0;
        blk_defaults(&k);
        for (i = 0; i < 6; i++) {
            e.key = moves[i].key;
            e.type = moves[i].type;
            e.mods = 0;
            blk_event(&k, &e, i * 0.05);
            n = blk_take(&k, a, BLK_MAX_ACTIONS);
            for (j = 0; j < n; j++)
                len += snprintf(got + len, sizeof got - (size_t)len, "%s%c%02X", len ? " " : "",
                                a[j].type == BLA_CHORD ? 'C' : 'H', a[j].bits);
        }
        snprintf(d, sizeof d, "got [%s]", got);
        check("input device: a bar held under a chord", !strcmp(got, "H80 H81 H80 C01 H00 H100 H00"), d);
    }
    {   /* a settings file from before the back bar ("advance = a ;"): a is the advance bar alone, not both */
        bl_keys k;
        char got[64], d[160];
        static const typed a_only[] = {{0.0, "a"}};
        blk_defaults(&k);
        blk_set(&k, "advance", "a ;");
        run_bl(&k, a_only, 1, 0.5, got, sizeof got);
        snprintf(d, sizeof d, "got [%s]", got);
        check("settings: an old advance = a ; keeps a on one bar", !strcmp(got, "C80"), d);
    }
    {
        char d[200], n1[48], n2[48];
        int e = blk_chord_of("e-chord"), low_d = blk_chord_of("2-5-6-chord"), dots = blk_chord_of("dots 1 3"),
            adv = blk_chord_of("advance"), one = blk_chord_of("1-chord");
        snprintf(d, sizeof d, "e-chord %02X, 2-5-6-chord %02X, dots 1 3 %02X, advance %02X, 1-chord %02X; %s, %s", e,
                 low_d, dots, adv, one, blk_chord_name(0x55, n1), blk_chord_name(0x72, n2));
        check("chord names", e == 0x51 && low_d == 0x72 && dots == 0x05 && adv == 0x80 && one == 0x41
              && !strcmp(n1, "o-chord") && !strcmp(n2, "dots 2 5 6 space"), d);
    }
    {   /* computer braille's digits are the dropped letters a-j (test_clock.c's 0 9 3 0 1 5) */
        static const char digits[] = "093015";
        static const int want[] = {0x34, 0x14, 0x12, 0x34, 0x02, 0x22};
        int i, ok = 1;
        for (i = 0; i < 6; i++)
            ok &= blk_cell_of(digits[i]) == want[i];
        check("computer braille digits", ok, "0 9 3 0 1 5 = 34 14 12 34 02 22");
    }
}

/* ---- the Type 'n Speak ------------------------------------------------------------------------------------------ */
static void tns_case(const char *name, const char *bytes, const char *want)
{
    term_dec d;
    key_event e[8];
    unsigned char codes[64];
    char got[200] = "", det[300];
    int n, i, c = 0, len = 0;
    term_dec_init(&d);
    n = term_dec_feed(&d, (const unsigned char *)bytes, (int)strlen(bytes), 0.0, e, 8);
    n += term_dec_flush(&d, 1.0, e + n, 8 - n);
    for (i = 0; i < n; i++)
        c += tns_press_codes(&e[i], codes + c, 64 - c);
    for (i = 0; i < c; i++)
        len += snprintf(got + len, sizeof got - (size_t)len, "%s%02X", i ? " " : "", codes[i]);
    snprintf(det, sizeof det, "got [%s], want [%s]", got, want);
    check(name, !strcmp(got, want), det);
}

static void tns_checks(void)
{
    tns_case("tns: y", "y", "BD 3D");
    tns_case("tns: A (shift)", "A", "E1 94 14 61");
    tns_case("tns: Ctrl+O", "\x0f", "81 D5 55 01");
    tns_case("tns: ! (shift 1)", "!", "E1 8B 0B 61");
    tns_case("tns: F4", "\x1bOS", "8E 0E");
    tns_case("tns: F9 (the options menu)", "\x1b[20~", "C6 46");
    tns_case("tns: Up", "\x1b[A", "DA 5A");
    tns_case("tns: Alt+x", "\x1bx", "A1 AA 2A 21");
    tns_case("tns: Enter, Esc", "\r\x1b", "DB 5B 89 09");
    tns_case("tns: a key it does not have (F12, `)", "\x1b[24~`", "");
    {
        char d[120];
        int sh = tns_code_of(K_LSHIFT), rs = tns_code_of(K_RSHIFT), ct = tns_code_of(K_RCTRL),
            ra = tns_code_of(K_RALT), pg = tns_code_of(K_PGDN);
        snprintf(d, sizeof d, "lshift %02X rshift %02X rctrl %02X ralt %02X pagedown %02X", sh, rs, ct, ra, pg);
        check("tns: an input device's keys", sh == 0xE1 && rs == 0xE2 && ct == 0x81 && ra == 0xB9 && pg == 0xEB, d);
    }
}

/* ---- names and the settings file ---------------------------------------------------------------------------- */
static void name_checks(const char *dir)
{
    key_combo c[8];
    int m, k = key_parse("ctrl-o", &m), n;
    char d[300], path[512], kk[64], vv[64];
    ini *f;
    FILE *fp;
    check("names: ctrl-o", k == 'o' && m == KM_CTRL, "");
    check("names: f11, code:57, space, escape", key_parse("f11", &m) == K_F11 && key_parse("code:57", &m) == K_CODE + 57
          && key_parse("space", &m) == ' ' && key_parse("escape", &m) == K_ESC, "");
    n = key_list("a ;", c, 8);
    check("names: the advance bar's \"a ;\"", n == 2 && c[0].key == 'a' && c[1].key == ';', "");
    n = key_list("a, ;", c, 8);
    check("names: \"a, ;\" and a lone comma", n == 2 && key_list(",", c + 2, 6) == 1 && c[2].key == ',', "");
    snprintf(path, sizeof path, "%s/test_keys.ini", dir);
    fp = fopen(path, "w");
    if (!fp) {
        check("settings file", 0, path);
        return;
    }
    fputs("; a comment\n[keys]\nadvance = a ;\nmenu = f11\n\n[sound]\nrate = 22050\n", fp);
    fclose(fp);
    f = ini_load(path);
    ini_set(f, "keys", "mode", "letters");          /* added at the end of [keys], before the blank line */
    ini_set(f, "sound", "rate", "44100");           /* changed where it is */
    ini_set(f, "serial", "port", "pty");            /* a new section */
    ini_save(f, path);
    ini_free(f);
    f = ini_load(path);
    {   /* (ini_get's value lasts until the next call) */
        char adv[32], mode[32], port[32];
        snprintf(adv, sizeof adv, "%s", ini_get(f, "keys", "advance", "?"));
        snprintf(mode, sizeof mode, "%s", ini_get(f, "keys", "mode", "?"));
        snprintf(port, sizeof port, "%s", ini_get(f, "serial", "port", "?"));
        snprintf(d, sizeof d, "advance [%s] mode [%s] rate %d port [%s]", adv, mode,
                 ini_get_int(f, "sound", "rate", 0), port);
    }
    {
        int ok = !strcmp(ini_get(f, "keys", "advance", ""), "a ;") && !strcmp(ini_get(f, "keys", "mode", ""), "letters")
                 && ini_get_int(f, "sound", "rate", 0) == 44100 && !strcmp(ini_get(f, "serial", "port", ""), "pty")
                 && ini_entry(f, "keys", 2, kk, sizeof kk, vv, sizeof vv) && !strcmp(kk, "mode");
        check("settings file: read, changed, kept", ok, d);
    }
    {   /* two values held at once (main_linux.c reads the evdev setting, then grab, then uses the first) */
        const char *a = ini_get(f, "keys", "advance", ""), *b = ini_get(f, "keys", "mode", "");
        int r = ini_get_int(f, "sound", "rate", 0);
        snprintf(d, sizeof d, "advance [%s] after reading mode [%s] and rate %d", a, b, r);
        check("settings file: two values at once", !strcmp(a, "a ;") && !strcmp(b, "letters") && r == 44100, d);
    }
    ini_free(f);
    remove(path);
}

int main(int argc, char **argv)
{
    blk_break = getenv("BLAZIE_KEYS_BREAK") != NULL;
    terminal_checks();
    bl_checks();
    tns_checks();
    name_checks(argc > 1 ? argv[1] : ".");
    if (failures)
        printf("test_keys: %d of %d FAILED\n", failures, checks);
    else
        printf("test_keys: all %d passed\n", checks);
    return failures ? 1 : 0;
}
