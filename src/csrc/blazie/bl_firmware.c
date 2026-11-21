/* bl_firmware.c -- see bl_firmware.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bl_board.h"
#include "bl_firmware.h"
#include "bl_fopen.h"
#include "bl_state.h"
#include "bl_voice.h"                      /* BLV_LATIN1 .. BLV_CP866: a release's text encoding */

#define FILE_ROM_OFFSET 0x3000             /* bl_board.c's: where bl_create reads the image from */
#define ROM_SIZE 0x40000

BL_API int blv_firmware_break = 0;

/* The list: every Braille Lite release the import accepts and the emulator knows, by the sha256 of its image (the
   .BNS from 3000h, `length` bytes), with the sha256 of the state its recipe makes from it.  bl_languages.txt holds
   the rows, where each release came from and what each column means; gen_languages.py writes them here.  Each was
   booted through its recipe before it was listed (test_import_native.py makes each state and checks it against the
   hash, then speaks with it).  Every other image on Tomi's disks is another unit's: the Type 'n Speak, the Braille
   Lite 18 and 40 and the older Braille 'n Speaks are refused by bl_create's sites; the Braille 'n Speak 2000, which
   bl_create runs (bl_model: the emulator's), by its model unless its row names a voice.
   The state hashes are MAME-made (0.7, all-MAME: Tomi): the recipe on MAME's Z180, the same bytes on Windows x64,
   Linux x86_64 and aarch64 and the phone.  z180emu made other bytes (its SLP slice accounting moves the recipe's
   timing), so the desktop's shipped bl2_2003_warm.state / bl2spa_fresh.state, made on z180emu, are not these; they
   still run on MAME and stay the desktop goldens' inputs. */
typedef struct {
    const char *id;                        /* bl_languages.txt's: stable once shipped */
    int model;                             /* bl_board.h BL_MODEL_* */
    const char *tag;                       /* its language, BCP 47 */
    int language;                          /* a shipped voice's (BLV_FW_*), else BLV_FW_NONE */
    const char *voice_key;                 /* a new voice's key ("blazie_hu": voice id blazie:blazie_hu), or NULL */
    int shape;                             /* bl_state.h BLV_SHAPE_* */
    unsigned char yes;                     /* the chord that answers its setup */
    int code_page;                         /* 850, 852; 0 not yet read */
    int text;                              /* the voice's text encoding (bl_voice.h BLV_*) */
    int slow_boot;                         /* the voice's boot keys later and further apart (a slower firmware) */
    int echo;                              /* the unit echoes ^F at a line's end */
    int speaks;                            /* the voice path speaks it */
    int trillions;                         /* bl_firmware.h BLV_TRILLIONS_*: its reading of 13 digits or more */
    int marks;                             /* its own index marks measured usable (a Ctrl-F inside a line) */
    int commas;                            /* bl_firmware.h BLV_COMMAS_*: its reading of a comma that touches a digit */
    long length;
    const char *sha256;                    /* the image */
    const char *state;                     /* the state made from it */
    const char *name;                      /* the unit's name in the emulators' menus */
    const char *voice_name;                /* the voice's name, its language in English, or NULL */
    const char *file;                      /* the file name a person is expected to bring */
    const char *label;
} bl_release;
#include "bl_languages_table.h"
#define N_KNOWN ((int)(sizeof KNOWN / sizeof *KNOWN))

/* ---- sha256 (FIPS 180-4) -------------------------------------------------------------------------------------- */
static const unsigned long K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & 0xFFFFFFFFUL)

static void sha256_block(unsigned long h[8], const unsigned char *p)
{
    unsigned long w[64], a, b, c, d, e, f, g, hh, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned long)p[4 * i] << 24) | ((unsigned long)p[4 * i + 1] << 16) |
               ((unsigned long)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 64; i++) {
        unsigned long s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned long s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xFFFFFFFFUL;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        t1 = (hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i]) & 0xFFFFFFFFUL;
        t2 = ((ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) & 0xFFFFFFFFUL;
        hh = g; g = f; f = e; e = (d + t1) & 0xFFFFFFFFUL;
        d = c; c = b; b = a; a = (t1 + t2) & 0xFFFFFFFFUL;
    }
    h[0] = (h[0] + a) & 0xFFFFFFFFUL; h[1] = (h[1] + b) & 0xFFFFFFFFUL;
    h[2] = (h[2] + c) & 0xFFFFFFFFUL; h[3] = (h[3] + d) & 0xFFFFFFFFUL;
    h[4] = (h[4] + e) & 0xFFFFFFFFUL; h[5] = (h[5] + f) & 0xFFFFFFFFUL;
    h[6] = (h[6] + g) & 0xFFFFFFFFUL; h[7] = (h[7] + hh) & 0xFFFFFFFFUL;
}

BL_API void blv_sha256(const unsigned char *data, long n, unsigned char *out)
{
    unsigned long h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
                          0x5be0cd19};
    unsigned char tail[128];
    unsigned long long bits = (unsigned long long)n * 8;
    long i, rest;
    int k, pad;
    for (i = 0; i + 64 <= n; i += 64)
        sha256_block(h, data + i);
    rest = n - i;
    memcpy(tail, data + i, (size_t)rest);
    tail[rest] = 0x80;
    pad = rest < 56 ? 64 : 128;
    memset(tail + rest + 1, 0, (size_t)(pad - rest - 1));
    for (k = 0; k < 8; k++)
        tail[pad - 1 - k] = (unsigned char)(bits >> (8 * k));
    sha256_block(h, tail);
    if (pad == 128)
        sha256_block(h, tail + 64);
    for (k = 0; k < 8; k++) {
        out[4 * k] = (unsigned char)(h[k] >> 24);
        out[4 * k + 1] = (unsigned char)(h[k] >> 16);
        out[4 * k + 2] = (unsigned char)(h[k] >> 8);
        out[4 * k + 3] = (unsigned char)h[k];
    }
}

static int sha256_is(const unsigned char *data, long n, const char *hex)
{
    unsigned char d[32];
    char s[65];
    int k;
    blv_sha256(data, n, d);
    for (k = 0; k < 32; k++)
        sprintf(s + 2 * k, "%02x", d[k]);
    return strcmp(s, hex) == 0;
}

/* ---- the image ------------------------------------------------------------------------------------------------ */
BL_API long blv_find_image(const unsigned char *data, long n)
{
    static const char notice[] = "COPYRIGHT";
    long i;
    for (i = 0; i + 5 + (long)sizeof notice - 1 <= n; i++)
        if (data[i] == 0xF3 && data[i + 1] == 0xC3 && data[i + 4] == 0xFF &&
            memcmp(data + i + 5, notice, sizeof notice - 1) == 0)
            return i;
    return -1;
}

/* The release on the list whose image starts `image` (n bytes available), or -1. */
static int release_of(const unsigned char *image, long n)
{
    int k;
    for (k = 0; k < N_KNOWN; k++)
        if (n >= KNOWN[k].length && sha256_is(image, KNOWN[k].length, KNOWN[k].sha256))
            return k;
    return -1;
}

/* The image in data written to out_bns as a .BNS, when it is a release on the list that the board runs: its row, or
   BLV_FW_NONE, _WRITE, _REFUSED, _UNKNOWN with the reason in msg (out_bns removed); TAKEN under the control.  unit:
   for the emulator, every Braille 'n Speak 2000 release too (the voices' import takes the Slovak one alone). */
#define TAKEN (-100)
static int import_image(const unsigned char *data, long n, const char *out_bns, int unit, char *msg, int msglen)
{
    long at = blv_find_image(data, n), len;
    int k, ok;
    FILE *f;
    bl_unit *u;
    static const unsigned char zeros[FILE_ROM_OFFSET];
    if (at < 0) {
        snprintf(msg, msglen, "no Braille Lite firmware in it");
        return BLV_FW_NONE;
    }
    k = release_of(data + at, n - at);
    /* a program that embeds a release may carry more after it: only the release is written */
    len = k >= 0 ? KNOWN[k].length : n - at < ROM_SIZE ? n - at : ROM_SIZE;
    f = fopen(out_bns, "wb");
    if (!f) {
        snprintf(msg, msglen, "cannot write %s", out_bns);
        return BLV_FW_WRITE;
    }
    ok = fwrite(at == FILE_ROM_OFFSET ? data : zeros, 1, FILE_ROM_OFFSET, f) == FILE_ROM_OFFSET &&
         fwrite(data + at, 1, (size_t)len, f) == (size_t)len;
    if (fclose(f) != 0 || !ok) {
        remove(out_bns);
        snprintf(msg, msglen, "cannot write %s", out_bns);
        return BLV_FW_WRITE;
    }
    /* the voice's own check: bl_create refuses an image whose sites it cannot find (another Blazie unit); of the
       Braille 'n Speak 2000's, which the board runs (the emulator's), only the Slovak release is a voice (0.8); the
       Braille 'n Speak 640's listed releases are the emulator's (the voices' import then says so: BLV_FW_NO_VOICE) */
    u = bl_create(out_bns, NULL, 20.0, NULL, NULL, 0, msg, msglen);
    if (!u || (bl_model(u) != BL_MODEL_BRAILLE_LITE
               && !(bl_model(u) == BL_MODEL_BNS2000 && k >= 0 && (unit || KNOWN[k].language != BLV_FW_NONE))
               && !(bl_model(u) == BL_MODEL_BNS640 && k >= 0))) {
        bl_destroy(u);
        remove(out_bns);
        snprintf(msg, msglen, "Braille Lite firmware, but not a release this voice can run");
        return BLV_FW_REFUSED;
    }
    bl_destroy(u);
    if (k < 0 && blv_firmware_break && !unit) {  /* the control: the list dropped */
        snprintf(msg, msglen, "%s", "a release not on the list (the control)");
        return TAKEN;
    }
    if (k < 0) {
        /* the voice could start it, but it was never heard: another country's release may lay its memory out
           differently, and its state recipe is not known */
        remove(out_bns);
        snprintf(msg, msglen, "Braille Lite 2000 firmware, but not a release this app knows");
        return BLV_FW_UNKNOWN;
    }
    return k;
}

BL_API int blv_import_firmware(const unsigned char *data, long n, const char *out_bns, char *msg, int msglen)
{
    int k = import_image(data, n, out_bns, 0, msg, msglen);
    if (k == TAKEN)
        return BLV_FW_ENGLISH;
    if (k < 0)
        return k;
    if (KNOWN[k].language == BLV_FW_NONE && blv_firmware_break == 2) {   /* the control: handed out as English */
        snprintf(msg, msglen, "%s (the control: taken as English)", KNOWN[k].label);
        return BLV_FW_ENGLISH;
    }
    if (KNOWN[k].language == BLV_FW_NONE) {
        /* the emulator runs it, but no voice speaks its language yet */
        remove(out_bns);
        snprintf(msg, msglen, "%s: the emulator's, not yet a voice", KNOWN[k].label);
        return BLV_FW_NO_VOICE;
    }
    snprintf(msg, msglen, "%s", KNOWN[k].label);
    return KNOWN[k].language;
}

BL_API int blv_import_release(const unsigned char *data, long n, const char *out_bns, char *msg, int msglen)
{
    int k = import_image(data, n, out_bns, 1, msg, msglen);
    if (k >= 0)
        snprintf(msg, msglen, "%s", KNOWN[k].label);
    return k;
}

BL_API int blv_firmware_count(void)
{
    return N_KNOWN;
}

BL_API const char *blv_firmware_label(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].label : NULL;
}

BL_API int blv_firmware_language(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].language : -1;
}

BL_API const char *blv_release_id(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].id : NULL;
}

BL_API const char *blv_release_name(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].name : NULL;
}

BL_API const char *blv_release_voice_key(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].voice_key : NULL;
}

BL_API const char *blv_release_voice_name(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].voice_name : NULL;
}

BL_API const char *blv_release_file(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].file : NULL;
}

BL_API int blv_release_text(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].text : -1;
}

BL_API int blv_release_slow_boot(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].slow_boot : -1;
}

BL_API int blv_release_echo(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].echo : -1;
}

BL_API int blv_release_speaks(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].speaks : -1;
}

BL_API int blv_release_trillions(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].trillions : -1;
}

BL_API int blv_release_marks(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].marks : -1;
}

BL_API int blv_release_commas(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].commas : -1;
}

BL_API const char *blv_release_tag(int release){
    return release >= 0 && release < N_KNOWN ? KNOWN[release].tag : NULL;
}

BL_API int blv_release_model(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].model : -1;
}

BL_API int blv_release_code_page(int release)
{
    return release >= 0 && release < N_KNOWN ? KNOWN[release].code_page : -1;
}

BL_API int blv_make_release_state(const char *firmware, int release, const char *out_path,
                                  blv_state_progress progress, void *ctx, char *err, int errlen)
{
    if (release < 0 || release >= N_KNOWN) {
        snprintf(err, errlen, "no release %d on the list", release);
        return 0;
    }
    return blv_make_state_yes(firmware, KNOWN[release].shape, KNOWN[release].yes, out_path, progress, ctx, err,
                              errlen);
}

BL_API int blv_firmware_recipe(int language)
{
    return language == BLV_FW_ENGLISH || language == BLV_FW_SLOVAK ? BLV_STATE_ENGLISH
         : language == BLV_FW_SPANISH ? BLV_STATE_SPANISH
         : language == BLV_FW_GERMAN ? BLV_STATE_GERMAN
         : language == BLV_FW_ITALIAN ? BLV_STATE_ITALIAN : -1;
}

/* A whole file, malloc'd; NULL when it cannot be read or is larger than `cap`. */
static unsigned char *read_file(const char *path, long cap, long *n)
{
    FILE *f = bl_fopen(path, "rb");             /* a UTF-8 path, letters beyond ASCII too (#62) */
    unsigned char *p = NULL;
    long size;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && size <= cap && fseek(f, 0, SEEK_SET) == 0 &&
        (p = (unsigned char *)malloc((size_t)size + 1)) != NULL && fread(p, 1, (size_t)size, f) != (size_t)size) {
        free(p);
        p = NULL;
    }
    if (p)
        *n = size;
    fclose(f);
    return p;
}

BL_API int blv_state_check(const char *bns_path, const char *state_path)
{
    long bn = 0, sn = 0;
    unsigned char *bns = read_file(bns_path, FILE_ROM_OFFSET + ROM_SIZE, &bn);
    unsigned char *state = bns ? read_file(state_path, 4L << 20, &sn) : NULL;
    int k = bns && bn > FILE_ROM_OFFSET ? release_of(bns + FILE_ROM_OFFSET, bn - FILE_ROM_OFFSET) : -1;
    int ok = k >= 0 && state && sha256_is(state, sn, KNOWN[k].state);
    free(bns);
    free(state);
    return ok;
}

BL_API int blv_release_of(const char *bns_path)
{
    long n = 0;
    unsigned char *bns = read_file(bns_path, FILE_ROM_OFFSET + ROM_SIZE, &n);
    int k = bns && n > FILE_ROM_OFFSET ? release_of(bns + FILE_ROM_OFFSET, n - FILE_ROM_OFFSET) : -1;
    free(bns);
    return k;
}
