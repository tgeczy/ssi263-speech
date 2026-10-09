/* ssp_import.c -- see ssp_import.h. */
#include <stdio.h>
#include <string.h>

#include "blazie/bl_firmware.h"            /* blv_sha256 */
#include "ssa_import.h"
#include "ssp_import.h"

int ssp_import_break = 0;

/* firmware/AICOM.txt's files, by sha256 (check_apk_no_firmware.py's AICOM) */
static const struct {
    int found;
    const char *sha256, *file, *label;
} AICOM[] = {
    {SSP_FW_ACCENT_U2, "8d6aa48880d1efd02f8dc759741c16b8902db33dcc2856d1f0db4776149992ad", "u2.BIN",
     "Aicom Accent SA: u2.BIN, the 8085 program ROM"},
    {SSP_FW_ACCENT_U3, "248dd6fb6d08f6b4316ce38f83fd330e55dde80ead4b26a652ba228e6b46859a", "u3.BIN",
     "Aicom Accent SA: u3.BIN, the dictionary ROM, part 1"},
    {SSP_FW_ACCENT_U4, "7c12a8cf56a573cd83c928d61cd49e90b3afc810279e8ee54852d7b90042d155", "u4.BIN",
     "Aicom Accent SA: u4.BIN, the dictionary ROM, part 2"},
    {SSP_FW_ACCENT_MINI, "6b99fea4cc5534ee1f693b141df9d734057f116d0a36972e3949d86db00cbe79", "SPKEMS.DVC",
     "Aicom Accent-mini: SPKEMS.DVC, its DOS driver"},
};
#define NAICOM ((int)(sizeof AICOM / sizeof *AICOM))

const char *ssp_aicom_file(int found)
{
    for (int i = 0; i < NAICOM; i++)
        if (AICOM[i].found == found) return AICOM[i].file;
    return NULL;
}

int ssp_import_firmware(const unsigned char *data, long n, const char *out, char *msg, int msglen)
{
    if (!ssp_import_break && n > 0 && n <= 0x40000) {   /* the largest, SPKEMS.DVC, is 128 KB */
        unsigned char d[32];
        char hex[65];
        blv_sha256(data, n, d);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", d[i]);
        for (int i = 0; i < NAICOM; i++)
            if (!strcmp(hex, AICOM[i].sha256)) {
                FILE *f = fopen(out, "wb");
                int ok = f && fwrite(data, 1, (size_t)n, f) == (size_t)n;
                if (f && fclose(f)) ok = 0;
                if (!ok) {
                    remove(out);
                    snprintf(msg, (size_t)msglen, "%s could not be written", AICOM[i].file);
                    return SSA_HEX_WRITE;
                }
                snprintf(msg, (size_t)msglen, "%s", AICOM[i].label);
                return AICOM[i].found;
            }
    }
    return ssa_import_firmware(data, n, out, msg, msglen);
}
