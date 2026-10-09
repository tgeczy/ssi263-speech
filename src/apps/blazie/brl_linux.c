/* brl_linux.c -- see brl_linux.h.  BrlAPI's calls are declared here as BrlAPI 0.8 has them (brlapi.h), so the
 * program builds without its headers and runs without its library. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brl_linux.h"

typedef uint64_t brl_key_t;
#define BRLAPI_TTY_DEFAULT (-1)
#define KEY_TYPE_CMD UINT64_C(0x20000000)
#define KEY_TYPE_MASK UINT64_C(0xE0000000)
#define KEY_CODE_MASK UINT64_C(0x1FFFFFFF)
#define CMD_FWINLT 23                   /* BRLAPI_KEY_CMD_FWINLT: BRLAPI_KEY_CMD(0) + 23 */
#define CMD_FWINRT 24
enum { RANGE_ALL = 0, RANGE_TYPE, RANGE_COMMAND, RANGE_KEY, RANGE_CODE };   /* brlapi_rangeType_t */

static int (*p_open)(const void *desired, void *actual);
static void (*p_close)(void);
static int (*p_size)(unsigned *x, unsigned *y);
static int (*p_enter)(int tty, const char *driver);
static int (*p_leave)(void);
static int (*p_dots)(const unsigned char *dots);
static int (*p_read)(int wait, brl_key_t *code);
static int (*p_ignore)(int type, const brl_key_t *keys, unsigned count);
static int (*p_accept)(int type, const brl_key_t *keys, unsigned count);

static int load(brl_out *b, char *msg, int msglen)
{
    static const char *const NAMES[] = {"libbrlapi.so.0.8", "libbrlapi.so"};
    unsigned i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && !b->lib; i++)
        b->lib = dlopen(NAMES[i], RTLD_NOW | RTLD_LOCAL);
    if (!b->lib) {
        snprintf(msg, (size_t)msglen, "BRLTTY's BrlAPI library is not here");
        return 0;
    }
    *(void **)&p_open = dlsym(b->lib, "brlapi_openConnection");
    *(void **)&p_close = dlsym(b->lib, "brlapi_closeConnection");
    *(void **)&p_size = dlsym(b->lib, "brlapi_getDisplaySize");
    *(void **)&p_enter = dlsym(b->lib, "brlapi_enterTtyMode");
    *(void **)&p_leave = dlsym(b->lib, "brlapi_leaveTtyMode");
    *(void **)&p_dots = dlsym(b->lib, "brlapi_writeDots");
    *(void **)&p_read = dlsym(b->lib, "brlapi_readKey");
    *(void **)&p_ignore = dlsym(b->lib, "brlapi_ignoreKeys");
    *(void **)&p_accept = dlsym(b->lib, "brlapi_acceptKeys");
    if (!p_open || !p_close || !p_size || !p_enter || !p_leave || !p_dots || !p_read || !p_ignore || !p_accept) {
        snprintf(msg, (size_t)msglen, "BRLTTY's BrlAPI library is not the one expected (0.8)");
        dlclose(b->lib);
        b->lib = NULL;
        return 0;
    }
    return 1;
}

/* the display held, only the panning commands taken from BRLTTY */
static int take(brl_out *b)
{
    brl_key_t keys[2];
    if (p_enter(BRLAPI_TTY_DEFAULT, NULL) < 0)
        return 0;
    keys[0] = KEY_TYPE_CMD | CMD_FWINLT;
    keys[1] = KEY_TYPE_CMD | CMD_FWINRT;
    p_ignore(RANGE_ALL, NULL, 0);
    p_accept(RANGE_KEY, keys, 2);
    b->tty = 1;
    return 1;
}

int brl_open(brl_out *b, char *msg, int msglen)
{
    unsigned x = 0, y = 0;
    memset(b, 0, sizeof *b);
    b->fd = -1;
    if (!load(b, msg, msglen))
        return 0;
    b->fd = p_open(NULL, NULL);
    if (b->fd < 0) {
        snprintf(msg, (size_t)msglen, "BRLTTY does not answer (its BrlAPI: the brlapi group, or no BRLTTY)");
        brl_close(b);
        return 0;
    }
    if (p_size(&x, &y) < 0 || !x || !y) {
        snprintf(msg, (size_t)msglen, "BRLTTY has no braille display");
        brl_close(b);
        return 0;
    }
    b->cols = (int)x;
    b->rows = (int)y;
    b->shown = (unsigned char *)calloc((size_t)(x * y), 1);
    if (!b->shown || !take(b)) {
        snprintf(msg, (size_t)msglen, "BRLTTY would not give the display to this program");
        brl_close(b);
        return 0;
    }
    snprintf(msg, (size_t)msglen, "%d cells", b->cols * b->rows);
    return 1;
}

void brl_close(brl_out *b)
{
    if (b->lib && b->fd >= 0) {
        if (b->tty)
            p_leave();
        p_close();
    }
    if (b->lib)
        dlclose(b->lib);
    free(b->shown);
    memset(b, 0, sizeof *b);
    b->fd = -1;
}

int brl_fd(const brl_out *b)
{
    return b->lib && b->tty ? b->fd : -1;
}

void brl_layout(const unsigned char *cells, int n, unsigned char *out, int cols)
{
    memset(out, 0, (size_t)cols);
    memcpy(out, cells, (size_t)(n < cols ? n : cols));
}

int brl_show(brl_out *b, const unsigned char *cells, int n)
{
    int size = b->cols * b->rows;
    if (!b->lib || b->fd < 0)
        return 1;
    brl_layout(cells, n, b->shown, size);
    if (!b->tty)
        return 1;                       /* the menu has the display: shown when it is held again */
    return p_dots(b->shown) >= 0;
}

int brl_keys(brl_out *b, int *back, int *advance)
{
    brl_key_t code;
    int r;
    if (!b->lib || b->fd < 0 || !b->tty)
        return 1;
    while ((r = p_read(0, &code)) > 0) {
        if ((code & KEY_TYPE_MASK) != KEY_TYPE_CMD)
            continue;
        if ((code & KEY_CODE_MASK & ~KEY_TYPE_MASK) == CMD_FWINLT)
            (*back)++;
        else if ((code & KEY_CODE_MASK & ~KEY_TYPE_MASK) == CMD_FWINRT)
            (*advance)++;
    }
    return r == 0;
}

void brl_hold(brl_out *b, int on)
{
    if (!b->lib || b->fd < 0 || on == b->tty)
        return;
    if (!on) {
        p_leave();
        b->tty = 0;
    } else if (take(b))
        p_dots(b->shown);
}
