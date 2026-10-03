/* test_bns.c -- the Braille 'n Speak 2000 on the Braille Lite's board (Tomi: the Slovak firmware), headless.
 *
 *   test_bns models BL2000.BNS BNS2000.BNS... [OTHER...]
 *       bl_create tells each image's unit (bl_model): the Braille Lite 2000's, the Braille 'n Speak 2000's (each
 *       BNS2000 argument up to "--"), and after "--" images of other units, which it must refuse (a Type 'n Speak);
 *       the voice's import (bl_firmware.h) still refuses the Braille 'n Speak 2000: it is not a voice yet.
 *   test_bns speech FIRMWARE STATE en|sk
 *       the unit switched on from its factory state, and the chip's phonemes as the firmware writes them (R0 with
 *       R3's control bit clear; PA left out): its words at power-on, then o-chord t (the time) answered -- against
 *       the language's words, held here as the phonemes the unit spoke.  Slovak: "Braille 'n Speak dvetisic ...",
 *       "hodiny nie su nastavene" (the clock not set) and the time read in Slovak ("dvanast nula ...").
 *
 * The board alone (no clock controller, the stand-in A/R timing the state recipes use), keys at instruction counts:
 * every run the same.  The must-fail control (run_tests): the Slovak words asked of the English unit.
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
#include "../../csrc/blazie/bl_firmware.h"

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-34s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

static const char *PH[64] = {"PA", "E", "E1", "Y", "YI", "AY", "IE", "I", "A", "AI", "EH", "EH1", "AE", "AE1", "AH",
    "AH1", "AW", "O", "OU", "OO", "IU", "IU1", "U", "U1", "UH", "UH1", "UH2", "UH3", "ER", "R", "R1", "R2", "L", "L1",
    "LF", "W", "B", "D", "KV", "P", "T", "K", "HV", "HVC", "HF", "HFC", "HN", "Z", "S", "J", "SCH", "V", "F", "THV",
    "TH", "M", "N", "NG", ":A", ":OH", ":U", ":UH", "E2", "LB"};

/* ---- models ------------------------------------------------------------------------------------------------------ */
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

static void models(int argc, char **argv)
{
    char err[256], d[400];
    int i, other = 0;
    for (i = 2; i < argc; i++) {
        int want = i == 2 ? BL_MODEL_BRAILLE_LITE : BL_MODEL_BNS2000, got;
        bl_unit *u;
        if (!strcmp(argv[i], "--")) {
            other = 1;
            continue;
        }
        u = bl_create(argv[i], NULL, 20.0, NULL, NULL, 0, err, sizeof err);
        got = u ? bl_model(u) : -1;
        bl_destroy(u);
        if (other) {
            snprintf(d, sizeof d, "%s: %s", argv[i], u ? "started" : err);
            check("another unit refused", !u, d);
            continue;
        }
        snprintf(d, sizeof d, "%s: %s", argv[i], !u ? err : got == BL_MODEL_BNS2000 ? "Braille 'n Speak 2000"
                 : got == BL_MODEL_BRAILLE_LITE ? "Braille Lite 2000" : "?");
        check(want == BL_MODEL_BNS2000 ? "a Braille 'n Speak 2000" : "a Braille Lite 2000", u && got == want, d);
        if (want == BL_MODEL_BNS2000) {   /* the voice's import: not a voice yet, refused as before */
            long n = 0;
            unsigned char *data = read_all(argv[i], &n);
            char out[64], msg[256];
            int r;
            /* its own name: run_tests runs this check and its swapped control at once, in one folder */
            snprintf(out, sizeof out, "test_bns.import.%d.bns", (int)_getpid());
            r = data ? blv_import_firmware(data, n, out, msg, sizeof msg) : -99;
            free(data);
            remove(out);
            snprintf(d, sizeof d, "%s: %d (%s)", argv[i], r, msg);
            check("the voice's import refuses it", r == BLV_FW_REFUSED, d);
        }
    }
}

/* ---- speech ------------------------------------------------------------------------------------------------------ */
#define M 1000000ULL

/* the phonemes written from instruction `from` to `to`, as names separated by spaces (PA left out) */
static void phonemes(bl_unit *u, unsigned long long from, unsigned long long to, int *ctl, char *out, int cap)
{
    unsigned long long t;
    int len = 0;
    out[0] = 0;
    bl_boot(u, from);
    bl_clear_events(u);
    for (t = from; t < to;) {
        const bl_event *ev;
        int n, j;
        t = t + M < to ? t + M : to;
        bl_boot(u, t);
        n = bl_events(u, &ev);
        for (j = 0; j < n; j++) {
            if (ev[j].type != 'W')
                continue;
            if (ev[j].a == 3)
                *ctl = ev[j].b >> 7;
            else if (ev[j].a == 0 && !*ctl && (ev[j].b & 0x3F) && len + 6 < cap)
                len += snprintf(out + len, (size_t)(cap - len), "%s%s", len ? " " : "", PH[ev[j].b & 0x3F]);
        }
        bl_clear_events(u);
    }
}

typedef struct {
    const char *lang, *power_on, *time;
} words;

/* what each unit said (read from its chip writes, then heard by no one: the phonemes are the check) */
static const words WORDS[] = {
    {"en",
     /* "Braille 'n Speak 2000 ready" "reset clock first date then time" "help page 1" */
     "B R A E L EH N S P E K T U U TH AH OU Z AE N D R EH D E1 R E S EH T K L AH K F ER ER S T D A E1 T THV EH N T AH "
     "E M E HF EH L P W UH1 N P A E1 D J",
     /* "option" "reset clock first date then time" "twelve am" */
     "AH P SCH UH2 N R E S EH T K L AH K F ER ER S T D A E1 T THV EH N T AH E M T W EH L V A E1 EH M"},
    {"sk",
     /* "Braille 'n Speak dvetisic ready" "hodiny nie su nastavene" "help je otvorene" */
     "B R A E LF EH N S P E E K D V UH3 T NG I S E E T S R1 R1 EH1 D I OU HF O D NG I N I N NG YI EH S U U N UH3 S T "
     "UH3 V EH1 N EH1 EH1 OU HF EH1 L P YI EH1 O T V O R1 R1 EH1 N E E",
     /* "vyber" (option) "hodiny nie su nastavene" "dvanast nula ... am" */
     "V E E B EH1 R1 R1 OU HF O D NG I N I N NG YI EH S U U N UH3 S T UH3 V EH1 N EH1 EH1 D V UH3 N UH3 UH3 S T NG "
     "EH1 N U L UH3 UH3 EH M"},
};

static void speech(const char *fw, const char *st, const char *lang)
{
    /* o-chord, then t: the time */
    static const unsigned long long at[] = {30 * M, 32 * M};
    static const unsigned char keys[] = {0x55, 0x1E};
    char err[256], got[4000], d[4400];
    const words *w = NULL;
    int k, ctl = 0;
    bl_unit *u;
    for (k = 0; k < (int)(sizeof WORDS / sizeof *WORDS); k++)
        if (!strcmp(WORDS[k].lang, lang))
            w = &WORDS[k];
    if (!w) {
        printf("FAIL no words for %s\n", lang);
        exit(2);
    }
    u = bl_create(fw, st, 20.0, at, keys, 2, err, sizeof err);
    if (!u) {
        printf("FAIL create: %s\n", err);
        exit(1);
    }
    snprintf(d, sizeof d, "%s", bl_model(u) == BL_MODEL_BNS2000 ? "Braille 'n Speak 2000" : "not a Braille 'n Speak 2000");
    check("the unit", bl_model(u) == BL_MODEL_BNS2000, d);
    phonemes(u, 0, 15 * M, &ctl, got, sizeof got);
    snprintf(d, sizeof d, "[%s]", got);
    check(!strcmp(lang, "sk") ? "Slovak words at power-on" : "English words at power-on", !strcmp(got, w->power_on),
          d);
    phonemes(u, 29 * M, 45 * M, &ctl, got, sizeof got);
    snprintf(d, sizeof d, "[%s]", got);
    check(!strcmp(lang, "sk") ? "o-chord t answered in Slovak" : "o-chord t answered in English", !strcmp(got, w->time),
          d);
    bl_destroy(u);
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "models"))
        models(argc, argv);
    else if (argc == 5 && !strcmp(argv[1], "speech"))
        speech(argv[2], argv[3], argv[4]);
    else {
        printf("usage: test_bns models BL2000.BNS BNS2000.BNS... [-- OTHER...] | speech FIRMWARE STATE en|sk\n");
        return 2;
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
