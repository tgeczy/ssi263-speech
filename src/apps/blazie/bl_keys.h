/* bl_keys.h -- the Braille Lite's six keys, space bar and advance bar from whatever keyboard the shell has
 * (keys.h's events), as the unit's chords (chords.h bits) and its keys held (emu_keys_down).  Portable C.
 *
 * Three ways in, by what the keyboard can tell:
 *
 *   Each key goes down and up (an input device, evdev_linux.c): chords.c, as the Windows app -- the chord is every
 *   key down since the first, sent when the last comes up, and the keys held are reported as they move, so a chord
 *   held while the unit starts is read there (i-chord: the cold reset).
 *
 *   KEYS mode from a terminal (F D S / J K L, space, A or ;): a terminal says when a key goes down, never when it
 *   comes up.  So the keys typed within chord_s of each other are one chord, sent chord_s after the last; a
 *   key's auto-repeat (the same key again within repeat_s) is dropped, so holding a chord down sends it once.
 *
 *   LETTERS mode from a terminal (the BTSpeak's own braille keyboard, or a braille display's): the keyboard's
 *   driver (BRLTTY) has already made each chord a character in computer braille (US: North American Braille
 *   Computer Code), so each character is its cell at once: 'p' is dots 1 2 3 4, '4' dots 2 5 6.  Space is the space
 *   bar.  Chords with the space bar, which BRLTTY keeps for itself: the chord prefix key, then the character ("ctrl-c
 *   p" is p-chord); or, with capital_is_chord, a capital letter (dot 7 added: 'P' is p-chord); or one of the keys
 *   BRLTTY makes of a chord, mapped back (Up arrow is dot-1 chord, as the BTSpeak's own table: see bl_keys.c).
 *
 * The HOLD key (a terminal's keys, both modes): the next chord typed is held down instead of sent -- the unit sees it
 * held while it starts, as a hand would hold it -- until the hold key is pressed again, when it comes up (and is
 * sent as a chord, as the real keys coming up would be).  p-chord, hold, i-chord, l: the unit restarts with i-chord
 * held, asks "initialize file system?"; then hold again.
 */
#ifndef BLAZIE_BL_KEYS_H
#define BLAZIE_BL_KEYS_H

#include "keys.h"
#include "chords.h"

enum { BLK_KEYS, BLK_LETTERS };
enum { BLA_CHORD, BLA_HELD };         /* a chord for emu_key; the keys held now for emu_keys_down */

#define BLK_MAX_MAP 48
#define BLK_MAX_SPECIAL 48
#define BLK_MAX_ACTIONS 16

typedef struct { int type, bits; } bl_action;

typedef struct {
    /* the settings (blk_defaults, then the shell's settings file) */
    int mode;                           /* BLK_KEYS or BLK_LETTERS: a terminal's keys */
    key_combo map_keys[BLK_MAX_MAP];    /* each key a chord bit: dots, space, advance (keys mode, an input device) */
    int map_bits[BLK_MAX_MAP];
    int n_map;
    double chord_s, repeat_s;           /* keys mode's timing */
    key_combo hold[4];                  /* the hold key */
    int n_hold;
    key_combo prefix[4];                /* letters mode: the next character with the space bar */
    int n_prefix;
    int capital_is_chord;               /* letters mode: a capital letter is the letter's chord */
    key_combo special_keys[BLK_MAX_SPECIAL];   /* letters mode: a key that stands for a chord */
    int special_bits[BLK_MAX_SPECIAL];
    int n_special;

    /* the state */
    chord_state chord;                  /* keys going down and up (an input device) */
    int bars;                           /* the advance bars down now (an input device): no part of the chord */
    int pending;                        /* keys mode: the chord being typed */
    double pending_until;               /* when it is complete */
    int pending_repeat;                 /* an auto-repeat run: dropped */
    int last_key;                       /* keys mode: the last key typed, and when (auto-repeat) */
    double last_at;
    int prefixed;                       /* letters mode: the chord prefix was typed */
    int hold_armed;                     /* the hold key was pressed: the next chord is held */
    int held;                           /* the chord held by the hold key (0: none) */
    bl_action out[BLK_MAX_ACTIONS];
    int n_out;
} bl_keys;

/* the defaults: keys mode; F D S J K L = dots 1-6 (with a braille keyboard's own dot keys), space, ; (or dot 8) the
   advance bar, A (or dot 7) the back bar -- the Braille Lite 2000 has two (emu_unit.h EMU_ADVANCE); chord 80 ms, repeat 150 ms; hold F12 or Ctrl+K; letters mode's prefix Ctrl+C, capitals as chords, and the
   BTSpeak's keys for chords mapped back */
void blk_defaults(bl_keys *k);
/* settings from the shell's file: setting is the name ("dot1".."dot6", "space", "advance", "back", "hold", "chord_ms",
   "repeat_ms", "mode"; letters mode: "prefix", "capital_is_chord", or a key's name for a special: "up = 1-chord");
   0 if the name or value is not understood */
int blk_set(bl_keys *k, const char *setting, const char *value);
/* more keys for a bit, beside the ones it has ("advance", "code:261": a BT Braille's panning key); 0 if the setting
   is not a bit's */
int blk_add(bl_keys *k, const char *setting, const char *value);
/* a chord from its name: "p" (a cell, computer braille), "p-chord" (with the space bar), "1-chord", "2-5-6-chord",
   "dots 1 3", "space", "advance", "back"; -1 if not understood */
int blk_chord_of(const char *name);
/* a character's cell in computer braille (6 dots, NABCC; letters either case); -1 if it has none */
int blk_cell_of(int c);
/* the chord's name ("p-chord", "dots 1 3 5", "advance"), for the shell's messages; out holds at least 40 */
const char *blk_chord_name(int bits, char *out);

/* a key: 1 if it was the unit's (taken), 0 if not (the shell's own, or nothing); the unit's actions wait in out */
int blk_event(bl_keys *k, const key_event *e, double now);
/* the time passing: keys mode's chord sent when complete */
void blk_tick(bl_keys *k, double now);
/* when blk_tick has something to do; -1: nothing waits */
double blk_deadline(const bl_keys *k);
/* the actions waiting, in order, into out (at most cap); how many */
int blk_take(bl_keys *k, bl_action *out, int cap);
/* everything let go (the unit changed, the keyboard lost): a held chord comes up without being sent */
void blk_reset(bl_keys *k);
/* the hold key's chord now held (0: none; -1: armed, waiting for the chord) */
int blk_holding(const bl_keys *k);

/* the tests' control: nonzero swaps dots 1 and 4 in every mapping blk_set and blk_defaults make (BLAZIE_KEYS_BREAK) */
extern int blk_break;

#endif
