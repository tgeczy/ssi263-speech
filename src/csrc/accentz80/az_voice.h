/* az_voice.h -- the Accent SA as a voice: the Accent voice (../accentsa/as_voice.h -- its settings commands, number
 * and currency processing, a capital's pitch, the speak loop, cancel, index marks and the job API) on the stand-alone
 * box's own unit (az_host.h).  What comes back is an as_voice; everything after creation is asv_*.
 *
 * That the stand-alone's firmware takes the PC card's commands the same way is CHECKED, not assumed
 * (test_az_firmware.py: the settings, the flush, the index marks' prompts, a text done only when said).
 * The ROMs come from the caller: Aicom's U5 and U4, never built in.  Not thread-safe per voice.  MIT.
 */
#ifndef AZ_VOICE_H
#define AZ_VOICE_H

#include <stddef.h>
#include "../accentsa/as_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the unit from the ROMs and boots it as asv_create does (the greeting flushed).  NULL on failure, the reason
   in err. */
AS_API as_voice *azv_create(const unsigned char *u5, size_t n5, const unsigned char *u4, size_t n4, double out_rate,
                            char *err, int errlen);

#ifdef __cplusplus
}
#endif
#endif
