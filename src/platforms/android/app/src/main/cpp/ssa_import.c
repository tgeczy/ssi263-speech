/* ssa_import.c -- see ssa_import.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blazie/bl_firmware.h"        /* blv_sha256 */
#include "mockingboard/mb_host.h"      /* MB_SHA256 */
#include "mockingboard/mb_dsk.h"       /* mb_firmware_from_dsk, MB_DSK_SIZE */
#include "soundingboard/sb_host.h"     /* SBH_SBLOAD_SHA256, SBH_SB_SHA256 */
#include "smartalk/st_host.h"          /* STH_SHA256 */
#include "synphonix/sp_host.h"         /* SPH_BANNER, sph_card_key */
#ifdef SSV_HAVE_SUPERVOICE
#include "supervoice/sv_dsk.h"         /* sv_firmware_from */
#endif
#include "ssa_engine.h"                /* ssa_voice_built */
#include "ssa_import.h"

#define MB_SIZE 11948L                 /* the Mockingboard's 1.1 file: the toolkit's six DOS files back to back */
#define MB_SIZE_EARLY 11309L           /* the earlier file: disk 1's four */
#define MAX_HEX (4L << 20)             /* the firmware is 78 KB of text; 1 MB of ROM is under 3 MB of it */

int ssa_import_break = 0;

static int hexval(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* One line (no line ending) as an Intel HEX record: 1 when well-formed with a right checksum, its type in *type;
   0 when it is not a record at all (no ':' or not hex); -1 when it is one but damaged (its length or checksum). */
static int record(const unsigned char *p, long len, int *type)
{
    long i;
    int sum = 0, count;
    if (len < 11 || p[0] != ':') return 0;
    for (i = 1; i < len; i++)
        if (hexval(p[i]) < 0) return 0;
    if ((len - 1) % 2) return -1;
    for (i = 1; i < len; i += 2)
        sum += hexval(p[i]) * 16 + hexval(p[i + 1]);
    count = hexval(p[1]) * 16 + hexval(p[2]);
    if (len != 11 + 2L * count || (sum & 0xFF)) return -1;
    *type = hexval(p[7]) * 16 + hexval(p[8]);
    return 1;
}

static int fail(char *msg, int msglen, int code, const char *text)
{
    snprintf(msg, (size_t)msglen, "%s", text);
    return code;
}

int ssa_import_soundingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    static const char HEXD[] = "0123456789abcdef";
    unsigned char digest[32];
    char got[65];
    int i, which;
    FILE *f;
    if (!data || n <= 0 || n > 65536)                  /* .COM files: under 64 KB */
        return BLV_FW_NONE;
    blv_sha256(data, n, digest);
    for (i = 0; i < 32; i++) {
        got[2 * i] = HEXD[digest[i] >> 4];
        got[2 * i + 1] = HEXD[digest[i] & 15];
    }
    got[64] = 0;
    which = !strcmp(got, SBH_SBLOAD_SHA256) ? SSA_FW_SBLOAD : !strcmp(got, SBH_SB_SHA256) ? SSA_FW_SB : BLV_FW_NONE;
    if (which == BLV_FW_NONE || ssa_import_break == 6)  /* the control: never known (ssa_import.h) */
        return BLV_FW_NONE;
    if (out) {
        if (!(f = fopen(out, "wb")) || fwrite(data, 1, (size_t)n, f) != (size_t)n) {
            if (f) fclose(f);
            remove(out);
            return fail(msg, msglen, BLV_FW_WRITE, "could not write the Sounding Board's driver");
        }
        fclose(f);
    }
    snprintf(msg, (size_t)msglen, "%s", which == SSA_FW_SBLOAD ? SSA_SBLOAD_LABEL : SSA_SB_LABEL);
    return which;
}

int ssa_import_smartalk(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    static const char HEXD[] = "0123456789abcdef";
    unsigned char digest[32];
    char got[65];
    int i;
    FILE *f;
    if (!data || n <= 0 || n > 65536)                  /* S_TALK.EXE is 36,882 bytes */
        return BLV_FW_NONE;
    blv_sha256(data, n, digest);
    for (i = 0; i < 32; i++) {
        got[2 * i] = HEXD[digest[i] >> 4];
        got[2 * i + 1] = HEXD[digest[i] & 15];
    }
    got[64] = 0;
    if (strcmp(got, STH_SHA256) || ssa_import_break == 7)  /* the control: never known (ssa_import.h) */
        return BLV_FW_NONE;
    if (out) {
        if (!(f = fopen(out, "wb")) || fwrite(data, 1, (size_t)n, f) != (size_t)n) {
            if (f) fclose(f);
            remove(out);
            return fail(msg, msglen, BLV_FW_WRITE, "could not write the SmarTalk's S_TALK.EXE");
        }
        fclose(f);
    }
    snprintf(msg, (size_t)msglen, "%s", SSA_SMARTALK_LABEL);
    return SSA_SMARTALK;
}

int ssa_import_synphonix(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
#ifdef SSV_HAVE_SYNPHONIX
    static const char BANNER[] = SPH_BANNER;
    unsigned char key[6];
    char why[160];
    long i, b = (long)sizeof BANNER - 1;
    int found = 0;
    FILE *f;
    if (!data || n <= b || n > 65536)                  /* VEST.COM 1.60 is 41,920 bytes */
        return BLV_FW_NONE;
    for (i = 0; i + b <= n && !found; i++)             /* its banner ... */
        found = !memcmp(data + i, BANNER, (size_t)b);
    /* ... and its serial check, as the host finds it to give the card its key: any copy, whatever its serial */
    if (!found || sph_card_key(data, (size_t)n, key, why, sizeof why) != 0 || ssa_import_break == 8)
        return BLV_FW_NONE;            /* (8: the control, never known: ssa_import.h) */
    if (out) {
        if (!(f = fopen(out, "wb")) || fwrite(data, 1, (size_t)n, f) != (size_t)n) {
            if (f) fclose(f);
            remove(out);
            return fail(msg, msglen, BLV_FW_WRITE, "could not write the SynPhonix's VEST.COM");
        }
        fclose(f);
    }
    snprintf(msg, (size_t)msglen, "%s", SSA_SYNPHONIX_LABEL);
    return SSA_SYNPHONIX;
#else
    (void)data; (void)n; (void)out; (void)msg; (void)msglen;
    return BLV_FW_NONE;
#endif
}

int ssa_import_supervoice(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
#ifdef SSV_HAVE_SUPERVOICE
    unsigned char *prog = NULL;
    size_t prog_n = 0;
    char why[200];
    FILE *f;
    /* the program (10,235 bytes) or a Disk BASIC image of 35 to 80 tracks: nothing larger is looked at */
    if (!data || n <= 0 || n > 80L * 18 * 256 || ssa_import_break == 9)   /* (9: the control, never known) */
        return BLV_FW_NONE;
    if (!sv_firmware_from(data, (size_t)n, &prog, &prog_n, why, sizeof why))
        return BLV_FW_NONE;
    if (out) {
        if (!(f = fopen(out, "wb")) || fwrite(prog, 1, prog_n, f) != prog_n) {
            if (f) fclose(f);
            remove(out);
            sv_free(prog);
            return fail(msg, msglen, BLV_FW_WRITE, "could not write the Super Voice's SUPERTOK.BIN");
        }
        fclose(f);
    }
    sv_free(prog);
    snprintf(msg, (size_t)msglen, "%s", SSA_SUPERVOICE_LABEL);
    return SSA_SUPERVOICE;
#else
    (void)data; (void)n; (void)out; (void)msg; (void)msglen;
    return BLV_FW_NONE;
#endif
}

int ssa_import_speakout(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    static const char HEXD[] = "0123456789abcdef";
    unsigned char *canon, digest[32];
    char want[65], got[65];
    long at = 0, k = 0, line = 0;
    int ended = 0, i;
    FILE *f;
    if (out) remove(out);
    if (!data || n <= 0 || n > MAX_HEX)
        return fail(msg, msglen, SSA_HEX_NONE, "not the Speak-Out's firmware: not an Intel HEX file");
    canon = (unsigned char *)malloc((size_t)n + (size_t)n / 8 + 4);       /* room for a CR on every line */
    if (!canon) return fail(msg, msglen, SSA_HEX_WRITE, "out of memory");
    while (at < n) {
        long end = at, len;
        int type = -1, r;
        while (end < n && data[end] != '\n') end++;
        len = end - at;
        if (end >= n && len && data[at + len - 1] == 0x1A) len--;      /* DOS's end-of-file mark at the very end */
        if (len && data[at + len - 1] == '\r') len--;
        line++;
        if (len == 0 || (len == 1 && data[at] == 0x1A)) {
            /* a blank line, or DOS's end-of-file mark: only after the last record */
            if (!ended && k) { free(canon); return fail(msg, msglen, SSA_HEX_OTHER,
                "an Intel HEX file, but damaged: a gap before its end-of-file record"); }
            at = end + 1;
            continue;
        }
        r = record(data + at, len, &type);
        if (r == 0 && k == 0) { free(canon); return fail(msg, msglen, SSA_HEX_NONE,
            "not the Speak-Out's firmware: not an Intel HEX file"); }
        if (r == 0 || ended) {
            free(canon);
            snprintf(msg, (size_t)msglen, "an Intel HEX file, but damaged: line %ld is not one of its records", line);
            return SSA_HEX_OTHER;
        }
        if (r < 0) {
            free(canon);
            snprintf(msg, (size_t)msglen, "an Intel HEX file, but damaged: line %ld's checksum or length is wrong",
                     line);
            return SSA_HEX_OTHER;
        }
        memcpy(canon + k, data + at, (size_t)len);
        k += len;
        canon[k++] = '\r';
        canon[k++] = '\n';
        if (type == 1) ended = 1;      /* the end-of-file record */
        at = end + 1;
    }
    if (!ended) { free(canon); return fail(msg, msglen, SSA_HEX_OTHER,
        "an Intel HEX file, but damaged: it stops before its end-of-file record (cut short)"); }
    blv_sha256(canon, k, digest);
    for (i = 0; i < 32; i++) { got[2 * i] = HEXD[digest[i] >> 4]; got[2 * i + 1] = HEXD[digest[i] & 15]; }
    got[64] = 0;
    strcpy(want, SSA_SPEAKOUT_SHA256);
    if (strcmp(got, want) && ssa_import_break != 1) {
        free(canon);
        snprintf(msg, (size_t)msglen, "an Intel HEX file, but not GW Micro's SPEAKOUT.HEX: its contents are not the "
                 "Speak-Out firmware this app knows (sha256 %.12s..., not %.12s...)", got, want);
        return SSA_HEX_OTHER;
    }
    if (!out || !(f = fopen(out, "wb"))) { free(canon); return fail(msg, msglen, SSA_HEX_WRITE, "cannot write it"); }
    if (fwrite(canon, 1, (size_t)k, f) != (size_t)k) {
        fclose(f);
        remove(out);
        free(canon);
        return fail(msg, msglen, SSA_HEX_WRITE, "cannot write it");
    }
    fclose(f);
    free(canon);
    return fail(msg, msglen, SSA_HEX_SPEAKOUT, SSA_SPEAKOUT_LABEL);
}

/* A known file (mbh_variant: MBH_V11 or MBH_VEARLY), written to out as it is; the voice's index (SSA_FW_MOCKINGBOARD,
   SSA_FW_MOCKINGBOARD_EARLY) and its label. */
static int write_mockingboard(int variant, const unsigned char *data, size_t n, const char *out, char *msg,
                              int msglen)
{
    int voice = variant == MBH_VEARLY ? SSA_FW_MOCKINGBOARD_EARLY : SSA_FW_MOCKINGBOARD;
    FILE *f;
    if (!ssa_voice_built(voice))
        return fail(msg, msglen, SSA_FW_MB_BUILD,
                    "the Mockingboard's firmware, but this copy of the app has no Mockingboard voice");
    if (!out || !(f = fopen(out, "wb"))) return fail(msg, msglen, BLV_FW_WRITE, "cannot write it");
    if (fwrite(data, 1, n, f) != n) {
        fclose(f);
        remove(out);
        return fail(msg, msglen, BLV_FW_WRITE, "cannot write it");
    }
    fclose(f);
    return fail(msg, msglen, voice, voice == SSA_FW_MOCKINGBOARD_EARLY ? SSA_MOCKINGBOARD_EARLY_LABEL
                                                                       : SSA_MOCKINGBOARD_LABEL);
}

#ifdef SSV_HAVE_MOCKINGBOARD
/* A 140 KB disk image: a known set of files out of it (mb_dsk.h) -- the toolkit's 1.1, or disk 1's earlier one. */
static int from_disk(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    unsigned char *fw;
    size_t fn;
    char why[200];
    int r;
    if (!mb_firmware_from_dsk(data, (size_t)n, &fw, &fn, why, (int)sizeof why))
        return fail(msg, msglen, SSA_FW_MB_DISK, why);
    r = write_mockingboard(mbh_variant(fw, fn), fw, fn, out, msg, msglen);
    free(fw);
    return r;
}
#endif

int ssa_import_mockingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
#ifdef SSV_HAVE_MOCKINGBOARD
    int variant;
    if (out) remove(out);
    if (data && n == MB_DSK_SIZE && ssa_import_break != 2)
        return from_disk(data, n, out, msg, msglen);
    if (!data || (n != MB_SIZE && n != MB_SIZE_EARLY))
        return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
    variant = ssa_import_break == 1 ? (n == MB_SIZE ? MBH_V11 : MBH_VEARLY)  /* the control: any file of its size */
            : mbh_variant(data, (size_t)n);
    if (!variant)
        return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
    return write_mockingboard(variant, data, (size_t)n, out, msg, msglen);
#else
    (void)data; (void)n; (void)write_mockingboard;
    if (out) remove(out);
    return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
#endif
}

/* bl_firmware.h's languages, by the app's voice: one table both ways */
static const struct { int language, voice; } BLAZIE[] = {
    {BLV_FW_ENGLISH, SSA_ENGLISH}, {BLV_FW_SPANISH, SSA_SPANISH}, {BLV_FW_GERMAN, SSA_GERMAN},
    {BLV_FW_ITALIAN, SSA_ITALIAN}, {BLV_FW_SLOVAK, SSA_SLOVAK},
};
#define N_BLAZIE ((int)(sizeof BLAZIE / sizeof *BLAZIE))

int ssa_blazie_voice(int language)
{
    int i;
    for (i = 0; i < N_BLAZIE; i++)
        if (BLAZIE[i].language == language) return BLAZIE[i].voice;
    return -1;
}

int ssa_blazie_recipe(int voice)
{
    int i;
    for (i = 0; i < N_BLAZIE; i++)
        if (BLAZIE[i].voice == voice) return blv_firmware_recipe(BLAZIE[i].language);
    return -1;
}

int ssa_import_voice(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    int r = ssa_import_firmware(data, n, out, msg, msglen), k, v;
    if (r != BLV_FW_UNKNOWN || ssa_import_break == 4)
        return r;                      /* the control: the release table's voices never tried */
    k = blv_import_release(data, n, out, msg, msglen);
    if (k < 0)
        return k;
    v = ssa_release_voice(k);
    if (v >= 0)
        return v;
    if (ssa_import_break == 5) {       /* the control: dropped, as before 0.8.5 */
        if (out) remove(out);
        snprintf(msg, (size_t)msglen, "%s: the emulator's, not yet a voice", blv_firmware_label(k));
        return BLV_FW_UNKNOWN;
    }
    /* a release no voice speaks yet: kept for the Braille Lite unit screen alone */
    snprintf(msg, (size_t)msglen, "%s", blv_firmware_label(k));
    return SSA_FW_RELEASE + k;
}

int ssa_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    char disk[256];
    int r = blv_import_firmware(data, n, out, msg, msglen), s;
    if (r >= 0) {                      /* a Blazie release: its language as the app's voice */
        if (ssa_import_break == 3)
            return r;                  /* the control: the raw language, as before the table (ssa_import.h) */
        s = ssa_blazie_voice(r);
        if (s < 0) {                   /* a language bl_firmware.c knows and this app has no voice for */
            if (out) remove(out);
            snprintf(msg, (size_t)msglen, "Blazie firmware this app has no voice for (language %d)", r);
            return BLV_FW_REFUSED;
        }
        return s;
    }
    if (r == BLV_FW_NO_VOICE)          /* the emulator's only: to the phone's voices, a release it does not know */
        return BLV_FW_UNKNOWN;
    if (r == BLV_FW_NONE)
        r = ssa_import_mockingboard(data, n, out, msg, msglen);
    if (r == BLV_FW_NONE && ssa_voice_built(SSA_SOUNDINGBOARD))  /* the Sounding Board's two drivers (0.8.5) */
        r = ssa_import_soundingboard(data, n, out, msg, msglen);
    if (r == BLV_FW_NONE && ssa_voice_built(SSA_SMARTALK))       /* the SmarTalk's S_TALK.EXE */
        r = ssa_import_smartalk(data, n, out, msg, msglen);
    if (r == BLV_FW_NONE && ssa_voice_built(SSA_SYNPHONIX))      /* the SynPhonix's VEST.COM (0.9.5) */
        r = ssa_import_synphonix(data, n, out, msg, msglen);
    if (r == BLV_FW_NONE && ssa_voice_built(SSA_SUPERVOICE))     /* the Super Voice's SUPERTOK.BIN or its disk (0.9.5) */
        r = ssa_import_supervoice(data, n, out, msg, msglen);
    if (r == BLV_FW_NONE)
        return ssa_import_speakout(data, n, out, msg, msglen);
    if (r != SSA_FW_MB_DISK)
        return r;
    /* a 140 KB file that is not the toolkit: the Speak-Out's after all, or refused with the disk reader's reason */
    snprintf(disk, sizeof disk, "%s", msg);
    s = ssa_import_speakout(data, n, out, msg, msglen);
    if (s != BLV_FW_NONE)
        return s;
    snprintf(msg, (size_t)msglen, "%s", disk);
    return r;
}
