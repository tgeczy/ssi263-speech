/* bl_voice.c -- see bl_voice.h.  Each part names the driver function it ports (nvda/blazie/synthDrivers/blazie.py);
 * change them together.  voice_equiv.py compares the two byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI                   /* wingdi.h has a DEFAULT_PITCH of its own */
#include <windows.h>
#else
#include <time.h>
#endif
#include "bl_voice.h"
#include "bl_cp850.h"
#include "bl_cp852.h"
#include "bl_cp857.h"
#include "bl_cp860.h"
#include "bl_cp866.h"
#include "bl_cp864ar.h"
#include "bl_cphun.h"
#include "bl_firmware.h"                    /* blv_release_of: the firmware's own boot keys and echo */
#include "../translit.h"

/* the driver's constants */
#define BLOCK_S 0.03
#define UNIT_VOLUME 6
#define MAKEUP 2.0
#define BOARD_LOWPASS_HZ 5000.0
#define CLOSURE_NOISE_LEAD_MS 10.0
#define DEFAULT_RATE 11
#define DEFAULT_PITCH 16
#define DEFAULT_TONE 7
#define MAX_RATE 15
#define MAX_WORDS 14
#define LEAD_THRESHOLD 0.003
#define LEAD_PREROLL 220
#define TAIL_MAX_S 0.3           /* blazie.py TAIL_MAX_S: after the unit is done, its last sound may decay this long */
#define LIFT_PAUSE_S 0.5         /* blazie.py LIFT_PAUSE_S: speech after this much silence starts fresh */
/* hosts/blazie.py boot_keys(menu=("punct_none", "numbers_toggle"), start=3000000, gap=1500000, status) */
#define KEY_START 3000000ULL
#define KEY_GAP 1500000ULL
/* a slower firmware's (bl_languages.txt boot = slow): the MASTER harness's schedule (8M, then 10M apart) */
#define KEY_START_SLOW 8000000ULL
#define KEY_GAP_SLOW 10000000ULL

struct bl_voice {
    ssi263 *chip;
    bl_host *host;
    int encoding;
    int rate, pitch, tone, volume, pack;          /* NVDA's scales */
    int run_ahead;                                /* the driver's runAhead (blv_set_run_ahead) */
    blv_numbers_fn numbers;                       /* the driver's numberWords (blv_set_numbers); NULL: off */
    int sent_rate, sent_pitch, sent_tone;         /* unit.sent_settings */
    double gain;
    int lead, active;
    int line_lift, lift_next;     /* blv_set_line_lift; the next utterance comes after a cancel (or is the first) */
    double quiet_since;           /* blv_clock() when the last utterance ended; < 0 while one is under way */
    double out_rate, play_end;    /* blv_clock() when the listener will have heard all the audio handed out */
    double tail;                  /* seconds rendered after the unit was done, its last sound decaying */
    int fault;                    /* the host refused or failed an utterance (bh_say/bh_busy < 0: run ahead's sticky
                                     faults, Astra's Reply 112); the next blv_speak recovers with bh_cancel first */
    short *pcm;
    int pcm_cap;
    struct { int id, ord; } *mk;   /* the utterance's index marks not yet reached: ord 0 at once, else the
                                     number (the host's "echo_f") of the echo that reaches it */
    int n_mk, cap_mk;
    int own_marks;                /* the firmware's release was measured for its own marks (blv_release_marks) */
    int commas;                   /* its own reader repeats digits around commas (blv_release_commas): none is sent
                                     touching a digit */
    ssv_reach *reached;         /* those the last blv_render's block reached (blv_reached) */
    int n_reached, cap_reached;
};

/* the tests' controls (test_release_voices.py; an app never sets it): 1 boots every firmware with the fast keys, 2
   ignores the release's echo -- each must fail its own case */
BL_API int blv_voice_break = 0;
BL_API int blv_commas_break = 0;

/* ---- _boot ---------------------------------------------------------------------------------------------------- */
BL_API bl_voice *blv_create(const char *firmware, const char *state, int encoding, double out_rate, int inflection,
                            int whine, char *err, int errlen)
{
    bl_voice *v = (bl_voice *)calloc(1, sizeof(bl_voice));
    ssi263_params p;
    unsigned char codes[16];
    unsigned long long key_at[16], start, gap;
    int n = 0, i, release, slow;
    const double *y;
    if (!v) { snprintf(err, errlen, "out of memory"); return NULL; }
    /* params = {"closure_noise_lead_ms": CLOSURE_NOISE_LEAD_MS}, and carrier_rel_db -300 under the whine model */
    ssi263_default_params(&p);
    p.closure_noise_lead_s = CLOSURE_NOISE_LEAD_MS / 1000.0;
    if (whine)
        p.carrier_rel_db = -300.0;
    v->chip = ssi263_new(&p, ssi263_default_rom(), out_rate);
    if (!v->chip) { snprintf(err, errlen, "could not create the chip"); free(v); return NULL; }
    /* boot_keys: the status menu first when inflection goes off (34-chord, i, n, e-chord), then the speech menu
       (345-chord, z = punctuation none, n = full numbers, 123456-chord, L, e) */
    if (!inflection) {
        codes[n++] = 0x4C; codes[n++] = 0x0A; codes[n++] = 0x1D; codes[n++] = 0x51;
    }
    codes[n++] = 0x5C; codes[n++] = 0x35; codes[n++] = 0x1D; codes[n++] = 0x7F; codes[n++] = 0x07; codes[n++] = 0x51;
    /* the firmware's own release (bl_languages.txt): a slower one takes its boot keys later and further apart, else
       it drops them and never enters speech-box mode; a unit that never echoes ^F is judged by the chip.  Any other
       firmware, and the shipped voices' releases, boot exactly as they always have */
    release = blv_release_of(firmware);
    slow = release >= 0 && blv_release_slow_boot(release) == 1 && blv_voice_break != 1;
    start = slow ? KEY_START_SLOW : KEY_START;
    gap = slow ? KEY_GAP_SLOW : KEY_GAP;
    for (i = 0; i < n; i++)
        key_at[i] = start + (unsigned long long)i * gap;
    v->host = bh_create(firmware, state, v->chip, out_rate, BOARD_LOWPASS_HZ, key_at, codes, n,
                        start + (unsigned long long)n * gap, 0, err, errlen);
    if (!v->host) { ssi263_free(v->chip); free(v); return NULL; }
    v->own_marks = release >= 0 && blv_release_marks(release) == 1;
    v->commas = release >= 0 && blv_release_commas(release) == BLV_COMMAS_REPEAT;
    if (release >= 0 && blv_release_echo(release) == 0 && blv_voice_break != 2) {
        bh_set_int(v->host, "no_echo", 1);
        if (blv_voice_break == 3)          /* the control: its own end left out (bl_host.c no_echo_finished) */
            bh_set_int(v->host, "no_echo_slow", 1);
    }
    bh_send(v->host, (const unsigned char *)"\x18", 1);
    bh_send(v->host, (const unsigned char *)"\r\x06", 2);
    bh_run(v->host, 0.3, 0.0005, &y);
    {
        char vol[16];
        int k = snprintf(vol, sizeof vol, "\x05%dV", UNIT_VOLUME);
        bh_send(v->host, (const unsigned char *)vol, k);
    }
    bh_run(v->host, 0.05, 0.0005, &y);
    bh_set_whine(v->host, whine);
    v->encoding = encoding;
    v->sent_rate = DEFAULT_RATE; v->sent_pitch = DEFAULT_PITCH; v->sent_tone = DEFAULT_TONE;
    v->rate = 50; v->pitch = 50; v->tone = DEFAULT_TONE; v->volume = 100; v->pack = 1;
    v->lift_next = 1;
    v->quiet_since = -1.0;
    v->out_rate = out_rate;
    v->play_end = 0.0;
    return v;
}

BL_API void blv_destroy(bl_voice *v)
{
    if (!v) return;
    if (v->host) bh_destroy(v->host);
    if (v->chip) ssi263_free(v->chip);
    free(v->pcm);
    free(v->mk);
    free(v->reached);
    free(v);
}

/* a monotonic clock in seconds (the lift's pause); the tests may set their own */
static double monotonic_s(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;                      /* every Windows (GetTickCount64 is Vista's, not in the
                                                x86 headers for Windows 7) */
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
#endif
}
BL_API double (*blv_clock)(void) = monotonic_s;

BL_API void blv_set_line_lift(bl_voice *v, int on) { v->line_lift = on != 0; }
BL_API int blv_get_line_lift(const bl_voice *v) { return v->line_lift; }
BL_API void blv_interrupted(bl_voice *v) { v->lift_next = 1; v->play_end = 0.0; }
BL_API bl_host *blv_host(bl_voice *v) { return v->host; }
BL_API ssi263 *blv_chip(bl_voice *v) { return v->chip; }

BL_API void blv_set(bl_voice *v, int rate, int pitch, int tone, int volume, int pack)
{
    v->rate = rate; v->pitch = pitch; v->tone = tone; v->volume = volume; v->pack = pack != 0;
}

BL_API void blv_set_run_ahead(bl_voice *v, int on)
{
    v->run_ahead = on != 0;
}

BL_API void blv_set_numbers(bl_voice *v, blv_numbers_fn fn)
{
    v->numbers = fn;
}

/* ---- _unit_rate, _unit_pitch (int() of a positive float = floor) ---------------------------------------------- */
static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

static int unit_rate(int r)
{
    r = clamp100(r);
    return r <= 50 ? 1 + (int)(r * (DEFAULT_RATE - 1) / 50.0 + 0.5)
                   : DEFAULT_RATE + (int)((r - 50) * (MAX_RATE - DEFAULT_RATE) / 50.0 + 0.5);
}

static int unit_pitch(int p)
{
    p = clamp100(p);
    return p <= 50 ? 1 + (int)(p * (DEFAULT_PITCH - 1) / 50.0 + 0.5)
                   : DEFAULT_PITCH + (int)((p - 50) * (63 - DEFAULT_PITCH) / 50.0 + 0.5);
}

/* ---- text as Python sees it: code points, str.isspace ---------------------------------------------------------- */
static int utf8_decode(const char *s, unsigned *out)      /* returns the count; invalid bytes -> U+FFFD */
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p) {
        unsigned c = *p, cp;
        int k, len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len) { out[n++] = 0xFFFD; p++; continue; }
        cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (k = 1; k < len; k++) {
            if ((p[k] & 0xC0) != 0x80) break;
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (k < len) { out[n++] = 0xFFFD; p++; continue; }
        out[n++] = cp;
        p += len;
    }
    return n;
}

static int py_isspace(unsigned c)
{
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* Letters a unit speaks only from another letter's byte, so the voice sends that byte (read from each unit: every
   byte 80h-FFh sent in speech-box mode and the phonemes logged, 2026-10-15/16).  The lower case letter's own byte
   (93h in both) says nothing; its capital's is spoken. */
static const unsigned short fold_hun[][2] = {{0x0151, 0x0150}};      /* o double acute -> O double acute (A7h) */
static const unsigned short fold_852sk[][2] = {{0x00F4, 0x00D4}};    /* o circumflex -> O circumflex (E2h) */
/* ... and the other way: capitals the unit says nothing for, sent as their lower case */
static const unsigned short fold_852pl[][2] = {{0x017B, 0x017C}};    /* Z dot above */
static const unsigned short fold_852hr[][2] = {{0x0106, 0x0107}, {0x0110, 0x0111}};   /* C acute, D stroke */
static const unsigned short fold_850fr[][2] = {{0x00C0, 0x00E0}, {0x00C2, 0x00E2}, {0x00C7, 0x00E7}, {0x00C8, 0x00E8},
    {0x00CA, 0x00EA}, {0x00CB, 0x00EB}, {0x00CE, 0x00EE}, {0x00CF, 0x00EF}, {0x00D9, 0x00F9}, {0x00DB, 0x00FB}};
static const unsigned short fold_850sv[][2] = {{0x00E9, 0x00C9}};    /* e acute -> E acute: the Swedish unit's */
static const unsigned short fold_850es[][2] = {{0x00C1, 0x00E1}, {0x00CD, 0x00ED}};   /* A acute, I acute */
static const unsigned short fold_850it[][2] = {{0x00C0, 0x00E0}, {0x00C8, 0x00E8}, {0x00CC, 0x00EC}, {0x00CD, 0x00ED},
    {0x00D2, 0x00F2}, {0x00D9, 0x00F9}};
static const unsigned short fold_857tr[][2] = {{0x00C7, 0x00E7}, {0x0130, 0x0069}};   /* C cedilla; dotted I -> i */
static const unsigned short fold_860pt[][2] = {{0x00C1, 0x00E1}, {0x00C2, 0x00E2}, {0x00C0, 0x00E0}, {0x00CA, 0x00EA},
    {0x00CD, 0x00ED}, {0x00D3, 0x00F3}, {0x00DA, 0x00FA}};
static const unsigned short fold_866ru[][2] = {{0x0423, 0x0443}};    /* Cyrillic U: its capital's byte (93h) is silent */
/* Letters the unit says nothing for in either case: left out, so translit.h sends the base letter ("o" for o
   circumflex) rather than a byte the unit skips */
static const unsigned short drop_o_circ[] = {0x00F4, 0x00D4};
static const unsigned short drop_850fr[] = {0x00F4, 0x00D4, 0x00FC, 0x00DC, 0x00FF};
static const unsigned short drop_850it[] = {0x00EE, 0x00CE};
static const unsigned short drop_857tr[] = {0x00E2, 0x00C2, 0x00EE, 0x00CE, 0x00FB, 0x00DB};
/* The Danish, Norwegian and Swedish units: only their own letters from 850 (the unit's answer to the rest of the page
   -- symbols, box drawing -- is noise; translit.h keeps them as before) */
static const unsigned short keep_850dano[] = {0x00E6, 0x00C6, 0x00F8, 0x00D8, 0x00E5, 0x00C5};
static const unsigned short keep_850sv[] = {0x00E5, 0x00C5, 0x00E4, 0x00C4, 0x00F6, 0x00D6, 0x00FC, 0x00DC, 0x00C9};

typedef struct {
    int encoding;
    const unsigned short *high;        /* the upper half (bytes 80h-FFh); 0 where the unit has no letter */
    const unsigned short (*fold)[2];   /* letter -> the letter whose byte is sent */
    int n_fold;
    const unsigned short *drop;        /* letters the unit has no byte for, though the page has one */
    int n_drop;
    const unsigned short *keep;        /* when set: the only letters above 7Fh the unit has (after the folds) */
    int n_keep;
} text_page;

#define N(a) ((int)(sizeof a / sizeof a[0]))
static const text_page PAGES[] = {
    {BLV_CP850, bl_cp850_high, NULL, 0, NULL, 0, NULL, 0}, {BLV_CP852, bl_cp852_high, NULL, 0, NULL, 0, NULL, 0},
    {BLV_CP857, bl_cp857_high, NULL, 0, NULL, 0, NULL, 0}, {BLV_CP860, bl_cp860_high, NULL, 0, NULL, 0, NULL, 0},
    {BLV_CP866, bl_cp866_high, NULL, 0, NULL, 0, NULL, 0},
    {BLV_CPHUN, bl_cphun_high, fold_hun, N(fold_hun), NULL, 0, NULL, 0},
    {BLV_CP852SK, bl_cp852_high, fold_852sk, N(fold_852sk), NULL, 0, NULL, 0},
    {BLV_CP852PL, bl_cp852_high, fold_852pl, N(fold_852pl), drop_o_circ, N(drop_o_circ), NULL, 0},
    {BLV_CP852HR, bl_cp852_high, fold_852hr, N(fold_852hr), drop_o_circ, N(drop_o_circ), NULL, 0},
    {BLV_CP850FR, bl_cp850_high, fold_850fr, N(fold_850fr), drop_850fr, N(drop_850fr), NULL, 0},
    {BLV_CP850DANO, bl_cp850_high, NULL, 0, NULL, 0, keep_850dano, N(keep_850dano)},
    {BLV_CP850SV, bl_cp850_high, fold_850sv, N(fold_850sv), NULL, 0, keep_850sv, N(keep_850sv)},
    {BLV_CP850ES, bl_cp850_high, fold_850es, N(fold_850es), NULL, 0, NULL, 0},
    {BLV_CP850IT, bl_cp850_high, fold_850it, N(fold_850it), drop_850it, N(drop_850it), NULL, 0},
    {BLV_CP857TR, bl_cp857_high, fold_857tr, N(fold_857tr), drop_857tr, N(drop_857tr), NULL, 0},
    {BLV_CP860PT, bl_cp860_high, fold_860pt, N(fold_860pt), drop_o_circ, N(drop_o_circ), NULL, 0},
    {BLV_CP866RU, bl_cp866_high, fold_866ru, N(fold_866ru), NULL, 0, NULL, 0},
    /* the Arabic (UAE) unit: its 27 spoken letters only, every Arabic letter and form folded onto them
       (gen_cp864ar.py) */
    {BLV_CP864AR, bl_cp864_high, fold_864ar, N(fold_864ar), NULL, 0, keep_864ar, N(keep_864ar)},
};
#undef N

static const text_page *page_of(int encoding)
{
    size_t i;
    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++)
        if (PAGES[i].encoding == encoding) return &PAGES[i];
    return NULL;                       /* the English unit's 7-bit text */
}

/* the unit's byte for c in its text encoding (BLV_CP850 ... BLV_CP852SK); -1 if the unit has no such letter */
static int cp_byte(int encoding, unsigned c)
{
    const text_page *p = page_of(encoding);
    int i;
    if (c < 0x80) return (int)c;
    if (!p) return -1;
    for (i = 0; i < p->n_fold; i++)
        if (p->fold[i][0] == c) { c = p->fold[i][1]; break; }
    if (c < 0x80) return (int)c;               /* folded to a plain letter (the Turkish dotted I -> i) */
    for (i = 0; i < p->n_drop; i++)
        if (p->drop[i] == c) return -1;
    if (p->keep) {
        for (i = 0; i < p->n_keep && p->keep[i] != c; i++)
            ;
        if (i == p->n_keep) return -1;
    }
    for (i = 0; i < 128; i++)
        if (p->high[i] == c) return 0x80 + i;
    return -1;
}

static int known_cp850(unsigned c) { return cp_byte(BLV_CP850, c) >= 0; }   /* those units' alphabet (translit.h) */
static int known_cp852(unsigned c) { return cp_byte(BLV_CP852, c) >= 0; }   /* the Polish and Croatian units' */
static int known_cp857(unsigned c) { return cp_byte(BLV_CP857, c) >= 0; }   /* the Turkish unit's */
static int known_cp860(unsigned c) { return cp_byte(BLV_CP860, c) >= 0; }   /* the Portuguese units' */
static int known_cp866(unsigned c) { return cp_byte(BLV_CP866, c) >= 0; }   /* the Russian unit's */
static int known_cphun(unsigned c) { return cp_byte(BLV_CPHUN, c) >= 0; }   /* the Hungarian unit's */
static int known_852sk(unsigned c) { return cp_byte(BLV_CP852SK, c) >= 0; }   /* the Slovak and Czech units' */
static int known_852pl(unsigned c) { return cp_byte(BLV_CP852PL, c) >= 0; }   /* the Polish unit's */
static int known_852hr(unsigned c) { return cp_byte(BLV_CP852HR, c) >= 0; }   /* the Croatian unit's */
static int known_850fr(unsigned c) { return cp_byte(BLV_CP850FR, c) >= 0; }   /* the French unit's */
static int known_850dano(unsigned c) { return cp_byte(BLV_CP850DANO, c) >= 0; }   /* the Danish and Norwegian */
static int known_850sv(unsigned c) { return cp_byte(BLV_CP850SV, c) >= 0; }   /* the Swedish unit's */
static int known_850es(unsigned c) { return cp_byte(BLV_CP850ES, c) >= 0; }   /* the Spanish unit's */
static int known_850it(unsigned c) { return cp_byte(BLV_CP850IT, c) >= 0; }   /* the Italian unit's */
static int known_857tr(unsigned c) { return cp_byte(BLV_CP857TR, c) >= 0; }   /* the Turkish unit's */
static int known_860pt(unsigned c) { return cp_byte(BLV_CP860PT, c) >= 0; }   /* the Portuguese units' */
static int known_866ru(unsigned c) { return cp_byte(BLV_CP866RU, c) >= 0; }   /* the Russian unit's */
static int known_864ar(unsigned c) { return cp_byte(BLV_CP864AR, c) >= 0; }   /* the Arabic (UAE) unit's */

/* translit.h's alphabet for an encoding: what tl_apply keeps as it is */
static int (*known_of(int encoding))(unsigned)
{
    switch (encoding) {
    case BLV_CP850: return known_cp850;
    case BLV_CP852: return known_cp852;
    case BLV_CP857: return known_cp857;
    case BLV_CP860: return known_cp860;
    case BLV_CP866: return known_cp866;
    case BLV_CPHUN: return known_cphun;
    case BLV_CP852SK: return known_852sk;
    case BLV_CP852PL: return known_852pl;
    case BLV_CP852HR: return known_852hr;
    case BLV_CP850FR: return known_850fr;
    case BLV_CP850DANO: return known_850dano;
    case BLV_CP850SV: return known_850sv;
    case BLV_CP850ES: return known_850es;
    case BLV_CP850IT: return known_850it;
    case BLV_CP857TR: return known_857tr;
    case BLV_CP860PT: return known_860pt;
    case BLV_CP866RU: return known_866ru;
    case BLV_CP864AR: return known_864ar;
    default: return tl_known_ascii;
    }
}

/* ---- ssi263_numwords.currencies (English only): "£2.63" -> "2 pounds 63 pence" -------------------------------------
   The regex, tried as Python's re does: at each position the three alternatives in order, each amount in the
   engine's preference order (longest first, then backtracking), the first that passes the lookahead wins.
   str.isalnum / \w beyond Latin-1 are approximated (letters unless punctuation, symbols or emoji). */
static int py_isalnum(unsigned c)
{
    if (c < 128) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    if (c < 256) return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA
                     || (c >= 0xBC && c <= 0xBE) || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xE000 && c <= 0xF8FF)
            || (c >= 0xFE00 && c <= 0xFE0F) || c >= 0x1F000)
        return 0;
    return !py_isspace(c);
}

static int py_isword(unsigned c) { return c == '_' || py_isalnum(c); }
static int isdig(unsigned c) { return c >= '0' && c <= '9'; }

/* the amount at i: (whole)? (.frac)? -- candidates in the regex's order; returns how many were written to cand
   (each: whole start/end, frac start/end, -1 = absent, and the end) */
typedef struct { int w0, w1, f0, f1, end; } amount;

static int amounts(const unsigned *t, int n, int i, amount *cand, int cap)
{
    int k = 0, wend[64], nw = 0, j, g;
    /* whole: \d{1,3}(?:,\d{3})+ -- greedy digits 3..1, then groups most..1 -- then \d+ longest..1, then absent */
    for (j = 3; j >= 1; j--) {
        int d;
        for (d = 0; d < j; d++) if (i + d >= n || !isdig(t[i + d])) break;
        if (d < j) continue;
        {
            int ends[32], ne = 0, p = i + j;
            while (ne < 32 && p + 3 < n && t[p] == ',' && isdig(t[p + 1]) && isdig(t[p + 2]) && isdig(t[p + 3])) {
                p += 4;
                ends[ne++] = p;
            }
            for (g = ne - 1; g >= 0 && nw < 64; g--) wend[nw++] = ends[g];
        }
    }
    for (j = i; j < n && isdig(t[j]); j++) ;
    for (g = j; g > i && nw < 63; g--) wend[nw++] = g;     /* one slot left for the absent whole below */
    wend[nw++] = -1;                                       /* whole absent */
    for (g = 0; g < nw && k < cap; g++) {
        int w1 = wend[g], p = w1 < 0 ? i : w1, f;
        if (p < n && t[p] == '.' && p + 1 < n && isdig(t[p + 1])) {
            for (f = p + 1; f < n && isdig(t[f]); f++) ;
            for (; f > p + 1 && k < cap; f--) {
                cand[k].w0 = w1 < 0 ? -1 : i; cand[k].w1 = w1; cand[k].f0 = p + 1; cand[k].f1 = f; cand[k].end = f;
                k++;
            }
        }
        if (k < cap) {
            cand[k].w0 = w1 < 0 ? -1 : i; cand[k].w1 = w1; cand[k].f0 = -1; cand[k].f1 = -1; cand[k].end = p;
            k++;
        }
    }
    return k;
}

static int put_ascii(unsigned *out, int m, const char *s)
{
    while (*s) out[m++] = (unsigned char)*s++;
    return m;
}

static int put_span(unsigned *out, int m, const unsigned *t, int a, int b)
{
    while (a < b) out[m++] = t[a++];
    return m;
}

/* _currency_words: sym 0 pound, 1 euro, 2 yen */
static int currency_words(const unsigned *t, const amount *a, int sym, unsigned *out, int m)
{
    static const char *units[3][4] = {{"pound", "pounds", "penny", "pence"}, {"euro", "euros", "cent", "cents"},
                                      {"yen", "yen", NULL, NULL}};
    int cents = 0, whole_nonzero = 0, first = 1, i;
    char buf[32];
    if (a->f0 >= 0 && (a->f1 - a->f0 > 2 || !units[sym][2])) {        /* "2.635 pounds" */
        if (a->w0 >= 0) m = put_span(out, m, t, a->w0, a->w1); else out[m++] = '0';
        out[m++] = '.';
        m = put_span(out, m, t, a->f0, a->f1);
        out[m++] = ' ';
        return put_ascii(out, m, units[sym][1]);
    }
    if (a->f0 >= 0)
        cents = (int)(t[a->f0] - '0') * 10 + (a->f1 - a->f0 > 1 ? (int)(t[a->f0 + 1] - '0') : 0);
    if (a->w0 >= 0)
        for (i = a->w0; i < a->w1; i++) if (isdig(t[i]) && t[i] != '0') whole_nonzero = 1;
    if (a->w0 >= 0 && (whole_nonzero || !cents)) {
        int one = a->w1 - a->w0 == 1 && t[a->w0] == '1';
        m = put_span(out, m, t, a->w0, a->w1);
        out[m++] = ' ';
        m = put_ascii(out, m, units[sym][one ? 0 : 1]);
        first = 0;
    }
    if (cents) {
        if (!first) out[m++] = ' ';
        snprintf(buf, sizeof buf, "%d ", cents);
        m = put_ascii(out, m, buf);
        m = put_ascii(out, m, units[sym][cents == 1 ? 2 : 3]);
    }
    return m;
}

static int lookahead_ok(const unsigned *t, int n, int p)   /* (?![\d.]\d) */
{
    return !(p + 1 < n && (isdig(t[p]) || t[p] == '.') && isdig(t[p + 1]));
}

static int currencies(const unsigned *t, int n, unsigned *out)
{
    int i = 0, m = 0;
    amount cand[256];
    while (i < n) {
        int nc, c, end = -1, sym = -1, done = 0, before = i > 0 && !py_isword(t[i - 1]) && t[i - 1] != '.'
                                                           && t[i - 1] != ',';
        int m0 = m;
        amount a;
        if (i == 0) before = 1;
        /* alternative 1: [£€¥] ?amount(?![\d.]\d) */
        if (t[i] == 0xA3 || t[i] == 0x20AC || t[i] == 0xA5) {
            int s = t[i] == 0xA3 ? 0 : t[i] == 0x20AC ? 1 : 2, sp;
            for (sp = (i + 1 < n && t[i + 1] == ' ') ? 1 : 0; sp >= 0 && !done; sp--) {
                nc = amounts(t, n, i + 1 + sp, cand, 256);
                for (c = 0; c < nc; c++)
                    if (lookahead_ok(t, n, cand[c].end)) { a = cand[c]; end = a.end; sym = s; done = 1; break; }
            }
        }
        /* alternative 2: (?<![\w.,])amount ?€ */
        if (!done && before) {
            nc = amounts(t, n, i, cand, 256);
            for (c = 0; c < nc && !done; c++) {
                int p = cand[c].end;
                if (p < n && t[p] == ' ' && p + 1 < n && t[p + 1] == 0x20AC) { a = cand[c]; end = p + 2; done = 2; }
                else if (p < n && t[p] == 0x20AC) { a = cand[c]; end = p + 1; done = 2; }
            }
        }
        /* alternative 3: (?<![\w.,])(\d+) ?¢ */
        if (!done && before && isdig(t[i])) {
            int p = i;
            while (p < n && isdig(t[p])) p++;
            if (p < n && t[p] == ' ' && p + 1 < n && t[p + 1] == 0xA2) { end = p + 2; done = 3; }
            else if (p < n && t[p] == 0xA2) { end = p + 1; done = 3; }
            a.w0 = i; a.w1 = p;
        }
        if (!done) { out[m++] = t[i++]; continue; }
        if ((done == 1 || done == 2) && a.w0 < 0 && a.f0 < 0) {  /* a bare symbol (or "€" alone): unchanged */
            m = put_span(out, m, t, i, end);
            i = end > i ? end : i + 1;
            continue;
        }
        if (i > 0 && py_isalnum(t[i - 1])) out[m++] = ' ';
        if (done == 3) {
            m = put_span(out, m, t, a.w0, a.w1);
            m = put_ascii(out, m, (a.w1 - a.w0 == 1 && t[a.w0] == '1') ? " cent" : " cents");
        } else {
            m = currency_words(t, &a, done == 1 ? sym : 1, out, m);
        }
        (void)m0;
        i = end;
    }
    return m;
}

/* ---- _clean ---------------------------------------------------------------------------------------------------- */
/* Arabic that the Arabic (UAE) unit has no byte for and that sits inside a word: the vowel marks (U+064B-U+065F, the
   superscript alef U+0670, the Quranic marks U+0610-U+061A), the tatweel (U+0640) and a bare hamza (U+0621).  Dropped,
   not made a space as an unknown character is: a space would split the word (blazie.py _arabic_silent) */
static int arabic_silent(unsigned c)
{
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x0610 && c <= 0x061A) || c == 0x0640 || c == 0x0621;
}

static int clean(const unsigned *in, int n, int encoding, unsigned *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        if (encoding == BLV_CP864AR && arabic_silent(c)) continue;
        if (c < 32 || c == 127) out[m++] = ' ';
        else if (c < 128) out[m++] = c;
        else if (encoding != BLV_LATIN1 && cp_byte(encoding, c) >= 0) out[m++] = c;
        else if (c == 0x2018 || c == 0x2019) out[m++] = '\'';
        else if (c == 0x201C || c == 0x201D) out[m++] = '"';
        else if (c == 0x2013 || c == 0x2014) out[m++] = '-';
        else if (c == 0x2026) { out[m++] = '.'; out[m++] = '.'; out[m++] = '.'; }
        else out[m++] = ' ';
    }
    return m;
}

/* ---- _lines: sentences split at whitespace after . ! ?, words by str.split(), long sentences cut at a comma (4+
   words) or at MAX_WORDS, then packed from the second line on.  Every line is a run of consecutive words. ---------- */
typedef struct { int w0, w1; } span;

static int lines(const unsigned *t, int n, int pack, int *ws, int *we, span *out)
{
    int nw = 0, i = 0, np = 0, no = 0, k;
    span *pieces = (span *)malloc(sizeof(span) * (size_t)(n + 1));
    int sent_w0 = 0;
    if (!pieces) return 0;
    while (i <= n) {
        /* one sentence: up to a whitespace run that follows . ! ? (or the end) */
        int end_of_sentence = 0;
        if (i < n && !py_isspace(t[i])) {
            ws[nw] = i;
            while (i < n && !py_isspace(t[i])) i++;
            we[nw++] = i;
            continue;
        }
        if (i == n) end_of_sentence = 1;
        else {
            int run = i;
            if (run > 0 && (t[run - 1] == '.' || t[run - 1] == '!' || t[run - 1] == '?')) end_of_sentence = 1;
            while (i < n && py_isspace(t[i])) i++;
        }
        if (end_of_sentence) {
            int a = sent_w0, b = nw;
            if (b - a <= MAX_WORDS) {
                if (b > a) { pieces[np].w0 = a; pieces[np].w1 = b; np++; }
            } else {
                int start = a, cnt = 0, w;
                for (w = a; w < b; w++) {
                    cnt++;
                    if ((t[we[w] - 1] == ',' && cnt >= 4) || cnt >= MAX_WORDS) {
                        pieces[np].w0 = start; pieces[np].w1 = w + 1; np++;
                        start = w + 1; cnt = 0;
                    }
                }
                if (cnt) { pieces[np].w0 = start; pieces[np].w1 = b; np++; }
            }
            sent_w0 = nw;
            if (i == n) break;
        }
    }
    for (k = 0; k < np; k++) {
        if (pack && no > 1 && (out[no - 1].w1 - out[no - 1].w0) + (pieces[k].w1 - pieces[k].w0) <= MAX_WORDS)
            out[no - 1].w1 = pieces[k].w1;
        else
            out[no++] = pieces[k];
    }
    free(pieces);
    return no;
}

/* ---- the driver's `if self._numbers: text = _numbers(text, unit.lang)`, between _clean and _lines: the caller's fn
   (bl_numbers.h) on the cleaned text as UTF-8, its answer decoded back.  *t replaced (malloc'd); returns the count, or
   -1 when out of memory (the text then goes on as it was) -------------------------------------------------------------- */
static int numbers(blv_numbers_fn fn, unsigned **t, int m, int encoding)
{
    unsigned char *u = (unsigned char *)malloc((size_t)m * 4 + 1);
    char *w;
    unsigned *r;
    int i, k = 0;
    if (!u) return -1;
    for (i = 0; i < m; i++) {
        unsigned c = (*t)[i];
        if (c < 0x80) u[k++] = (unsigned char)c;
        else if (c < 0x800) { u[k++] = (unsigned char)(0xC0 | (c >> 6)); u[k++] = (unsigned char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            u[k++] = (unsigned char)(0xE0 | (c >> 12)); u[k++] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
            u[k++] = (unsigned char)(0x80 | (c & 0x3F));
        } else {
            u[k++] = (unsigned char)(0xF0 | (c >> 18)); u[k++] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
            u[k++] = (unsigned char)(0x80 | ((c >> 6) & 0x3F)); u[k++] = (unsigned char)(0x80 | (c & 0x3F));
        }
    }
    u[k] = 0;
    w = fn((const char *)u, encoding);
    free(u);
    if (!w) return -1;
    r = (unsigned *)malloc(sizeof(unsigned) * (strlen(w) + 1));
    if (!r) { free(w); return -1; }
    k = utf8_decode(w, r);
    free(w);
    free(*t);
    *t = r;
    return k;
}

/* ---- a release whose own reader repeats digits around commas (bl_languages.txt's commas = repeat; the Spanish unit:
   two commas touching a digit in one sentence and it says digits again, more each time it meets such a line): no
   comma reaches it touching a digit.  One between two digits becomes a space, one beside a digit gets a space on
   that side.  After the number words, so only the digits they left ("MP3, MP4") or, with them off, all.  blazie.py
   _commas, the same walk.  *t replaced (malloc'd); the count, -1 when out of memory (the text goes on as it was) */
static int detach_commas(unsigned **t, int m)
{
    unsigned *r = (unsigned *)malloc(sizeof(unsigned) * (size_t)(3 * m + 1));
    int i, k = 0;
    if (!r) return -1;
    for (i = 0; i < m; i++) {
        unsigned c = (*t)[i];
        int before, after;
        if (c != ',') { r[k++] = c; continue; }
        before = i > 0 && (*t)[i - 1] >= '0' && (*t)[i - 1] <= '9';
        after = i + 1 < m && (*t)[i + 1] >= '0' && (*t)[i + 1] <= '9';
        if (before && after) { r[k++] = ' '; continue; }
        if (before) r[k++] = ' ';
        r[k++] = ',';
        if (after) r[k++] = ' ';
    }
    free(*t);
    *t = r;
    return k;
}

/* ---- the text the driver hands unit.say(): _translit (translit.h; the driver's ssv_translit), currencies, _clean,
   (_numbers), _lines, encoded, each line \r ^F, one more \r ^F.  malloc'd into *out (the caller frees it); returns the number of lines (0: nothing to say) -------------- */
/* the text as the unit is to read it (_translit, currencies, _clean, _numbers): code points, malloc'd into *out (the
   caller frees it); their count, -1 when out of memory */
static int prepared(const char *utf8, int encoding, blv_numbers_fn fn, int commas, unsigned **out)
{
    int n = (int)strlen(utf8), m;
    unsigned *cps, *cur, *t;
    *out = NULL;
    cps = (unsigned *)malloc(sizeof(unsigned) * (size_t)(n + 1));
    if (!cps) return -1;
    m = utf8_decode(utf8, cps);
    /* translit.h first: the letters this unit's alphabet lacks, as base letters (or a lone one's words) */
    cur = (unsigned *)malloc(sizeof(unsigned) * (size_t)tl_room(m));
    if (!cur) { free(cps); return -1; }
    m = tl_apply(cps, m, known_of(encoding), cur);
    free(cps);
    cps = cur;
    /* sizes: currencies() writes at most 8 code points per one it reads ("£.5" -> "50 pence"), _clean 3 ("...") */
    cur = (unsigned *)malloc(sizeof(unsigned) * (size_t)(8 * m + 16));
    t = (unsigned *)malloc(sizeof(unsigned) * (size_t)(24 * m + 48));
    if (!cur || !t) {
        free(cps); free(cur); free(t);
        return -1;
    }
    /* numwords.currencies(value, unit.lang): the English unit only */
    if (encoding == BLV_LATIN1)
        m = currencies(cps, m, cur);
    else
        memcpy(cur, cps, sizeof(unsigned) * (size_t)m);
    m = clean(cur, m, encoding, t);
    if (fn) {
        int r = numbers(fn, &t, m, encoding);
        if (r >= 0) m = r;
    }
    if (commas && !blv_commas_break) {
        int r = detach_commas(&t, m);
        if (r >= 0) m = r;
    }
    free(cps); free(cur);
    *out = t;
    return m;
}

/* the words of prepared text, as lines() and str.split() take them: their starts and ends; their count */
static int words_of(const unsigned *t, int n, int *ws, int *we)
{
    int nw = 0, i = 0;
    while (i < n) {
        if (py_isspace(t[i])) { i++; continue; }
        ws[nw] = i;
        while (i < n && !py_isspace(t[i])) i++;
        we[nw++] = i;
    }
    return nw;
}

/* the tests' controls (voice_marks.py; an app never sets it): 1 reports every mark in the first block, not where its
   echo comes; 2 gives the unit a Ctrl-F for every mark, also those that fall on the same word; 3 leaves the marks
   after a cancel; 4 takes a mark inside a run of text where it stands, not from the run's start; 5 puts the Ctrl-F
   inside its word, after the first letter; 6 takes every echoing release's marks for its own */
BL_API int blv_marks_break = 0;

/* The word of the whole prepared text that a mark at byte `at` of the source stands before (0 to nw; nw: after the
   last word).  The text is never prepared in pieces: the source up to the mark is prepared by itself and its words
   are held against the whole's.  Where they stop agreeing is the word the mark belongs to -- the next word when the
   mark stands between two, the START of the word when it stands inside one, the start of a number or an amount
   that the preparation rewrites as a whole (marks.h). */
static int mark_word(const char *utf8, int at, int encoding, blv_numbers_fn fn, int commas, const unsigned *t,
                     const int *ws,
                     const int *we, int nw)
{
    int len = (int)strlen(utf8), pm, pnw, j = 0, *pws, *pwe;
    unsigned *pt;
    char *prefix;
    if (at <= 0) return 0;
    if (at > len) at = len;
    /* inside a run of text with no space in it: from the run's start -- the preparation may rewrite the run as a
       whole (an amount of money, a number), and its first word is where the run begins to be spoken */
    if (at < len && blv_marks_break != 4)
        while (at > 0 && (unsigned char)utf8[at] > ' ' && (unsigned char)utf8[at - 1] > ' ') at--;
    while (at > 0 && at < len && ((unsigned char)utf8[at] & 0xC0) == 0x80) at--;    /* never inside a character */
    prefix = (char *)malloc((size_t)at + 1);
    if (!prefix) return 0;
    memcpy(prefix, utf8, (size_t)at);
    prefix[at] = 0;
    pm = prepared(prefix, encoding, fn, commas, &pt);
    free(prefix);
    if (pm < 0) return 0;
    pws = (int *)malloc(sizeof(int) * (size_t)(pm + 1));
    pwe = (int *)malloc(sizeof(int) * (size_t)(pm + 1));
    if (pws && pwe) {
        pnw = words_of(pt, pm, pws, pwe);
        for (j = 0; j < pnw && j < nw; j++) {
            int a = pwe[j] - pws[j];
            if (a != we[j] - ws[j] || memcmp(pt + pws[j], t + ws[j], sizeof(unsigned) * (size_t)a)) break;
        }
    }
    free(pt); free(pws); free(pwe);
    return j;
}


/* marks: the caller's, in rising order of `at`; ords (n_marks of them), for each: 0 it stands before the first word
   (reached at once), > 0 the number of its Ctrl-F among the 06h bytes of the data, -1 after the last word.  *n06:
   the 06h bytes in the data, the flush line's included. */
static int say_bytes(const char *utf8, int encoding, int pack, blv_numbers_fn fn, int commas, const ssv_mark *marks,
                     int n_marks,
                     int *ords, int *n06, unsigned char **out, int *out_len)
{
    int m, nl, k, w, len = 0, nw, count = 0, i;
    unsigned *t;
    int *ws = NULL, *we = NULL, *mk = NULL;
    span *ln = NULL;
    unsigned char *data = NULL;
    *out = NULL;
    *out_len = 0;
    if (n06) *n06 = 0;
    for (i = 0; i < n_marks; i++) ords[i] = 0;
    m = prepared(utf8, encoding, fn, commas, &t);
    if (m < 0) return 0;
    ws = (int *)malloc(sizeof(int) * (size_t)(m + 1));
    we = (int *)malloc(sizeof(int) * (size_t)(m + 1));
    ln = (span *)malloc(sizeof(span) * (size_t)(m + 1));
    mk = (int *)calloc((size_t)(m + 2), sizeof(int));          /* per word: the Ctrl-Fs in front of it */
    data = (unsigned char *)malloc((size_t)m * 3 + 16 + (size_t)n_marks);
    if (!ws || !we || !ln || !mk || !data) {
        free(t); free(ws); free(we); free(ln); free(mk); free(data);
        return 0;
    }
    nl = lines(t, m, pack, ws, we, ln);
    nw = nl ? ln[nl - 1].w1 : 0;
    for (i = 0; i < n_marks; i++) {
        w = mark_word(utf8, marks[i].at, encoding, fn, commas, t, ws, we, nw);
        if (i && w < ords[i - 1]) w = ords[i - 1];             /* in the caller's order, never back */
        ords[i] = w;                                           /* (its word, until the data is laid out) */
        if (w > 0 && w < nw && (!mk[w] || blv_marks_break == 2)) mk[w]++;
    }
    /* unit.say(lines): each line encoded ("replace" -> '?'), then \r ^F; one more \r ^F flushes.  A marked word has
       its Ctrl-F glued to its front: the unit echoes it when speech reaches the word, at a line's start too */
    for (k = 0; k < nl; k++) {
        for (w = ln[k].w0; w < ln[k].w1; w++) {
            int j;
            int n6 = mk[w];
            if (w > ln[k].w0) data[len++] = ' ';
            for (j = blv_marks_break == 5 ? 0 : n6; j > 0; j--) data[len++] = 0x06;
            count += n6;
            if (n6) mk[w] = count;                             /* now: the number of its (last) Ctrl-F */
            for (j = ws[w]; j < we[w]; j++) {
                int b = encoding != BLV_LATIN1 ? cp_byte(encoding, t[j]) : (t[j] < 256 ? (int)t[j] : -1);
                data[len++] = (unsigned char)(b < 0 ? '?' : b);
                if (blv_marks_break == 5 && j == ws[w])        /* the control: inside the word, after its first letter */
                    for (b = n6; b > 0; b--) data[len++] = 0x06;
            }
        }
        data[len++] = '\r'; data[len++] = 0x06;
        count++;
    }
    if (nl) {
        data[len++] = '\r'; data[len++] = 0x06;
        count++;
    }
    for (i = 0; i < n_marks; i++)
        ords[i] = !nl || ords[i] <= 0 ? 0 : ords[i] >= nw ? -1 : mk[ords[i]];
    if (n06) *n06 = count;
    free(t); free(ws); free(we); free(ln); free(mk);
    *out = data;
    *out_len = len;
    return nl;
}

BL_API int blv_text_byte(int encoding, unsigned code_point)
{
    if (encoding == BLV_LATIN1) return code_point < 256 ? (int)code_point : -1;
    return cp_byte(encoding, code_point);
}

BL_API blv_known_fn blv_text_known(int encoding)
{
    return known_of(encoding);
}

BL_API int blv_say_bytes(const char *utf8, int encoding, int pack, unsigned char *out, int cap)
{
    return blv_say_bytes_with(utf8, encoding, pack, NULL, out, cap);
}

BL_API int blv_say_bytes_with(const char *utf8, int encoding, int pack, blv_numbers_fn fn, unsigned char *out, int cap)
{
    return blv_say_bytes_release(utf8, encoding, pack, fn, 0, out, cap);
}

BL_API int blv_say_bytes_release(const char *utf8, int encoding, int pack, blv_numbers_fn fn, int commas,
                                 unsigned char *out, int cap)
{
    unsigned char *data;
    int len, nl = say_bytes(utf8, encoding, pack, fn, commas, NULL, 0, NULL, NULL, &data, &len);
    (void)nl;
    if (data && len <= cap)
        memcpy(out, data, (size_t)len);
    free(data);
    return len;
}


/* ---- index marks (marks.h) --------------------------------------------------------------------------------------- */
BL_API int blv_marks_exact(const bl_voice *v)
{
    /* the unit's own Ctrl-F, when its release was measured for it (bl_languages.txt's marks column), it echoes (a
       caller may have set "no_echo" itself) and it is not run ahead of the listener (its echoes then come at the
       capture's frontier, bl_host.h) */
    return (v->own_marks || blv_marks_break == 6) && !(v->run_ahead && v->pack) && bh_get_int(v->host, "no_echo") <= 0;
}

#define ORD_END 0x7FFFFFFF         /* a mark after the last word: reached with the utterance's last sample */

/* The marks speech has reached in the block just run: those whose Ctrl-F the unit has echoed, each at the sample its
   echo came (less the `start` samples trimmed from the block's head), and, with `all`, the rest after the block's
   last sample (the utterance is over). */
static void report_marks(bl_voice *v, int start, int count, int all)
{
    const bh_echo *e;
    int ne = bh_echoes(v->host, &e), ef = bh_get_int(v->host, "echo_f"), i, j, kept = 0, last = 0;
    v->n_reached = 0;
    if (!v->n_mk)
        return;
    if (blv_marks_break == 1)
        ef = 0x7FFFFFFF;
    if (v->n_mk > v->cap_reached) {
        ssv_reach *r = (ssv_reach *)realloc(v->reached, sizeof(ssv_reach) * (size_t)v->n_mk);
        if (!r) { v->n_mk = 0; return; }
        v->reached = r;
        v->cap_reached = v->n_mk;
    }
    for (i = 0; i < v->n_mk; i++) {
        int ord = v->mk[i].ord, s = 0, exact = ord <= 0 || ord == ORD_END;   /* (before the first word, after the
                                                                                last: the utterance's start, its end) */
        if (ord > ef && !all) {                              /* not yet: its echo is still to come */
            v->mk[kept++] = v->mk[i];
            continue;
        }
        if (ord > ef)                                        /* the utterance is over and its echo never came (a
                                                                fault, a unit that dropped it): with the last block,
                                                                so that no caller is left waiting -- and not exact */
            s = count;
        else if (ord > 0)
            for (j = 0; j < ne; j++)
                if (e[j].n == ord) { s = e[j].sample - start; exact = 1; break; }
        if (s < last) s = last;
        if (s > count) s = count;
        last = s;
        v->reached[v->n_reached].id = v->mk[i].id;
        v->reached[v->n_reached].sample = s;
        v->reached[v->n_reached].exact = exact;              /* (0 too for an echo this block's run did not record) */
        v->n_reached++;
    }
    v->n_mk = kept;
}

BL_API int blv_reached(bl_voice *v, const ssv_reach **out)
{
    *out = v->reached;
    return v->n_reached;
}

BL_API int blv_say_bytes_marked(const char *utf8, int encoding, int pack, const ssv_mark *marks, int n_marks,
                                unsigned char *out, int cap)
{
    unsigned char *data;
    int len, *ords = (int *)malloc(sizeof(int) * (size_t)(n_marks > 0 ? n_marks : 1));
    if (!ords) return 0;
    say_bytes(utf8, encoding, pack, NULL, 0, marks, n_marks, ords, NULL, &data, &len);
    if (data && len <= cap)
        memcpy(out, data, (size_t)len);
    free(data);
    free(ords);
    return len;
}

/* ---- _speakSegment + _speakItems (one text item) ----------------------------------------------------------------- */
BL_API int blv_speak(bl_voice *v, const char *utf8)
{
    return blv_speak_marked(v, utf8, NULL, 0);
}

BL_API int blv_speak_marked(bl_voice *v, const char *utf8, const ssv_mark *marks, int n_marks)
{
    int r = unit_rate(v->rate), p = unit_pitch(v->pitch), tone = v->tone, nl, len, n06 = 0, base, i;
    int *ords = NULL;
    unsigned char *data;
    const double *y;
    if (r != v->sent_rate || p != v->sent_pitch || tone != v->sent_tone) {
        char cmd[48];
        int c = snprintf(cmd, sizeof cmd, "\x05%dE\x05%dP\x05%dT", r, p, tone);
        bh_send(v->host, (const unsigned char *)cmd, c);
        bh_run(v->host, 0.02, 0.0005, &y);
        v->sent_rate = r; v->sent_pitch = p; v->sent_tone = tone;
    }
    v->gain = MAKEUP * v->volume / 100.0;
    v->lead = 1;
    v->tail = 0.0;
    v->active = 0;
    /* unit.run_ahead: on with short pauses only (both Braille Lite voices are in the driver's RUN_AHEAD_TESTED); read
       by the host's next say, which runs the mode (run_ahead.h) -- nothing of it here */
    bh_set_int(v->host, "run_ahead", v->run_ahead && v->pack);
    if (v->fault) {                                  /* the explicit recovery: a cancel clears the host's fault */
        bh_cancel(v->host, 3.0, -1.0, -1.0);
        v->fault = 0;
    }
    v->n_mk = v->n_reached = 0;
    if (!marks || n_marks <= 0 || !blv_marks_exact(v))
        n_marks = 0;                                 /* (a caller asks blv_marks_exact first) */
    if (n_marks) {
        ords = (int *)malloc(sizeof(int) * (size_t)n_marks);
        if (n_marks > v->cap_mk) {
            void *q = realloc(v->mk, sizeof *v->mk * (size_t)n_marks);
            if (q) { v->mk = q; v->cap_mk = n_marks; }
        }
        if (!ords || n_marks > v->cap_mk)
            n_marks = 0;
    }
    nl = say_bytes(utf8, v->encoding, v->pack, v->numbers, v->commas, marks, n_marks, ords, &n06, &data, &len);
    /* each mark's echo, by its number: the unit echoes its Ctrl-Fs in the order it was sent them, the lines' own
       among them; one after the last word is reached at the utterance's end (ORD_END) */
    base = bh_get_int(v->host, "sent_f");
    for (i = 0; i < n_marks; i++) {
        v->mk[i].id = marks[i].id;
        v->mk[i].ord = ords[i] == 0 ? 0 : ords[i] > 0 ? base + ords[i] : ORD_END;
    }
    v->n_mk = n_marks;
    free(ords);
    if (nl) {
        /* the line-start lift (blv_set_line_lift): the first line of speech that interrupted other speech (or
           came first), as the unit lifts a line moved to; never speech queued behind other speech (say-all) */
        if (v->line_lift && (v->lift_next || (v->quiet_since >= 0.0 && blv_clock() - v->quiet_since >= LIFT_PAUSE_S)))
            bh_set_int(v->host, "line_lift", 1);
        v->lift_next = 0;
        v->quiet_since = -1.0;
        bh_set_int(v->host, "turbo_between_lines", v->pack);
        if (bh_say(v->host, data, len) < 0 && !blv_break_fault) {   /* refused: say so, never silently */
            v->fault = 1;
            v->n_mk = 0;
            free(data);
            return -1;
        }
        v->active = 1;
    }
    free(data);
    return nl;
}

BL_API int blv_render(bl_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0;
    *pcm = v->pcm;
    if (!v->active) {                                /* nothing to say: its marks are reached at once */
        report_marks(v, 0, 0, 1);
        *done = 1;
        return 0;
    }
    n = bh_run(v->host, BLOCK_S, 0.0005, &y);
    if (v->lead) {                                   /* _trim_lead */
        int k;
        for (k = 0; k < n; k++)
            if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) break;
        if (k < n) { start = k > LEAD_PREROLL ? k - LEAD_PREROLL : 0; v->lead = 0; }
        else start = n;
    }
    if (n - start > v->pcm_cap) {
        short *b = (short *)realloc(v->pcm, sizeof(short) * (size_t)(n - start));
        if (!b) { *done = 1; v->active = 0; return 0; }
        v->pcm = b;
        v->pcm_cap = n - start;
    }
    if (n > start) {
        double now = blv_clock();
        ssi_pcm16(y + start, n - start, v->gain, v->pcm);
        /* _feed: a device plays from the first block, in real time, each block after the one before */
        v->play_end = (v->play_end > now ? v->play_end : now) + (n - start) / v->out_rate;
    }
    *pcm = v->pcm;
    *done = 0;
    {
        int busy = bh_busy(v->host, 0.1, 3.0);
        if (busy < 0 && blv_break_fault)
            busy = 1;                                /* the control: the fault taken as busy, as before */
        if (busy < 0)                                /* a fault ends the utterance here, not silence until a cancel */
            v->fault = 1;
        if (busy == 0 && v->tail < TAIL_MAX_S) {
            /* _tail: the unit is done, but its last phoneme may still be decaying (the German unit ends 0.1 s after
               its last load, mid-decay: a blip, Tomi); render on until a block is quiet */
            int k;
            for (k = start; k < n; k++)
                if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) break;
            if (k < n) {
                v->tail += BLOCK_S;
                busy = 1;
            }
        }
        if (busy <= 0) {
            v->active = 0;
            *done = 1;
            /* quiet once the listener has heard it all (blazie.py), not once it is rendered: the callers render
               ahead of the ear (speech-dispatcher's module a whole message, voices.c eight blocks a mark), and a
               line read on was taken for one spoken into a pause and lifted (#77) */
            {
                double now = blv_clock();
                v->quiet_since = v->play_end > now ? v->play_end : now;
            }
        }
        report_marks(v, start, n - start, busy <= 0);
    }
    return n - start;
}

BL_API void blv_cancel(bl_voice *v)
{
    bh_cancel(v->host, 3.0, -1.0, -1.0);
    v->lift_next = 1;
    v->play_end = 0.0;                               /* the caller drops what it had */
    v->active = 0;
    if (blv_marks_break != 3)
        v->n_mk = v->n_reached = 0;                  /* no mark of a cancelled text is reported */
    v->fault = 0;
}

BL_API int blv_break_fault = 0;

BL_API int blv_fault(const bl_voice *v)
{
    return v->fault;
}
