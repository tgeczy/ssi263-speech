/* as_usart.h -- the Accent-PC's 8251 USART as its firmware uses it, reduced exactly as src/hosts/accent_pc.py reduces
 * it: whole bytes, no baud rate, no framing.
 *
 *   port 20h  in: the received byte (reading it drops RxRDY, and with it RST 6.5); out: a byte sent to the host
 *   port 21h  in: status 85h (TxRDY, TxEMPTY, DSR: the host's DTR is up) | 02h while a byte waits (RxRDY)
 *             out: after a reset, the mode word; then commands (bit 5 = RTS, bit 6 = internal reset: a mode word
 *             follows)
 *
 * The host's bytes wait in a queue; one is loaded (RxRDY) only while none is waiting and the firmware holds RTS up
 * (its handshake: command 37h raises it, 15h drops it while its buffer is full).  MIT.
 */
#ifndef AS_USART_H
#define AS_USART_H

#include <stdint.h>

#define AS_USART_RTS 0x20
#define AS_USART_IR 0x40                   /* command bit 6: internal reset */

typedef struct {
    uint8_t *q;                            /* the host's bytes not yet loaded (a ring) */
    int head, n, cap;
    int rx_ready;                          /* a byte is loaded: RxRDY, RST 6.5 */
    uint8_t rx_byte;
    int mode_next;                         /* the next port 21h write is a mode word */
    uint8_t cmd;                           /* the last command */
    uint8_t *tx;                           /* every byte the firmware sent */
    int n_tx, cap_tx;
} as_usart;

void as_usart_init(as_usart *u);
void as_usart_free(as_usart *u);
int as_usart_queue(as_usart *u, const uint8_t *bytes, int n);   /* 1, or 0 when out of memory (nothing taken) */
void as_usart_drop(as_usart *u);           /* the queued bytes (not the loaded one): accent_pc.py's rx.clear() */
/* accent_pc.py's _serial(): load the next byte when none is loaded, one is queued and RTS is up.  1 if loaded (the
   caller raises RST 6.5). */
int as_usart_load(as_usart *u);
/* port 20h or 21h; *rst65_drop is set when the read dropped RxRDY (the caller lowers RST 6.5) */
uint8_t as_usart_in(as_usart *u, int port, int *rst65_drop);
void as_usart_out(as_usart *u, int port, uint8_t v);

#endif
