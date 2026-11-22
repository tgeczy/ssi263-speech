/* az_host.h -- the Accent SA as a unit: its Z80 board (az_board.h) under the Accent host (../accentpc/as_host.h).
 *
 * The stand-alone Accent runs the same firmware design as the Accent-PC card, on another processor and behind the
 * same serial line, so the host is the same one: its chip-time lockstep, say, busy, boot and cancel (as_host.h) ask
 * only what az_board gives.  What comes back is an as_host; everything after creation is ash_*.
 *
 * Its numbers:
 *   cpu_hz  8,000,000: the board's (Kevin Horton's drawing: the 8.000 MHz oscillator on the Z80's clock pin)
 *   turbo   8, the host's feature (as_host.h): while the firmware reads a sentence the Z80 runs that much faster
 *   the chip's clock: 1 MHz inside (2 MHz on its XCK pin, halved by the strapped DIV2 pin): the chip model's default
 *
 * The two ROMs come from the caller -- memory (azh_create) or a folder (azh_create_dir: AZH_U5_NAME, AZH_U4_NAME) --
 * and are never built in.  Not thread-safe per instance.  MIT.
 */
#ifndef AZ_HOST_H
#define AZ_HOST_H

#include <stddef.h>
#include "../accentpc/as_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AZH_U5_NAME "ACU5_V41.BIN"         /* firmware 4.1 (2 October 1996): the program */
#define AZH_U4_NAME "ACU4_V40.BIN"         /* the dictionary */

/* The unit, powered up and not yet run (ash_boot does that).  chip NULL: one made from the built-in defaults and
   freed with the host.  NULL on failure, the reason in err. */
AS_API as_host *azh_create(const unsigned char *u5, size_t n5, const unsigned char *u4, size_t n4, ssi263 *chip,
                           double out_rate, char *err, int errlen);
AS_API as_host *azh_create_dir(const char *rom_dir, ssi263 *chip, double out_rate, char *err, int errlen);

#ifdef __cplusplus
}
#endif
#endif
