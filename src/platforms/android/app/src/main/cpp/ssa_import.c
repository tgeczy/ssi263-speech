/* ssa_import.c -- see ssa_import.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blazie/bl_firmware.h"        /* blv_sha256 */
#include "mockingboard/mb_host.h"      /* MB_SHA256 */
#include "mockingboard/mb_dsk.h"       /* mb_firmware_from_dsk, MB_DSK_SIZE */
#include "ssa_engine.h"                /* ssa_voice_built */
#include "ssa_import.h"

#define MB_SIZE 11948L                 /* the Mockingboard's file: the toolkit's six DOS files back to back */
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

/* The known file, written to out as it is. */
static int write_mockingboard(const unsigned char *data, size_t n, const char *out, char *msg, int msglen)
{
    FILE *f;
    if (!ssa_voice_built(SSA_MOCKINGBOARD))
        return fail(msg, msglen, SSA_FW_MB_BUILD,
                    "the Mockingboard's firmware, but this copy of the app has no Mockingboard voice");
    if (!out || !(f = fopen(out, "wb"))) return fail(msg, msglen, BLV_FW_WRITE, "cannot write it");
    if (fwrite(data, 1, n, f) != n) {
        fclose(f);
        remove(out);
        return fail(msg, msglen, BLV_FW_WRITE, "cannot write it");
    }
    fclose(f);
    return fail(msg, msglen, SSA_FW_MOCKINGBOARD, SSA_MOCKINGBOARD_LABEL);
}

/* A 140 KB disk image: the toolkit's six files out of it (mb_dsk.h), the known set only. */
static int from_disk(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
#ifdef SSV_HAVE_MOCKINGBOARD
    unsigned char *fw;
    size_t fn;
    char why[200];
    int r;
    if (!mb_firmware_from_dsk(data, (size_t)n, &fw, &fn, why, (int)sizeof why))
        return fail(msg, msglen, SSA_FW_MB_DISK, why);
    r = write_mockingboard(fw, fn, out, msg, msglen);
    free(fw);
    return r;
#else
    (void)data; (void)n; (void)out;
    return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
#endif
}

int ssa_import_mockingboard(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    static const char HEXD[] = "0123456789abcdef";
    unsigned char digest[32];
    char got[65];
    int i;
    if (out) remove(out);
    if (data && n == MB_DSK_SIZE && ssa_import_break != 2)
        return from_disk(data, n, out, msg, msglen);
    if (!data || n != MB_SIZE)
        return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
    blv_sha256(data, n, digest);
    for (i = 0; i < 32; i++) { got[2 * i] = HEXD[digest[i] >> 4]; got[2 * i + 1] = HEXD[digest[i] & 15]; }
    got[64] = 0;
    if (strcmp(got, MB_SHA256) && ssa_import_break != 1)
        return fail(msg, msglen, BLV_FW_NONE, "not the Mockingboard's firmware");
    return write_mockingboard(data, (size_t)n, out, msg, msglen);
}

int ssa_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    char disk[256];
    int r = blv_import_firmware(data, n, out, msg, msglen), s;
    if (r == BLV_FW_NONE)
        r = ssa_import_mockingboard(data, n, out, msg, msglen);
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
