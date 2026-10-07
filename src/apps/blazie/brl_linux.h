/* brl_linux.h -- the emulated Braille Lite's display on a real braille display, through BRLTTY's BrlAPI: the BT
 * Braille's own 40 cells, or any display BRLTTY drives (on a BT Speak, one plugged into it).
 *
 * BrlAPI's library (libbrlapi.so.0.8) is loaded when the program runs, not linked: where it is missing, or no
 * BRLTTY runs, or BRLTTY has no display, there is simply no braille.  While the unit runs the program holds the
 * display (BrlAPI's tty mode on the program's own console): the unit's 18 cells at its left, the rest blank.  From
 * BRLTTY it takes only the two commands that pan the display (FWINLT, FWINRT: whichever keys the user bound to
 * them), as the unit's back and advance bars -- every other key stays BRLTTY's.  On a BT Braille the panning keys
 * come through the keyboard server first (btkb_linux.h), held as long as they are held; these are the way in when
 * there is no keyboard server, or the display is not the device's own.
 */
#ifndef BLAZIE_BRL_LINUX_H
#define BLAZIE_BRL_LINUX_H

typedef struct {
    void *lib;                          /* libbrlapi, dlopen'd */
    int fd;                             /* the connection, -1 when none */
    int cols, rows;                     /* the display's size */
    int tty;                            /* the display held (tty mode) */
    unsigned char *shown;               /* what the display shows (cols * rows): again when it is held again */
} brl_out;

/* connects to BRLTTY and holds its display: 1 when there is a display, 0 with the reason in msg.  A brl_out never
   opened must be all zero (a static one is): the calls below then do nothing. */
int brl_open(brl_out *b, char *msg, int msglen);
void brl_close(brl_out *b);
/* the fd to poll for BRLTTY's panning commands (-1 when not open) */
int brl_fd(const brl_out *b);
/* shows the unit's cells (dot 1 = bit 0 .. dot 8 = bit 7) at the display's left, blank beyond; 0 if the connection
   was lost (then brl_close it) */
int brl_show(brl_out *b, const unsigned char *cells, int n);
/* BRLTTY's panning commands that came: *back, *advance counted; 0 if the connection was lost */
int brl_keys(brl_out *b, int *back, int *advance);
/* 0: the display given back to BRLTTY (the program's menu, read on the display as BRLTTY shows the console); 1: held
   again, and the last cells shown again */
void brl_hold(brl_out *b, int on);

/* the cells as a 40-cell display gets them: n cells at the left, `cols` in all (the tests) */
void brl_layout(const unsigned char *cells, int n, unsigned char *out, int cols);

#endif
