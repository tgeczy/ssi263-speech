/* evdev_linux.c -- see evdev_linux.h. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>
#include "evdev_linux.h"

static const struct { int code, key; } CODES[] = {
    {KEY_A, 'a'}, {KEY_B, 'b'}, {KEY_C, 'c'}, {KEY_D, 'd'}, {KEY_E, 'e'}, {KEY_F, 'f'}, {KEY_G, 'g'}, {KEY_H, 'h'},
    {KEY_I, 'i'}, {KEY_J, 'j'}, {KEY_K, 'k'}, {KEY_L, 'l'}, {KEY_M, 'm'}, {KEY_N, 'n'}, {KEY_O, 'o'}, {KEY_P, 'p'},
    {KEY_Q, 'q'}, {KEY_R, 'r'}, {KEY_S, 's'}, {KEY_T, 't'}, {KEY_U, 'u'}, {KEY_V, 'v'}, {KEY_W, 'w'}, {KEY_X, 'x'},
    {KEY_Y, 'y'}, {KEY_Z, 'z'},
    {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'}, {KEY_4, '4'}, {KEY_5, '5'}, {KEY_6, '6'}, {KEY_7, '7'}, {KEY_8, '8'},
    {KEY_9, '9'}, {KEY_0, '0'},
    {KEY_SPACE, ' '}, {KEY_DOT, '.'}, {KEY_COMMA, ','}, {KEY_SLASH, '/'}, {KEY_SEMICOLON, ';'},
    {KEY_APOSTROPHE, '\''}, {KEY_MINUS, '-'}, {KEY_EQUAL, '='}, {KEY_LEFTBRACE, '['}, {KEY_RIGHTBRACE, ']'},
    {KEY_BACKSLASH, '\\'}, {KEY_GRAVE, '`'},
    {KEY_ENTER, K_ENTER}, {KEY_KPENTER, K_ENTER}, {KEY_TAB, K_TAB}, {KEY_BACKSPACE, K_BACKSPACE}, {KEY_ESC, K_ESC},
    {KEY_UP, K_UP}, {KEY_DOWN, K_DOWN}, {KEY_LEFT, K_LEFT}, {KEY_RIGHT, K_RIGHT}, {KEY_HOME, K_HOME},
    {KEY_END, K_END}, {KEY_PAGEUP, K_PGUP}, {KEY_PAGEDOWN, K_PGDN}, {KEY_INSERT, K_INSERT}, {KEY_DELETE, K_DELETE},
    {KEY_F1, K_F1}, {KEY_F2, K_F2}, {KEY_F3, K_F3}, {KEY_F4, K_F4}, {KEY_F5, K_F5}, {KEY_F6, K_F6},
    {KEY_F7, K_F7}, {KEY_F8, K_F8}, {KEY_F9, K_F9}, {KEY_F10, K_F10}, {KEY_F11, K_F11}, {KEY_F12, K_F12},
    {KEY_LEFTSHIFT, K_LSHIFT}, {KEY_RIGHTSHIFT, K_RSHIFT}, {KEY_LEFTCTRL, K_LCTRL}, {KEY_RIGHTCTRL, K_RCTRL},
    {KEY_LEFTALT, K_LALT}, {KEY_RIGHTALT, K_RALT}, {KEY_CAPSLOCK, K_CAPS}, {KEY_NUMLOCK, K_NUMLOCK},
    {KEY_SCROLLLOCK, K_SCROLL}, {KEY_SYSRQ, K_PRINT}, {KEY_PAUSE, K_PAUSE},
    {KEY_BRL_DOT1, K_BRL1}, {KEY_BRL_DOT2, K_BRL2}, {KEY_BRL_DOT3, K_BRL3}, {KEY_BRL_DOT4, K_BRL4},
    {KEY_BRL_DOT5, K_BRL5}, {KEY_BRL_DOT6, K_BRL6}, {KEY_BRL_DOT7, K_BRL7}, {KEY_BRL_DOT8, K_BRL8},
};

int evdev_key_of(int code)
{
    unsigned i;
    for (i = 0; i < sizeof CODES / sizeof CODES[0]; i++)
        if (CODES[i].code == code)
            return CODES[i].key;
    return code > 0 && code < 0x300 ? K_CODE + code : 0;
}

#define BITS_LONG (8 * (int)sizeof(long))
static int has_bit(const unsigned long *bits, int n)
{
    return (bits[n / BITS_LONG] >> (n % BITS_LONG)) & 1;
}

/* a keyboard: the F, J and space keys, or a braille keyboard's first dot key */
static int is_keyboard(int fd)
{
    unsigned long keys[(KEY_MAX + 1 + 8 * sizeof(long) - 1) / (8 * sizeof(long))];
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0)
        return 0;
    return (has_bit(keys, KEY_F) && has_bit(keys, KEY_J) && has_bit(keys, KEY_SPACE)) || has_bit(keys, KEY_BRL_DOT1);
}

int evdev_skip_bt;

static int add_device(evdev_set *s, const char *path, int must, char *msg, int msglen)
{
    int fd;
    if (s->n == EVDEV_MAX)
        return 0;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (must)
            snprintf(msg, (size_t)msglen, "could not read %s: %s%s", path, strerror(errno),
                     errno == EACCES ? " (the input group is needed: sudo usermod -aG input $USER)" : "");
        return errno == EACCES ? -1 : 0;
    }
    if (!must && !is_keyboard(fd)) {
        close(fd);
        return 0;
    }
    if (!must) {
        char name[80] = "";
        if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
            name[0] = 0;
        /* BRLTTY's own keyboards (the keys it types into the console): grabbed, nothing BRLTTY types would reach the
           console while the program runs -- not even the menu, typed on a braille keyboard */
        if (!strncmp(name, "BRLTTY", 6)
                /* the BT keypad and the keyboard its server makes for BRLTTY: the server's keys come to the program
                   already (btkb_linux.h) */
                || (evdev_skip_bt && (!strcmp(name, "4x3braille") || !strcmp(name, "braille_keyboard")))) {
            close(fd);
            return 0;
        }
    }
    /* another program holding it for itself (BRLTTY can): it would give nothing, and its keys would never reach the
       terminal either -- left alone, so the terminal's keys are used */
    if (ioctl(fd, EVIOCGRAB, 1) < 0 && errno == EBUSY) {
        char name[80] = "";
        if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0 || !name[0])
            snprintf(name, sizeof name, "%s", path);
        name[sizeof name - 1] = 0;
        snprintf(s->busy, sizeof s->busy, "%s", name);
        close(fd);
        return -2;
    }
    ioctl(fd, EVIOCGRAB, 0);
    s->fd[s->n] = fd;
    if (ioctl(fd, EVIOCGNAME(sizeof s->name[s->n]), s->name[s->n]) < 0)
        snprintf(s->name[s->n], sizeof s->name[s->n], "%s", path);
    s->name[s->n][sizeof s->name[s->n] - 1] = 0;
    s->n++;
    return 1;
}

int evdev_open(evdev_set *s, const char *path, int grab, char *msg, int msglen)
{
    int i, denied = 0, held = 0, len;
    memset(s, 0, sizeof *s);
    msg[0] = 0;
    if (path && *path) {
        int r = add_device(s, path, 1, msg, msglen);
        if (r == -2)
            snprintf(msg, (size_t)msglen, "%s is held by another program (BRLTTY?)", s->busy);
        if (r <= 0)
            return 0;
    } else {
        DIR *d = opendir("/dev/input");
        struct dirent *e;
        if (!d) {
            snprintf(msg, (size_t)msglen, "no input devices (/dev/input)");
            return 0;
        }
        while ((e = readdir(d)) != NULL)
            if (!strncmp(e->d_name, "event", 5)) {
                char p[300];
                snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
                int r = add_device(s, p, 0, msg, msglen);
                if (r == -1)
                    denied = 1;
                else if (r == -2)
                    held = 1;
            }
        closedir(d);
        if (!s->n) {
            if (held)
                snprintf(msg, (size_t)msglen, "the keyboard %s is held by another program (BRLTTY?)", s->busy);
            else
                snprintf(msg, (size_t)msglen, denied ? "the input devices cannot be read (the input group is "
                         "needed: sudo usermod -aG input $USER, then log in again)"
                         : "no keyboard found among the input devices");
            return 0;
        }
    }
    len = snprintf(msg, (size_t)msglen, "reading the keys of");
    for (i = 0; i < s->n && len < msglen; i++)
        len += snprintf(msg + len, (size_t)(msglen - len), "%s %s", i ? "," : "", s->name[i]);
    evdev_grab(s, grab);
    return s->n;
}

void evdev_grab(evdev_set *s, int on)
{
    int i;
    for (i = 0; i < s->n; i++)
        ioctl(s->fd[i], EVIOCGRAB, on ? 1 : 0);
    s->grabbed = on;
}

void evdev_close(evdev_set *s)
{
    int i;
    if (s->grabbed)
        evdev_grab(s, 0);
    for (i = 0; i < s->n; i++)
        close(s->fd[i]);
    s->n = 0;
}

int evdev_event(evdev_set *s, int type, int code, int value, key_event *e)
{
    int key, *count = NULL;
    if (type != EV_KEY || value == 2)           /* auto-repeat: the shells decide what a held key does */
        return 0;
    key = evdev_key_of(code);
    if (!key)
        return 0;
    if (key == K_LSHIFT || key == K_RSHIFT) count = &s->shift;
    else if (key == K_LCTRL || key == K_RCTRL) count = &s->ctrl;
    else if (key == K_LALT || key == K_RALT) count = &s->alt;
    if (count) {
        *count += value ? 1 : -1;
        if (*count < 0)
            *count = 0;
    }
    e->key = key;
    e->type = value ? KE_DOWN : KE_UP;
    e->mods = (s->shift ? KM_SHIFT : 0) | (s->ctrl ? KM_CTRL : 0) | (s->alt ? KM_ALT : 0);
    return 1;
}

int evdev_read(evdev_set *s, int fd, key_event *out, int cap)
{
    struct input_event ev[32];
    int got = 0;
    while (got < cap) {
        ssize_t r = read(fd, ev, sizeof ev);
        int i, n;
        if (r <= 0)
            break;
        n = (int)(r / (ssize_t)sizeof ev[0]);
        for (i = 0; i < n && got < cap; i++)
            got += evdev_event(s, ev[i].type, ev[i].code, ev[i].value, &out[got]);
    }
    return got;
}
