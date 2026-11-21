/* test_win_keys.c -- the Windows app's braille keys (win_keys.c) from its window messages, as Windows sends them.
 *
 *   alt_up      a dot down without Alt, up with Alt held (WM_SYSKEYUP): it is up -- no key left held -- and the next
 *               chord is sent as itself (#84: the up fell to Windows, the key stayed down, every chord was swallowed)
 *
 *     test_win_keys                exit 1 on any failure
 *     WIN_KEYS_BREAK=sysup         the up with Alt left to Windows, as it was -- must fail: alt_up
 */
#include <stdio.h>
#include <string.h>

#include "win_keys.h"

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %-10s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}

/* one message; returns the chord it sent (0: none), *held the keys then held */
static int msg(win_keys *k, int m, int bit, int *held)
{
    int chord = 0;
    if (wk_message(k, m, bit, held, &chord) < 0)
        return 0;                                /* Windows' (the menu): nothing for the unit */
    return chord;
}

int main(void)
{
    win_keys k;
    int held = 0, c1, c2;
    char d[200];
    memset(&k, 0, sizeof k);
    wk_reset(&k);
    msg(&k, WK_KEYDOWN, CHORD_SPACE, &held);     /* the space bar down, without Alt */
    msg(&k, WK_SYSKEYUP, CHORD_SPACE, &held);    /* ... up with Alt held */
    c1 = held;
    msg(&k, WK_KEYDOWN, CHORD_DOT(1), &held);    /* then dot 1, a chord of its own */
    c2 = msg(&k, WK_KEYUP, CHORD_DOT(1), &held);
    snprintf(d, sizeof d, "space up with Alt held: keys held after it %02X; dot 1 then typed: chord %02X sent", c1, c2);
    check("alt_up", c1 == 0 && c2 == CHORD_DOT(1), d);
    printf(failures ? "win keys: %d FAILED\n" : "win keys: all ok\n", failures);
    return failures ? 1 : 0;
}
