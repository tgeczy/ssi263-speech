/* az_voice.c -- the Accent voice on the Accent SA's unit (az_voice.h).  MIT. */
#include "az_voice.h"

#include "az_host.h"

static as_host *sa_unit(const unsigned char *const *rom, const size_t *n, double out_rate, char *err, int errlen)
{
    return azh_create(rom[0], n[0], rom[1], n[1], NULL, out_rate, err, errlen);
}

AS_API as_voice *azv_create(const unsigned char *u5, size_t n5, const unsigned char *u4, size_t n4, double out_rate,
                            char *err, int errlen)
{
    const unsigned char *rom[2];
    size_t n[2];
    rom[0] = u5;
    rom[1] = u4;
    n[0] = n5;
    n[1] = n4;
    return asv_create_unit(sa_unit, rom, n, 2, out_rate, err, errlen);
}
