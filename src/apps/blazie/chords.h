/* chords.h -- a braille chord from separate key presses, as a Perkins-style keyboard gives it.
 *
 * Keys go down one by one; the chord is every key that was down at any moment since the first one went down, and it
 * is sent when the last one comes up.  Auto-repeat (a key reported down again while down) changes nothing.  Portable:
 * the platform shell maps its own key codes to bits (chord_map_*), this file knows nothing of any keyboard.
 */
#ifndef BLAZIE_CHORDS_H
#define BLAZIE_CHORDS_H

/* the Braille Lite's port 40h bits: dot 1 = bit 0 .. dot 6 = bit 5, space = bit 6; and its two advance bars, which
   the firmware reads elsewhere (emu_unit.h EMU_ADVANCE, EMU_BACK: the same bits) */
#define CHORD_DOT(n) (1 << ((n) - 1))
#define CHORD_SPACE 0x40
#define CHORD_ADVANCE 0x80
#define CHORD_BACK 0x100
#define CHORD_BARS (CHORD_ADVANCE | CHORD_BACK)

typedef struct {
    int down;                      /* bits held now */
    int seen;                      /* bits held at any moment since the chord began */
} chord_state;

void chord_reset(chord_state *s);
/* a key went down (bit: one of the bits above); auto-repeats are harmless */
void chord_down(chord_state *s, int bit);
/* a key came up: returns the finished chord when it was the last one held, else 0 */
int chord_up(chord_state *s, int bit);

#endif
