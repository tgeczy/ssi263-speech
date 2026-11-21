/* win_keys.c -- see win_keys.h. */
#include <stdlib.h>

#include "win_keys.h"

void wk_reset(win_keys *k)
{
    chord_reset(&k->chord);
    k->bars = 0;
}

int wk_message(win_keys *k, int msg, int bit, int *held, int *chord)
{
    *chord = 0;
    /* the key's up with Alt held is an up (#84: it fell to Windows, the key stayed down, and every chord after it was
       swallowed).  WIN_KEYS_BREAK=sysup, the test's control: left to Windows, as it was */
    if (msg == WK_SYSKEYDOWN || (msg == WK_SYSKEYUP && getenv("WIN_KEYS_BREAK")
                                 && getenv("WIN_KEYS_BREAK")[0] == 's'))
        return -1;
    /* a bar is no part of a chord: down while held, under the dots of a chord typed meanwhile -- the firmware's bar +
       chord command -- as bl_keys.c has it for the other shells.  It rode in the chord: nothing went while the bar was
       down, and bar and dots came up together as an ordinary chord (#85).  WIN_KEYS_BREAK=bars, the test's control:
       in the chord, as it was */
    if ((bit & CHORD_BARS) && !(getenv("WIN_KEYS_BREAK") && getenv("WIN_KEYS_BREAK")[0] == 'b')) {
        if (msg == WK_KEYDOWN)
            k->bars |= bit & CHORD_BARS;
        else
            k->bars &= ~(bit & CHORD_BARS);
        *held = k->chord.down | k->bars;
        return 1;
    }
    if (msg == WK_KEYDOWN)
        chord_down(&k->chord, bit);
    else
        *chord = chord_up(&k->chord, bit);
    *held = k->chord.down | k->bars;
    return 1;
}
