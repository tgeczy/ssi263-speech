/* btkb_linux.h -- the BT Speak's and the BT Braille's own keyboard (Blazie Technologies' notetakers), read from the
 * device's keyboard server, where each key goes down and comes up: the Braille Lite's chords as the Windows app has
 * them, keys held included.
 *
 * The server: /run/BTSpeak/keyboard-socket, a Unix socket served by kb_driver on a BT Speak and by braille_spi on a
 * BT Braille (the BT Speak's own Libraries/kb_proto.h and kb_client.c say the protocol; it is written out here, so
 * nothing of the BT Speak's is needed to build).  A client says hello asking for exclusive access to the raw keys:
 * then every key goes to it first, and each must be answered within 10 ms (consumed, or passed on to BRLTTY) or the
 * server drops the client -- so a thread of its own reads and answers them, consuming all, and hands them on through
 * a pipe the program polls.  Closing the socket gives the keyboard back to BRLTTY (the server presses again, for
 * BRLTTY, any key still held).
 *
 * The keys: space and dots 1-8 (the BT Speak's whole keypad), and on a BT Braille its six navigation keys (L1-L3,
 * R1-R3) and 40 routing keys, as Linux input codes (KEY_SPACE, KEY_BRL_DOT1.., BTN_0..).
 *
 * btkb_keys -- portable, no platform calls -- makes key_events of them, with the BT frontend's two gestures (Leo,
 * #4: src/platforms/btspeak/keymap.py), each acted on when all its keys are up: M-chord with dot 7 (space, dots 1 3 4
 * 7) opens the program's menu, Z-chord with dot 7 (space, dots 1 3 5 6 7) saves the unit and ends the program.  The
 * Braille Lite has no dots 7 and 8, so any chord with one is the program's, and none of it reaches the unit.  Dot 7
 * or 8 alone is a bar ([keys] back, advance): it waits gesture_s for a chord's other keys before it goes on as one,
 * and a chord typed while it is held is the unit's bar and chord.
 */
#ifndef BLAZIE_BTKB_LINUX_H
#define BLAZIE_BTKB_LINUX_H

#include "keys.h"

#define BTKB_SOCKET "/run/BTSpeak/keyboard-socket"

/* Linux's input codes for the BT keyboards' keys (linux/input-event-codes.h) */
#define BTKB_SPACE 57
#define BTKB_DOT1 0x1f1                 /* KEY_BRL_DOT1; dot n is BTKB_DOT1 + n - 1 */
#define BTKB_DOT7 0x1f7
#define BTKB_DOT8 0x1f8
#define BTKB_NAV0 0x100                 /* BTN_0: the BT Braille's L1; L2, L3, R1, R2, R3 follow */

/* ---- the keys (portable) ------------------------------------------------------------------------------------------ */
#define BTKB_MAX_DEFER 8

typedef struct {
    double gesture_s;                   /* how long a 7 or 8 waits for a gesture's other keys */
    int passed;                         /* the chord keys gone on to the unit and down now (G_* bits) */
    int deferred_code[BTKB_MAX_DEFER], deferred_down[BTKB_MAX_DEFER], n_deferred;
    double defer_until;
    int seen;                           /* a gesture's keys so far (G_* bits; 0: no gesture) */
    int swallow;                        /* its keys still down: their ups are the gesture's */
} btkb_keys;

/* what a gesture asks for (btkb_keys_feed's *action) */
enum { BTKB_NONE, BTKB_MENU, BTKB_QUIT };

void btkb_keys_init(btkb_keys *k, double gesture_s);
/* one key from the server (its input code, 1 down / 0 up) at `now` (seconds): the key_events for the unit into out
   (KE_DOWN / KE_UP, at most cap); how many.  *action: BTKB_MENU or BTKB_QUIT when a gesture's last key came up;
   *reset: 1 when the keys the shell holds must be dropped (keys that went on to the unit became a gesture's).  Either
   may be NULL. */
int btkb_keys_feed(btkb_keys *k, int code, int down, double now, key_event *out, int cap, int *action, int *reset);
/* the time passing: a 7 or 8 that waited long enough goes on as a bar; how many key_events */
int btkb_keys_tick(btkb_keys *k, double now, key_event *out, int cap);
/* when btkb_keys_tick has something to do; -1: nothing waits */
double btkb_keys_deadline(const btkb_keys *k);
/* everything let go (the menu opened, the keyboard given back) */
void btkb_keys_reset(btkb_keys *k);
/* 1 while a gesture's keys are still down (the menu waits for them before it gives the keyboard back) */
int btkb_keys_busy(const btkb_keys *k);

/* ---- the server (Linux) ------------------------------------------------------------------------------------------ */
typedef struct {
    int sock;                           /* the server's socket, -1 when not connected */
    int pipe_r, pipe_w;                 /* the reader thread to the program: its keys, 3 bytes each */
    int stop_r, stop_w;                 /* the program to the reader thread: stop */
    unsigned long thread;               /* pthread_t */
    int running;
    int opened;                         /* btkb_open has set the fds (an all-zero btkb is one never opened) */
} btkb;

/* Connects to the server at path (NULL: BTKB_SOCKET) and takes the keyboard, exclusively.  1 on success; 0 with the
   reason in msg (no server here, another program holds the keyboard, ...); *absent (NULL: not wanted) is 1 when
   there is no server at all (not a BT Speak or BT Braille). */
int btkb_open(btkb *b, const char *path, char *msg, int msglen, int *absent);
/* gives the keyboard back to BRLTTY (safe when not open, and on an all-zero btkb never opened: a static one) */
void btkb_close(btkb *b);
/* the fd to poll for the keys (-1 when not open) */
int btkb_fd(const btkb *b);
/* the keys that came (input code and 1 down / 0 up), at most cap; how many, or -1 when the server is gone (then
   btkb_close it) */
int btkb_read(btkb *b, int *codes, int *downs, int cap);

/* The BT Braille's panning keys as the user set them for BRLTTY (Blazie mode's navigation-key table: the user's own
   under /var/lib/BTSpeak, else the shipped one; else R2 and R3, the shipped default): the input codes of the keys
   bound to FWINLT (back) and FWINRT (advance), -1 each when none.  `tables`: the files to try, NULL for the
   device's own. */
void btkb_panning_keys(const char *const *tables, int *back_code, int *advance_code);

#endif
