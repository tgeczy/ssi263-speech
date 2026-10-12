/* A nonblocking pseudo-terminal carrying the unit's ASCI0 serial line. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>
#include "serial_macos.h"

#define OUT_CAP 1024

struct mac_serial_link {
    int master, slave;
    unsigned char in[BL_SERIAL_ROOM];
    int n_in;
    unsigned char out[OUT_CAP];
    int n_out, off;
    int blocked_pumps;
    bl_serial_status applied;
    int have_applied;
};

static speed_t speed_of(long baud)
{
    static const struct { long baud; speed_t speed; } rates[] = {
        {300, B300}, {600, B600}, {1200, B1200}, {2400, B2400}, {4800, B4800},
        {9600, B9600}, {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}
    };
    unsigned i;
    for (i = 0; i < sizeof rates / sizeof rates[0]; i++)
        if (rates[i].baud == baud)
            return rates[i].speed;
    return 0;
}

static void raw_mode(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) != 0)
        return;
    cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
    t.c_cflag &= ~CRTSCTS;
#endif
    t.c_iflag &= ~(IXON | IXOFF | IXANY);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

mac_serial_link *mac_serial_open(char *path, int path_cap, char *err, int err_cap)
{
    mac_serial_link *link;
    int flags;
    char name[128];
    if (path_cap > 0) path[0] = 0;
    link = (mac_serial_link *)calloc(1, sizeof *link);
    if (!link) {
        snprintf(err, (size_t)err_cap, "Out of memory creating the serial port");
        return NULL;
    }
    if (openpty(&link->master, &link->slave, name, NULL, NULL) != 0) {
        snprintf(err, (size_t)err_cap, "Could not create a virtual serial port: %s", strerror(errno));
        free(link);
        return NULL;
    }
    if ((int)strlen(name) + 1 > path_cap) {
        snprintf(err, (size_t)err_cap, "The virtual serial port path is too long");
        close(link->slave);
        close(link->master);
        free(link);
        return NULL;
    }
    /* Keep the slave open: macOS restores cooked mode when its last opener
       closes, even while the master remains open.  A later client must see a
       raw serial device, including the firmware's current baud setting. */
    raw_mode(link->slave);
    flags = fcntl(link->master, F_GETFL);
    if (flags < 0 || fcntl(link->master, F_SETFL, flags | O_NONBLOCK) != 0) {
        snprintf(err, (size_t)err_cap, "Could not prepare the virtual serial port: %s", strerror(errno));
        close(link->slave);
        close(link->master);
        free(link);
        return NULL;
    }
    memcpy(path, name, strlen(name) + 1);
    return link;
}

static void apply(mac_serial_link *link, const bl_serial_status *status)
{
    struct termios t;
    speed_t speed;
    if (link->have_applied && bl_serial_same(&link->applied, status))
        return;
    if (tcgetattr(link->slave, &t) == 0) {
        speed = speed_of(status->baud);
        if (speed) {
            cfsetispeed(&t, speed);
            cfsetospeed(&t, speed);
        }
        t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
        t.c_cflag |= status->data_bits == 7 ? CS7 : CS8;
        if (status->parity != 'N')
            t.c_cflag |= PARENB | (status->parity == 'O' ? PARODD : 0);
        if (status->stop_bits == 2)
            t.c_cflag |= CSTOPB;
        tcsetattr(link->slave, TCSANOW, &t);
    }
    link->applied = *status;
    link->have_applied = 1;
}

void mac_serial_pump(mac_serial_link *link, emu_unit *unit)
{
    int room, taken;
    if (!link || !unit)
        return;
    room = emu_serial_space(unit) - link->n_in;
    if (room > (int)sizeof link->in - link->n_in)
        room = (int)sizeof link->in - link->n_in;
    if (room > 0) {
        ssize_t n = read(link->master, link->in + link->n_in, (size_t)room);
        if (n > 0)
            link->n_in += (int)n;
    }
    if (link->n_in) {
        taken = emu_serial_write(unit, link->in, link->n_in);
        if (taken > 0) {
            memmove(link->in, link->in + taken, (size_t)(link->n_in - taken));
            link->n_in -= taken;
        }
    }
    for (;;) {
        if (link->off == link->n_out) {
            bl_serial_status status;
            link->n_out = emu_serial_read(unit, link->out, OUT_CAP, &status);
            link->off = 0;
            apply(link, &status);       /* status accompanies the bytes that left under it */
            if (!link->n_out)
                break;
        }
        ssize_t n = write(link->master, link->out + link->off, (size_t)(link->n_out - link->off));
        if (n > 0) {
            link->off += (int)n;
            link->blocked_pumps = 0;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* No program may have opened the slave.  Do not let an unread
               terminal queue stop draining the emulated unit indefinitely. */
            if (++link->blocked_pumps < 25)
                break;
            tcflush(link->slave, TCIFLUSH);
            link->blocked_pumps = 0;
            link->off = link->n_out;
        } else
            link->off = link->n_out;    /* the slave is closed: this output has no listener */
    }
}

void mac_serial_reset(mac_serial_link *link)
{
    if (!link) return;
    link->n_in = link->n_out = link->off = 0;
    link->blocked_pumps = 0;
    link->have_applied = 0;
    tcflush(link->slave, TCIOFLUSH);
}

void mac_serial_close(mac_serial_link *link)
{
    if (!link) return;
    close(link->slave);
    close(link->master);
    free(link);
}
