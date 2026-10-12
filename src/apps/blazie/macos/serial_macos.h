/* The emulated unit's serial port on a macOS pseudo-terminal.  The other
   program opens the path returned by mac_serial_open; the app pumps bytes
   after each audio block while holding the unit's lock. */
#ifndef BLAZIE_SERIAL_MACOS_H
#define BLAZIE_SERIAL_MACOS_H

#include "../emu_unit.h"

typedef struct mac_serial_link mac_serial_link;

mac_serial_link *mac_serial_open(char *path, int path_cap, char *err, int err_cap);
void mac_serial_pump(mac_serial_link *link, emu_unit *unit);
void mac_serial_reset(mac_serial_link *link); /* a different unit takes the port */
void mac_serial_close(mac_serial_link *link);

#endif
