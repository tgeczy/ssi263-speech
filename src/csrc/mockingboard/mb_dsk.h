/* mb_dsk.h -- the Mockingboard's firmware file (mb_host.h: MB_FILE) from a disk image the user owns: Sweet Micro
 * Systems' Mockingboard Developers Toolkit as a 140 KB Apple II DOS 3.3 image (.dsk / .do in DOS order, .po in ProDOS
 * order).  Its catalog is read, the six files the voice runs are taken exactly as DOS stores them (TEXT TO SPEECH,
 * INFLECTION, IIE TTS DRIVER, MKB:RULE.INDEX, MKB:RULE.LENGTH, MKB:RULE.TABLE), and the result is accepted only when
 * it is the known set (MB_SHA256): any intact copy of that disk works, whatever else it holds.  Nothing else of the
 * disk is taken -- not Apple's DOS, nor any other program on it.  As tools/mockingboard_firmware.py.  MIT.
 */
#ifndef MB_DSK_H
#define MB_DSK_H

#include <stddef.h>
#include "mb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MB_DSK_SIZE 143360                 /* 35 tracks x 16 sectors x 256 bytes */

/* 1 and the firmware file in *out (malloc'd, *out_n bytes; free() it) when the image holds the known set; 0 with
   the reason in err otherwise (not a DOS 3.3 image, a file missing, not the known version). */
MB_API int mb_firmware_from_dsk(const unsigned char *image, size_t n, unsigned char **out, size_t *out_n, char *err,
                                int errlen);

#ifdef __cplusplus
}
#endif
#endif
