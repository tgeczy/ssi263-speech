/* test_btkb.c -- the BT Speak's and BT Braille's keyboard (btkb_linux.c) without a device: its keys and the menu
 * gesture by time, the keyboard server's protocol against a server of this test's own (each key answered within the
 * server's 10 ms while the program is busy elsewhere, the keys handed on in order, a server that says busy, none at
 * all, one that goes away), the panning keys read from BRLTTY's tables, and the display's layout (brl_linux.c).
 *
 *   test_btkb               every check
 *   BTKB_BREAK=1 test_btkb  the control: dots 7 and 8 never wait for a chord's other keys, so the gesture typed 7
 *                           first must fail (a bar goes to the unit, no menu), and only that check
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "brl_linux.h"
#include "btkb_linux.h"

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf("%-4s %s%s%s\n", ok ? "ok" : "FAIL", name, detail[0] ? ": " : "", detail);
    failures += !ok;
}

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* ---- the keys by time ---------------------------------------------------------------------------------------------- */
typedef struct { char text[512]; int menu, quit, reset; } run_out;

/* each step "t code down" (code: s, 1-8; down 1/0), ticked every 10 ms; the key_events as "BRL1+ BRL8- ' '+" */
static void run(const char *steps, run_out *o)
{
    btkb_keys k;
    double t;
    char copy[512], *tok;
    double at[64];
    int code[64], down[64], n = 0, i = 0;
    snprintf(copy, sizeof copy, "%s", steps);
    for (tok = strtok(copy, ","); tok && n < 64; tok = strtok(NULL, ",")) {
        char c;
        int d;
        if (sscanf(tok, "%lf %c %d", &at[n], &c, &d) != 3)
            continue;
        code[n] = c == 's' ? BTKB_SPACE : BTKB_DOT1 + (c - '1');
        down[n++] = d;
    }
    memset(o, 0, sizeof *o);
    btkb_keys_init(&k, 0.08);
    if (getenv("BTKB_BREAK"))
        k.gesture_s = 0;                        /* the control: a 7 or 8 never waits */
    for (t = 0; t < 2.0; t += 0.01) {
        key_event ev[16];
        int m, j;
        while (i < n && at[i] <= t + 1e-9) {
            int action = BTKB_NONE, reset = 0;
            m = btkb_keys_feed(&k, code[i], down[i], at[i], ev, 16, &action, &reset);
            if (k.gesture_s == 0) {             /* the control: flushed at once */
                key_event more[16];
                int extra = btkb_keys_tick(&k, at[i], more, 16), x;
                for (x = 0; x < extra && m < 16; x++)
                    ev[m++] = more[x];
            }
            o->menu += action == BTKB_MENU;
            o->quit += action == BTKB_QUIT;
            o->reset += reset;
            for (j = 0; j < m; j++) {
                char nm[16];
                snprintf(o->text + strlen(o->text), sizeof o->text - strlen(o->text), "%s%s%s",
                         o->text[0] ? " " : "", ev[j].key == ' ' ? "space" : key_name(ev[j].key, nm),
                         ev[j].type == KE_DOWN ? "+" : "-");
            }
            i++;
        }
        m = btkb_keys_tick(&k, t, ev, 16);
        for (j = 0; j < m; j++) {
            char nm[16];
            snprintf(o->text + strlen(o->text), sizeof o->text - strlen(o->text), "%s%s%s", o->text[0] ? " " : "",
                     ev[j].key == ' ' ? "space" : key_name(ev[j].key, nm), ev[j].type == KE_DOWN ? "+" : "-");
        }
    }
}

static void keys_checks(void)
{
    run_out o;
    char d[600];
    run("0.10 1 1, 0.15 4 1, 0.30 1 0, 0.31 4 0", &o);
    snprintf(d, sizeof d, "[%s]", o.text);
    check("keys: dots 1 and 4 down and up, as they move", !strcmp(o.text, "brl_dot1+ brl_dot4+ brl_dot1- brl_dot4-")
          && !o.menu, d);
    run("0.10 8 1, 0.13 8 0", &o);
    snprintf(d, sizeof d, "[%s]", o.text);
    check("keys: dot 8 tapped goes on after the gesture's wait, down and up", !strcmp(o.text, "brl_dot8+ brl_dot8-")
          && !o.menu, d);
    run("0.10 7 1, 0.60 7 0", &o);
    snprintf(d, sizeof d, "[%s]", o.text);
    check("keys: dot 7 held goes down after the wait, up when let go", !strcmp(o.text, "brl_dot7+ brl_dot7-"), d);
    run("0.10 7 1, 0.11 8 1, 0.40 7 0, 0.41 8 0", &o);
    snprintf(d, sizeof d, "[%s] menu %d", o.text, o.menu);
    check("keys: dots 7 and 8 together are both bars", !o.menu
          && !strcmp(o.text, "brl_dot7+ brl_dot8+ brl_dot7- brl_dot8-"), d);
    run("0.10 8 1, 0.30 1 1, 0.35 1 0, 0.50 8 0", &o);
    snprintf(d, sizeof d, "[%s]", o.text);
    check("keys: a chord typed with the bar held is the unit's bar and chord",
          !strcmp(o.text, "brl_dot8+ brl_dot1+ brl_dot1- brl_dot8-"), d);
    run("0.10 7 1, 0.12 s 1, 0.12 1 1, 0.13 3 1, 0.13 4 1, 0.30 7 0, 0.31 s 0, 0.31 1 0, 0.32 3 0, 0.32 4 0", &o);
    snprintf(d, sizeof d, "[%s] menu %d", o.text, o.menu);
    check("gesture: dot 7 first, then M-chord's keys: the menu when all are up, nothing reaches the unit",
          o.menu == 1 && !o.quit && !o.text[0], d);
    run("0.10 s 1, 0.10 1 1, 0.11 3 1, 0.11 4 1, 0.20 7 1, 0.40 s 0, 0.40 1 0, 0.41 3 0, 0.41 4 0, 0.42 7 0, "
        "0.90 7 1, 0.95 7 0", &o);
    snprintf(d, sizeof d, "[%s] menu %d reset %d", o.text, o.menu, o.reset);
    check("gesture: M-chord's keys first (gone on to the unit), then dot 7: the menu, those keys dropped, ups "
          "swallowed; a 7 afterwards is a bar again", o.menu == 1 && o.reset == 1
          && !strcmp(o.text, "space+ brl_dot1+ brl_dot3+ brl_dot4+ brl_dot7+ brl_dot7-"), d);
    run("0.10 s 1, 0.10 1 1, 0.11 3 1, 0.11 5 1, 0.12 6 1, 0.15 7 1, 0.40 s 0, 0.40 1 0, 0.41 3 0, 0.41 5 0, "
        "0.42 6 0, 0.42 7 0", &o);
    snprintf(d, sizeof d, "menu %d quit %d", o.menu, o.quit);
    check("gesture: Z-chord with dot 7 saves and leaves", o.quit == 1 && !o.menu, d);
    run("0.10 s 1, 0.12 7 1, 0.30 7 0, 0.31 s 0", &o);
    snprintf(d, sizeof d, "[%s] menu %d quit %d reset %d", o.text, o.menu, o.quit, o.reset);
    check("gesture: another chord with dot 7 is nothing: no bar, the keys gone on dropped", !o.menu && !o.quit
          && o.reset == 1 && !strcmp(o.text, "space+"), d);
}

/* ---- the server ---------------------------------------------------------------------------------------------------- */
typedef struct {
    char path[108];
    int status;                         /* answered to the hello */
    int n_keys;                         /* sent, then closed */
    unsigned char hello[8];
    double ack_ms[16];                  /* each key's answer, after it was sent */
    unsigned char ack[16][2];
    int n_acks;
    int listen_fd;
} server;

static void *serve(void *arg)
{
    server *s = (server *)arg;
    unsigned char resp[8] = {'B', 'T', 'K', 'B', 1, 0, 0, 0};
    int c = accept(s->listen_fd, NULL, NULL), i;
    if (c < 0)
        return NULL;
    if (read(c, s->hello, 8) != 8) {
        close(c);
        return NULL;
    }
    resp[5] = (unsigned char)s->status;
    if (write(c, resp, 8) != 8 || s->status) {
        close(c);
        return NULL;
    }
    for (i = 0; i < s->n_keys && i < 16; i++) {
        int code = i % 2 ? BTKB_DOT1 : BTKB_DOT1;          /* dot 1 down, then up */
        unsigned char m[4] = {0x01, (unsigned char)(code & 0xFF), (unsigned char)(code >> 8), (unsigned char)!(i % 2)};
        struct pollfd p;
        double t0 = mono();
        if (i == 2)
            m[1] = BTKB_SPACE & 0xFF, m[2] = 0;             /* the third: space down */
        if (write(c, m, 4) != 4)
            break;
        p.fd = c;
        p.events = POLLIN;
        if (poll(&p, 1, 1000) <= 0 || read(c, s->ack[s->n_acks], 2) != 2)
            break;
        s->ack_ms[s->n_acks++] = (mono() - t0) * 1000.0;
        usleep(2000);
    }
    close(c);
    return NULL;
}

static void start(server *s, pthread_t *th, int status, int n_keys)
{
    struct sockaddr_un a;
    memset(s, 0, sizeof *s);
    snprintf(s->path, sizeof s->path, "/tmp/test_btkb_%d.sock", (int)getpid());
    unlink(s->path);
    s->status = status;
    s->n_keys = n_keys;
    s->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", s->path);
    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(s->listen_fd, 1) < 0)
        perror("server");
    pthread_create(th, NULL, serve, s);
}

static void stop(server *s, pthread_t th)
{
    pthread_join(th, NULL);
    close(s->listen_fd);
    unlink(s->path);
}

static void server_checks(void)
{
    server s;
    pthread_t th;
    btkb b;
    char msg[256], d[400];
    int absent = 0, codes[16], downs[16], got = 0, gone = 0, i;
    double worst = 0;

    start(&s, &th, 0, 3);
    if (!btkb_open(&b, s.path, msg, sizeof msg, &absent)) {
        check("server: connected", 0, msg);
        stop(&s, th);
        return;
    }
    usleep(200000);                     /* the program busy: the reader thread answers the keys meanwhile */
    for (i = 0; i < 50 && !gone; i++) {
        struct pollfd p;
        int k;
        p.fd = btkb_fd(&b);
        p.events = POLLIN;
        if (poll(&p, 1, 100) <= 0)
            continue;
        k = btkb_read(&b, codes + got, downs + got, 16 - got);
        if (k < 0)
            gone = 1;
        else
            got += k;
    }
    btkb_close(&b);
    stop(&s, th);
    check("server: the hello asks for the raw keys, exclusively (BTKB, version 1, flags 3)",
          !memcmp(s.hello, "BTKB", 4) && s.hello[4] == 1 && s.hello[5] == 3, "");
    for (i = 0; i < s.n_acks; i++)
        if (s.ack_ms[i] > worst)
            worst = s.ack_ms[i];
    snprintf(d, sizeof d, "%d answers, the slowest %.1f ms, each 20 00: %s", s.n_acks, worst,
             s.n_acks == 3 && !memcmp(s.ack[0], "\x20\x00", 2) && !memcmp(s.ack[1], "\x20\x00", 2)
             && !memcmp(s.ack[2], "\x20\x00", 2) ? "yes" : "no");
    check("server: every key consumed within the server's 10 ms while the program was busy", s.n_acks == 3
          && worst < 10.0 && !memcmp(s.ack[0], "\x20\x00", 2) && !memcmp(s.ack[2], "\x20\x00", 2), d);
    snprintf(d, sizeof d, "%d keys: %03X/%d %03X/%d %03X/%d, then gone %d", got, got > 0 ? codes[0] : 0,
             got > 0 ? downs[0] : 0, got > 1 ? codes[1] : 0, got > 1 ? downs[1] : 0, got > 2 ? codes[2] : 0,
             got > 2 ? downs[2] : 0, gone);
    check("server: the keys handed on in order, and the server's end told", got == 3 && codes[0] == BTKB_DOT1
          && downs[0] == 1 && codes[1] == BTKB_DOT1 && downs[1] == 0 && codes[2] == BTKB_SPACE && downs[2] == 1
          && gone, d);

    start(&s, &th, 1, 0);
    i = btkb_open(&b, s.path, msg, sizeof msg, &absent);
    stop(&s, th);
    snprintf(d, sizeof d, "[%s]", msg);
    check("server: another program holding the keyboard is said so", !i && strstr(msg, "another program") && !absent,
          d);

    i = btkb_open(&b, "/tmp/test_btkb_no_such.sock", msg, sizeof msg, &absent);
    snprintf(d, sizeof d, "absent %d [%s]", absent, msg);
    check("server: none at all (not a BT Speak) is told apart", !i && absent, d);
    btkb_close(&b);                     /* closing what never opened is safe */
    {   /* an all-zero btkb (a static one never opened: bt = off, --no-bt) closed: its 0s are not fds -- stdin stays */
        static btkb never;
        int before = fcntl(0, F_GETFD) != -1;
        btkb_close(&never);
        check("server: a keyboard never opened, closed, leaves stdin open", !before || fcntl(0, F_GETFD) != -1, "");
    }
}

/* ---- the panning keys, the display --------------------------------------------------------------------------------- */
static void table_checks(void)
{
    char p1[64], p2[64], d[128];
    const char *tables[3];
    int back = 0, adv = 0;
    FILE *f;
    snprintf(p1, sizeof p1, "/tmp/test_btkb_%d_user.kti", (int)getpid());
    snprintf(p2, sizeof p2, "/tmp/test_btkb_%d_shipped.kti", (int)getpid());
    f = fopen(p1, "w");
    fputs("# Generated by navigation-key-setup\nbind L1 CHRLT\nbind R2+R3 FWINRT\nbind L3 FWINLT\nbind R1 FWINRT\n", f);
    fclose(f);
    f = fopen(p2, "w");
    fputs("bind R2 FWINLT\nbind R3 FWINRT\n", f);
    fclose(f);
    tables[0] = p1; tables[1] = p2; tables[2] = NULL;
    btkb_panning_keys(tables, &back, &adv);
    snprintf(d, sizeof d, "back %03X advance %03X", back, adv);
    check("panning keys: the user's table first, a combination left out (L3 back, R1 advance)",
          back == BTKB_NAV0 + 2 && adv == BTKB_NAV0 + 3, d);
    tables[0] = "/tmp/test_btkb_none.kti"; tables[1] = p2;
    btkb_panning_keys(tables, &back, &adv);
    snprintf(d, sizeof d, "back %03X advance %03X", back, adv);
    check("panning keys: the shipped table when the user has none (R2, R3)", back == BTKB_NAV0 + 4
          && adv == BTKB_NAV0 + 5, d);
    tables[0] = NULL;
    btkb_panning_keys(tables, &back, &adv);
    check("panning keys: no table at all: R2 and R3", back == BTKB_NAV0 + 4 && adv == BTKB_NAV0 + 5, "");
    unlink(p1);
    unlink(p2);
    {
        unsigned char cells[18], out[40];
        int i, ok = 1;
        for (i = 0; i < 18; i++)
            cells[i] = (unsigned char)(i + 1);
        memset(out, 0xAA, sizeof out);
        brl_layout(cells, 18, out, 40);
        for (i = 0; i < 40; i++)
            ok &= out[i] == (i < 18 ? i + 1 : 0);
        brl_layout(cells, 18, out, 12);
        for (i = 0; i < 12; i++)
            ok &= out[i] == i + 1;
        check("display: the 18 cells at the left of 40, blank beyond; cut to a 12-cell display", ok, "");
    }
}

int main(void)
{
    keys_checks();
    server_checks();
    table_checks();
    printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
