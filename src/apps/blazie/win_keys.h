/* win_keys.h -- the Windows app's braille keys from its window messages, as chords (chords.h) and keys held
 * (emu_keys_down).  Portable C: main_win.c gives it the message's kind and the key's bit (its keymap), and sends
 * what it returns; test_win_keys.c drives it as Windows would.
 *
 * A key that goes down without Alt and comes up with Alt held (the thumb still on the space bar while the other goes
 * to Alt for the menu) comes up as WM_SYSKEYUP: it is an up like any other (#84).  Alt with a key going down is the
 * window's (its menu), never the unit's.
 */
#ifndef BLAZIE_WIN_KEYS_H
#define BLAZIE_WIN_KEYS_H

#include "chords.h"

enum { WK_KEYDOWN, WK_KEYUP, WK_SYSKEYDOWN, WK_SYSKEYUP };

typedef struct {
    chord_state chord;
} win_keys;

void wk_reset(win_keys *k);
/* one message for a key of the unit (bit: its chords.h bit): -1 when it is not the unit's (left to Windows), else 1
   with *held the keys held now (for emu_keys_down) and *chord the chord to send (0: none yet) */
int wk_message(win_keys *k, int msg, int bit, int *held, int *chord);

#endif
