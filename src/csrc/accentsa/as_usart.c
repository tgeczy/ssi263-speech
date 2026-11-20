/* as_usart.c -- the Accent-PC's 8251 as its firmware uses it (as_usart.h).  MIT. */
#include "as_usart.h"

#include <stdlib.h>
#include <string.h>

void as_usart_init(as_usart *u)
{
    memset(u, 0, sizeof *u);
    u->mode_next = 1;                      /* the 8251 takes a mode word after reset */
}

void as_usart_free(as_usart *u)
{
    free(u->q);
    free(u->tx);
    memset(u, 0, sizeof *u);
}

int as_usart_queue(as_usart *u, const uint8_t *bytes, int n)
{
    int i;
    if (n <= 0)
        return 1;
    if (u->n + n > u->cap) {
        int cap = u->cap ? u->cap : 256;
        uint8_t *q;
        while (cap < u->n + n)
            cap *= 2;
        q = (uint8_t *)malloc((size_t)cap);
        if (!q)
            return 0;
        for (i = 0; i < u->n; i++)         /* unwrap the ring into the new buffer */
            q[i] = u->q[(u->head + i) % u->cap];
        free(u->q);
        u->q = q;
        u->cap = cap;
        u->head = 0;
    }
    for (i = 0; i < n; i++)
        u->q[(u->head + u->n + i) % u->cap] = bytes[i];
    u->n += n;
    return 1;
}

void as_usart_drop(as_usart *u)
{
    u->head = 0;
    u->n = 0;
}

int as_usart_load(as_usart *u)
{
    if (u->rx_ready || !u->n || !(u->cmd & AS_USART_RTS))
        return 0;
    u->rx_byte = u->q[u->head];
    u->head = (u->head + 1) % u->cap;
    u->n--;
    u->rx_ready = 1;
    return 1;
}

uint8_t as_usart_in(as_usart *u, int port, int *rst65_drop)
{
    *rst65_drop = 0;
    if (port == 0x20) {
        u->rx_ready = 0;
        *rst65_drop = 1;
        return u->rx_byte;
    }
    return (uint8_t)(0x85 | (u->rx_ready ? 0x02 : 0));   /* TxRDY, TxEMPTY, DSR; RxRDY */
}

void as_usart_out(as_usart *u, int port, uint8_t v)
{
    if (port == 0x21) {
        if (u->mode_next)
            u->mode_next = 0;              /* the mode word: the format is not modelled */
        else {
            u->cmd = v;
            if (v & AS_USART_IR)
                u->mode_next = 1;          /* internal reset: a mode word follows */
        }
        return;
    }
    if (u->n_tx == u->cap_tx) {            /* port 20h: a byte to the host, kept */
        int cap = u->cap_tx ? u->cap_tx * 2 : 64;
        uint8_t *t = (uint8_t *)realloc(u->tx, (size_t)cap);
        if (!t)
            return;                        /* out of memory: the byte is lost, the run goes on */
        u->tx = t;
        u->cap_tx = cap;
    }
    u->tx[u->n_tx++] = v;
}
