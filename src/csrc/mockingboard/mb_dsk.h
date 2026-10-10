/* mb_dsk.h -- a Mockingboard firmware file (mb_host.h: MB_FILE, MB_FILE_EARLY) from a disk image the user owns, a
 * 140 KB Apple II DOS 3.3 image (.dsk / .do in DOS order, .po in ProDOS order):
 *   Sweet Micro Systems' Mockingboard Developers Toolkit (1984; also "Mockingboard - Developer's Toolkit" and
 *     "MNBTOOLKIT for IIc"): text-to-speech 1.1, its six files (TEXT TO SPEECH, INFLECTION, IIE TTS DRIVER,
 *     MKB:RULE.INDEX, MKB:RULE.LENGTH, MKB:RULE.TABLE) -> MB_FILE;
 *   the Mockingboard disks 1 and 2 (the same files on both): the earlier text-to-speech, its four (TEXT TO SPEECH at
 *     6600h, MKB:RULE.INDEX, MKB:RULE.LENGTH, MKB:RULE.TABLE) -> MB_FILE_EARLY.
 * Its catalog is read, the files taken exactly as DOS stores them, and the result accepted only when it is a known set
 * (mbh_variant): any intact copy of such a disk works, whatever else it holds.  Nothing else of the disk is taken --
 * not Apple's DOS, nor any other program on it.  As tools/mockingboard_firmware.py.  MIT.
 */
#ifndef MB_DSK_H
#define MB_DSK_H

#include <stddef.h>
#include "mb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MB_DSK_SIZE 143360                 /* 35 tracks x 16 sectors x 256 bytes */

/* 1 and the firmware file in *out (malloc'd, *out_n bytes; free() it) when the image holds a known set -- which one,
   mbh_variant(*out, *out_n) says, and its name mbh_variant_file; 0 with the reason in err otherwise (not a DOS 3.3
   image, a file missing, not a known version). */
MB_API int mb_firmware_from_dsk(const unsigned char *image, size_t n, unsigned char **out, size_t *out_n, char *err,
                                int errlen);

#ifdef __cplusplus
}
#endif
#endif
