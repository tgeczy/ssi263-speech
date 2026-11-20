/* accent_text.h -- what the two Aicom voices share: nvda/accent/synthDrivers/accentmini.py's text preparation and
 * settings mapping (one driver speaks both the Accent-PC and the Accent-mini), and the Accent's ESC command syntax as
 * the two Python hosts read it (src/hosts/accent.py and accent_sa.py, the same _ESC).  Used by accentsa/as_voice.c
 * and accentmini/am_voice.c, am_host.c.  Each function names the Python it ports; change them together.  Needs
 * numwords.c.  MIT.
 */
#ifndef SSI263_ACCENT_TEXT_H
#define SSI263_ACCENT_TEXT_H

#include "marks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the driver's DEFAULTS = ("5", 5, 5, 0): rate, pitch, voice, intonation after power-up */
#define AT_DEF_RATE '5'
#define AT_DEF_PITCH 5
#define AT_DEF_VOICE 5
#define AT_DEF_INFL 0

/* _accent_pitch: NVDA's pitch 0-100 on ESC P 0-9 (50 -> 5) */
int at_pitch_step(int pitch);
/* _accent_settings' rate: NVDA's rate 0-100 on ESC R 0-9, A-H (50 -> '5'), the character sent */
char at_rate_char(int rate);
/* _accent_settings' inflection: NVDA's 0-100 on ESC M (INFLECTION, the nearest; the first of a tie) */
int at_inflection(int inflection);

/* The text the driver sends for one text item, without its "\r": _clean(currencies(text)).strip(), then _numbers()
   when numbers is on.  malloc'd; NULL when out of memory. */
char *at_say_text(const char *utf8, int numbers);
/* The same text with the Accents' index mark, Ctrl-F (06h), for each word one of the caller's marks stands before
   (marks.h).  where: AT_MARK_FRONT, at the front of that word, after the space before it (the Accent-PC:
   as_voice.h for what the unit does with it); AT_MARK_AFTER, directly after the last word before it that has a
   letter or a digit, its punctuation included (the Accent-mini: am_voice.h; two Ctrl-Fs with nothing spoken
   between them cost that unit 60 ms, so a dash alone carries none).  The text is
   prepared whole, as at_say_text prepares it; a mark inside a word, or inside a number or an amount that is
   rewritten, goes to its start; marks on one word are ONE Ctrl-F (N of them with no word between speak as N-1
   carriage returns there: measured).  seqs (n_marks of them): for each mark 0 (before the first word: reached at
   once), -1 (after the last word) or the number, from 1, of its Ctrl-F among those in the text.  *n06: how many
   Ctrl-Fs the text has.  brk: a test's control, 0 in use (2: a Ctrl-F for every mark; 4: inside the word, after
   its first letter).  malloc'd, NUL-terminated; NULL or "" when there is nothing to say (every seq 0). */
#define AT_MARK_FRONT 0
#define AT_MARK_AFTER 1
char *at_say_marked(const char *utf8, int numbers, const ssv_mark *marks, int n_marks, int *seqs, int *n06, int where,
                    int brk);

/* accent.py's / accent_sa.py's say(speech=None) on Latin-1 bytes: 1 if a letter or digit (str.isalnum) is left
   outside the ESC commands (_ESC). */
int at_is_speech(const unsigned char *bytes, int n);

#ifdef __cplusplus
}
#endif
#endif
