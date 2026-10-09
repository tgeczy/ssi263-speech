/* btkb_linux.c -- see btkb_linux.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "btkb_linux.h"
#include "evdev_linux.h"

/* ---- the keys ------------------------------------------------------------------------------------------------------ */
/* the chord keys as bits: the space bar, dots 1-8 */
#define G_SPACE 1
#define G_DOT(n) (1 << (n))
#define G_78 (G_DOT(7) | G_DOT(8))
#define G_MENU (G_SPACE | G_DOT(1) | G_DOT(3) | G_DOT(4) | G_DOT(7))                /* M-chord with dot 7 */
#define G_QUIT (G_SPACE | G_DOT(1) | G_DOT(3) | G_DOT(5) | G_DOT(6) | G_DOT(7))     /* Z-chord with dot 7 */

void btkb_keys_init(btkb_keys *k, double gesture_s)
{
    memset(k, 0, sizeof *k);
    k->gesture_s = gesture_s;
}

void btkb_keys_reset(btkb_keys *k)
{
    btkb_keys_init(k, k->gesture_s);
}

int btkb_keys_busy(const btkb_keys *k)
{
    return k->swallow != 0;
}

static int put(key_event *out, int n, int cap, int code, int down)
{
    if (n < cap) {
        out[n].key = evdev_key_of(code);
        out[n].mods = 0;
        out[n].type = down ? KE_DOWN : KE_UP;
        n++;
    }
    return n;
}

static int bit_of(int code)
{
    if (code == BTKB_SPACE)
        return G_SPACE;
    if (code >= BTKB_DOT1 && code <= BTKB_DOT8)
        return G_DOT(code - BTKB_DOT1 + 1);
    return 0;
}

/* the 7s and 8s waiting that are still down */
static int deferred_held(const btkb_keys *k)
{
    int held = 0, i;
    for (i = 0; i < k->n_deferred; i++) {
        if (k->deferred_down[i]) held |= bit_of(k->deferred_code[i]);
        else held &= ~bit_of(k->deferred_code[i]);
    }
    return held;
}

/* a gesture begins with these keys down: none of it reaches the unit; keys already gone on are dropped (*reset) */
static void gesture_begin(btkb_keys *k, int bits, int *reset)
{
    if (k->passed && reset)
        *reset = 1;
    k->seen = k->swallow = bits | k->passed;
    k->passed = 0;
    k->n_deferred = 0;
}

int btkb_keys_feed(btkb_keys *k, int code, int down, double now, key_event *out, int cap, int *action, int *reset)
{
    int bit = bit_of(code);
    if (!bit)                                   /* the panel's keys: the unit's, as they are */
        return put(out, 0, cap, code, down);
    if (down) {
        if (k->swallow) {                       /* more keys of a gesture */
            k->seen |= bit;
            k->swallow |= bit;
            return 0;
        }
        if ((bit & G_78) && (k->passed & ~G_78)) {  /* 7 or 8 joining a chord's keys: the chord is the program's */
            gesture_begin(k, bit | deferred_held(k), reset);
            return 0;
        }
        if (!(bit & G_78) && deferred_held(k)) {    /* a chord's key joining a waiting 7 or 8: likewise */
            gesture_begin(k, bit | deferred_held(k), reset);
            return 0;
        }
        if ((bit & G_78) && !(k->passed & bit)) {   /* 7 or 8 alone so far: waits to see */
            if (!k->n_deferred)
                k->defer_until = now + k->gesture_s;
            if (k->n_deferred < BTKB_MAX_DEFER) {
                k->deferred_code[k->n_deferred] = code;
                k->deferred_down[k->n_deferred++] = 1;
            }
            return 0;
        }
        k->passed |= bit;
        return put(out, 0, cap, code, 1);
    }
    if (k->swallow & bit) {                     /* a gesture's key up; the last one decides */
        k->swallow &= ~bit;
        if (!k->swallow) {
            if (action)
                *action = k->seen == G_MENU ? BTKB_MENU : k->seen == G_QUIT ? BTKB_QUIT : BTKB_NONE;
            k->seen = 0;
        }
        return 0;
    }
    if ((bit & G_78) && k->n_deferred) {        /* a 7 or 8 tapped while it waited: a tap of the bar */
        if (k->n_deferred < BTKB_MAX_DEFER) {
            k->deferred_code[k->n_deferred] = code;
            k->deferred_down[k->n_deferred++] = 0;
        }
        return 0;
    }
    if (!(k->passed & bit))
        return 0;                               /* an up whose down this never saw (the keyboard taken mid-press) */
    k->passed &= ~bit;
    return put(out, 0, cap, code, 0);
}

int btkb_keys_tick(btkb_keys *k, double now, key_event *out, int cap)
{
    int n = 0, i;
    if (!k->n_deferred || now < k->defer_until)
        return 0;
    for (i = 0; i < k->n_deferred; i++) {
        n = put(out, n, cap, k->deferred_code[i], k->deferred_down[i]);
        if (k->deferred_down[i]) k->passed |= bit_of(k->deferred_code[i]);
        else k->passed &= ~bit_of(k->deferred_code[i]);
    }
    k->n_deferred = 0;
    return n;
}

double btkb_keys_deadline(const btkb_keys *k)
{
    return k->n_deferred ? k->defer_until : -1.0;
}

/* ---- the server ---------------------------------------------------------------------------------------------------- */
/* the wire (BT Speak Libraries/kb_proto.h, protocol version 1) */
#define KB_VERSION 1
#define KB_STATUS_OK 0
#define KB_STATUS_BUSY 1
#define KB_MSG_KEY_EVENT 0x01
#define KB_MSG_CHORD_EVENT 0x02
#define KB_MSG_ACK 0x20
#define KB_ACK_CONSUME 0
#define KB_FLAG_EXCLUSIVE 1
#define KB_FLAG_WANT_RAW 2
#define HELLO_WAIT_MS 500                   /* the server answers from its main loop (its own client waits 10 ms) */
#define GONE 0xFFFF

static int read_exact(int fd, unsigned char *buf, int n, int wait_ms)
{
    int got = 0;
    while (got < n) {
        struct pollfd p;
        ssize_t r;
        p.fd = fd;
        p.events = POLLIN;
        if (wait_ms >= 0 && poll(&p, 1, wait_ms) <= 0)
            return 0;
        r = read(fd, buf + got, (size_t)(n - got));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return 0;
        got += (int)r;
    }
    return 1;
}

static void tell(btkb *b, int code, int down)
{
    unsigned char m[3];
    m[0] = (unsigned char)(code & 0xFF);
    m[1] = (unsigned char)(code >> 8);
    m[2] = (unsigned char)down;
    if (write(b->pipe_w, m, 3) != 3) {}         /* 3 bytes: atomic on a pipe; a full pipe drops the key */
}

/* every key: answered at once (consumed: the unit's, never BRLTTY's), then handed to the program */
static void *reader(void *arg)
{
    btkb *b = (btkb *)arg;
    for (;;) {
        struct pollfd p[2];
        unsigned char id, rest[3], ack[2] = {KB_MSG_ACK, KB_ACK_CONSUME};
        p[0].fd = b->sock; p[0].events = POLLIN;
        p[1].fd = b->stop_r; p[1].events = POLLIN;
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (p[1].revents)
            return NULL;                        /* btkb_close: it says nothing */
        if (!read_exact(b->sock, &id, 1, -1))
            break;
        if (id == KB_MSG_KEY_EVENT) {
            if (!read_exact(b->sock, rest, 3, 100))
                break;
        } else if (id == KB_MSG_CHORD_EVENT) {  /* not asked for; answered all the same */
            if (!read_exact(b->sock, rest, 2, 100))
                break;
        } else
            break;                              /* the protocol is not one we know */
        if (write(b->sock, ack, 2) != 2)
            break;
        if (id == KB_MSG_KEY_EVENT)
            tell(b, rest[0] | rest[1] << 8, rest[2] != 0);
    }
    tell(b, GONE, 0);
    return NULL;
}

int btkb_open(btkb *b, const char *path, char *msg, int msglen, int *absent)
{
    struct sockaddr_un addr;
    unsigned char hello[8] = {'B', 'T', 'K', 'B', KB_VERSION, KB_FLAG_EXCLUSIVE | KB_FLAG_WANT_RAW, 0, 0}, resp[8];
    int fds[2], attempt;
    pthread_t th;
    memset(b, 0, sizeof *b);
    b->sock = b->pipe_r = b->pipe_w = b->stop_r = b->stop_w = -1;
    b->opened = 1;
    if (absent)
        *absent = 0;
    if (!path || !*path)
        path = BTKB_SOCKET;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (attempt = 0; attempt < 3; attempt++) { /* a busy device can miss the hello's answer: try again */
        b->sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (b->sock < 0) {
            snprintf(msg, (size_t)msglen, "no socket: %s", strerror(errno));
            return 0;
        }
        if (connect(b->sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
            int e = errno;
            close(b->sock);
            b->sock = -1;
            if (absent)
                *absent = e == ENOENT || e == ECONNREFUSED;
            snprintf(msg, (size_t)msglen, "no keyboard server at %s (%s)", path, strerror(e));
            return 0;
        }
        if (write(b->sock, hello, sizeof hello) == (ssize_t)sizeof hello
                && read_exact(b->sock, resp, sizeof resp, HELLO_WAIT_MS))
            break;
        close(b->sock);
        b->sock = -1;
    }
    if (b->sock < 0) {
        snprintf(msg, (size_t)msglen, "the keyboard server at %s did not answer", path);
        return 0;
    }
    if (memcmp(resp, "BTKB", 4) || resp[5] != KB_STATUS_OK) {
        snprintf(msg, (size_t)msglen, resp[5] == KB_STATUS_BUSY && !memcmp(resp, "BTKB", 4)
                 ? "another program holds the keyboard" : "the keyboard server refused (status %d)", resp[5]);
        close(b->sock);
        b->sock = -1;
        return 0;
    }
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        snprintf(msg, (size_t)msglen, "no pipe: %s", strerror(errno));
        btkb_close(b);
        return 0;
    }
    b->pipe_r = fds[0];
    b->pipe_w = fds[1];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        snprintf(msg, (size_t)msglen, "no pipe: %s", strerror(errno));
        btkb_close(b);
        return 0;
    }
    b->stop_r = fds[0];
    b->stop_w = fds[1];
    if (pthread_create(&th, NULL, reader, b) != 0) {
        snprintf(msg, (size_t)msglen, "no thread");
        btkb_close(b);
        return 0;
    }
    b->thread = (unsigned long)th;
    b->running = 1;
    snprintf(msg, (size_t)msglen, "the keyboard server at %s", path);
    return 1;
}

void btkb_close(btkb *b)
{
    if (!b->opened)                     /* never opened: its fds are 0s, which must never be closed (stdin) */
        return;
    if (b->running) {
        if (write(b->stop_w, "x", 1) != 1) {}
        pthread_join((pthread_t)b->thread, NULL);
        b->running = 0;
    }
    if (b->sock >= 0) close(b->sock);            /* the server gives the keyboard back to BRLTTY */
    if (b->pipe_r >= 0) close(b->pipe_r);
    if (b->pipe_w >= 0) close(b->pipe_w);
    if (b->stop_r >= 0) close(b->stop_r);
    if (b->stop_w >= 0) close(b->stop_w);
    b->sock = b->pipe_r = b->pipe_w = b->stop_r = b->stop_w = -1;
}

int btkb_fd(const btkb *b)
{
    return b->running ? b->pipe_r : -1;
}

int btkb_read(btkb *b, int *codes, int *downs, int cap)
{
    unsigned char m[3 * 64];
    ssize_t got;
    int n = 0, i;
    if (cap > 64)
        cap = 64;
    got = read(b->pipe_r, m, (size_t)(3 * cap));
    if (got <= 0)
        return got < 0 && errno == EAGAIN ? 0 : -1;
    for (i = 0; i + 3 <= got; i += 3) {
        int code = m[i] | m[i + 1] << 8;
        if (code == GONE) {             /* the keys before it first, and the end put back for the next call (the
                                           reader has stopped: nothing comes after it) */
            if (n)
                tell(b, GONE, 0);
            return n ? n : -1;
        }
        codes[n] = code;
        downs[n++] = m[i + 2];
    }
    return n;
}

/* ---- the panning keys ---------------------------------------------------------------------------------------------- */
static const char *const DEVICE_TABLES[] = {
    "/var/lib/BTSpeak/btbraille-nav-keys.kti",  /* the user's (BT Speak's nav_keys.py: Braille Settings) */
    "/etc/xdg/brltty/btbraille-nav-keys.kti",   /* the shipped default */
    NULL,
};

static int nav_code(const char *name)
{
    static const char *const NAMES[] = {"L1", "L2", "L3", "R1", "R2", "R3"};
    int i;
    for (i = 0; i < 6; i++)
        if (!strcasecmp(name, NAMES[i]))
            return BTKB_NAV0 + i;
    return -1;
}

void btkb_panning_keys(const char *const *tables, int *back_code, int *advance_code)
{
    int i;
    if (!tables)
        tables = DEVICE_TABLES;
    for (i = 0; tables[i]; i++) {
        FILE *f = fopen(tables[i], "r");
        char line[256];
        int back = -1, adv = -1;
        if (!f)
            continue;
        while (fgets(line, sizeof line, f)) {   /* "bind R2 FWINLT": one key alone, as navigation-key-setup writes */
            char key[32], cmd[32];
            if (sscanf(line, " bind %31s %31s", key, cmd) != 2 || strchr(key, '+'))
                continue;
            if (!strcasecmp(cmd, "FWINLT"))
                back = nav_code(key);
            else if (!strcasecmp(cmd, "FWINRT"))
                adv = nav_code(key);
        }
        fclose(f);
        *back_code = back;                      /* the first table there is the one BRLTTY reads */
        *advance_code = adv;
        return;
    }
    *back_code = nav_code("R2");
    *advance_code = nav_code("R3");
}
