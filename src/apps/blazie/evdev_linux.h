/* evdev_linux.h -- the keyboard read from Linux's input devices (/dev/input/event*), where each key goes down AND
 * comes up: what a terminal cannot tell, and what the Braille Lite needs to see keys held (a chord held while the
 * unit starts: i-chord, the cold reset) and the Type 'n Speak to see Shift, Ctrl and Alt as keys.
 *
 * Reading them needs the input group (sudo usermod -aG input $USER, then log in again) or root.  Grabbed (the
 * default), the keys reach only the emulator -- not the console, not a screen reader -- until it lets go (its menu
 * lets go while it is open, and the kernel lets go if the program ends).  The devices read: every keyboard (one with
 * the F, J and space keys, or a braille keyboard's dot keys) or the one named.
 */
#ifndef BLAZIE_EVDEV_LINUX_H
#define BLAZIE_EVDEV_LINUX_H

#include "keys.h"

#define EVDEV_MAX 8

typedef struct {
    int fd[EVDEV_MAX];
    char name[EVDEV_MAX][80];
    int n;
    int grabbed;
    int shift, ctrl, alt;               /* the modifier keys down now (any device) */
    char busy[80];                      /* a keyboard another program holds for itself (left alone) */
} evdev_set;

/* nonzero: every keyboard leaves out the BT Speak's keypad and its server's keyboard for BRLTTY (4x3braille,
   braille_keyboard), whose keys the program has from the keyboard server */
extern int evdev_skip_bt;
/* path: one device, or NULL (or "") for every keyboard that can be read.  0 when none could be opened, the reason
   in msg; else how many, their names in msg */
int evdev_open(evdev_set *s, const char *path, int grab, char *msg, int msglen);
void evdev_grab(evdev_set *s, int on);
void evdev_close(evdev_set *s);
/* the keys from device fd (readable now), into out (KE_DOWN / KE_UP, auto-repeats dropped); how many */
int evdev_read(evdev_set *s, int fd, key_event *out, int cap);
/* one input event (its type, code and value) as a key: 1 and the key in *e, else 0.  The modifier keys are tracked
   in s, so each event carries the modifiers down with it. */
int evdev_event(evdev_set *s, int type, int code, int value, key_event *e);
/* an input device's key code as keys.h's key (an unnamed one: K_CODE + code) */
int evdev_key_of(int code);

#endif
