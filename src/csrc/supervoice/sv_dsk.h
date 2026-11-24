/* sv_dsk.h -- the Super Voice's program (sv_host.h: SV_FILE, SUPERTOK.BIN) from the disk image a user brings: the
 * Color Computer Archive's SUPERV.DSK, a Disk BASIC (RS-DOS) image of 35 tracks of 18 sectors of 256 bytes (161,280
 * bytes; a 40- or 80-track image of the same layout too).
 *
 * The directory (track 17, sectors 3 to 11) is read for SUPERTOK.BIN, its granules followed through the allocation
 * table (track 17, sector 2), and the file taken exactly as Disk BASIC stores it: whole granules of nine sectors, the
 * last granule's sector count from its table entry (C1h to C9h) and the last sector's length from the directory.  The
 * result is accepted only when it is the known file (svh_is_known: its sha256), so any intact copy of the disk works
 * and nothing else of the disk is ever taken.  The program bytes alone are accepted too, as they are.  MIT.
 */
#ifndef SV_DSK_H
#define SV_DSK_H

#include <stddef.h>
#include "sv_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_DSK_SIZE 161280                 /* 35 tracks x 18 sectors x 256 bytes */
#define SV_DISK_FILE "SUPERV.DSK"          /* the name the Color Computer Archive gives the disk */
#define SV_DISK_SHA256 "7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784"

/* 1 and SV_FILE in *out (malloc'd, *out_n bytes; free() it) when `data` is the known SUPERTOK.BIN itself or a Disk
   BASIC image holding it; 0 with the reason in err otherwise (not such an image, no SUPERTOK.BIN on it, its chain
   broken, or not the known program). */
SV_API int sv_firmware_from(const unsigned char *data, size_t n, unsigned char **out, size_t *out_n, char *err,
                            int errlen);
SV_API void sv_free(void *p);              /* frees what sv_firmware_from gave (another runtime's free cannot) */

#ifdef __cplusplus
}
#endif
#endif
